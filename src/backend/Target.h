#pragma once

#include <stdexcept>
#include <string>

enum class LinkerFlavor {
    BuiltinPeCoff,
    WslGccElf
};

enum class AbiFlavor {
    WindowsX64,
    SystemVAMD64
};

enum class RuntimeEntryFlavor {
    ExitProcessStub,
    LinuxSyscall
};

enum class WindowsObjectBackend {
    Nasm,
    Coff
};

enum class TargetKind {
    WindowsX64,
    LinuxX64
};

struct TargetSpec {
    TargetKind kind;
    const char *name;
    const char *nasmObjectFormat;
    const char *objectExtension;
    const char *executableExtension;
    const char *entrySymbol;
    int integerRegisterArgumentCount;
    LinkerFlavor linkerFlavor;
    AbiFlavor abiFlavor;
    RuntimeEntryFlavor runtimeEntryFlavor;
};

inline const TargetSpec &targetSpec(TargetKind target) {
    static const TargetSpec windows{
        TargetKind::WindowsX64,
        "x86_64-windows",
        "win64",
        ".obj",
        ".exe",
        "mainCRTStartup",
        4,
        LinkerFlavor::BuiltinPeCoff,
        AbiFlavor::WindowsX64,
        RuntimeEntryFlavor::ExitProcessStub
    };
    static const TargetSpec linux{
        TargetKind::LinuxX64,
        "x86_64-linux",
        "elf64",
        ".o",
        "",
        "_start",
        6,
        LinkerFlavor::WslGccElf,
        AbiFlavor::SystemVAMD64,
        RuntimeEntryFlavor::LinuxSyscall
    };

    switch (target) {
    case TargetKind::WindowsX64:
        return windows;
    case TargetKind::LinuxX64:
        return linux;
    }
    return windows;
}

inline const char *targetName(TargetKind target) {
    return targetSpec(target).name;
}

inline TargetKind parseTargetName(const std::string &name) {
    if (name == "x86_64-windows" || name == "windows" || name == "win64") {
        return TargetKind::WindowsX64;
    }
    if (name == "x86_64-linux" || name == "linux" || name == "elf64") {
        return TargetKind::LinuxX64;
    }
    throw std::runtime_error("unknown target: " + name);
}

inline const char *windowsObjectBackendName(WindowsObjectBackend backend) {
    switch (backend) {
    case WindowsObjectBackend::Nasm:
        return "nasm";
    case WindowsObjectBackend::Coff:
        return "coff";
    }
    return "nasm";
}

inline WindowsObjectBackend parseWindowsObjectBackend(const std::string &name) {
    if (name == "nasm") {
        return WindowsObjectBackend::Nasm;
    }
    if (name == "coff") {
        return WindowsObjectBackend::Coff;
    }
    throw std::runtime_error("unknown windows object backend: " + name);
}
