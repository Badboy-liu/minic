#include "Toolchain.h"

#include "LinkerBackend.h"
#include "ProcessUtils.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

ToolchainPaths Toolchain::detect() {
    ToolchainPaths paths;

    const std::vector<fs::path> nasmCandidates = {
        "D:/software/nasm/nasm.exe",
        "C:/Program Files/NASM/nasm.exe"
    };

    for (const auto &candidate : nasmCandidates) {
        if (fs::exists(candidate)) {
            paths.nasm = candidate;
            break;
        }
    }
    if (paths.nasm.empty()) {
        paths.nasm = findExecutableOnPath("nasm.exe");
    }
    if (paths.nasm.empty()) {
        throw std::runtime_error("could not find nasm.exe");
    }

    const fs::path wslCandidate = "C:/Windows/System32/wsl.exe";
    if (fs::exists(wslCandidate)) {
        paths.wsl = wslCandidate;
    } else {
        paths.wsl = findExecutableOnPath("wsl.exe");
    }

    return paths;
}

void Toolchain::invokeExternalLinker(
    const fs::path &linkerExecutable,
    TargetKind target,
    const std::vector<fs::path> &objPaths,
    const fs::path &exePath,
    bool traceLinker) {
    if (objPaths.empty()) {
        throw std::runtime_error("no object inputs were produced for final linking");
    }
    if (!fs::exists(linkerExecutable)) {
        throw std::runtime_error("standalone linker executable not found: " + linkerExecutable.string());
    }

    std::vector<std::string> arguments;
    arguments.reserve(objPaths.size() + 5);
    for (const auto &objPath : objPaths) {
        arguments.push_back(objPath.string());
    }
    arguments.push_back("--target");
    arguments.push_back(targetSpec(target).name);
    arguments.push_back("-o");
    arguments.push_back(exePath.string());
    if (traceLinker) {
        arguments.push_back("--link-trace");
    }

    ProcessUtils::runCommand(linkerExecutable, arguments, "invoking standalone minic-link");
}

void Toolchain::assembleObject(
    const ToolchainPaths &paths,
    TargetKind target,
    const fs::path &asmPath,
    const fs::path &objPath) {
    const TargetSpec &spec = targetSpec(target);
    if (!objPath.parent_path().empty()) {
        fs::create_directories(objPath.parent_path());
    }

    ProcessUtils::runCommand(
        paths.nasm,
        {"-f", spec.nasmObjectFormat, "-o", objPath.string(), asmPath.string()},
        "assembling generated NASM x64 assembly");
}

void Toolchain::linkObjects(
    const ToolchainPaths &paths,
    TargetKind target,
    const std::vector<fs::path> &objPaths,
    const fs::path &exePath,
    bool traceLinker) {
    const TargetSpec &spec = targetSpec(target);
    createLinkerBackend(spec.linkerFlavor)->link(paths, spec, objPaths, exePath, traceLinker);
}

fs::path Toolchain::findExecutableOnPath(const std::string &name) {
    const char *pathEnv = std::getenv("PATH");
    if (!pathEnv) {
        return {};
    }

    std::stringstream stream(pathEnv);
    std::string directory;
    while (std::getline(stream, directory, ';')) {
        if (directory.empty()) {
            continue;
        }

        const fs::path candidate = fs::path(directory) / name;
        if (fs::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}
