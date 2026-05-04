#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ProcessResult {
    int exitCode = -1;
    std::string output;
};

struct CompilerRegressionCase {
    std::string name;
    std::vector<std::string> sources;
    std::vector<std::string> compilerArgs;
    std::string outputExe;
    int expectedExit = 0;
    std::vector<std::string> requiredTraceMarkers;
    std::vector<std::string> requiredOutputMarkers;
    std::vector<std::string> requiredErrorMarkers;
    std::string checkFile;
    std::vector<std::string> requiredFileMarkers;
    std::string catalogFixture;
    std::string relocProbe;
    std::vector<std::string> relocProbeArgs;
    bool expectCompilerFailure = false;
    bool runWithWsl = false;
    bool skipIfWslUnavailable = false;
    bool skipRun = false;
    bool preassembleAsmInputs = false;
};

struct LinkerRegressionCase {
    std::string name;
    std::vector<std::string> sources;
    std::vector<std::string> outputObjects;
    std::string outputExe;
    int expectedExit = 0;
    std::string target = "x86_64-windows";
    std::vector<std::string> compilerArgs;
    std::vector<std::string> linkerArgs;
    bool runWithWsl = false;
    bool skipIfWslUnavailable = false;
};

namespace RegressionTestUtils {

std::filesystem::path repoRoot();
std::filesystem::path compilerPath();
std::filesystem::path linkerPath();
std::filesystem::path relocProbePath();

ProcessResult runProcess(
    const std::filesystem::path &executable,
    const std::vector<std::string> &arguments,
    const std::filesystem::path &workingDirectory,
    const std::unordered_map<std::string, std::optional<std::string>> &environment = {});

bool isWslAvailable();
bool isWslGccAvailable();
std::filesystem::path findWsl();
std::filesystem::path findAssembler();
std::filesystem::path toRepoPath(const std::string &relativePath);
std::filesystem::path toWindowsObjectPathForAsm(const std::filesystem::path &asmPath);
std::string toWslPath(const std::filesystem::path &path);
std::filesystem::path assembleWindowsObjectFromAsm(const std::filesystem::path &asmPath);
int runExecutable(const std::filesystem::path &executablePath);
int runWslExecutable(const std::filesystem::path &executablePath);
void runRelocProbe(const std::filesystem::path &executablePath, const std::vector<std::string> &arguments);

void runCompilerRegression(const CompilerRegressionCase &testCase);
void runLinkerRegression(const LinkerRegressionCase &testCase);

} // namespace RegressionTestUtils
