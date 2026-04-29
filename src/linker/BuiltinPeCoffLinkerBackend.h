#pragma once

#include "LinkerBackend.h"

class BuiltinPeCoffLinkerBackend final : public LinkerBackend {
public:
    void link(
        const ToolchainPaths &paths,
        const TargetSpec &target,
        const std::vector<std::filesystem::path> &objPaths,
        const std::filesystem::path &exePath,
        bool traceLinker) const override;
};
