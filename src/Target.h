#pragma once

#include <stdexcept>
#include <string>

enum class TargetKind {
    WindowsX64,
    LinuxX64
};

inline const char *targetName(TargetKind target) {
    switch (target) {
    case TargetKind::WindowsX64:
        return "x86_64-windows";
    case TargetKind::LinuxX64:
        return "x86_64-linux";
    }
    return "unknown";
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
