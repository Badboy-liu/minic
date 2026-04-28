#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Target.h"

struct ToolchainPaths {
    std::filesystem::path nasm;
};

class Toolchain {
public:
    static ToolchainPaths detect();
    static void assembleObject(
        const ToolchainPaths &paths,
        TargetKind target,
        const std::filesystem::path &asmPath,
        const std::filesystem::path &objPath);
    static void linkObjects(
        TargetKind target,
        const std::vector<std::filesystem::path> &objPaths,
        const std::filesystem::path &exePath,
        bool traceLinker);

private:
    static std::filesystem::path findExecutableOnPath(const std::string &name);
    static void runCommand(
        const std::filesystem::path &executable,
        const std::vector<std::string> &arguments,
        const std::string &what);
    static std::string buildCommandLine(
        const std::filesystem::path &executable,
        const std::vector<std::string> &arguments);
    static std::string quote(const std::filesystem::path &path);
};
