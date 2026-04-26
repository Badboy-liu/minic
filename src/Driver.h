#pragma once

#include <filesystem>
#include <string>
#include <vector>

class Driver {
public:
    int run(const std::vector<std::string> &args);

private:
    struct Options {
        std::filesystem::path inputPath;
        std::filesystem::path outputPath;
        std::filesystem::path asmPath;
        bool keepObject = false;
    };

    Options parseOptions(const std::vector<std::string> &args) const;
    static std::string readFile(const std::filesystem::path &path);
    static void writeFile(const std::filesystem::path &path, const std::string &content);
    static std::string usage();
};
