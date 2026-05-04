#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Target.h"

struct ToolchainPaths {
    std::filesystem::path assembler;
    AssemblerFlavor assemblerFlavor = AssemblerFlavor::NasmCompatible;
    std::filesystem::path wsl;
    std::string linuxLinker = "gcc";
};

class Toolchain {
public:
    static ToolchainPaths detect();
    static void invokeExternalLinker(
        const std::filesystem::path &linkerExecutable,
        const TargetSpec &target,
        const std::vector<std::filesystem::path> &objPaths,
        const std::filesystem::path &exePath,
        bool traceLinker,
        unsigned int jobs);
    static void assembleObject(
        const ToolchainPaths &paths,
        TargetKind target,
        const std::filesystem::path &asmPath,
        const std::filesystem::path &objPath);
    static void linkObjects(const ToolchainPaths &paths, const TargetSpec &target, const std::vector<std::filesystem::path> &objPaths, const std::filesystem::path &exePath, bool traceLinker, unsigned int jobs);

private:
    static std::filesystem::path findExecutableOnPath(const std::string &name);
};
