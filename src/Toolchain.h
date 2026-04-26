#pragma once

#include <filesystem>
#include <string>

struct ToolchainPaths {
    std::filesystem::path ml64;
    std::filesystem::path link;
    std::filesystem::path sdkUmLib;
};

class Toolchain {
public:
    static ToolchainPaths detect();
    static void assembleAndLink(
        const ToolchainPaths &paths,
        const std::filesystem::path &asmPath,
        const std::filesystem::path &objPath,
        const std::filesystem::path &exePath);

private:
    static std::filesystem::path findNewestDirectory(const std::filesystem::path &root);
    static void runCommand(const std::string &command, const std::string &what);
    static std::string quote(const std::filesystem::path &path);
};
