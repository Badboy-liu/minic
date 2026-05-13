#include "IRPassManager.h"
#include <iostream>

namespace ir {

void IRPassManager::addPass(std::unique_ptr<IRPass> pass) {
    passes.push_back(std::move(pass));
}

bool IRPassManager::runOnFunction(IRFunction &func, IRModule &module) {
    bool changed = false;
    for (auto &pass : passes) {
        std::cerr << "[PassMgr] running pass on " << func.name << "..." << std::endl;
        changed |= pass->runOnFunction(func, module);
        std::cerr << "[PassMgr] pass done." << std::endl;
    }
    return changed;
}

bool IRPassManager::runToFixpoint(IRFunction &func, IRModule &module, int maxIterations) {
    bool anyChanged = false;
    for (int i = 0; i < maxIterations; ++i) {
        if (!runOnFunction(func, module)) break;
        anyChanged = true;
    }
    return anyChanged;
}

} // namespace ir
