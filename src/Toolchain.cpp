#include "Toolchain.h"

#include "PeLinker.h"

#include <Windows.h>

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

    return paths;
}

void Toolchain::assembleObject(
    const ToolchainPaths &paths,
    TargetKind target,
    const fs::path &asmPath,
    const fs::path &objPath) {
    if (!objPath.parent_path().empty()) {
        fs::create_directories(objPath.parent_path());
    }

    runCommand(
        paths.nasm,
        {"-f", target == TargetKind::WindowsX64 ? "win64" : "elf64", "-o", quote(objPath), quote(asmPath)},
        "assembling generated NASM x64 assembly");
}

void Toolchain::linkObjects(
    TargetKind target,
    const std::vector<fs::path> &objPaths,
    const fs::path &exePath) {
    if (target != TargetKind::WindowsX64) {
        throw std::runtime_error(
            std::string("linking is not implemented yet for target ") + targetName(target) +
            "; use -S or -c to generate portable assembly/object output");
    }

    PeLinker::linkObjects(objPaths, exePath);
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

void Toolchain::runCommand(
    const fs::path &executable,
    const std::vector<std::string> &arguments,
    const std::string &what) {
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};
    const std::string executableString = executable.string();
    std::string mutableCommand = buildCommandLine(executable, arguments);

    const BOOL created = CreateProcessA(
        executableString.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created) {
        throw std::runtime_error(what + " failed to start: " + mutableCommand);
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    if (exitCode != 0) {
        throw std::runtime_error(
            what + " failed with exit code " + std::to_string(exitCode) + ": " + mutableCommand);
    }
}

std::string Toolchain::buildCommandLine(
    const fs::path &executable,
    const std::vector<std::string> &arguments) {
    std::string command = quote(executable);
    for (const auto &argument : arguments) {
        command += " ";
        command += argument;
    }
    return command;
}

std::string Toolchain::quote(const fs::path &path) {
    return "\"" + path.string() + "\"";
}
