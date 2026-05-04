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
};

class LinkerBackend {
public:
    virtual ~LinkerBackend() = default;

    virtual void link(const ToolchainPaths &paths, const LinkerInvocation &invocation) const = 0;
};

std::unique_ptr<LinkerBackend> createLinkerBackend(LinkerFlavor flavor);
