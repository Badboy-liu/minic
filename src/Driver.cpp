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

namespace fs = std::filesystem;

int Driver::run(const std::vector<std::string> &args) {
    const Options options = parseOptions(args);

    const std::string source = readFile(options.inputPath);
    Lexer lexer(source);
    Parser parser(lexer.tokenize());
    Program program = parser.parseProgram();

    SemanticAnalyzer semanticAnalyzer;
    semanticAnalyzer.analyze(program);

    CodeGenerator generator;
    const std::string assembly = generator.generate(program);
    writeFile(options.asmPath, assembly);

    const fs::path objPath = options.outputPath.parent_path() / (options.outputPath.stem().string() + ".obj");
    const ToolchainPaths toolchain = Toolchain::detect();
    Toolchain::assembleAndLink(toolchain, options.asmPath, objPath, options.outputPath);

    if (!options.keepObject && fs::exists(objPath)) {
        fs::remove(objPath);
    }

    std::cout << "Generated assembly: " << options.asmPath.string() << '\n';
    std::cout << "Generated executable: " << options.outputPath.string() << '\n';
    return 0;
}

Driver::Options Driver::parseOptions(const std::vector<std::string> &args) const {
    if (args.empty()) {
        throw std::runtime_error(usage());
    }

    Options options;
    options.inputPath = args[0];

    for (std::size_t i = 1; i < args.size(); ++i) {
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
        } else if (args[i] == "--keep-obj") {
            options.keepObject = true;
        } else {
            throw std::runtime_error("unknown option: " + args[i] + "\n" + usage());
        }
    }

    if (options.outputPath.empty()) {
        options.outputPath = options.inputPath.parent_path() / (options.inputPath.stem().string() + ".exe");
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
    return "Usage: minic <input.c> [-o output.exe] [--emit-asm output.asm] [--keep-obj]";
}
