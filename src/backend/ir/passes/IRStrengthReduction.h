#pragma once
#include "IRPassManager.h"

namespace ir {

// 强度削减 Pass
// x * 2^n → x << n
// x / 2^n → x >> n (unsigned) 或算术右移 (signed)
// x * 小常量 → shift + add 组合
class IRStrengthReduction : public IRPass {
public:
    const char *name() const override { return "strengthred"; }
    bool runOnFunction(IRFunction &func, IRModule &module) override;

private:
    bool changed = false;
};

} // namespace ir
