#include "LinkerBackend.h"

#include "BuiltinPeCoffLinkerBackend.h"
#include "WslGccElfLinkerBackend.h"

#include <memory>
#include <stdexcept>
#include <string>

std::unique_ptr<LinkerBackend> createLinkerBackend(LinkerFlavor flavor) {
    switch (flavor) {
    case LinkerFlavor::BuiltinPeCoff:
        return std::make_unique<BuiltinPeCoffLinkerBackend>();
    case LinkerFlavor::WslGccElf:
        return std::make_unique<WslGccElfLinkerBackend>();
    }
    throw std::runtime_error("unsupported linker backend flavor");
}
