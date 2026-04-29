#include "ProcessUtils.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

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

std::string buildCommandLine(
    const fs::path &executable,
    const std::vector<std::string> &arguments) {
    std::string command = quoteArgument(executable.string());
    for (const auto &argument : arguments) {
        command += " ";
        command += quoteArgument(argument);
    }
    return command;
}

} // namespace

namespace ProcessUtils {

void runCommand(
    const fs::path &executable,
    const std::vector<std::string> &arguments,
    const std::string &what) {
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};
    const std::string executableString = executable.string();
    std::string mutableCommand = buildCommandLine(executable, arguments);

    const BOOL created = CreateProcessA(
        executableString.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created) {
        throw std::runtime_error(what + " failed to start: " + mutableCommand);
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    if (exitCode != 0) {
        throw std::runtime_error(
            what + " failed with exit code " + std::to_string(exitCode) + ": " + mutableCommand);
    }
}

int runCommandAllowFailure(
    const fs::path &executable,
    const std::vector<std::string> &arguments,
    const std::string &what) {
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};
    const std::string executableString = executable.string();
    std::string mutableCommand = buildCommandLine(executable, arguments);

    const BOOL created = CreateProcessA(
        executableString.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created) {
        throw std::runtime_error(what + " failed to start: " + mutableCommand);
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return static_cast<int>(exitCode);
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

} // namespace ProcessUtils
