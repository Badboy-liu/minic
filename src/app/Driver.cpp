#include "Driver.h"

#include "CodeGenerator.h"
#include "Lexer.h"
#include "Optimizer.h"
#include "Parser.h"
#include "Semantics.h"
#include "Toolchain.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

namespace {

unsigned int detectedParallelism() {
    const unsigned int detected = std::thread::hardware_concurrency();
    return detected == 0 ? 4u : detected;
}

unsigned int workerCountFor(std::size_t taskCount, unsigned int requestedJobs) {
    if (taskCount <= 1) {
        return 1;
    }

    const unsigned int available = requestedJobs == 0 ? detectedParallelism() : requestedJobs;
    return static_cast<unsigned int>(std::min<std::size_t>(taskCount, available));
}

template <typename Result, typename Factory>
std::vector<Result> runParallelTasks(std::size_t taskCount, unsigned int requestedJobs, Factory &&factory) {
    std::vector<Result> results;
    results.reserve(taskCount);
    if (taskCount == 0) {
        return results;
    }

    if (workerCountFor(taskCount, requestedJobs) == 1) {
        for (std::size_t i = 0; i < taskCount; ++i) {
            results.push_back(factory(i)());
        }
        return results;
    }

    std::vector<std::future<Result>> futures;
    futures.reserve(taskCount);
    for (std::size_t i = 0; i < taskCount; ++i) {
        futures.push_back(std::async(std::launch::async, factory(i)));
    }
    for (auto &future : futures) {
        results.push_back(future.get());
    }
    return results;
}

struct ParsedTranslationUnit {
    Program program;
    std::size_t functionCount = 0;
    std::size_t globalCount = 0;
};

struct PreparedTranslationUnit {
    Program program;
    fs::path asmPath;
    fs::path objPath;
    bool emitEntryPoint = false;
};

struct GeneratedTranslationUnit {
    fs::path asmPath;
    fs::path objPath;
    bool producedObject = false;
};

struct PreparedAssemblyInput {
    fs::path asmPath;
    fs::path objPath;
};

} // namespace

