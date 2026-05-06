#include "BuiltinElfLinkerBackend.h"

#include "ElfLinker.h"

#include <iostream>
#include <stdexcept>

void BuiltinElfLinkerBackend::link(
    const ToolchainPaths &,
    const LinkerInvocation &invocation) const {
    const TargetSpec &target = *invocation.target;
    for (const auto &objPath : invocation.objPaths) {
        if (objPath.extension() != target.objectExtension) {
            throw std::runtime_error(
                std::string("target ") + target.name +
                " only accepts ELF " + target.objectExtension +
                " object inputs for final linking: " + objPath.string());
        }
    }
    ElfLinker::linkObjects(
        invocation.objPaths,
        invocation.outputPath,
        invocation.jobs,
        invocation.traceLinker ? &std::cout : nullptr);
}
