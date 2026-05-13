#pragma once
#include "IRValue.h"
#include <memory>
#include <vector>

namespace ir {

class IRBasicBlock;

enum class IROpcode {
    // 算术
    Add, Sub, Mul, SDiv, UDiv, SRem, URem,
    FAdd, FSub, FMul, FDiv,
    // 位运算
    And, Or, Xor, Shl, LShr, AShr,
    // 比较
    ICmp, FCmp,
    // 内存
    Alloca, Load, Store, GEP,
    // 控制流（终结指令）
    Br, CondBr, Ret, Switch,
    // 类型转换
    Trunc, ZExt, SExt, BitCast,
    IntToPtr, PtrToInt,
    FPToUI, FPToSI, UIToFP, SIToFP,
    // 其他
    Phi, Call, Select, ExtractValue,
    // 内联汇编
    InlineAsm,
};

// 整数比较条件
enum class ICmpKind {
    EQ, NE, SLT, SLE, SGT, SGE, ULT, ULE, UGT, UGE,
};

// 浮点比较条件
enum class FCmpKind {
    OEQ, ONE, OLT, OLE, OGT, OGE,
    UEQ, UNE, ULT, ULE, UGT, UGE,
};

// 判断是否是终结指令
inline bool isTerminator(IROpcode op) {
    return op == IROpcode::Br || op == IROpcode::CondBr ||
           op == IROpcode::Ret || op == IROpcode::Switch;
}

class IRInstruction : public IRValue {
public:
    IROpcode opcode;
    std::vector<IRValue *> operands;
    IRBasicBlock *parentBB = nullptr;

    // 指令的数字 ID（用于打印 %0, %1, ...）
    int id = -1;

    IRInstruction(IROpcode op, IRType resultType, std::vector<IRValue *> ops = {})
        : IRValue(IRValueKind::Instruction, std::move(resultType)), opcode(op),
          operands(std::move(ops)) {}

    bool isTerminator() const { return ir::isTerminator(opcode); }

    // 删除前调用：从所有操作数的 use 列表中移除此指令
    // 注意：ICmp/FCmp 的 operands[2] 存储的是比较类型（枚举值），不是真正的 IRValue 指针，
    // 必须跳过这些非值操作数（地址 < 4096 的指针是枚举值，不是堆对象）
    void cleanupOperands() {
        for (int i = 0; i < getNumOperands(); ++i) {
            auto *op = getOperand(i);
            if (op && reinterpret_cast<uintptr_t>(op) >= 4096) {
                auto &uses = op->uses;
                for (auto it = uses.begin(); it != uses.end(); ++it) {
                    if (it->user == this) { uses.erase(it); break; }
                }
            }
        }
    }

    // 便捷方法
    IRValue *getOperand(int i) const { return operands[i]; }
    int getNumOperands() const { return static_cast<int>(operands.size()); }
    void setOperand(int i, IRValue *v);

    std::string toString() const override;

    // 工厂方法（返回 unique_ptr，由 appendInstruction 转移所有权）
    static std::unique_ptr<IRInstruction> createAlloca(IRType elemType, const std::string &name = "");
    static std::unique_ptr<IRInstruction> createLoad(IRValue *ptr, const std::string &name = "");
    static std::unique_ptr<IRInstruction> createStore(IRValue *val, IRValue *ptr);
    static std::unique_ptr<IRInstruction> createGEP(IRValue *ptr, IRValue *idx, const std::string &name = "");
    static std::unique_ptr<IRInstruction> createBinary(IROpcode op, IRValue *lhs, IRValue *rhs, const std::string &name = "");
    static std::unique_ptr<IRInstruction> createICmp(ICmpKind kind, IRValue *lhs, IRValue *rhs, const std::string &name = "");
    static std::unique_ptr<IRInstruction> createFCmp(FCmpKind kind, IRValue *lhs, IRValue *rhs, const std::string &name = "");
    static std::unique_ptr<IRInstruction> createBr(IRBasicBlock *target);
    static std::unique_ptr<IRInstruction> createCondBr(IRValue *cond, IRBasicBlock *trueBB, IRBasicBlock *falseBB);
    static std::unique_ptr<IRInstruction> createRet(IRValue *val = nullptr);
    static std::unique_ptr<IRInstruction> createPhi(IRType ty, const std::string &name = "");
    static std::unique_ptr<IRInstruction> createCall(IRValue *func, std::vector<IRValue *> args, IRType retTy, const std::string &name = "");
    static std::unique_ptr<IRInstruction> createCast(IROpcode op, IRValue *val, IRType destTy, const std::string &name = "");
    static std::unique_ptr<IRInstruction> createSelect(IRValue *cond, IRValue *trueVal, IRValue *falseVal, const std::string &name = "");
};

// Phi 节点的便捷操作
inline void addPhiIncoming(IRInstruction *phi, IRValue *val, IRBasicBlock *bb) {
    int valIdx = static_cast<int>(phi->operands.size());
    phi->operands.push_back(val);
    phi->operands.push_back(reinterpret_cast<IRValue *>(bb));
    if (val) val->addUse(phi, valIdx);
}

} // namespace ir
