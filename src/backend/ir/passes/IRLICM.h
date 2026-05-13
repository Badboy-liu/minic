#pragma once
#include "IRPassManager.h"
#include <unordered_set>

namespace ir {

// 循环不变量外提 Pass (Loop Invariant Code Motion)
// 检测循环中的不变指令，将其移到循环前驱块
class IRLICM : public IRPass {
public:
    const char *name() const override { return "licm"; }
    bool runOnFunction(IRFunction &func, IRModule &module) override;

private:
    bool changed = false;

    // 检测自然循环（基于后边）
    struct Loop {
        IRBasicBlock *header;
        std::unordered_set<int> body; // 循环体块 ID 集合
    };
    std::vector<Loop> findLoops(IRFunction &func);

    // 获取循环的前驱块（header 的不在循环中的前驱）
    IRBasicBlock *getPreheader(const Loop &loop);

    // 判断指令是否为循环不变量
    bool isLoopInvariant(const IRInstruction &inst, const Loop &loop,
                         const std::unordered_set<int> &invariantVregs);

    // 判断指令是否可安全外提（无副作用、非终结指令）
    bool isSafeToHoist(const IRInstruction &inst);
};

} // namespace ir
