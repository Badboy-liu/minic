#include "Toolchain.h"

#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

ToolchainPaths Toolchain::detect() {
    ToolchainPaths paths;

    std::vector<fs::path> vsRoots = {
        "D:/software/vs/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC",
        "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC",
        "C:/Program Files/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC"
    };

    if (const char *env = std::getenv("VCToolsInstallDir")) {
        vsRoots.insert(vsRoots.begin(), fs::path(env).parent_path());
    }

    for (const auto &root : vsRoots) {
        if (!fs::exists(root)) {
            continue;
        }

        const fs::path versionDir = findNewestDirectory(root);
        if (versionDir.empty()) {
            continue;
        }

        const fs::path binDir = versionDir / "bin" / "Hostx64" / "x64";
        const fs::path ml64 = binDir / "ml64.exe";
        const fs::path link = binDir / "link.exe";
        if (fs::exists(ml64) && fs::exists(link)) {
            paths.ml64 = ml64;
            paths.link = link;
            break;
        }
    }

    if (paths.ml64.empty() || paths.link.empty()) {
        throw std::runtime_error("could not find Visual Studio ml64.exe and link.exe");
    }

    std::vector<fs::path> sdkRoots = {
        "C:/Program Files (x86)/Windows Kits/10/Lib"
    };

    if (const char *env = std::getenv("WindowsSdkDir")) {
        sdkRoots.insert(sdkRoots.begin(), fs::path(env) / "Lib");
    }

    for (const auto &root : sdkRoots) {
        if (!fs::exists(root)) {
            continue;
        }

        fs::path versionDir;
        if (const char *versionEnv = std::getenv("WindowsSDKLibVersion")) {
            const fs::path candidate = root / fs::path(versionEnv);
            if (fs::exists(candidate)) {
                versionDir = candidate;
            }
        }
        if (versionDir.empty()) {
            versionDir = findNewestDirectory(root);
        }

        const fs::path umLib = versionDir / "um" / "x64";
        if (fs::exists(umLib / "kernel32.lib")) {
            paths.sdkUmLib = umLib;
            break;
        }
    }

    if (paths.sdkUmLib.empty()) {
        throw std::runtime_error("could not find Windows SDK import libraries");
    }

    return paths;
}

void Toolchain::assembleAndLink(
    const ToolchainPaths &paths,
    const fs::path &asmPath,
    const fs::path &objPath,
    const fs::path &exePath) {
    runCommand(
        quote(paths.ml64) + " /nologo /c /Fo" + quote(objPath) + " " + quote(asmPath),
        "assembling generated x64 assembly");

    runCommand(
        quote(paths.link) +
            " /nologo /subsystem:console /entry:mainCRTStartup /machine:x64 /out:" + quote(exePath) +
            " " + quote(objPath) + " /libpath:" + quote(paths.sdkUmLib) + " kernel32.lib",
        "linking executable");
}

fs::path Toolchain::findNewestDirectory(const fs::path &root) {
    fs::path newest;

    for (const auto &entry : fs::directory_iterator(root)) {
        if (!entry.is_directory()) {
            continue;
        }
        if (newest.empty() || entry.path().filename().string() > newest.filename().string()) {
            newest = entry.path();
        }
    }

    return newest;
}

void Toolchain::runCommand(const std::string &command, const std::string &what) {
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);

    PROCESS_INFORMATION processInfo{};
    std::string mutableCommand = command;

    const BOOL created = CreateProcessA(
        nullptr,
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
        throw std::runtime_error(what + " failed to start");
    }

    WaitForSingleObject(processInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    if (exitCode != 0) {
        throw std::runtime_error(what + " failed with exit code " + std::to_string(exitCode));
    }
}

std::string Toolchain::quote(const fs::path &path) {
    return "\"" + path.string() + "\"";
}
