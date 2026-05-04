#pragma once

#include <filesystem>
#include <memory>

#include "Target.h"
#include "Toolchain.h"

struct AssemblerInvocation {
    const TargetSpec *target = nullptr;
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
};

class AssemblerBackend {
public:
    virtual ~AssemblerBackend() = default;

    virtual void assemble(const ToolchainPaths &paths, const AssemblerInvocation &invocation) const = 0;
};

std::unique_ptr<AssemblerBackend> createAssemblerBackend(AssemblerFlavor flavor);
