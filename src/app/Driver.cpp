#include "Driver.h"

#include "CodeGenerator.h"
#include "ir/IRLowering.h"
#include "ir/IRCodeGenerator.h"
#include "ir/SSAConstructor.h"
#include "ir/passes/IRPassManager.h"
#include "ir/passes/IRConstProp.h"
#include "ir/passes/IRDeadCodeElim.h"
#include "ir/passes/IRCopyProp.h"
#include "ir/passes/IRStrengthReduction.h"
#include "ir/passes/IRLICM.h"
#include "Diagnostics.h"
#include "Lexer.h"
#include "Optimizer.h"
#include "Parser.h"
#include "Preprocessor.h"
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
            results.push_back(std::move(factory(i)()));
        }
        return results;
    }

    std::vector<std::future<Result>> futures;
    futures.reserve(taskCount);
    for (std::size_t i = 0; i < taskCount; ++i) {
        futures.push_back(std::async(std::launch::async, factory(i)));
    }
    for (auto &future : futures) {
        results.push_back(std::move(future.get()));
    }
    return results;
}

struct ParsedTranslationUnit {
    Program program;
    std::size_t functionCount = 0;
    std::size_t globalCount = 0;
    DiagnosticEngine diag;
    std::vector<fs::path> includedFiles;
};

struct PreparedTranslationUnit {
    Program program;
    fs::path asmPath;
    fs::path objPath;
    std::string sourceFileName;
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
    DiagnosticEngine diag;
    std::vector<ParsedTranslationUnit> parsedUnits =
        runParallelTasks<ParsedTranslationUnit>(sourceInputs.size(), requestedJobs, [&](std::size_t index) {
            return [&, index]() {
                const std::string rawSource = readFile(sourceInputs[index]);
                ParsedTranslationUnit unit;
                unit.diag.setSourceFile(sourceInputs[index].string());
                unit.diag.setWarningLevel(static_cast<WarningLevel>(options.warningLevel));
                unit.diag.setWarningsAsErrors(options.warningsAsErrors);
                Preprocessor preprocessor(options.includePaths, &unit.diag);
                for (const auto &def : options.defines) {
                    auto eq = def.find('=');
                    if (eq != std::string::npos) {
                        preprocessor.addDefine(def.substr(0, eq), def.substr(eq + 1));
                    } else {
                        preprocessor.addDefine(def);
                    }
                }
                const std::string source = preprocessor.processSource(sourceInputs[index], rawSource);
                unit.diag.setSourceContent(source);
                // 收集被包含的文件（用于依赖文件生成）
                std::vector<fs::path> includedFiles;
                includedFiles.push_back(sourceInputs[index]);
                for (const auto &f : preprocessor.getIncludedFiles()) {
                    includedFiles.push_back(f);
                }
                Lexer lexer(source, &unit.diag);
                auto tokens = lexer.tokenize();
                Parser parser(std::move(tokens), &unit.diag);
                unit.program = parser.parseProgram();
                unit.functionCount = unit.program.functions.size();
                unit.globalCount = unit.program.globals.size();
                unit.includedFiles = std::move(includedFiles);
                return unit;
            };
        });

    for (auto &parsedUnit : parsedUnits) {
        functionCounts.push_back(parsedUnit.functionCount);
        globalCounts.push_back(parsedUnit.globalCount);
        // 合并解析阶段的诊断信息（保留源文件路径）
        for (const auto &d : parsedUnit.diag.diagnostics()) {
            diag.setSourceFile(d.filePath);
            diag.report(d.severity, d.line, d.column, d.message);
        }
        for (auto &function : parsedUnit.program.functions) {
            combinedProgram.functions.push_back(std::move(function));
        }
        for (auto &global : parsedUnit.program.globals) {
            combinedProgram.globals.push_back(std::move(global));
        }
        for (auto &assertStmt : parsedUnit.program.globalStaticAsserts) {
            combinedProgram.globalStaticAsserts.push_back(std::move(assertStmt));
        }
    }

    // 生成依赖文件（-M 或 -MD）
    if (options.depFileOnly || options.depFileGenerate) {
        fs::path depPath = options.depFilePath;
        if (depPath.empty()) {
            // 默认依赖文件名：将第一个输入文件的扩展名替换为 .d
            depPath = sourceInputs.front().stem().string() + ".d";
        }
        // 收集所有被包含的文件（去重）
        std::unordered_set<fs::path> allIncluded;
        for (const auto &unit : parsedUnits) {
            for (const auto &f : unit.includedFiles) {
                allIncluded.insert(f);
            }
        }
        // 生成 Makefile 格式的依赖文件
        std::ostringstream depOut;
        for (const auto &src : sourceInputs) {
            // 目标文件名：将 .c 替换为 .o
            std::string target = src.stem().string() + ".o";
            depOut << target << ":";
            for (const auto &f : allIncluded) {
                depOut << " " << f.string();
            }
            depOut << "\n";
        }
        writeFile(depPath, depOut.str());
        if (options.depFileOnly) {
            // -M: 只输出依赖，不编译
            return 0;
        }
    }

