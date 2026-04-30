#include "Driver.h"

#include "CodeGenerator.h"
#include "CoffObjectWriter.h"
#include "Lexer.h"
#include "Parser.h"
#include "Semantics.h"
#include "Toolchain.h"
#include "WindowsObjectEmitter.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

int Driver::run(const fs::path &selfPath, const std::vector<std::string> &args) {
    const Options options = parseOptions(args);
    const TargetSpec &target = targetSpec(options.target);
    std::vector<fs::path> sourceInputs;
    std::vector<fs::path> objectInputs;
    for (const auto &inputPath : options.inputPaths) {
        if (isObjectInput(inputPath)) {
            objectInputs.push_back(inputPath);
        } else {
            sourceInputs.push_back(inputPath);
        }
    }
    if (!objectInputs.empty() && (options.assemblyOnly || options.compileOnly)) {
        throw std::runtime_error("existing object inputs are only supported for final linking");
    }

    Program combinedProgram;
    std::vector<std::size_t> functionCounts;
    std::vector<std::size_t> globalCounts;
    for (const auto &inputPath : sourceInputs) {
        const std::string source = readFile(inputPath);
        Lexer lexer(source);
        Parser parser(lexer.tokenize());
        Program fileProgram = parser.parseProgram();
        functionCounts.push_back(fileProgram.functions.size());
        globalCounts.push_back(fileProgram.globals.size());
        for (auto &function : fileProgram.functions) {
            combinedProgram.functions.push_back(std::move(function));
        }
        for (auto &global : fileProgram.globals) {
            combinedProgram.globals.push_back(std::move(global));
        }
    }

    std::vector<Function> globalDefinitions;
    std::vector<GlobalVar> globalVariables;
    if (!sourceInputs.empty()) {
        SemanticAnalyzer semanticAnalyzer;
        semanticAnalyzer.analyze(combinedProgram);

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

    CodeGenerator generator(options.target);
    WindowsObjectEmitter windowsObjectEmitter;
    const fs::path outputDirectory = options.outputPath.parent_path().empty()
        ? fs::path(".")
        : options.outputPath.parent_path();
    const std::string objectExtension = target.objectExtension;

    std::vector<fs::path> generatedObjPaths;
    std::vector<fs::path> objPaths;
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

        const bool useWindowsCoffBackend =
            options.target == TargetKind::WindowsX64 &&
            options.windowsObjectBackend == WindowsObjectBackend::Coff;

        if (useWindowsCoffBackend) {
            if (options.assemblyOnly) {
                throw std::runtime_error("assembly-only output is not supported with --windows-obj-backend coff");
            }

            const ObjectFileModel model = windowsObjectEmitter.emit(fileProgram, emitEntryPoint);
            writeBinaryFile(objPath, CoffObjectWriter::writeAmd64(model));
            objPaths.push_back(objPath);
            generatedObjPaths.push_back(objPath);
            std::cout << "Generated object: " << objPath.string() << '\n';
            continue;
        }

        const std::string assembly = generator.generate(fileProgram, emitEntryPoint);
        writeFile(asmPath, assembly);
        std::cout << "Generated assembly: " << asmPath.string() << '\n';

        if (!options.assemblyOnly) {
            Toolchain::assembleObject(toolchain, options.target, asmPath, objPath);
            objPaths.push_back(objPath);
            generatedObjPaths.push_back(objPath);
            std::cout << "Generated object: " << objPath.string() << '\n';
        }
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
        options.target,
        objPaths,
        options.outputPath,
        options.linkTrace);

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
        } else if (args[i] == "--windows-obj-backend") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing backend name after --windows-obj-backend");
            }
            options.windowsObjectBackend = parseWindowsObjectBackend(args[++i]);
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

void Driver::writeBinaryFile(const fs::path &path, const std::vector<std::uint8_t> &content) {
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path());
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("failed to write file: " + path.string());
    }
    out.write(reinterpret_cast<const char *>(content.data()), static_cast<std::streamsize>(content.size()));
}

std::string Driver::usage() {
    return "Usage: minic <input.c>... [--target x86_64-windows|x86_64-linux] [--windows-obj-backend nasm|coff] [-S] [-c] [-o output] [--emit-asm output.asm] [--keep-obj] [--link-trace]";
}
