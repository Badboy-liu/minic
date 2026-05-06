#pragma once

#include <filesystem>
#include <iosfwd>
#include <vector>

class ElfLinker {
public:
    static void linkObjects(
        const std::vector<std::filesystem::path> &objPaths,
        const std::filesystem::path &exePath,
        unsigned int jobs = 0,
        std::ostream *trace = nullptr);
};
