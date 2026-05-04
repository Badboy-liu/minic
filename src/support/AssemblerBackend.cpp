#include "AssemblerBackend.h"

#include "ProcessUtils.h"

#include <filesystem>
#include <memory>
#include <stdexcept>

namespace fs = std::filesystem;

namespace {

class NasmCompatibleAssemblerBackend final : public AssemblerBackend {
public:
    void assemble(const ToolchainPaths &paths, const AssemblerInvocation &invocation) const override {
        if (!invocation.outputPath.parent_path().empty()) {
            fs::create_directories(invocation.outputPath.parent_path());
        }

        ProcessUtils::runCommand(
            paths.assembler,
            {
                "-f",
                invocation.target->objectFormat,
                "-o",
                invocation.outputPath.string(),
                invocation.inputPath.string()
            },
            "assembling generated x64 assembly");
    }
};

} // namespace

std::unique_ptr<AssemblerBackend> createAssemblerBackend(AssemblerFlavor flavor) {
    switch (flavor) {
    case AssemblerFlavor::NasmCompatible:
        return std::make_unique<NasmCompatibleAssemblerBackend>();
    }
    throw std::runtime_error("unsupported assembler backend flavor");
}
