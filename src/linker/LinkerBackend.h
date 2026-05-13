#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "Target.h"
#include "Toolchain.h"

struct LinkerInvocation {
    const TargetSpec *target = nullptr;
    std::vector<std::filesystem::path> objPaths;
    std::filesystem::path outputPath;
    bool traceLinker = false;
    unsigned int jobs = 0;

    struct ExportEntry {
        std::string name;        // 导出名称
        std::string symbolName;  // 内部符号名（可能与 name 相同）
    };
    std::vector<ExportEntry> exports;
};

class LinkerBackend {
public:
    virtual ~LinkerBackend() = default;

    virtual void link(const ToolchainPaths &paths, const LinkerInvocation &invocation) const = 0;
};

std::unique_ptr<LinkerBackend> createLinkerBackend(LinkerFlavor flavor);