int Driver::run(const fs::path &selfPath, const std::vector<std::string> &args) {
    const Options options = parseOptions(args);
    const TargetSpec &target = targetSpec(options.target);
    const unsigned int requestedJobs = sanitizeJobs(options.jobs);
    std::vector<fs::path> sourceInputs;
    std::vector<fs::path> assemblyInputs;
    std::vector<fs::path> objectInputs;
    for (const auto &inputPath : options.inputPaths) {
        if (isObjectInput(inputPath)) {
            objectInputs.push_back(inputPath);
        } else if (isAssemblyInput(inputPath)) {
            assemblyInputs.push_back(inputPath);
        } else {
            sourceInputs.push_back(inputPath);
        }
    }
    if (!objectInputs.empty() && (options.assemblyOnly || options.compileOnly)) {
        throw std::runtime_error("existing object inputs are only supported for final linking");
    }
    if (!assemblyInputs.empty() && options.assemblyOnly) {
        throw std::runtime_error("assembly inputs cannot be used with -S; they are already assembly sources");
    }
    if (!assemblyInputs.empty() && !target.supportsAssemblyInputs) {
        throw std::runtime_error(std::string("target does not support direct assembly inputs: ") + target.name);
    }

    Program combinedProgram;
    std::vector<std::size_t> functionCounts;
    std::vector<std::size_t> globalCounts;
    std::vector<ParsedTranslationUnit> parsedUnits =
        runParallelTasks<ParsedTranslationUnit>(sourceInputs.size(), requestedJobs, [&](std::size_t index) {
            return [&, index]() {
                const std::string source = readFile(sourceInputs[index]);
                Lexer lexer(source);
                Parser parser(lexer.tokenize());
                Program fileProgram = parser.parseProgram();

                ParsedTranslationUnit unit;
                unit.functionCount = fileProgram.functions.size();
                unit.globalCount = fileProgram.globals.size();
                unit.program = std::move(fileProgram);
                return unit;
            };
        });

    for (auto &parsedUnit : parsedUnits) {
        functionCounts.push_back(parsedUnit.functionCount);
        globalCounts.push_back(parsedUnit.globalCount);
        for (auto &function : parsedUnit.program.functions) {
            combinedProgram.functions.push_back(std::move(function));
        }
        for (auto &global : parsedUnit.program.globals) {
            combinedProgram.globals.push_back(std::move(global));
        }
    }

    std::vector<Function> globalDefinitions;
    std::vector<GlobalVar> globalVariables;
    if (!sourceInputs.empty()) {
        SemanticAnalyzer semanticAnalyzer;
        const bool requireMain = !options.assemblyOnly && !options.compileOnly;
        semanticAnalyzer.analyze(combinedProgram, requireMain);
        Optimizer optimizer;
        optimizer.optimize(combinedProgram);

        for (const auto &function : combinedProgram.functions) {
            if (!function.isDeclaration()) {
                Function declaration;
                declaration.name = function.name;
                declaration.returnType = function.returnType;
                declaration.parameters = function.parameters;
                globalDefinitions.push_back(std::move(declaration));
            }
        }
        for (const auto &global : combinedProgram.globals) {
            GlobalVar declaration;
            declaration.type = global.type;
            declaration.name = global.name;
            declaration.symbolName = global.symbolName;
            declaration.isExternal = true;
            globalVariables.push_back(std::move(declaration));
        }
    }

    const fs::path outputDirectory = options.outputPath.parent_path().empty()
        ? fs::path(".")
        : options.outputPath.parent_path();
    const std::string objectExtension = target.objectExtension;

    std::vector<fs::path> generatedObjPaths;
    std::vector<fs::path> objPaths;
    std::vector<PreparedTranslationUnit> preparedUnits;
    std::vector<PreparedAssemblyInput> preparedAssemblyInputs;
    preparedUnits.reserve(sourceInputs.size());
    preparedAssemblyInputs.reserve(assemblyInputs.size());
    std::size_t functionOffset = 0;
    std::size_t globalOffset = 0;
    bool emittedEntryPoint = false;
    const bool multipleInputs = options.inputPaths.size() > 1;

    const ToolchainPaths toolchain = Toolchain::detect();

    for (std::size_t i = 0; i < sourceInputs.size(); ++i) {
        Program fileProgram;
        const std::size_t count = functionCounts[i];
        const std::size_t globalCount = globalCounts[i];
        bool fileHasMain = false;
        std::unordered_set<std::string> definedInFile;
        std::unordered_set<std::string> declaredInFile;
        std::unordered_set<std::string> globalsInFile;
        for (std::size_t j = 0; j < count; ++j) {
            Function &function = combinedProgram.functions[functionOffset + j];
            if (function.name == "main" && !function.isDeclaration()) {
                fileHasMain = true;
            }
            if (function.isDeclaration()) {
                declaredInFile.insert(function.name);
            } else {
                definedInFile.insert(function.name);
            }
        }
        for (std::size_t j = 0; j < globalCount; ++j) {
            globalsInFile.insert(combinedProgram.globals[globalOffset + j].name);
        }

        const bool emitEntryPoint = fileHasMain && !emittedEntryPoint;
        emittedEntryPoint = emittedEntryPoint || emitEntryPoint;
        for (std::size_t j = 0; j < count; ++j) {
            fileProgram.functions.push_back(std::move(combinedProgram.functions[functionOffset + j]));
        }
        for (std::size_t j = 0; j < globalCount; ++j) {
            fileProgram.globals.push_back(std::move(combinedProgram.globals[globalOffset + j]));
        }
        for (const auto &declaration : globalDefinitions) {
            if (definedInFile.find(declaration.name) == definedInFile.end() &&
                declaredInFile.find(declaration.name) == declaredInFile.end()) {
                Function externalDeclaration;
                externalDeclaration.name = declaration.name;
                externalDeclaration.returnType = declaration.returnType;
                externalDeclaration.parameters = declaration.parameters;
                fileProgram.functions.push_back(std::move(externalDeclaration));
            }
        }
        for (const auto &global : globalVariables) {
            if (globalsInFile.find(global.name) == globalsInFile.end()) {
                GlobalVar externalGlobal;
                externalGlobal.type = global.type;
                externalGlobal.name = global.name;
                externalGlobal.symbolName = global.symbolName;
                externalGlobal.isExternal = true;
                fileProgram.globals.push_back(std::move(externalGlobal));
            }
        }
        functionOffset += count;
        globalOffset += globalCount;

        const fs::path stem = sourceInputs[i].stem();
        const fs::path asmPath = (!multipleInputs && options.asmPathExplicit)
            ? options.asmPath
            : outputDirectory / (stem.string() + ".asm");
        const fs::path objPath = (!multipleInputs && options.compileOnly)
            ? options.outputPath
            : outputDirectory / (stem.string() + objectExtension);

        PreparedTranslationUnit unit;
        unit.program = std::move(fileProgram);
        unit.asmPath = asmPath;
        unit.objPath = objPath;
        unit.emitEntryPoint = emitEntryPoint;
        preparedUnits.push_back(std::move(unit));
    }

    const std::vector<GeneratedTranslationUnit> generatedUnits =
        runParallelTasks<GeneratedTranslationUnit>(preparedUnits.size(), requestedJobs, [&](std::size_t index) {
            return [&, index]() mutable {
                PreparedTranslationUnit unit = std::move(preparedUnits[index]);
                CodeGenerator generator(options.target);
                const std::string assembly = generator.generate(unit.program, unit.emitEntryPoint);
                writeFile(unit.asmPath, assembly);

                GeneratedTranslationUnit generated;
                generated.asmPath = unit.asmPath;
                generated.objPath = unit.objPath;
                if (!options.assemblyOnly) {
                    Toolchain::assembleObject(toolchain, options.target, unit.asmPath, unit.objPath);
                    generated.producedObject = true;
                }
                return generated;
            };
        });

    for (const auto &generated : generatedUnits) {
        std::cout << "Generated assembly: " << generated.asmPath.string() << '\n';
        if (generated.producedObject) {
            objPaths.push_back(generated.objPath);
            generatedObjPaths.push_back(generated.objPath);
            std::cout << "Generated object: " << generated.objPath.string() << '\n';
        }
    }

    for (std::size_t i = 0; i < assemblyInputs.size(); ++i) {
        const fs::path stem = assemblyInputs[i].stem();
        const fs::path objPath =
            (!multipleInputs && sourceInputs.empty() && assemblyInputs.size() == 1 && options.compileOnly)
            ? options.outputPath
            : outputDirectory / (stem.string() + objectExtension);
        preparedAssemblyInputs.push_back(PreparedAssemblyInput{assemblyInputs[i], objPath});
    }

    const std::vector<GeneratedTranslationUnit> assembledInputs =
        runParallelTasks<GeneratedTranslationUnit>(preparedAssemblyInputs.size(), requestedJobs, [&](std::size_t index) {
            return [&, index]() {
                const PreparedAssemblyInput &unit = preparedAssemblyInputs[index];
                Toolchain::assembleObject(toolchain, options.target, unit.asmPath, unit.objPath);
                GeneratedTranslationUnit generated;
                generated.asmPath = unit.asmPath;
                generated.objPath = unit.objPath;
                generated.producedObject = true;
                return generated;
            };
        });

    for (const auto &generated : assembledInputs) {
        objPaths.push_back(generated.objPath);
        generatedObjPaths.push_back(generated.objPath);
        std::cout << "Assembled input: " << generated.asmPath.string() << '\n';
        std::cout << "Generated object: " << generated.objPath.string() << '\n';
    }

    objPaths.insert(objPaths.end(), objectInputs.begin(), objectInputs.end());

    if (options.assemblyOnly) {
        return 0;
    }

    if (options.compileOnly) {
        return 0;
    }

    Toolchain::invokeExternalLinker(
        linkerExecutablePath(selfPath),
        target,
        objPaths,
        options.outputPath,
        options.linkTrace,
        requestedJobs);

    if (!options.keepObject) {
        for (const auto &objPath : generatedObjPaths) {
            if (fs::exists(objPath)) {
                fs::remove(objPath);
            }
        }
    }

    std::cout << "Generated executable: " << options.outputPath.string() << '\n';
    return 0;
}

