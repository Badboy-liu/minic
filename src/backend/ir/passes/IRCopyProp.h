#pragma once
#include "IRPassManager.h"

namespace ir {

// 复制传播 Pass
// 当 %b = %a 时（如 load/phi/cast 等单操作数赋值），将 %b 的所有 use 替换为 %a
class IRCopyProp : public IRPass {
public:
    const char *name() const override { return "copyprop"; }
    bool runOnFunction(IRFunction &func, IRModule &module) override;

private:
    bool changed = false;
};

} // namespace ir
