#pragma once

#include "LinkerBackend.h"

class WslGccElfLinkerBackend final : public LinkerBackend {
public:
    void link(const ToolchainPaths &paths, const LinkerInvocation &invocation) const override;

private:
    static void validateObjectInputs(
        const TargetSpec &target,
        const std::vector<std::filesystem::path> &objPaths);
};
