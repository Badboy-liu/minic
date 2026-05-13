#pragma once
#include "IRPassManager.h"
#include <unordered_set>

namespace ir {

// 死代码消除 Pass
// 1. 删除无 uses 的非副作用指令
// 2. 删除不可达基本块
class IRDeadCodeElim : public IRPass {
public:
    const char *name() const override { return "dce"; }
    bool runOnFunction(IRFunction &func, IRModule &module) override;

private:
    bool changed = false;

    // 删除无 uses 的指令
    void removeUnusedInstructions(IRFunction &func);

    // 删除不可达基本块
    void removeUnreachableBlocks(IRFunction &func);

    // 从入口 BFS 标记可达块
    std::unordered_set<IRBasicBlock *> computeReachableBlocks(IRFunction &func);

    // 判断指令是否有副作用（不可删除）
    bool hasSideEffects(const IRInstruction &inst);
};

} // namespace ir
