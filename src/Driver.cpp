#include "Driver.h"

#include "CodeGenerator.h"
#include "Lexer.h"
#include "Parser.h"
#include "Semantics.h"
#include "Toolchain.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace fs = std::filesystem;

int Driver::run(const std::vector<std::string> &args) {
    const Options options = parseOptions(args);

    Program combinedProgram;
    std::vector<std::size_t> functionCounts;
    std::vector<std::size_t> globalCounts;
    for (const auto &inputPath : options.inputPaths) {
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

    SemanticAnalyzer semanticAnalyzer;
    semanticAnalyzer.analyze(combinedProgram);

    std::vector<Function> globalDefinitions;
    std::vector<GlobalVar> globalVariables;
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

    CodeGenerator generator(options.target);
    const fs::path outputDirectory = options.outputPath.parent_path().empty()
        ? fs::path(".")
        : options.outputPath.parent_path();
    const std::string objectExtension = options.target == TargetKind::WindowsX64 ? ".obj" : ".o";

    std::vector<fs::path> objPaths;
    std::size_t functionOffset = 0;
    std::size_t globalOffset = 0;
    bool emittedEntryPoint = false;
    const bool multipleInputs = options.inputPaths.size() > 1;

    const ToolchainPaths toolchain = Toolchain::detect();

    for (std::size_t i = 0; i < options.inputPaths.size(); ++i) {
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

        const fs::path stem = options.inputPaths[i].stem();
        const fs::path asmPath = (!multipleInputs && options.asmPathExplicit)
            ? options.asmPath
            : outputDirectory / (stem.string() + ".asm");
        const fs::path objPath = (!multipleInputs && options.compileOnly)
            ? options.outputPath
            : outputDirectory / (stem.string() + objectExtension);

        const std::string assembly = generator.generate(fileProgram, emitEntryPoint);
        writeFile(asmPath, assembly);
        std::cout << "Generated assembly: " << asmPath.string() << '\n';

        if (!options.assemblyOnly) {
            Toolchain::assembleObject(toolchain, options.target, asmPath, objPath);
            objPaths.push_back(objPath);
            std::cout << "Generated object: " << objPath.string() << '\n';
        }
    }

    if (options.assemblyOnly) {
        return 0;
    }

    if (options.compileOnly) {
        return 0;
    }

    Toolchain::linkObjects(options.target, objPaths, options.outputPath);

    if (!options.keepObject) {
        for (const auto &objPath : objPaths) {
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
            extension = options.target == TargetKind::WindowsX64 ? ".obj" : ".o";
        } else {
            extension = options.target == TargetKind::WindowsX64 ? ".exe" : "";
        }
        options.outputPath = fs::path("build") / "output" / (outputName + extension);
    } else if (options.compileOnly && !options.outputPath.has_extension()) {
        options.outputPath += options.target == TargetKind::WindowsX64 ? ".obj" : ".o";
    }
    if (options.asmPath.empty()) {
        options.asmPath = options.outputPath.parent_path() / (options.outputPath.stem().string() + ".asm");
    }

    return options;
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

std::string Driver::usage() {
    return "Usage: minic <input.c>... [--target x86_64-windows|x86_64-linux] [-S] [-c] [-o output] [--emit-asm output.asm] [--keep-obj]";
}
