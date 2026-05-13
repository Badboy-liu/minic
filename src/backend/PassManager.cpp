#include "PassManager.h"
#include "CFGBuilder.h"
#include "Optimizer.h"

void PassManager::addPass(std::unique_ptr<OptimizationPass> pass) {
    passes.push_back(std::move(pass));
}

bool PassManager::needsCFG() const {
    for (auto &pass : passes) {
        if (dynamic_cast<CFGPass *>(pass.get())) {
            return true;
        }
    }
    return false;
}

void PassManager::runFunction(Function &function, Optimizer &optimizer) {
    // 如果有 CFGPass，预先构建 CFG（只构建一次）
    std::unique_ptr<CFG> cfg;
    if (needsCFG() && function.body) {
        CFGBuilder builder;
        cfg = std::make_unique<CFG>(builder.build(function));
    }

    for (auto &pass : passes) {
        if (auto *cfgPass = dynamic_cast<CFGPass *>(pass.get())) {
            if (cfg) {
                cfgPass->runOnCFG(function, *cfg, optimizer);
            }
        } else {
            pass->run(function, optimizer);
        }
    }
}

void CFGPass::run(Function &function, Optimizer &optimizer) {
    // 不应被直接调用 — PassManager 通过 runOnCFG 分发
    (void)function;
    (void)optimizer;
}

void ConstantPropagationPass::run(Function &function, Optimizer &optimizer) {
    optimizer.propagateBlock(*function.body);
}

void TailRecursionPass::run(Function &function, Optimizer &optimizer) {
    optimizer.eliminateTailRecursion(function);
}

void ConstantFoldingPass::run(Function &function, Optimizer &optimizer) {
    optimizer.optimizeBlock(*function.body);
}

void CSEPass::run(Function &function, Optimizer &optimizer) {
    optimizer.applyCSE(function);
}

void CopyPropagationPass::run(Function &function, Optimizer &optimizer) {
    optimizer.applyCopyPropagation(function);
}

void DeadStoreElimPass::run(Function &function, Optimizer &optimizer) {
    optimizer.applyDeadStoreElimination(function);
}

void LoopUnrollPass::run(Function &function, Optimizer &optimizer) {
    optimizer.applyLoopUnrolling(function);
}
