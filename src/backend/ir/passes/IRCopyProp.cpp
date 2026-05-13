#include "IRCopyProp.h"

namespace ir {

bool IRCopyProp::runOnFunction(IRFunction &func, IRModule &) {
    changed = false;

    for (auto &bb : func.basicBlocks) {
        auto it = bb.instructions.begin();
        while (it != bb.instructions.end()) {
            auto &inst = *it;

            // 检查是否是复制指令：单操作数的非终结指令
            // 例如：%b = load %ptr（结果等价于 %ptr 指向的值）
            // 但这里我们更关注 phi 和 cast 类型的复制
            bool isCopy = false;
            IRValue *source = nullptr;

            if (inst.opcode == IROpcode::Phi && inst.operands.size() == 2) {
                // phi 只有一个 incoming 值，等价于复制
                source = inst.getOperand(0);
                isCopy = true;
            } else if (inst.opcode == IROpcode::BitCast ||
                       inst.opcode == IROpcode::ZExt ||
                       inst.opcode == IROpcode::SExt ||
                       inst.opcode == IROpcode::Trunc ||
                       inst.opcode == IROpcode::PtrToInt ||
                       inst.opcode == IROpcode::IntToPtr) {
                // 类型转换指令，如果源和目标的 kind + bitWidth 相同，可以消除
                auto *val = inst.getOperand(0);
                if (val->type.kind == inst.type.kind &&
                    val->type.bitWidth == inst.type.bitWidth) {
                    source = val;
                    isCopy = true;
                }
            }

            if (isCopy && source && source != &inst) {
                inst.replaceAllUsesWith(source);
                inst.cleanupOperands();
                it = bb.instructions.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }

    return changed;
}

} // namespace ir
