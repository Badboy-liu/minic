#include "LinkerDriver.h"

#include "Toolchain.h"

#include <iostream>
#include <stdexcept>

namespace fs = std::filesystem;

int LinkerDriver::run(const std::vector<std::string> &args) {
    const Options options = parseOptions(args);
    const ToolchainPaths toolchain = Toolchain::detect();
    Toolchain::linkObjects(toolchain, options.target, options.inputPaths, options.outputPath, options.linkTrace);
    std::cout << "Generated executable: " << options.outputPath.string() << '\n';
    return 0;
}

LinkerDriver::Options LinkerDriver::parseOptions(const std::vector<std::string> &args) const {
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
        } else if (args[i] == "--target") {
            if (i + 1 >= args.size()) {
                throw std::runtime_error("missing target name after --target");
            }
            options.target = parseTargetName(args[++i]);
        } else if (args[i] == "--link-trace") {
            options.linkTrace = true;
        } else {
            const fs::path inputPath = args[i];
            if (!isObjectInput(inputPath)) {
                throw std::runtime_error("minic-link only accepts object inputs (.obj or .o): " + inputPath.string());
            }
            options.inputPaths.push_back(inputPath);
        }
    }

    if (options.inputPaths.empty()) {
        throw std::runtime_error(usage());
    }

    if (options.outputPath.empty()) {
        const std::string outputName =
            options.inputPaths.size() == 1 ? options.inputPaths[0].stem().string() : "a";
        options.outputPath = fs::path("build") / "output" /
            (outputName + targetSpec(options.target).executableExtension);
    }

    return options;
}

bool LinkerDriver::isObjectInput(const fs::path &path) {
    const std::string extension = path.extension().string();
    return extension == ".obj" || extension == ".o";
}

std::string LinkerDriver::usage() {
    return "Usage: minic-link <input.obj|input.o>... [--target x86_64-windows|x86_64-linux] [-o output] [--link-trace]";
}
