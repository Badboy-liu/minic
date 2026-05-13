#pragma once
#include "IRPassManager.h"

namespace ir {

// 常量传播与折叠 Pass
// 对所有指令检查：若操作数全为常量，计算结果替换为常量
class IRConstProp : public IRPass {
public:
    const char *name() const override { return "constprop"; }
    bool runOnFunction(IRFunction &func, IRModule &module) override;

private:
    bool changed = false;

    // 尝试折叠一条指令，返回替换常量或 nullptr
    IRValue *tryFold(IRInstruction &inst, IRFunction &func, IRModule &module);

    // 辅助：尝试将 ICmp 折叠为常量
    IRValue *tryFoldICmp(IRInstruction &inst, IRFunction &func, IRModule &module);
};

} // namespace ir
