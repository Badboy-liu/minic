#pragma once

#include <filesystem>
#include <iosfwd>
#include <vector>

class PeLinker {
public:
    static void linkSingleObject(
        const std::filesystem::path &objPath,
        const std::filesystem::path &exePath,
        std::ostream *trace = nullptr);
    static void linkObjects(
        const std::vector<std::filesystem::path> &objPaths,
        const std::filesystem::path &exePath,
        std::ostream *trace = nullptr);
};
