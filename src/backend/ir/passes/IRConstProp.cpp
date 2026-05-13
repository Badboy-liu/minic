#include "IRConstProp.h"
#include <iostream>

namespace ir {

bool IRConstProp::runOnFunction(IRFunction &func, IRModule &module) {
    changed = false;

    for (auto &bb : func.basicBlocks) {
        auto it = bb.instructions.begin();
        while (it != bb.instructions.end()) {
            auto &inst = *it;

            // 跳过终结指令和 phi（phi 的操作数不是普通值）
            if (inst.isTerminator() || inst.opcode == IROpcode::Phi) {
                ++it;
                continue;
            }

            std::cerr << "[ConstProp] bb=" << bb.name << " op=" << static_cast<int>(inst.opcode)
                      << " uses=" << inst.uses.size() << " ops=" << inst.getNumOperands() << std::endl;

            IRValue *folded = tryFold(inst, func, module);
            if (folded) {
                std::cerr << "[ConstProp]   folded! uses_before=" << inst.uses.size() << std::endl;
                inst.replaceAllUsesWith(folded);
                std::cerr << "[ConstProp]   replaceAll done, uses_after=" << inst.uses.size() << std::endl;
                inst.cleanupOperands();
                std::cerr << "[ConstProp]   cleanup done" << std::endl;
                it = bb.instructions.erase(it);
                std::cerr << "[ConstProp]   erased" << std::endl;
                changed = true;
            } else {
                ++it;
            }
        }
    }

    return changed;
}

IRValue *IRConstProp::tryFold(IRInstruction &inst, IRFunction &func, IRModule &module) {
    if (inst.getNumOperands() == 0) return nullptr;

    // 跳过有非值操作数的指令（如存储了枚举值的 ICmp/FCmp operands[2]）
    if (inst.opcode == IROpcode::ICmp || inst.opcode == IROpcode::FCmp) {
        return tryFoldICmp(inst, func, module);
    }

    // 二元算术运算：检查操作数是否为有效常量
    // 注意：某些指令的操作数是 reinterpret_cast 的非值指针（如 BB 指针），
    // 必须先检查 valueKind 再做 dynamic_cast，否则是 UB
    auto *op0 = inst.getOperand(0);
    if (!op0 || !op0->isConstant()) return nullptr;

    if (auto *lhs = dynamic_cast<IRConstantInt *>(op0)) {
        if (inst.getNumOperands() >= 2) {
            auto *op1 = inst.getOperand(1);
            if (!op1 || !op1->isConstant()) return nullptr;
            auto *rhs = dynamic_cast<IRConstantInt *>(op1);
            if (!rhs) return nullptr;

            int64_t l = lhs->value;
            int64_t r = rhs->value;
            int64_t result = 0;
            bool valid = true;

            switch (inst.opcode) {
                case IROpcode::Add:  result = l + r; break;
                case IROpcode::Sub:  result = l - r; break;
                case IROpcode::Mul:  result = l * r; break;
                case IROpcode::SDiv:
                    if (r == 0) return nullptr;
                    result = l / r;
                    break;
                case IROpcode::SRem:
                    if (r == 0) return nullptr;
                    result = l % r;
                    break;
                case IROpcode::And:  result = l & r; break;
                case IROpcode::Or:   result = l | r; break;
                case IROpcode::Xor:  result = l ^ r; break;
                case IROpcode::Shl:  result = l << r; break;
                case IROpcode::LShr: result = static_cast<int64_t>(static_cast<uint64_t>(l) >> r); break;
                case IROpcode::AShr: result = l >> r; break;
                default: valid = false; break;
            }

            if (valid) {
                // 使用函数所属模块的常量池（通过 func 的 parent 获取模块）
                // 由于 IRFunction 不直接持有模块指针，这里创建一个新的常量
                // 注意：常量的生命周期需要由调用者管理
                return module.createConstantInt(result, lhs->type.bitWidth);
            }
        }
    }

    // 类型转换：常量到常量
    if (inst.opcode == IROpcode::ZExt || inst.opcode == IROpcode::SExt ||
        inst.opcode == IROpcode::Trunc) {
        auto *castOp = inst.getOperand(0);
        if (castOp && castOp->isConstant()) {
            if (auto *val = dynamic_cast<IRConstantInt *>(castOp)) {
                int newBits = inst.type.bitWidth;
                return module.createConstantInt(val->value, newBits);
            }
        }
    }

    return nullptr;
}

IRValue *IRConstProp::tryFoldICmp(IRInstruction &inst, IRFunction &func, IRModule &module) {
    auto *op0 = inst.getOperand(0);
    auto *op1 = inst.getOperand(1);
    if (!op0 || !op0->isConstant() || !op1 || !op1->isConstant()) return nullptr;

    auto *lhs = dynamic_cast<IRConstantInt *>(op0);
    auto *rhs = dynamic_cast<IRConstantInt *>(op1);
    if (!lhs || !rhs) return nullptr;

    // ICmp 的比较类型存在 operands[2] 中（通过指针转换存储）
    if (inst.getNumOperands() < 3) return nullptr;
    auto kind = static_cast<ICmpKind>(reinterpret_cast<uintptr_t>(inst.getOperand(2)));

    int64_t l = lhs->value;
    int64_t r = rhs->value;
    bool result = false;

    switch (kind) {
        case ICmpKind::EQ:  result = (l == r); break;
        case ICmpKind::NE:  result = (l != r); break;
        case ICmpKind::SLT: result = (l < r); break;
        case ICmpKind::SLE: result = (l <= r); break;
        case ICmpKind::SGT: result = (l > r); break;
        case ICmpKind::SGE: result = (l >= r); break;
        case ICmpKind::ULT: result = (static_cast<uint64_t>(l) < static_cast<uint64_t>(r)); break;
        case ICmpKind::ULE: result = (static_cast<uint64_t>(l) <= static_cast<uint64_t>(r)); break;
        case ICmpKind::UGT: result = (static_cast<uint64_t>(l) > static_cast<uint64_t>(r)); break;
        case ICmpKind::UGE: result = (static_cast<uint64_t>(l) >= static_cast<uint64_t>(r)); break;
        default: return nullptr;
    }

    return module.createConstantInt(result ? 1 : 0, 1);
}

} // namespace ir
