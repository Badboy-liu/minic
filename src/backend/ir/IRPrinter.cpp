#include "IRPrinter.h"
#include <iomanip>
#include <sstream>

namespace ir {

// === IRType::toString ===

std::string IRType::toString() const {
    switch (kind) {
    case IRTypeKind::Void: return "void";
    case IRTypeKind::Int1: return "i1";
    case IRTypeKind::Int8: return "i8";
    case IRTypeKind::Int16: return "i16";
    case IRTypeKind::Int32: return "i32";
    case IRTypeKind::Int64: return "i64";
    case IRTypeKind::Float: return "float";
    case IRTypeKind::Double: return "double";
    case IRTypeKind::Pointer:
        return elementType ? elementType->toString() + "*" : "ptr";
    case IRTypeKind::Array:
        return "[" + std::to_string(arraySize) + " x " +
               (elementType ? elementType->toString() : "?") + "]";
    case IRTypeKind::Struct: {
        std::string s = "{";
        for (size_t i = 0; i < structMembers.size(); ++i) {
            if (i > 0) s += ", ";
            s += structMembers[i].toString();
        }
        s += "}";
        return s;
    }
    case IRTypeKind::Function: {
        std::string s = (returnType ? returnType->toString() : "void") + " (";
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            if (i > 0) s += ", ";
            s += paramTypes[i].toString();
        }
        if (isVarArg) {
            if (!paramTypes.empty()) s += ", ";
            s += "...";
        }
        s += ")";
        return s;
    }
    }
    return "?";
}

// === IRValue::toString ===

std::string IRValue::toString() const {
    if (!name.empty()) return "%" + name;
    return "%" + std::to_string(reinterpret_cast<uintptr_t>(this) & 0xFFFF);
}

std::string IRConstantInt::toString() const {
    return std::to_string(value);
}

std::string IRConstantFloat::toString() const {
    std::ostringstream ss;
    ss << std::setprecision(15) << value;
    return ss.str();
}

std::string IRArgument::toString() const {
    if (!name.empty()) return "%" + name;
    return "%" + std::to_string(argIndex);
}

// === IRValue use tracking ===

void IRValue::addUse(IRInstruction *user, int idx) {
    uses.push_back({user, idx});
}

void IRValue::replaceAllUsesWith(IRValue *newValue) {
    auto usesSnapshot = std::move(uses);
    uses.clear();

    std::cerr << "[RAW] this=" << this << " newValue=" << newValue << " usesSnapshot.size=" << usesSnapshot.size() << std::endl;
    for (size_t si = 0; si < usesSnapshot.size(); ++si) {
        auto &use = usesSnapshot[si];
        std::cerr << "[RAW]   use[" << si << "].user=" << use.user << " idx=" << use.operandIndex
                  << " user_ops_size=" << (use.user ? static_cast<int>(use.user->operands.size()) : -1) << std::endl;
        if (use.user && use.operandIndex >= 0 &&
            use.operandIndex < static_cast<int>(use.user->operands.size())) {
            use.user->operands[use.operandIndex] = newValue;
            if (newValue) {
                newValue->uses.push_back({use.user, use.operandIndex});
            }
        }
    }

    // 如果 this 是指令，清理其操作数的 use 列表（移除 this）
    // 注意：ICmp/FCmp 的 operands[2] 存储的是比较类型（枚举值），不是真正的 IRValue 指针，
    // 必须跳过这些非值操作数（地址 < 4096 的指针是枚举值，不是堆对象）
    if (valueKind == IRValueKind::Instruction) {
        auto *inst = static_cast<IRInstruction *>(this);
        for (int i = 0; i < inst->getNumOperands(); ++i) {
            auto *op = inst->getOperand(i);
            std::cerr << "[RAW]   cleanup op[" << i << "]=" << op << std::endl;
            if (op && reinterpret_cast<uintptr_t>(op) >= 4096) {
                auto &opUses = op->uses;
                for (auto it = opUses.begin(); it != opUses.end(); ++it) {
                    if (it->user == inst) { opUses.erase(it); break; }
                }
            }
        }
    }
    std::cerr << "[RAW] replaceAllUsesWith done" << std::endl;
}

// === IRInstruction ===

