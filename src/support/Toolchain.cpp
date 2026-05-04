#include "Toolchain.h"

#include "AssemblerBackend.h"
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

    const std::vector<fs::path> assemblerCandidates = {
        "D:/software/nasm/nasm.exe",
        "C:/Program Files/NASM/nasm.exe"
    };

    for (const auto &candidate : assemblerCandidates) {
        if (fs::exists(candidate)) {
            paths.assembler = candidate;
            break;
        }
    }
    if (paths.assembler.empty()) {
        paths.assembler = findExecutableOnPath("nasm.exe");
    }
    if (paths.assembler.empty()) {
        throw std::runtime_error("could not find a compatible assembler executable");
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
    const TargetSpec &target,
    const std::vector<fs::path> &objPaths,
    const fs::path &exePath,
    bool traceLinker,
    unsigned int jobs) {
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
    arguments.push_back(target.name);
    arguments.push_back("-o");
    arguments.push_back(exePath.string());
    if (jobs > 0) {
        arguments.push_back("--jobs");
        arguments.push_back(std::to_string(jobs));
    }
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
    AssemblerInvocation invocation;
    invocation.target = &spec;
    invocation.inputPath = asmPath;
    invocation.outputPath = objPath;
    createAssemblerBackend(paths.assemblerFlavor)->assemble(paths, invocation);
}

void Toolchain::linkObjects(
    const ToolchainPaths &paths,
    const TargetSpec &target,
    const std::vector<fs::path> &objPaths,
    const fs::path &exePath,
    bool traceLinker,
    unsigned int jobs) {
    LinkerInvocation invocation;
    invocation.target = &target;
    invocation.objPaths = objPaths;
    invocation.outputPath = exePath;
    invocation.traceLinker = traceLinker;
    invocation.jobs = jobs;
    createLinkerBackend(target.linkerFlavor)->link(paths, invocation);
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
