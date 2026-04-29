#include "BuiltinPeCoffLinkerBackend.h"

#include "PeLinker.h"

#include <iostream>

void BuiltinPeCoffLinkerBackend::link(
    const ToolchainPaths &,
    const TargetSpec &,
    const std::vector<std::filesystem::path> &objPaths,
    const std::filesystem::path &exePath,
    bool traceLinker) const {
    PeLinker::linkObjects(objPaths, exePath, traceLinker ? &std::cout : nullptr);
}
