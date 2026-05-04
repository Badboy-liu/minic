#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Target.h"

class Driver {
public:
    int run(const std::filesystem::path &selfPath, const std::vector<std::string> &args);

private:
    struct Options {
        std::vector<std::filesystem::path> inputPaths;
        std::filesystem::path outputPath;
        std::filesystem::path asmPath;
        TargetKind target = TargetKind::WindowsX64;
        bool asmPathExplicit = false;
        bool assemblyOnly = false;
        bool compileOnly = false;
        bool keepObject = false;
        bool linkTrace = false;
        unsigned int jobs = 0;
    };

    Options parseOptions(const std::vector<std::string> &args) const;
    static std::filesystem::path linkerExecutablePath(const std::filesystem::path &selfPath);
    static bool isObjectInput(const std::filesystem::path &path);
    static bool isAssemblyInput(const std::filesystem::path &path);
    static std::string readFile(const std::filesystem::path &path);
    static void writeFile(const std::filesystem::path &path, const std::string &content);
    static unsigned int sanitizeJobs(unsigned int requestedJobs);
    static std::string usage();
};
