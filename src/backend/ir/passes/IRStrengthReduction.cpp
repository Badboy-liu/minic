#include "IRStrengthReduction.h"
#include <cmath>

namespace ir {

// 检查是否为 2 的幂
static bool isPowerOf2(int64_t v) {
    return v > 0 && (v & (v - 1)) == 0;
}

// 计算 log2（v 必须是 2 的幂）
static int log2(int64_t v) {
    int r = 0;
    while (v > 1) { v >>= 1; ++r; }
    return r;
}

bool IRStrengthReduction::runOnFunction(IRFunction &func, IRModule &module) {
    changed = false;

    for (auto &bb : func.basicBlocks) {
        auto it = bb.instructions.begin();
        while (it != bb.instructions.end()) {
            auto &inst = *it;

            if (inst.getNumOperands() < 2) { ++it; continue; }

            auto *rhs = inst.getOperand(1);
            auto *intConst = dynamic_cast<IRConstantInt *>(rhs);
            if (!intConst) { ++it; continue; }

            int64_t val = intConst->value;

            switch (inst.opcode) {
            case IROpcode::Mul: {
                if (val == 0) {
                    // x * 0 → 0
                    auto *zero = module.createConstantInt(0, inst.type.bitWidth);
                    inst.replaceAllUsesWith(zero);
                    inst.cleanupOperands();
                    it = bb.instructions.erase(it);
                    changed = true;
                    continue;
                }
                if (val == 1) {
                    // x * 1 → x
                    auto *lhs = inst.getOperand(0);
                    inst.replaceAllUsesWith(lhs);
                    inst.cleanupOperands();
                    it = bb.instructions.erase(it);
                    changed = true;
                    continue;
                }
                if (isPowerOf2(val)) {
                    // x * 2^n → x << n
                    int shiftAmt = log2(val);
                    auto *shiftConst = module.createConstantInt(shiftAmt, inst.type.bitWidth);
                    inst.opcode = IROpcode::Shl;
                    inst.setOperand(1, shiftConst);
                    changed = true;
                }
                break;
            }
            case IROpcode::SDiv: {
                if (val == 1) {
                    // x / 1 → x
                    auto *lhs = inst.getOperand(0);
                    inst.replaceAllUsesWith(lhs);
                    inst.cleanupOperands();
                    it = bb.instructions.erase(it);
                    changed = true;
                    continue;
                }
                if (isPowerOf2(val)) {
                    // x / 2^n → x >> n（算术右移）
                    int shiftAmt = log2(val);
                    auto *shiftConst = module.createConstantInt(shiftAmt, inst.type.bitWidth);
                    inst.opcode = IROpcode::AShr;
                    inst.setOperand(1, shiftConst);
                    changed = true;
                }
                break;
            }
            case IROpcode::UDiv: {
                if (val == 1) {
                    auto *lhs = inst.getOperand(0);
                    inst.replaceAllUsesWith(lhs);
                    inst.cleanupOperands();
                    it = bb.instructions.erase(it);
                    changed = true;
                    continue;
                }
                if (isPowerOf2(val)) {
                    int shiftAmt = log2(val);
                    auto *shiftConst = module.createConstantInt(shiftAmt, inst.type.bitWidth);
                    inst.opcode = IROpcode::LShr;
                    inst.setOperand(1, shiftConst);
                    changed = true;
                }
                break;
            }
            case IROpcode::SRem: {
                if (val == 1) {
                    // x % 1 → 0
                    auto *zero = module.createConstantInt(0, inst.type.bitWidth);
                    inst.replaceAllUsesWith(zero);
                    inst.cleanupOperands();
                    it = bb.instructions.erase(it);
                    changed = true;
                    continue;
                }
                if (isPowerOf2(val)) {
                    // x % 2^n → x & (2^n - 1)
                    int64_t mask = val - 1;
                    auto *maskConst = module.createConstantInt(mask, inst.type.bitWidth);
                    inst.opcode = IROpcode::And;
                    inst.setOperand(1, maskConst);
                    changed = true;
                }
                break;
            }
            case IROpcode::URem: {
                if (val == 1) {
                    auto *zero = module.createConstantInt(0, inst.type.bitWidth);
                    inst.replaceAllUsesWith(zero);
                    inst.cleanupOperands();
                    it = bb.instructions.erase(it);
                    changed = true;
                    continue;
                }
                if (isPowerOf2(val)) {
                    int64_t mask = val - 1;
                    auto *maskConst = module.createConstantInt(mask, inst.type.bitWidth);
                    inst.opcode = IROpcode::And;
                    inst.setOperand(1, maskConst);
                    changed = true;
                }
                break;
            }
            default:
                break;
            }

            ++it;
        }
    }

    return changed;
}

} // namespace ir
