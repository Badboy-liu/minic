#pragma once

#include <filesystem>
#include <vector>

class PeLinker {
public:
    static void linkSingleObject(
        const std::filesystem::path &objPath,
        const std::filesystem::path &exePath);
    static void linkObjects(
        const std::vector<std::filesystem::path> &objPaths,
        const std::filesystem::path &exePath);
};