void IRInstruction::setOperand(int i, IRValue *v) {
    // 从旧操作数的 use 列表中移除
    // 跳过非值操作数（如 ICmp/FCmp 的比较类型枚举值）
    if (operands[i] && reinterpret_cast<uintptr_t>(operands[i]) >= 4096) {
        auto &oldUses = operands[i]->uses;
        for (auto it = oldUses.begin(); it != oldUses.end(); ++it) {
            if (it->user == this && it->operandIndex == i) {
                oldUses.erase(it);
                break;
            }
        }
    }
    operands[i] = v;
    if (v) v->addUse(this, i);
}

std::unique_ptr<IRInstruction> IRInstruction::createAlloca(IRType elemType, const std::string &name) {
    auto inst = std::make_unique<IRInstruction>(IROpcode::Alloca, IRType::ptr(elemType));
    inst->name = name;
    // alloca 的操作数 0 存储元素类型信息（用 nullptr 占位）
    inst->operands.push_back(nullptr);
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createLoad(IRValue *ptr, const std::string &name) {
    auto ptrTy = ptr->type;
    IRType resultTy = ptrTy.isPointer() && ptrTy.elementType ? *ptrTy.elementType : IRType::i32();
    auto inst = std::make_unique<IRInstruction>(IROpcode::Load, resultTy, std::vector<IRValue *>{ptr});
    inst->name = name;
    if (ptr) ptr->addUse(inst.get(), 0);
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createStore(IRValue *val, IRValue *ptr) {
    auto inst = std::make_unique<IRInstruction>(IROpcode::Store, IRType::voidTy(), std::vector<IRValue *>{val, ptr});
    if (val) val->addUse(inst.get(), 0);
    if (ptr) ptr->addUse(inst.get(), 1);
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createGEP(IRValue *ptr, IRValue *idx, const std::string &name) {
    // GEP 返回指向元素类型的指针
    // 衰变规则：
    //   i32[5]     → ptr(i32)     （数组衰变为指向元素的指针）
    //   ptr(i32[5]) → ptr(i32)    （指向数组的指针衰变为指向元素的指针）
    IRType resultTy = ptr->type;
    if (resultTy.isArray() && resultTy.elementType) {
        resultTy = IRType::ptr(*resultTy.elementType);
    } else if (resultTy.isPointer() && resultTy.elementType &&
               resultTy.elementType->isArray() && resultTy.elementType->elementType) {
        resultTy = IRType::ptr(*resultTy.elementType->elementType);
    }
    auto inst = std::make_unique<IRInstruction>(IROpcode::GEP, resultTy, std::vector<IRValue *>{ptr, idx});
    inst->name = name;
    if (ptr) ptr->addUse(inst.get(), 0);
    if (idx) idx->addUse(inst.get(), 1);
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createBinary(IROpcode op, IRValue *lhs, IRValue *rhs,
                                            const std::string &name) {
    auto inst = std::make_unique<IRInstruction>(op, lhs->type, std::vector<IRValue *>{lhs, rhs});
    inst->name = name;
    if (lhs) lhs->addUse(inst.get(), 0);
    if (rhs) rhs->addUse(inst.get(), 1);
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createICmp(ICmpKind kind, IRValue *lhs, IRValue *rhs,
                                          const std::string &name) {
    auto inst = std::make_unique<IRInstruction>(IROpcode::ICmp, IRType::i1(), std::vector<IRValue *>{lhs, rhs});
    inst->name = name;
    // 存储比较类型到额外字段（用 operands[2] 模拟）
    inst->operands.push_back(reinterpret_cast<IRValue *>(static_cast<uintptr_t>(kind)));
    if (lhs) lhs->addUse(inst.get(), 0);
    if (rhs) rhs->addUse(inst.get(), 1);
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createFCmp(FCmpKind kind, IRValue *lhs, IRValue *rhs,
                                          const std::string &name) {
    auto inst = std::make_unique<IRInstruction>(IROpcode::FCmp, IRType::i1(), std::vector<IRValue *>{lhs, rhs});
    inst->name = name;
    inst->operands.push_back(reinterpret_cast<IRValue *>(static_cast<uintptr_t>(kind)));
    if (lhs) lhs->addUse(inst.get(), 0);
    if (rhs) rhs->addUse(inst.get(), 1);
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createBr(IRBasicBlock *target) {
    auto inst = std::make_unique<IRInstruction>(IROpcode::Br, IRType::voidTy());
    inst->operands.push_back(reinterpret_cast<IRValue *>(target));
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createCondBr(IRValue *cond, IRBasicBlock *trueBB,
                                            IRBasicBlock *falseBB) {
    auto inst = std::make_unique<IRInstruction>(IROpcode::CondBr, IRType::voidTy(), std::vector<IRValue *>{cond});
    if (cond) cond->addUse(inst.get(), 0);
    inst->operands.push_back(reinterpret_cast<IRValue *>(trueBB));
    inst->operands.push_back(reinterpret_cast<IRValue *>(falseBB));
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createRet(IRValue *val) {
    std::vector<IRValue *> ops;
    if (val) {
        ops.push_back(val);
    }
    auto inst = std::make_unique<IRInstruction>(IROpcode::Ret, IRType::voidTy(), ops);
    if (val) val->addUse(inst.get(), 0);
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createPhi(IRType ty, const std::string &name) {
    auto inst = std::make_unique<IRInstruction>(IROpcode::Phi, std::move(ty));
    inst->name = name;
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createCall(IRValue *func, std::vector<IRValue *> args,
                                          IRType retTy, const std::string &name) {
    std::vector<IRValue *> ops = {func};
    for (auto *a : args) {
        ops.push_back(a);
    }
    auto inst = std::make_unique<IRInstruction>(IROpcode::Call, std::move(retTy), ops);
    inst->name = name;
    if (func) func->addUse(inst.get(), 0);
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i]) args[i]->addUse(inst.get(), static_cast<int>(i + 1));
    }
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createCast(IROpcode op, IRValue *val, IRType destTy,
                                          const std::string &name) {
    auto inst = std::make_unique<IRInstruction>(op, std::move(destTy), std::vector<IRValue *>{val});
    inst->name = name;
    if (val) val->addUse(inst.get(), 0);
    return inst;
}

std::unique_ptr<IRInstruction> IRInstruction::createSelect(IRValue *cond, IRValue *trueVal,
                                            IRValue *falseVal, const std::string &name) {
    auto inst = std::make_unique<IRInstruction>(IROpcode::Select, trueVal->type,
                                                 std::vector<IRValue *>{cond, trueVal, falseVal});
    inst->name = name;
    if (cond) cond->addUse(inst.get(), 0);
    if (trueVal) trueVal->addUse(inst.get(), 1);
    if (falseVal) falseVal->addUse(inst.get(), 2);
    return inst;
}

// === IRInstruction::toString ===

static const char *opcodeName(IROpcode op) {
    switch (op) {
    case IROpcode::Add: return "add";
    case IROpcode::Sub: return "sub";
    case IROpcode::Mul: return "mul";
    case IROpcode::SDiv: return "sdiv";
    case IROpcode::UDiv: return "udiv";
    case IROpcode::SRem: return "srem";
    case IROpcode::URem: return "urem";
    case IROpcode::FAdd: return "fadd";
    case IROpcode::FSub: return "fsub";
    case IROpcode::FMul: return "fmul";
    case IROpcode::FDiv: return "fdiv";
    case IROpcode::And: return "and";
    case IROpcode::Or: return "or";
    case IROpcode::Xor: return "xor";
    case IROpcode::Shl: return "shl";
    case IROpcode::LShr: return "lshr";
    case IROpcode::AShr: return "ashr";
    case IROpcode::ICmp: return "icmp";
    case IROpcode::FCmp: return "fcmp";
    case IROpcode::Alloca: return "alloca";
    case IROpcode::Load: return "load";
    case IROpcode::Store: return "store";
    case IROpcode::GEP: return "getelementptr";
    case IROpcode::Br: return "br";
    case IROpcode::CondBr: return "br";
    case IROpcode::Ret: return "ret";
    case IROpcode::Switch: return "switch";
    case IROpcode::Trunc: return "trunc";
    case IROpcode::ZExt: return "zext";
    case IROpcode::SExt: return "sext";
    case IROpcode::BitCast: return "bitcast";
    case IROpcode::IntToPtr: return "inttoptr";
    case IROpcode::PtrToInt: return "ptrtoint";
    case IROpcode::FPToUI: return "fptoui";
    case IROpcode::FPToSI: return "fptosi";
    case IROpcode::UIToFP: return "uitofp";
    case IROpcode::SIToFP: return "sitofp";
    case IROpcode::Phi: return "phi";
    case IROpcode::Call: return "call";
    case IROpcode::Select: return "select";
    case IROpcode::ExtractValue: return "extractvalue";
    case IROpcode::InlineAsm: return "inline_asm";
    }
    return "?";
}

static const char *icmpKindName(ICmpKind k) {
    switch (k) {
    case ICmpKind::EQ: return "eq";
    case ICmpKind::NE: return "ne";
    case ICmpKind::SLT: return "slt";
    case ICmpKind::SLE: return "sle";
    case ICmpKind::SGT: return "sgt";
    case ICmpKind::SGE: return "sge";
    case ICmpKind::ULT: return "ult";
    case ICmpKind::ULE: return "ule";
    case ICmpKind::UGT: return "ugt";
    case ICmpKind::UGE: return "uge";
    }
    return "?";
}

std::string IRInstruction::toString() const {
    std::ostringstream ss;

    // 结果赋值
    if (!name.empty() && type.kind != IRTypeKind::Void) {
        ss << "%" << name << " = ";
    }

    switch (opcode) {
    case IROpcode::Alloca:
        ss << "alloca " << type.toString();
        break;
    case IROpcode::Load:
        ss << "load " << type.toString() << ", " << operands[0]->type.toString()
           << " " << operands[0]->toString();
        break;
    case IROpcode::Store:
        ss << "store " << operands[0]->type.toString() << " " << operands[0]->toString()
           << ", " << operands[1]->type.toString() << " " << operands[1]->toString();
        break;
    case IROpcode::GEP:
        ss << "getelementptr " << operands[0]->type.toString() << ", "
           << operands[0]->toString() << ", " << operands[1]->toString();
        break;
    case IROpcode::Add: case IROpcode::Sub: case IROpcode::Mul:
    case IROpcode::SDiv: case IROpcode::UDiv: case IROpcode::SRem: case IROpcode::URem:
    case IROpcode::And: case IROpcode::Or: case IROpcode::Xor:
    case IROpcode::Shl: case IROpcode::LShr: case IROpcode::AShr:
        ss << opcodeName(opcode) << " " << type.toString()
           << " " << operands[0]->toString() << ", " << operands[1]->toString();
        break;
    case IROpcode::FAdd: case IROpcode::FSub: case IROpcode::FMul: case IROpcode::FDiv:
        ss << opcodeName(opcode) << " " << type.toString()
           << " " << operands[0]->toString() << ", " << operands[1]->toString();
        break;
    case IROpcode::ICmp: {
        auto kind = static_cast<ICmpKind>(reinterpret_cast<uintptr_t>(operands[2]));
        ss << "icmp " << icmpKindName(kind) << " "
           << operands[0]->type.toString()
           << " " << operands[0]->toString() << ", " << operands[1]->toString();
        break;
    }
    case IROpcode::FCmp:
        ss << "fcmp " << operands[0]->type.toString()
           << " " << operands[0]->toString() << ", " << operands[1]->toString();
        break;
    case IROpcode::Br:
        ss << "br label %" << reinterpret_cast<IRBasicBlock *>(operands[0])->name;
        break;
    case IROpcode::CondBr:
        ss << "br i1 " << operands[0]->toString()
           << ", label %" << reinterpret_cast<IRBasicBlock *>(operands[1])->name
           << ", label %" << reinterpret_cast<IRBasicBlock *>(operands[2])->name;
        break;
    case IROpcode::Ret:
        if (!operands.empty())
            ss << "ret " << operands[0]->type.toString() << " " << operands[0]->toString();
        else
            ss << "ret void";
        break;
    case IROpcode::Phi: {
        ss << "phi " << type.toString();
        // phi 操作数是 (value, block) 对
        for (size_t i = 0; i + 1 < operands.size(); i += 2) {
            if (i > 0) ss << ",";
            ss << " [ " << operands[i]->toString() << ", %"
               << reinterpret_cast<IRBasicBlock *>(operands[i + 1])->name << " ]";
        }
        break;
    }
    case IROpcode::Call: {
        auto *callee = operands[0];
        IRType funcTy = callee->type;
        ss << "call " << (funcTy.isFunction() ? funcTy.returnType->toString() : "?")
           << " " << callee->toString() << "(";
        for (size_t i = 1; i < operands.size(); ++i) {
            if (i > 1) ss << ", ";
            ss << operands[i]->type.toString() << " " << operands[i]->toString();
        }
        ss << ")";
        break;
    }
    case IROpcode::Trunc: case IROpcode::ZExt: case IROpcode::SExt:
    case IROpcode::BitCast: case IROpcode::IntToPtr: case IROpcode::PtrToInt:
    case IROpcode::FPToUI: case IROpcode::FPToSI: case IROpcode::UIToFP: case IROpcode::SIToFP:
        ss << opcodeName(opcode) << " " << operands[0]->type.toString()
           << " " << operands[0]->toString() << " to " << type.toString();
        break;
    case IROpcode::Select:
        ss << "select " << operands[0]->type.toString() << " " << operands[0]->toString()
           << ", " << operands[1]->type.toString() << " " << operands[1]->toString()
           << ", " << operands[2]->type.toString() << " " << operands[2]->toString();
        break;
    default:
        ss << opcodeName(opcode);
        for (auto *op : operands) ss << " " << op->toString();
        break;
    }

    return ss.str();
}

// === IRBasicBlock ===

IRInstruction *IRBasicBlock::getTerminator() {
    if (instructions.empty()) return nullptr;
    auto &last = instructions.back();
    return last.isTerminator() ? &last : nullptr;
}

const IRInstruction *IRBasicBlock::getTerminator() const {
    if (instructions.empty()) return nullptr;
    auto &last = instructions.back();
    return last.isTerminator() ? &last : nullptr;
}

void IRBasicBlock::appendInstruction(std::unique_ptr<IRInstruction> inst) {
    inst->parentBB = this;
    // 为指令分配唯一 ID（用于寄存器分配）
    if (parentFunc && inst->id < 0 && inst->type.kind != IRTypeKind::Void) {
        inst->id = parentFunc->allocValueId();
    }
    instructions.push_back(std::move(*inst));
}

void IRBasicBlock::insertBefore(std::list<IRInstruction>::iterator pos, std::unique_ptr<IRInstruction> inst) {
    inst->parentBB = this;
    instructions.insert(pos, std::move(*inst));
}

void IRBasicBlock::addSuccessor(IRBasicBlock *succ) {
    for (auto *s : successors) if (s == succ) return;
    successors.push_back(succ);
    succ->addPredecessor(this);
}

void IRBasicBlock::removeSuccessor(IRBasicBlock *succ) {
    for (auto it = successors.begin(); it != successors.end(); ++it) {
        if (*it == succ) {
            successors.erase(it);
            succ->removePredecessor(this);
            return;
        }
    }
}

void IRBasicBlock::addPredecessor(IRBasicBlock *pred) {
    for (auto *p : predecessors) if (p == pred) return;
    predecessors.push_back(pred);
}

void IRBasicBlock::removePredecessor(IRBasicBlock *pred) {
    for (auto it = predecessors.begin(); it != predecessors.end(); ++it) {
        if (*it == pred) {
            predecessors.erase(it);
            return;
        }
    }
}

std::string IRBasicBlock::toString() const {
    return name + ":";
}

// === IRFunction::toString ===

std::string IRFunction::toString() const {
    std::ostringstream ss;
    ss << "define " << returnType.toString() << " @" << name << "(";
    for (size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << arguments[i]->type.toString() << " " << arguments[i]->toString();
    }
    if (isVarArg) {
        if (!arguments.empty()) ss << ", ";
        ss << "...";
    }
    ss << ") {";
    return ss.str();
}

// === IRModule::toString ===

std::string IRModule::toString() const {
    std::ostringstream ss;
    for (auto &gv : globals) {
        ss << gv->name << " = " << (gv->isConstant ? "constant " : "global ")
           << gv->type.toString() << "\n";
    }
    if (!globals.empty()) ss << "\n";
    for (auto &fn : functions) {
        ss << fn->toString() << "\n";
        for (auto &bb : fn->basicBlocks) {
            ss << "  " << bb.name << ":\n";
            for (auto &inst : bb.instructions) {
                ss << "    " << inst.toString() << "\n";
            }
        }
        ss << "}\n\n";
    }
    return ss.str();
}

// === IRPrinter ===

std::string IRPrinter::printModule(const IRModule &module) {
    return module.toString();
}

std::string IRPrinter::printFunction(const IRFunction &func) {
    return func.toString();
}

std::string IRPrinter::printBasicBlock(const IRBasicBlock &bb, int indent) {
    std::string result;
    for (int i = 0; i < indent; ++i) result += "  ";
    result += bb.name + ":\n";
    for (auto &inst : bb.instructions) {
        for (int i = 0; i < indent + 1; ++i) result += "  ";
        result += inst.toString() + "\n";
    }
    return result;
}

std::string IRPrinter::printInstruction(const IRInstruction &inst, int indent) {
    std::string result;
    for (int i = 0; i < indent; ++i) result += "  ";
    result += inst.toString();
    return result;
}

} // namespace ir
