#include "WslGccElfLinkerBackend.h"

#include "ProcessUtils.h"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

void WslGccElfLinkerBackend::link(
    const ToolchainPaths &paths,
    const TargetSpec &target,
    const std::vector<fs::path> &objPaths,
    const fs::path &exePath,
    bool) const {
    validateObjectInputs(target, objPaths);

    if (paths.wsl.empty()) {
        throw std::runtime_error(
            std::string(target.name) +
            " linking requires WSL; install and initialize a WSL distribution, then ensure gcc is available");
    }

    const int gccCheckExitCode = ProcessUtils::runCommandAllowFailure(
        paths.wsl,
        {"sh", "-lc", "command -v " + paths.linuxLinker + " >/dev/null 2>&1"},
        "probing WSL linux linker backend");
    if (gccCheckExitCode != 0) {
        throw std::runtime_error(
            std::string(target.name) + " linking requires a working WSL distribution with gcc installed and runnable");
    }

    if (!exePath.parent_path().empty()) {
        fs::create_directories(exePath.parent_path());
    }

    std::vector<std::string> arguments = {
        paths.linuxLinker,
        "-nostdlib",
        "-no-pie",
        "-o",
        ProcessUtils::toWslPath(exePath)
    };
    for (const auto &objPath : objPaths) {
        arguments.push_back(ProcessUtils::toWslPath(objPath));
    }

    try {
        ProcessUtils::runCommand(paths.wsl, arguments, "linking ELF executable with WSL gcc backend");
    } catch (const std::runtime_error &) {
        throw std::runtime_error(
            std::string(target.name) +
            " linking via WSL gcc failed; verify WSL virtualization support and gcc availability");
    }
}

void WslGccElfLinkerBackend::validateObjectInputs(
    const TargetSpec &target,
    const std::vector<fs::path> &objPaths) {
    for (const auto &objPath : objPaths) {
        if (objPath.extension() != target.objectExtension) {
            throw std::runtime_error(
                std::string("target ") + target.name +
                " only accepts ELF " + target.objectExtension +
                " object inputs for final linking: " + objPath.string());
        }
    }
}
