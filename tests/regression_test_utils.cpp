#include "regression_test_utils.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

std::string quoteArgument(const std::string &text) {
    if (text.empty()) {
        return "\"\"";
    }

    const bool needsQuotes = text.find_first_of(" \t\"") != std::string::npos;
    if (!needsQuotes) {
        return text;
    }

    std::string escaped = "\"";
    std::size_t backslashCount = 0;
    for (const char ch : text) {
        if (ch == '\\') {
            ++backslashCount;
            escaped.push_back(ch);
            continue;
        }
        if (ch == '"') {
            escaped.insert(escaped.end(), backslashCount, '\\');
            escaped.push_back('\\');
            escaped.push_back('"');
            backslashCount = 0;
            continue;
        }
        backslashCount = 0;
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::string buildCommandLine(const fs::path &executable, const std::vector<std::string> &arguments) {
    std::string command = quoteArgument(executable.string());
    for (const auto &argument : arguments) {
        command += " ";
        command += quoteArgument(argument);
    }
    return command;
}

std::vector<char> buildEnvironmentBlock(
    const std::unordered_map<std::string, std::optional<std::string>> &overrides) {
    if (overrides.empty()) {
        return {};
    }

    LPCH rawEnvironment = GetEnvironmentStringsA();
    if (rawEnvironment == nullptr) {
        throw std::runtime_error("failed to query process environment");
    }

    std::unordered_map<std::string, std::string> environment;
    for (const char *entry = rawEnvironment; *entry != '\0'; entry += std::strlen(entry) + 1) {
        std::string text(entry);
        const std::size_t separator = text.find('=');
        if (separator == std::string::npos || separator == 0) {
            continue;
        }
        environment.emplace(text.substr(0, separator), text.substr(separator + 1));
    }
    FreeEnvironmentStringsA(rawEnvironment);

    for (const auto &[name, value] : overrides) {
        if (value.has_value()) {
            environment[name] = *value;
        } else {
            environment.erase(name);
        }
    }

    std::vector<std::string> entries;
    entries.reserve(environment.size());
    for (const auto &[name, value] : environment) {
        entries.push_back(name + "=" + value);
    }
    std::sort(entries.begin(), entries.end());

    std::vector<char> block;
    for (const auto &entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back('\0');
    }
    block.push_back('\0');
    return block;
}

void expectContainsAll(const std::string &haystack, const std::vector<std::string> &markers, const char *label) {
    for (const auto &marker : markers) {
        EXPECT_NE(haystack.find(marker), std::string::npos) << "missing " << label << " marker: " << marker
                                                            << "\noutput:\n"
                                                            << haystack;
    }
}

std::string readFile(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

namespace RegressionTestUtils {

fs::path repoRoot() {
    return fs::path(MINIC_TEST_REPO_ROOT);
}

fs::path compilerPath() {
    return fs::path(MINIC_TEST_COMPILER_PATH);
}

fs::path linkerPath() {
    return fs::path(MINIC_TEST_LINKER_PATH);
}

fs::path relocProbePath() {
    return fs::path(MINIC_TEST_RELOC_PROBE_PATH);
}

ProcessResult runProcess(
    const fs::path &executable,
    const std::vector<std::string> &arguments,
    const fs::path &workingDirectory,
    const std::unordered_map<std::string, std::optional<std::string>> &environment) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &attributes, 0)) {
        throw std::runtime_error("failed to create process output pipe");
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION processInfo{};
    const std::string executableString = executable.string();
    std::string commandLine = buildCommandLine(executable, arguments);
    std::vector<char> environmentBlock = buildEnvironmentBlock(environment);

    const BOOL created = CreateProcessA(
        executableString.c_str(),
        commandLine.data(),
        nullptr,
        nullptr,
        TRUE,
        0,
        environmentBlock.empty() ? nullptr : environmentBlock.data(),
        workingDirectory.string().c_str(),
        &startupInfo,
        &processInfo);

    CloseHandle(writePipe);

    if (!created) {
        CloseHandle(readPipe);
        throw std::runtime_error("failed to start process: " + commandLine);
    }

    std::string output;
    std::array<char, 4096> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer.data(), bytesRead);
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);

    CloseHandle(readPipe);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return ProcessResult{static_cast<int>(exitCode), std::move(output)};
}

