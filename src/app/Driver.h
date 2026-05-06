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
        std::vector<std::filesystem::path> includePaths;  // -I 搜索路径
        std::vector<std::string> defines;  // -D 宏定义
        std::filesystem::path outputPath;
        std::filesystem::path asmPath;
        TargetKind target = TargetKind::WindowsX64;
        bool asmPathExplicit = false;
        bool assemblyOnly = false;
        bool compileOnly = false;
        bool keepObject = false;
        bool linkTrace = false;
        int optimizationLevel = 2;  // -O0/-O1/-O2
        int warningLevel = 1;  // 0=none, 1=default, 2=all, 3=extra
        bool warningsAsErrors = false;  // -Werror
        bool enableShadowWarning = false;  // -Wshadow
        bool enableUnusedParamWarning = false;  // -Wunused-parameter
        bool depFileOnly = false;           // -M: 只输出依赖，不编译
        bool depFileGenerate = false;       // -MD: 编译并生成依赖文件
        std::filesystem::path depFilePath;  // -MF: 依赖文件输出路径
        unsigned int jobs = 0;
    };

    Options parseOptions(const std::vector<std::string> &rawArgs) const;
    static std::filesystem::path linkerExecutablePath(const std::filesystem::path &selfPath);
    static bool isObjectInput(const std::filesystem::path &path);
    static bool isAssemblyInput(const std::filesystem::path &path);
    static std::string readFile(const std::filesystem::path &path);
    static void writeFile(const std::filesystem::path &path, const std::string &content);
    static unsigned int sanitizeJobs(unsigned int requestedJobs);
    static std::string usage();
};
