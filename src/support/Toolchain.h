#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Target.h"

struct ToolchainPaths {
    std::filesystem::path nasm;
    std::filesystem::path wsl;
    std::string linuxLinker = "gcc";
};

class Toolchain {
public:
    static ToolchainPaths detect();
    static void invokeExternalLinker(
        const std::filesystem::path &linkerExecutable,
        TargetKind target,
        const std::vector<std::filesystem::path> &objPaths,
        const std::filesystem::path &exePath,
        bool traceLinker);
    static void assembleObject(
        const ToolchainPaths &paths,
        TargetKind target,
        const std::filesystem::path &asmPath,
        const std::filesystem::path &objPath);
    static void linkObjects(
        const ToolchainPaths &paths,
        TargetKind target,
        const std::vector<std::filesystem::path> &objPaths,
        const std::filesystem::path &exePath,
        bool traceLinker);

private:
    static std::filesystem::path findExecutableOnPath(const std::string &name);
};
