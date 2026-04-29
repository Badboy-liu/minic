#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include "Target.h"
#include "Toolchain.h"

class LinkerBackend {
public:
    virtual ~LinkerBackend() = default;

    virtual void link(
        const ToolchainPaths &paths,
        const TargetSpec &target,
        const std::vector<std::filesystem::path> &objPaths,
        const std::filesystem::path &exePath,
        bool traceLinker) const = 0;
};

std::unique_ptr<LinkerBackend> createLinkerBackend(LinkerFlavor flavor);