bool isWslAvailable() {
    return !findWsl().empty();
}

fs::path findWsl() {
    const std::vector<fs::path> candidates = {
        "C:/Windows/System32/wsl.exe"
    };
    for (const auto &candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool isWslGccAvailable() {
    const fs::path wsl = findWsl();
    if (wsl.empty()) {
        return false;
    }
    const ProcessResult result = runProcess(wsl, {"sh", "-lc", "command -v gcc >/dev/null 2>&1"}, repoRoot());
    return result.exitCode == 0;
}

fs::path findAssembler() {
    const std::vector<fs::path> candidates = {
        "D:/software/nasm/nasm.exe",
        "C:/Program Files/NASM/nasm.exe"
    };
    for (const auto &candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    char foundPath[MAX_PATH] = {};
    const DWORD result = SearchPathA(nullptr, "nasm.exe", nullptr, MAX_PATH, foundPath, nullptr);
    if (result > 0) {
        return fs::path(foundPath);
    }
    throw std::runtime_error("Could not find a compatible assembler executable");
}

fs::path toRepoPath(const std::string &relativePath) {
    return repoRoot() / fs::path(relativePath);
}

fs::path toWindowsObjectPathForAsm(const fs::path &asmPath) {
    const fs::path directory = repoRoot() / "build/test-objects";
    fs::create_directories(directory);
    return directory / (asmPath.stem().string() + ".obj");
}

std::string toWslPath(const fs::path &path) {
    std::string normalized = fs::absolute(path).lexically_normal().string();
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.size() >= 2 && normalized[1] == ':' && std::isalpha(static_cast<unsigned char>(normalized[0]))) {
        const char driveLetter = static_cast<char>(std::tolower(static_cast<unsigned char>(normalized[0])));
        return "/mnt/" + std::string(1, driveLetter) + normalized.substr(2);
    }
    throw std::runtime_error("cannot convert path to WSL form: " + path.string());
}

fs::path assembleWindowsObjectFromAsm(const fs::path &asmPath) {
    const fs::path outputObject = toWindowsObjectPathForAsm(asmPath);
    const ProcessResult result = runProcess(
        findAssembler(),
        {"-f", "win64", "-o", outputObject.string(), asmPath.string()},
        repoRoot());
    EXPECT_EQ(result.exitCode, 0) << result.output;
    return outputObject;
}

int runExecutable(const fs::path &executablePath) {
    const ProcessResult result = runProcess(executablePath, {}, repoRoot());
    return result.exitCode;
}

int runWslExecutable(const fs::path &executablePath) {
    const fs::path wsl = findWsl();
    if (wsl.empty()) {
        throw std::runtime_error("WSL is required to run Linux regression cases");
    }
    const ProcessResult result = runProcess(wsl, {toWslPath(executablePath)}, repoRoot());
    return result.exitCode;
}

void runRelocProbe(const fs::path &executablePath, const std::vector<std::string> &arguments) {
    if (arguments.empty()) {
        return;
    }
    std::vector<std::string> commandArguments;
    commandArguments.push_back(executablePath.string());
    commandArguments.insert(commandArguments.end(), arguments.begin(), arguments.end());
    const ProcessResult result = runProcess(relocProbePath(), commandArguments, repoRoot());
    EXPECT_EQ(result.exitCode, 0) << result.output;
}

void runCompilerRegression(const CompilerRegressionCase &testCase) {
    if (testCase.skipIfWslUnavailable && !isWslGccAvailable()) {
        GTEST_SKIP() << "WSL with gcc is not available on this host";
    }

    std::unordered_map<std::string, std::optional<std::string>> environment;
    if (!testCase.catalogFixture.empty()) {
        environment["MINIC_IMPORT_CATALOG"] = toRepoPath(testCase.catalogFixture).string();
    } else {
        environment["MINIC_IMPORT_CATALOG"] = std::nullopt;
    }

    std::vector<std::string> arguments;
    for (const auto &source : testCase.sources) {
        const fs::path resolved = toRepoPath(source);
        if (testCase.preassembleAsmInputs && resolved.extension() == ".asm") {
            arguments.push_back(assembleWindowsObjectFromAsm(resolved).string());
        } else {
            arguments.push_back(resolved.string());
        }
    }
    for (const auto &argument : testCase.compilerArgs) {
        if (argument.rfind("REPO:", 0) == 0) {
            arguments.push_back((repoRoot() / argument.substr(5)).string());
        } else {
            arguments.push_back(argument);
        }
    }

    const ProcessResult compiler = runProcess(compilerPath(), arguments, repoRoot(), environment);
    if (testCase.expectCompilerFailure) {
        EXPECT_NE(compiler.exitCode, 0) << compiler.output;
    } else {
        EXPECT_EQ(compiler.exitCode, 0) << compiler.output;
    }

    expectContainsAll(compiler.output, testCase.requiredTraceMarkers, "trace");
    expectContainsAll(compiler.output, testCase.requiredOutputMarkers, "compiler output");
    expectContainsAll(compiler.output, testCase.requiredErrorMarkers, "compiler error");

    if (testCase.expectCompilerFailure) {
        return;
    }

    if (!testCase.checkFile.empty()) {
        const std::string fileContents = readFile(toRepoPath(testCase.checkFile));
        expectContainsAll(fileContents, testCase.requiredFileMarkers, "file");
    }

    if (!testCase.relocProbe.empty()) {
        runRelocProbe(toRepoPath(testCase.outputExe), testCase.relocProbeArgs);
    }

    if (testCase.skipRun) {
        return;
    }

    const fs::path executable = toRepoPath(testCase.outputExe);
    const int exitCode = testCase.runWithWsl
        ? runWslExecutable(executable)
        : runExecutable(executable);
    EXPECT_EQ(exitCode, testCase.expectedExit);
}

void runLinkerRegression(const LinkerRegressionCase &testCase) {
    if (testCase.skipIfWslUnavailable && !isWslGccAvailable()) {
        GTEST_SKIP() << "WSL with gcc is not available on this host";
    }

    const std::unordered_map<std::string, std::optional<std::string>> environment = {
        {"MINIC_IMPORT_CATALOG", std::nullopt}
    };

    ASSERT_FALSE(testCase.sources.empty());
    ASSERT_EQ(testCase.sources.size(), testCase.outputObjects.size());

    std::vector<std::string> resolvedObjects;
    for (std::size_t index = 0; index < testCase.sources.size(); ++index) {
        const fs::path source = toRepoPath(testCase.sources[index]);
        const fs::path object = toRepoPath(testCase.outputObjects[index]);
        resolvedObjects.push_back(object.string());

        std::vector<std::string> compilerArguments = {
            source.string(),
            "--target",
            testCase.target
        };
        compilerArguments.insert(compilerArguments.end(), testCase.compilerArgs.begin(), testCase.compilerArgs.end());
        compilerArguments.insert(compilerArguments.end(), {"-c", "-o", object.string()});

        const ProcessResult compile = runProcess(compilerPath(), compilerArguments, repoRoot(), environment);
        EXPECT_EQ(compile.exitCode, 0) << compile.output;
    }

    std::vector<std::string> linkerArguments = resolvedObjects;
    linkerArguments.insert(linkerArguments.end(), {"--target", testCase.target});
    linkerArguments.insert(linkerArguments.end(), testCase.linkerArgs.begin(), testCase.linkerArgs.end());
    linkerArguments.insert(linkerArguments.end(), {"-o", toRepoPath(testCase.outputExe).string()});

    const ProcessResult link = runProcess(linkerPath(), linkerArguments, repoRoot(), environment);
    EXPECT_EQ(link.exitCode, 0) << link.output;

    const fs::path executable = toRepoPath(testCase.outputExe);
    const int exitCode = testCase.runWithWsl
        ? runWslExecutable(executable)
        : runExecutable(executable);
    EXPECT_EQ(exitCode, testCase.expectedExit);
}

} // namespace RegressionTestUtils
