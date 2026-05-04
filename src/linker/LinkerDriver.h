#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Target.h"

class LinkerDriver {
public:
    int run(const std::vector<std::string> &args);

private:
    struct Options {
        std::vector<std::filesystem::path> inputPaths;
        std::filesystem::path outputPath;
        TargetKind target = TargetKind::WindowsX64;
        bool linkTrace = false;
        unsigned int jobs = 0;
    };

    Options parseOptions(const std::vector<std::string> &args) const;
    static bool isObjectInput(const std::filesystem::path &path);
    static unsigned int sanitizeJobs(unsigned int requestedJobs);
    static std::string usage();
};
