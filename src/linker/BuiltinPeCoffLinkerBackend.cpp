#include "BuiltinPeCoffLinkerBackend.h"

#include "PeLinker.h"

#include <iostream>

void BuiltinPeCoffLinkerBackend::link(
    const ToolchainPaths &,
    const LinkerInvocation &invocation) const {
    PeLinker::linkObjects(
        invocation.objPaths,
        invocation.outputPath,
        invocation.jobs,
        invocation.traceLinker ? &std::cout : nullptr);
}