Driver::Options Driver::parseOptions(const std::vector<std::string> &args) const {
    if (args.empty()) {
        throw std::runtime_error(usage());
    }

    Options options;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-o") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing file name after -o");
            }
            options.outputPath = args[++i];
        } else if (args[i] == "--emit-asm") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing file name after --emit-asm");
            }
            options.asmPath = args[++i];
            options.asmPathExplicit = true;
        } else if (args[i] == "--keep-obj") {
            options.keepObject = true;
        } else if (args[i] == "--link-trace") {
            options.linkTrace = true;
        } else if (args[i] == "-j" || args[i] == "--jobs") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing worker count after " + args[i]);
            }
            options.jobs = sanitizeJobs(static_cast<unsigned int>(std::stoul(args[++i])));
        } else if (args[i].rfind("-j", 0) == 0 && args[i].size() > 2) {
            options.jobs = sanitizeJobs(static_cast<unsigned int>(std::stoul(args[i].substr(2))));
        } else if (args[i] == "-S") {
            options.assemblyOnly = true;
            options.keepObject = true;
        } else if (args[i] == "-c") {
            options.compileOnly = true;
            options.keepObject = true;
        } else if (args[i] == "--target") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing target name after --target");
            }
            options.target = parseTargetName(args[++i]);
        } else {
            options.inputPaths.push_back(args[i]);
        }
    }

    if (options.inputPaths.empty()) {
        throw std::runtime_error(usage());
    }

    if (options.outputPath.empty()) {
        const std::string outputName =
            options.inputPaths.size() == 1 ? options.inputPaths[0].stem().string() : "a";
        std::string extension;
        if (options.compileOnly) {
            extension = targetSpec(options.target).objectExtension;
        } else {
            extension = targetSpec(options.target).executableExtension;
        }
        options.outputPath = fs::path("build") / "output" / (outputName + extension);
    } else if (options.compileOnly && !options.outputPath.has_extension()) {
        options.outputPath += targetSpec(options.target).objectExtension;
    }
    if (options.asmPath.empty()) {
        options.asmPath = options.outputPath.parent_path() / (options.outputPath.stem().string() + ".asm");
    }

    return options;
}

