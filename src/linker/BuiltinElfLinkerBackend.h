#pragma once

#include "LinkerBackend.h"

class BuiltinElfLinkerBackend final : public LinkerBackend {
public:
    void link(const ToolchainPaths &paths, const LinkerInvocation &invocation) const override;
};