    // 如果解析阶段已有错误，输出诊断信息并终止
    if (diag.hasErrors()) {
        diag.printAll(std::cerr);
        return 1;
    }

    std::vector<Function> globalDefinitions;
    std::vector<GlobalVar> globalVariables;
    if (!sourceInputs.empty()) {
        SemanticAnalyzer semanticAnalyzer(&diag);
        semanticAnalyzer.setShadowWarning(options.enableShadowWarning || options.warningLevel >= 2);
        semanticAnalyzer.setUnusedParamWarning(options.enableUnusedParamWarning || options.warningLevel >= 3);
        const bool requireMain = !options.assemblyOnly && !options.compileOnly;
        try {
            semanticAnalyzer.analyze(combinedProgram, requireMain);
        } catch (const std::runtime_error &) {
            // 语义错误已通过 diag 收集
        }
        // 如果语义分析阶段有错误，输出诊断信息并终止
        if (diag.hasErrors()) {
            diag.printAll(std::cerr);
            return 1;
        }
        Optimizer optimizer;
        optimizer.optimize(combinedProgram, options.optimizationLevel);

        for (const auto &function : combinedProgram.functions) {
            if (!function.isDeclaration()) {
                Function declaration;
                declaration.name = function.name;
                declaration.returnType = function.returnType;
                declaration.parameters = function.parameters;
                declaration.isVariadic = function.isVariadic;
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
                externalDeclaration.isVariadic = declaration.isVariadic;
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
        unit.sourceFileName = sourceInputs[i].filename().string();
        preparedUnits.push_back(std::move(unit));
    }

    const std::vector<GeneratedTranslationUnit> generatedUnits =
        runParallelTasks<GeneratedTranslationUnit>(preparedUnits.size(), requestedJobs, [&](std::size_t index) {
            return [&, index]() mutable {
                PreparedTranslationUnit unit = std::move(preparedUnits[index]);
                std::string assembly;
                if (options.useIR) {
                    // SSA IR 流水线：AST → IR → SSA → 优化 → x64
                    std::cerr << "[Pipeline] lowering..." << std::endl;
                    ir::IRLowering lowering;
                    auto irModule = lowering.lowerProgram(unit.program);
                    auto constPool = irModule->getConstantPool();
                    ir::SSAConstructor ssa;
                    for (auto &fn : irModule->functions) {
                        fn->rebuildUseLists(constPool);
                        ssa.run(*fn, constPool);
                        constPool = irModule->getConstantPool();
                        fn->rebuildUseLists(constPool);
                    }
                    std::cerr << "[Pipeline] optimization passes..." << std::endl;
                    // 优化 pass
                    {
                        ir::IRPassManager passMgr;
                        passMgr.addPass(std::make_unique<ir::IRConstProp>());
                        passMgr.addPass(std::make_unique<ir::IRDeadCodeElim>());
                        passMgr.addPass(std::make_unique<ir::IRCopyProp>());
                        passMgr.addPass(std::make_unique<ir::IRStrengthReduction>());
                        passMgr.addPass(std::make_unique<ir::IRLICM>());
                        for (auto &fn : irModule->functions) {
                            constPool = irModule->getConstantPool();
                            fn->rebuildUseLists(constPool);
                            std::cerr << "[Pipeline] runToFixpoint: " << fn->name << std::endl;
                            passMgr.runToFixpoint(*fn, *irModule);
                            constPool = irModule->getConstantPool();
                            fn->rebuildUseLists(constPool);
                        }
                    }
                    std::cerr << "[Pipeline] code generation..." << std::endl;
                    ir::IRCodeGenerator irCodeGen;
                    assembly = irCodeGen.generate(*irModule);
                    std::cerr << "[Pipeline] done." << std::endl;
                } else {
                    // 传统流水线：AST → x64
                    CodeGenerator generator(options.target);
                    generator.setSourceFileName(unit.sourceFileName);
                    assembly = generator.generate(unit.program, unit.emitEntryPoint);
                }
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
        if (!diag.diagnostics().empty()) {
            diag.printAll(std::cerr);
        }
        return 0;
    }

    if (options.compileOnly) {
        if (!diag.diagnostics().empty()) {
            diag.printAll(std::cerr);
        }
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

    if (!diag.diagnostics().empty()) {
        diag.printAll(std::cerr);
    }
    std::cout << "Generated executable: " << options.outputPath.string() << '\n';
    return 0;
}

// 展开响应文件（@file）：将 @file 替换为文件中的参数
static std::vector<std::string> expandResponseFiles(const std::vector<std::string> &args) {
    std::vector<std::string> expanded;
    for (const auto &arg : args) {
        if (!arg.empty() && arg[0] == '@') {
            std::string path = arg.substr(1);
            std::ifstream in(path);
            if (!in) {
                throw std::runtime_error("cannot open response file: " + path);
            }
            std::string line;
            while (std::getline(in, line)) {
                // 移除行尾 \r
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                // 跳过空行和注释
                auto trimmed = line.find_first_not_of(" \t");
                if (trimmed == std::string::npos || line[trimmed] == '#') continue;
                // 提取到行尾或注释
                auto end = line.find_first_of(" \t#", trimmed);
                if (end == std::string::npos) end = line.size();
                expanded.push_back(line.substr(trimmed, end - trimmed));
            }
        } else {
            expanded.push_back(arg);
        }
    }
    return expanded;
}

Driver::Options Driver::parseOptions(const std::vector<std::string> &rawArgs) const {
    const std::vector<std::string> args = expandResponseFiles(rawArgs);
    if (args.empty()) {
        throw std::runtime_error(usage());
    }

    Options options;

    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") {
            throw std::runtime_error(usage());
        } else if (args[i] == "--version") {
            throw std::runtime_error("minic 0.1.0 (C compiler + PE/COFF linker, x86_64)");
        } else if (args[i] == "-o") {
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
        } else if (args[i] == "-I") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing directory path after -I");
            }
            options.includePaths.push_back(args[++i]);
        } else if (args[i].rfind("-I", 0) == 0 && args[i].size() > 2) {
            // 支持 -Ipath 形式（无空格）
            options.includePaths.push_back(args[i].substr(2));
        } else if (args[i] == "-D") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing macro name after -D");
            }
            options.defines.push_back(args[++i]);
        } else if (args[i].rfind("-D", 0) == 0 && args[i].size() > 2) {
            options.defines.push_back(args[i].substr(2));
        } else if (args[i] == "-Wall") {
            options.warningLevel = 2;
        } else if (args[i] == "-Wextra") {
            options.warningLevel = 3;
        } else if (args[i] == "-Werror") {
            options.warningsAsErrors = true;
        } else if (args[i] == "-Wshadow") {
            options.enableShadowWarning = true;
        } else if (args[i] == "-Wunused-parameter") {
            options.enableUnusedParamWarning = true;
        } else if (args[i] == "-M") {
            options.depFileOnly = true;
        } else if (args[i] == "-MD") {
            options.depFileGenerate = true;
        } else if (args[i] == "-MF") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing filename after -MF");
            }
            options.depFilePath = args[++i];
        } else if (args[i] == "-w") {
            options.warningLevel = 0;
        } else if (args[i] == "--target") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing target name after --target");
            }
            options.target = parseTargetName(args[++i]);
        } else if (args[i] == "--ir") {
            options.useIR = true;
        } else if (args[i].rfind("-O", 0) == 0 && args[i].size() == 3) {
            char level = args[i][2];
            if (level < '0' || level > '2') {
                throw std::runtime_error("unsupported optimization level: " + args[i] + " (supported: -O0, -O1, -O2)");
            }
            options.optimizationLevel = level - '0';
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
    return extension == ".obj" || extension == ".o" || extension == ".lib" || extension == ".a";
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
    return "Usage: minic <input.c|input.asm|input.obj|input.o>... [options]\n\n"
           "Options:\n"
           "  --target <target>     Target: x86_64-windows (default), x86_64-linux\n"
           "  -O0|-O1|-O2           Optimization level (default: -O2)\n"
           "  -S                    Compile to assembly only\n"
           "  -c                    Compile to object file only\n"
           "  -o <file>             Output file path\n"
           "  --emit-asm <file>     Emit assembly to file\n"
           "  --keep-obj            Keep intermediate object file\n"
           "  --link-trace          Print linker trace\n"
           "  -j N|--jobs N         Parallel compilation jobs\n"
           "  -I <dir>              Add include search path\n"
           "  -D <name>[=value]     Define macro\n"
           "  -M                    Output dependency file only (no compile)\n"
           "  -MD                   Compile and generate dependency file\n"
           "  -MF <file>            Dependency file output path\n"
           "  -Wall                 Enable all warnings\n"
           "  -Wextra               Enable extra warnings\n"
           "  -Wshadow              Warn about shadowed variables\n"
           "  -Wunused-parameter    Warn about unused parameters\n"
           "  -Werror               Treat warnings as errors\n"
           "  -w                    Suppress all warnings\n"
           "  @<file>               Read arguments from response file\n"
           "  --help                Show this help message\n"
           "  --version             Show version information";
}