fs::path Driver::linkerExecutablePath(const fs::path &selfPath) {
    fs::path resolved = selfPath;
    if (resolved.empty()) {
        resolved = fs::current_path() / "minic.exe";
    } else if (resolved.is_relative()) {
        resolved = fs::current_path() / resolved;
    }
    resolved = resolved.lexically_normal();

    std::string extension = resolved.extension().string();
    if (extension.empty()) {
        extension = ".exe";
    }

    return resolved.parent_path() / ("minic-link" + extension);
}

bool Driver::isObjectInput(const fs::path &path) {
    const std::string extension = path.extension().string();
    return extension == ".obj" || extension == ".o";
}

bool Driver::isAssemblyInput(const fs::path &path) {
    const std::string extension = path.extension().string();
    return extension == ".asm";
}

std::string Driver::readFile(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open input file: " + path.string());
    }

    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void Driver::writeFile(const fs::path &path, const std::string &content) {
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to write file: " + path.string());
    }
    out << content;
}

unsigned int Driver::sanitizeJobs(unsigned int requestedJobs) {
    if (requestedJobs == 0) {
        return detectedParallelism();
    }
    return requestedJobs;
}

std::string Driver::usage() {
    return "Usage: minic <input.c|input.asm|input.obj|input.o>... [--target x86_64-windows|x86_64-linux] [-S] [-c] [-o output] [--emit-asm output.asm] [--keep-obj] [--link-trace] [-j N|--jobs N]";
}
