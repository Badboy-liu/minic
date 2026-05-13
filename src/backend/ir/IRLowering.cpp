#include "IRLowering.h"
#include <algorithm>
#include <cassert>

namespace ir {

IRLowering::IRLowering() = default;

// === 类型转换 ===

IRType IRLowering::convertType(const TypePtr &type) {
    if (!type) return IRType::voidTy();
    switch (type->kind) {
    case TypeKind::Void: return IRType::voidTy();
    case TypeKind::Bool: return IRType::i1();
    case TypeKind::Char: return IRType::i8();
    case TypeKind::Short: return IRType::i16();
    case TypeKind::Int: return IRType::i32();
    case TypeKind::Long:
    case TypeKind::LongLong: return IRType::i64();
    case TypeKind::Float: return IRType::f32();
    case TypeKind::Double: return IRType::f64();
    case TypeKind::Pointer: return IRType::ptr(type->elementType ? convertType(type->elementType) : IRType::i8());
    case TypeKind::Array:
        return IRType::array(convertType(type->elementType), type->arrayLength);
    case TypeKind::Struct:
    case TypeKind::Union: {
        std::vector<IRType> members;
        for (auto &m : type->members) {
            members.push_back(convertType(m.type));
        }
        return IRType::structTy(std::move(members));
    }
    case TypeKind::Function: {
        auto retTy = convertType(type->elementType);
        std::vector<IRType> params;
        for (auto &p : type->parameterTypes) {
            params.push_back(convertType(p));
        }
        return IRType::function(std::move(retTy), std::move(params), type->isVariadic);
    }
    }
    return IRType::i32();
}

// === 工具方法 ===

std::string IRLowering::nextTemp(const std::string &prefix) {
    return prefix + "_" + std::to_string(tempCounter++);
}

IRValue *IRLowering::emitAlloca(IRType ty, const std::string &name) {
    auto inst = IRInstruction::createAlloca(std::move(ty), name.empty() ? nextTemp("alloca") : name);
    // alloca 总是插入到 entry block 的开头
    if (currentFunc && currentFunc->entryBlock) {
        auto &entryInsts = currentFunc->entryBlock->instructions;
        auto it = entryInsts.begin();
        while (it != entryInsts.end() && it->opcode == IROpcode::Alloca) {
            ++it;
        }
        currentFunc->entryBlock->insertBefore(it, std::move(inst));
        auto &inserted = *std::prev(it);
        inserted.parentBB = currentFunc->entryBlock;
        return &inserted;
    }
    currentBB->appendInstruction(std::move(inst));
    return &currentBB->instructions.back();
}

void IRLowering::emitStore(IRValue *val, IRValue *ptr) {
    currentBB->appendInstruction(IRInstruction::createStore(val, ptr));
}

IRValue *IRLowering::emitLoad(IRValue *ptr, const std::string &name) {
    currentBB->appendInstruction(IRInstruction::createLoad(ptr, name.empty() ? nextTemp("load") : name));
    return &currentBB->instructions.back();
}

IRValue *IRLowering::emitGEP(IRValue *base, IRValue *idx, const std::string &name) {
    currentBB->appendInstruction(IRInstruction::createGEP(base, idx, name.empty() ? nextTemp("gep") : name));
    return &currentBB->instructions.back();
}

IRBasicBlock *IRLowering::createBlock(const std::string &name) {
    return currentFunc->createBasicBlock(name);
}

void IRLowering::setInsertBlock(IRBasicBlock *bb) {
    currentBB = bb;
}

void IRLowering::emitBr(IRBasicBlock *target) {
    currentBB->appendInstruction(IRInstruction::createBr(target));
    currentBB->addSuccessor(target);
}

void IRLowering::emitCondBr(IRValue *cond, IRBasicBlock *trueBB, IRBasicBlock *falseBB) {
    auto inst = IRInstruction::createCondBr(cond, trueBB, falseBB);
    currentBB->appendInstruction(std::move(inst));
    currentBB->addSuccessor(trueBB);
    currentBB->addSuccessor(falseBB);
}

void IRLowering::emitRet(IRValue *val) {
    auto inst = IRInstruction::createRet(val);
    currentBB->appendInstruction(std::move(inst));
}

IRValue *IRLowering::lookupVar(const std::string &name) {
    for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) return found->second;
    }
    return nullptr;
}

void IRLowering::pushScope() {
    scopeStack.emplace_back();
}

void IRLowering::popScope() {
    scopeStack.pop_back();
}

// === Program 降低 ===

std::unique_ptr<IRModule> IRLowering::lowerProgram(const Program &program) {
    module = std::make_unique<IRModule>();

    // 先声明所有函数
    for (auto &fn : program.functions) {
        auto retTy = convertType(fn.returnType);
        std::vector<IRType> paramTypes;
        for (auto &p : fn.parameters) {
            paramTypes.push_back(convertType(p.type));
        }
        auto *irFn = module->createFunction(fn.name, retTy, paramTypes, fn.isVariadic);
        functionMap[fn.name] = irFn;
    }

    // 降低全局变量
    for (auto &gv : program.globals) {
        lowerGlobalVar(gv);
    }

    // 降低函数体
    for (auto &fn : program.functions) {
        if (!fn.isDeclaration()) {
            lowerFunction(fn);
        }
    }

    return std::move(module);
}

void IRLowering::lowerGlobalVar(const GlobalVar &gv) {
    if (gv.isExternStorage || gv.isExternal) return;
    auto elemTy = convertType(gv.type);
    auto *irGv = module->createGlobal(elemTy, false, gv.symbolName.empty() ? gv.name : gv.symbolName);
    globalMap[gv.name] = irGv;
}

void IRLowering::lowerFunction(const Function &func) {
    auto it = functionMap.find(func.name);
    if (it == functionMap.end()) return;
    currentFunc = it->second;
    tempCounter = 0;
    labelMap.clear();
    scopeStack.clear();

    // 创建 entry block
    auto *entry = createBlock("entry");
    currentFunc->entryBlock = entry;
    setInsertBlock(entry);

    pushScope();

    // 创建参数 alloca
    for (size_t i = 0; i < func.parameters.size(); ++i) {
        auto &param = func.parameters[i];
        auto paramTy = convertType(param.type);
        auto *alloca = emitAlloca(paramTy, param.name);
        scopeStack.back()[param.name] = alloca;
        // 存储参数值到 alloca
        if (i < currentFunc->arguments.size()) {
            emitStore(currentFunc->arguments[i].get(), alloca);
        }
    }

    // 第一遍：收集所有 label
    // （简化实现：在 visitLabelStmt 时按需创建）

    // 降低函数体
    func.body->accept(*this);

    // 如果末尾没有终结指令，添加 ret
    if (!currentBB->getTerminator()) {
        if (func.returnType && func.returnType->isVoid()) {
            emitRet();
        } else {
            // 返回 0 作为默认值
            auto *zero = module->createConstantInt(0, 32);
            emitRet(zero);
        }
    }

    popScope();
    currentFunc = nullptr;
    currentBB = nullptr;
}

// === 表达式 Visitor ===

void IRLowering::visitNumberExpr(NumberExpr &node) {
    int bitWidth = 32;
    if (node.type) {
        auto irTy = convertType(node.type);
        bitWidth = irTy.bitWidth;
    }
    lastValue = module->createConstantInt(node.value, bitWidth);
}

void IRLowering::visitFloatLiteralExpr(FloatLiteralExpr &node) {
    bool isDouble = !node.type || node.type->kind == TypeKind::Double;
    lastValue = module->createConstantFloat(node.value, isDouble);
}

void IRLowering::visitStringExpr(StringExpr &node) {
    // 查找或创建字符串常量全局变量
    auto it = stringPool.find(node.value);
    if (it != stringPool.end()) {
        lastValue = it->second;
        return;
    }
    auto strTy = IRType::array(IRType::i8(), static_cast<int>(node.value.size()) + 1);
    auto *gv = module->createGlobal(strTy, true, "str_" + std::to_string(stringPool.size()));
    stringPool[node.value] = gv;
    lastValue = gv;
}

void IRLowering::visitVariableExpr(VariableExpr &node) {
    auto *allocaAddr = lookupVar(node.name);
    if (allocaAddr) {
        // 数组类型返回地址（数组到指针衰减），其他类型 load
        if (node.type && node.type->isArray()) {
            lastValue = allocaAddr;
        } else {
            lastValue = emitLoad(allocaAddr, node.name);
        }
    } else {
        // 全局变量
        auto git = globalMap.find(node.name);
        if (git != globalMap.end()) {
            lastValue = emitLoad(git->second, node.name);
        } else {
            // 未找到，创建一个零值
            lastValue = module->createConstantInt(0, 32);
        }
    }
}

void IRLowering::visitUnaryExpr(UnaryExpr &node) {
    node.operand->accept(*this);
    auto *operand = lastValue;

    switch (node.op) {
    case UnaryOp::Plus:
        lastValue = operand;
        break;
    case UnaryOp::Minus: {
        auto *zero = module->createConstantInt(0, operand->type.bitWidth);
        currentBB->appendInstruction(IRInstruction::createBinary(IROpcode::Sub, zero, operand, nextTemp("neg")));
        lastValue = &currentBB->instructions.back();
        break;
    }
    case UnaryOp::LogicalNot: {
        auto *zero = module->createConstantInt(0, operand->type.bitWidth);
        currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::EQ, operand, zero, nextTemp("not")));
        lastValue = &currentBB->instructions.back();
        break;
    }
    case UnaryOp::BitwiseNot: {
        auto *neg1 = module->createConstantInt(-1, operand->type.bitWidth);
        currentBB->appendInstruction(IRInstruction::createBinary(IROpcode::Xor, operand, neg1, nextTemp("bnot")));
        lastValue = &currentBB->instructions.back();
        break;
    }
    case UnaryOp::PreIncrement:
    case UnaryOp::PreDecrement: {
        // 前缀 ++/--
        auto *allocaAddr = lookupVar(static_cast<VariableExpr *>(node.operand.get())->name);
        if (!allocaAddr) break;
        auto *loaded = emitLoad(allocaAddr, nextTemp("pre"));
        auto *one = module->createConstantInt(1, loaded->type.bitWidth);
        auto op = node.op == UnaryOp::PreIncrement ? IROpcode::Add : IROpcode::Sub;
        currentBB->appendInstruction(IRInstruction::createBinary(op, loaded, one, nextTemp("inc")));
        auto *result = &currentBB->instructions.back();
        emitStore(result, allocaAddr);
        lastValue = result;
        break;
    }
    case UnaryOp::PostIncrement:
    case UnaryOp::PostDecrement: {
        // 后缀 ++/--
        auto *allocaAddr = lookupVar(static_cast<VariableExpr *>(node.operand.get())->name);
        if (!allocaAddr) break;
        auto *loaded = emitLoad(allocaAddr, nextTemp("post"));
        auto *one = module->createConstantInt(1, loaded->type.bitWidth);
        auto op = node.op == UnaryOp::PostIncrement ? IROpcode::Add : IROpcode::Sub;
        currentBB->appendInstruction(IRInstruction::createBinary(op, loaded, one, nextTemp("inc")));
        auto *result = &currentBB->instructions.back();
        emitStore(result, allocaAddr);
        lastValue = loaded; // 后缀返回旧值
        break;
    }
    case UnaryOp::AddressOf: {
        // 取地址：返回 alloca 地址（不 load）
        if (node.operand->kind == Expr::Kind::Variable) {
            auto &var = static_cast<VariableExpr &>(*node.operand);
            lastValue = lookupVar(var.name);
            if (!lastValue) {
                auto git = globalMap.find(var.name);
                if (git != globalMap.end()) lastValue = git->second;
            }
        }
        break;
    }
    case UnaryOp::Dereference: {
        // 解引用：load 指针指向的值
        lastValue = emitLoad(operand, nextTemp("deref"));
        break;
    }
    case UnaryOp::Sizeof: {
        // sizeof 返回类型的大小
        int size = 0;
        if (node.sizeofType) {
            size = node.sizeofType->valueSize();
        } else if (node.operand && node.operand->type) {
            size = node.operand->type->valueSize();
        }
        lastValue = module->createConstantInt(size, 64);
        break;
    }
    case UnaryOp::Alignof: {
        int align = 1;
        if (node.sizeofType) {
            align = node.sizeofType->alignment();
        }
        lastValue = module->createConstantInt(align, 64);
        break;
    }
    }
}

void IRLowering::visitBinaryExpr(BinaryExpr &node) {
    // 短路求值：LogicalAnd / LogicalOr
    if (node.op == BinaryOp::LogicalAnd) {
        auto *rhsBB = createBlock(nextTemp("land_rhs"));
        auto *mergeBB = createBlock(nextTemp("land_merge"));

        node.left->accept(*this);
        auto *lhs = lastValue;
        auto *lhsBB = currentBB;  // 保存左侧求值所在块
        // 转换为 bool
        auto *zero = module->createConstantInt(0, lhs->type.bitWidth);
        currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::NE, lhs, zero, nextTemp("tobool")));
        auto *cond = &currentBB->instructions.back();
        emitCondBr(cond, rhsBB, mergeBB);

        setInsertBlock(rhsBB);
        node.right->accept(*this);
        auto *rhs = lastValue;
        auto *zero2 = module->createConstantInt(0, rhs->type.bitWidth);
        currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::NE, rhs, zero2, nextTemp("tobool")));
        auto *rhsBool = &currentBB->instructions.back();
        emitBr(mergeBB);

        setInsertBlock(mergeBB);
        currentBB->appendInstruction(IRInstruction::createPhi(IRType::i1(), nextTemp("and")));
        auto *phi = &currentBB->instructions.back();
        addPhiIncoming(phi, zero, lhsBB);    // false 路径：左侧为 0
        addPhiIncoming(phi, rhsBool, rhsBB);  // true 路径：右侧布尔值
        lastValue = phi;
        return;
    }

    if (node.op == BinaryOp::LogicalOr) {
        auto *rhsBB = createBlock(nextTemp("lor_rhs"));
        auto *mergeBB = createBlock(nextTemp("lor_merge"));

        node.left->accept(*this);
        auto *lhs = lastValue;
        auto *lhsBB = currentBB;  // 保存左侧求值所在块
        auto *zero = module->createConstantInt(0, lhs->type.bitWidth);
        currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::NE, lhs, zero, nextTemp("tobool")));
        auto *cond = &currentBB->instructions.back();
        emitCondBr(cond, mergeBB, rhsBB);

        setInsertBlock(rhsBB);
        node.right->accept(*this);
        auto *rhs = lastValue;
        auto *zero2 = module->createConstantInt(0, rhs->type.bitWidth);
        currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::NE, rhs, zero2, nextTemp("tobool")));
        auto *rhsBool = &currentBB->instructions.back();
        emitBr(mergeBB);

        setInsertBlock(mergeBB);
        auto *one = module->createConstantInt(1, 1);
        currentBB->appendInstruction(IRInstruction::createPhi(IRType::i1(), nextTemp("or")));
        auto *phi = &currentBB->instructions.back();
        addPhiIncoming(phi, one, lhsBB);     // true 路径：左侧非零
        addPhiIncoming(phi, rhsBool, rhsBB);  // false 路径：右侧布尔值
        lastValue = phi;
        return;
    }

    // 逗号运算符
    if (node.op == BinaryOp::Comma) {
        node.left->accept(*this);
        node.right->accept(*this);
        return;
    }

    // 普通二元运算
    node.left->accept(*this);
    auto *lhs = lastValue;
    node.right->accept(*this);
    auto *rhs = lastValue;

    // 指针算术：ptr ± int → 用 GEP 保持指针语义，手动缩放偏移量
    // 注意：node.left->type 可能是数组类型（未 decay），需要同时检查 isPointer 和 isArray
    if ((node.op == BinaryOp::Add || node.op == BinaryOp::Subtract) &&
        node.left->type && (node.left->type->isPointer() || node.left->type->isArray()) &&
        node.left->type->elementType && node.right->type && node.right->type->isInteger()) {
        int elemSize = node.left->type->elementType->valueSize();
        // 减法取反
        if (node.op == BinaryOp::Subtract) {
            auto *zero = module->createConstantInt(0, 64);
            currentBB->appendInstruction(IRInstruction::createBinary(IROpcode::Sub, zero, rhs, nextTemp("neg")));
            rhs = &currentBB->instructions.back();
        }
        // 缩放偏移量
        if (elemSize > 1) {
            auto *scale = module->createConstantInt(elemSize, 64);
            currentBB->appendInstruction(IRInstruction::createBinary(IROpcode::Mul, rhs, scale, nextTemp("scale")));
            rhs = &currentBB->instructions.back();
        }
        lastValue = emitGEP(lhs, rhs, nextTemp("ptradd"));
        return;
    }

    auto binInst = emitBinaryOp(node.op, lhs, rhs, node.type);
    currentBB->appendInstruction(std::move(binInst));
    lastValue = &currentBB->instructions.back();
}

std::unique_ptr<IRInstruction> IRLowering::emitBinaryOp(BinaryOp op, IRValue *lhs, IRValue *rhs, const TypePtr &astType) {
    bool isFloat = astType && astType->isFloatingPoint();
    bool isUnsigned = astType && astType->isUnsigned;

    IROpcode irOp;
    switch (op) {
    case BinaryOp::Add: irOp = isFloat ? IROpcode::FAdd : IROpcode::Add; break;
    case BinaryOp::Subtract: irOp = isFloat ? IROpcode::FSub : IROpcode::Sub; break;
    case BinaryOp::Multiply: irOp = isFloat ? IROpcode::FMul : IROpcode::Mul; break;
    case BinaryOp::Divide: irOp = isFloat ? IROpcode::FDiv : (isUnsigned ? IROpcode::UDiv : IROpcode::SDiv); break;
    case BinaryOp::Modulo: irOp = isUnsigned ? IROpcode::URem : IROpcode::SRem; break;
    case BinaryOp::ShiftLeft: irOp = IROpcode::Shl; break;
    case BinaryOp::ShiftRight: irOp = isUnsigned ? IROpcode::LShr : IROpcode::AShr; break;
    case BinaryOp::BitwiseAnd: irOp = IROpcode::And; break;
    case BinaryOp::BitwiseXor: irOp = IROpcode::Xor; break;
    case BinaryOp::BitwiseOr: irOp = IROpcode::Or; break;
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual: {
        ICmpKind kind;
        switch (op) {
        case BinaryOp::Equal: kind = ICmpKind::EQ; break;
        case BinaryOp::NotEqual: kind = ICmpKind::NE; break;
        case BinaryOp::Less: kind = isUnsigned ? ICmpKind::ULT : ICmpKind::SLT; break;
        case BinaryOp::LessEqual: kind = isUnsigned ? ICmpKind::ULE : ICmpKind::SLE; break;
        case BinaryOp::Greater: kind = isUnsigned ? ICmpKind::UGT : ICmpKind::SGT; break;
        case BinaryOp::GreaterEqual: kind = isUnsigned ? ICmpKind::UGE : ICmpKind::SGE; break;
        default: kind = ICmpKind::EQ; break;
        }
        return IRInstruction::createICmp(kind, lhs, rhs, nextTemp("cmp"));
    }
    default:
        // 未知操作：返回一个 Add 指令（不可达代码）
        return IRInstruction::createBinary(IROpcode::Add, lhs, rhs, nextTemp("fallback"));
    }

    return IRInstruction::createBinary(irOp, lhs, rhs, nextTemp("binop"));
}

IRValue *IRLowering::emitCast(IRValue *val, const TypePtr &fromType, const TypePtr &toType) {
    if (!fromType || !toType) return val;
    auto fromIR = convertType(fromType);
    auto toIR = convertType(toType);

    if (fromIR.kind == toIR.kind && fromIR.bitWidth == toIR.bitWidth) return val;

    // 整数宽度变化
    if (fromIR.isInt() && toIR.isInt()) {
        if (fromIR.bitWidth < toIR.bitWidth) {
            currentBB->appendInstruction(IRInstruction::createCast(IROpcode::ZExt, val, toIR, nextTemp("cast")));
            return &currentBB->instructions.back();
        }
        if (fromIR.bitWidth > toIR.bitWidth) {
            currentBB->appendInstruction(IRInstruction::createCast(IROpcode::Trunc, val, toIR, nextTemp("cast")));
            return &currentBB->instructions.back();
        }
        return val;
    }

    // 整数 ↔ 浮点
    if (fromIR.isInt() && toIR.isFloat()) {
        auto op = fromType->isUnsigned ? IROpcode::UIToFP : IROpcode::SIToFP;
        currentBB->appendInstruction(IRInstruction::createCast(op, val, toIR, nextTemp("cast")));
        return &currentBB->instructions.back();
    }
    if (fromIR.isFloat() && toIR.isInt()) {
        auto op = toType->isUnsigned ? IROpcode::FPToUI : IROpcode::FPToSI;
        currentBB->appendInstruction(IRInstruction::createCast(op, val, toIR, nextTemp("cast")));
        return &currentBB->instructions.back();
    }

    // 指针 ↔ 整数
    if (fromIR.isPointer() && toIR.isInt()) {
        currentBB->appendInstruction(IRInstruction::createCast(IROpcode::PtrToInt, val, toIR, nextTemp("cast")));
        return &currentBB->instructions.back();
    }
    if (fromIR.isInt() && toIR.isPointer()) {
        currentBB->appendInstruction(IRInstruction::createCast(IROpcode::IntToPtr, val, toIR, nextTemp("cast")));
        return &currentBB->instructions.back();
    }

    // bitcast
    currentBB->appendInstruction(IRInstruction::createCast(IROpcode::BitCast, val, toIR, nextTemp("cast")));
    return &currentBB->instructions.back();
}

void IRLowering::visitInitializerListExpr(InitializerListExpr &node) {
    // 初始化列表：创建临时 alloca，逐元素 store
    // 简化实现：返回最后一个元素
    if (!node.elements.empty()) {
        node.elements.back()->accept(*this);
    } else {
        lastValue = module->createConstantInt(0, 32);
    }
}

void IRLowering::visitAssignExpr(AssignExpr &node) {
    node.value->accept(*this);
    auto *rhs = lastValue;

    if (node.isCompound) {
        // 复合赋值：a op= b → a = a op b
        node.target->accept(*this);
        auto *lhs = lastValue;
        auto binInst = emitBinaryOp(node.compoundOp, lhs, rhs, node.type);
        currentBB->appendInstruction(std::move(binInst));
        rhs = &currentBB->instructions.back();
    }

    // 存储到目标
    if (node.target->kind == Expr::Kind::Variable) {
        auto &var = static_cast<VariableExpr &>(*node.target);
        auto *allocaAddr = lookupVar(var.name);
        if (allocaAddr) {
            emitStore(rhs, allocaAddr);
        } else {
            auto git = globalMap.find(var.name);
            if (git != globalMap.end()) {
                emitStore(rhs, git->second);
            }
        }
    } else if (node.target->kind == Expr::Kind::Unary &&
               static_cast<UnaryExpr &>(*node.target).op == UnaryOp::Dereference) {
        // *ptr = val
        auto &unary = static_cast<UnaryExpr &>(*node.target);
        unary.operand->accept(*this);
        auto *ptr = lastValue;
        emitStore(rhs, ptr);
    } else if (node.target->kind == Expr::Kind::Index) {
        // arr[i] = val
        auto &idx = static_cast<IndexExpr &>(*node.target);
        idx.base->accept(*this);
        auto *base = lastValue;
        idx.index->accept(*this);
        auto *index = lastValue;

        // 按元素大小缩放索引
        int elemSize = 1;
        if (idx.base->type) {
            if (idx.base->type->isArray() && idx.base->type->elementType) {
                elemSize = idx.base->type->elementType->valueSize();
            } else if (idx.base->type->isPointer() && idx.base->type->elementType) {
                elemSize = idx.base->type->elementType->valueSize();
            }
        }
        if (elemSize > 1) {
            auto *scale = module->createConstantInt(elemSize, 64);
            currentBB->appendInstruction(IRInstruction::createBinary(IROpcode::Mul, index, scale, nextTemp("scale")));
            index = &currentBB->instructions.back();
        }

        auto *addr = emitGEP(base, index, nextTemp("idx"));
        emitStore(rhs, addr);
    } else if (node.target->kind == Expr::Kind::MemberAccess) {
        // s.field = val
        auto &member = static_cast<MemberAccessExpr &>(*node.target);
        member.base->accept(*this);
        auto *base = lastValue;
        auto *offset = module->createConstantInt(member.memberOffset, 64);
        auto *addr = emitGEP(base, offset, nextTemp("field"));
        emitStore(rhs, addr);
    }

    lastValue = rhs;
}

void IRLowering::visitCallExpr(CallExpr &node) {
    // 获取函数名
    std::string funcName;
    if (node.callee->kind == Expr::Kind::Variable) {
        funcName = static_cast<VariableExpr &>(*node.callee).name;
    }

    // 查找函数
    IRFunction *callee = nullptr;
    auto it = functionMap.find(funcName);
    if (it != functionMap.end()) callee = it->second;

    // 降低参数
    std::vector<IRValue *> args;
    for (auto &arg : node.arguments) {
        arg->accept(*this);
        args.push_back(lastValue);
    }

    IRType retTy = IRType::voidTy();
    if (callee) {
        retTy = callee->returnType;
    } else if (node.type) {
        retTy = convertType(node.type);
    }

    std::string name = retTy.isVoid() ? "" : nextTemp("call");
    auto *funcVal = callee ? static_cast<IRValue *>(callee) : nullptr;
    if (!funcVal) {
        // 未找到函数，创建一个外部声明
        // 简化：用全局变量模拟
        funcVal = module->createGlobal(IRType::i32(), false, funcName);
    }

    currentBB->appendInstruction(IRInstruction::createCall(funcVal, args, retTy, name));
    lastValue = &currentBB->instructions.back();
    if (!retTy.isVoid()) {
        lastValue = &currentBB->instructions.back();
    } else {
        lastValue = nullptr;
    }
}

void IRLowering::visitIndexExpr(IndexExpr &node) {
    node.base->accept(*this);
    auto *base = lastValue;
    node.index->accept(*this);
    auto *idx = lastValue;

    // 按元素大小缩放索引（GEP 的 codegen 做 base+idx，需要手动缩放）
    int elemSize = 1;
    if (node.base->type) {
        if (node.base->type->isArray() && node.base->type->elementType) {
            elemSize = node.base->type->elementType->valueSize();
        } else if (node.base->type->isPointer() && node.base->type->elementType) {
            elemSize = node.base->type->elementType->valueSize();
        }
    }
    if (elemSize > 1) {
        auto *scale = module->createConstantInt(elemSize, 64);
        currentBB->appendInstruction(IRInstruction::createBinary(IROpcode::Mul, idx, scale, nextTemp("scale")));
        idx = &currentBB->instructions.back();
    }

    auto *addr = emitGEP(base, idx, nextTemp("idx"));
    lastValue = emitLoad(addr, nextTemp("elem"));
}

void IRLowering::visitMemberAccessExpr(MemberAccessExpr &node) {
    node.base->accept(*this);
    auto *base = lastValue;

    // 计算成员偏移
    auto *offset = module->createConstantInt(node.memberOffset, 64);
    auto *addr = emitGEP(base, offset, nextTemp("field"));

    // 如果是指针访问 (->), base 已经是指针
    // 如果是值访问 (.), 需要取地址再偏移
    if (node.base->type && node.base->type->isPointer()) {
        lastValue = emitLoad(addr, nextTemp("member"));
    } else {
        // 值类型：base 本身需要是地址
        lastValue = emitLoad(addr, nextTemp("member"));
    }
}

void IRLowering::visitTernaryExpr(TernaryExpr &node) {
    auto *thenBB = createBlock(nextTemp("tern_then"));
    auto *elseBB = createBlock(nextTemp("tern_else"));
    auto *mergeBB = createBlock(nextTemp("tern_merge"));

    node.condition->accept(*this);
    auto *cond = lastValue;
    auto *zero = module->createConstantInt(0, cond->type.bitWidth);
    currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::NE, cond, zero, nextTemp("tobool")));
    auto *condBool = &currentBB->instructions.back();
    emitCondBr(condBool, thenBB, elseBB);

    setInsertBlock(thenBB);
    node.thenExpr->accept(*this);
    auto *thenVal = lastValue;
    emitBr(mergeBB);

    setInsertBlock(elseBB);
    node.elseExpr->accept(*this);
    auto *elseVal = lastValue;
    emitBr(mergeBB);

    setInsertBlock(mergeBB);
    // 用 Select 指令代替 Phi（简化实现）
    currentBB->appendInstruction(IRInstruction::createSelect(condBool, thenVal, elseVal, nextTemp("tern")));
    lastValue = &currentBB->instructions.back();
}

void IRLowering::visitCastExpr(CastExpr &node) {
    node.operand->accept(*this);
    auto *operand = lastValue;
    if (node.targetType && node.operand->type) {
        lastValue = emitCast(operand, node.operand->type, node.targetType);
    }
}

void IRLowering::visitBuiltinVaStartExpr(BuiltinVaStartExpr &node) {
    // 简化：创建一个占位 alloca
    lastValue = emitAlloca(IRType::i8(), "va_start");
}

void IRLowering::visitBuiltinVaArgExpr(BuiltinVaArgExpr &node) {
    lastValue = module->createConstantInt(0, 32);
}

void IRLowering::visitBuiltinVaEndExpr(BuiltinVaEndExpr &node) {
    lastValue = nullptr;
}

void IRLowering::visitGenericExpr(GenericExpr &node) {
    if (node.selectedExpr) {
        node.selectedExpr->accept(*this);
    } else if (!node.associations.empty()) {
        node.associations.back().expr->accept(*this);
    } else {
        lastValue = module->createConstantInt(0, 32);
    }
}

void IRLowering::visitCompoundLiteralExpr(CompoundLiteralExpr &node) {
    auto ty = convertType(node.compoundType);
    auto *alloca = emitAlloca(ty, nextTemp("compound"));
    if (node.init) {
        node.init->accept(*this);
        if (lastValue) emitStore(lastValue, alloca);
    }
    lastValue = alloca;
}

void IRLowering::visitStmtExpr(StmtExpr &node) {
    pushScope();
    for (auto &stmt : node.statements) {
        stmt->accept(*this);
    }
    if (node.result) {
        node.result->accept(*this);
    }
    popScope();
}

// === 语句 Visitor ===

void IRLowering::visitReturnStmt(ReturnStmt &node) {
    if (node.expr) {
        node.expr->accept(*this);
        emitRet(lastValue);
    } else {
        emitRet();
    }
}

void IRLowering::visitExprStmt(ExprStmt &node) {
    node.expr->accept(*this);
    lastValue = nullptr;
}

void IRLowering::visitDeclStmt(DeclStmt &node) {
    auto ty = convertType(node.type);
    auto *alloca = emitAlloca(ty, node.name);
    scopeStack.back()[node.name] = alloca;

    if (node.init) {
        // 数组初始化：逐元素 store
        if (node.type && node.type->isArray() && node.init->kind == Expr::Kind::InitializerList) {
            auto &list = static_cast<InitializerListExpr &>(*node.init);
            int elemSize = node.type->elementType ? node.type->elementType->valueSize() : 4;
            for (size_t i = 0; i < list.elements.size(); ++i) {
                list.elements[i]->accept(*this);
                if (lastValue) {
                    if (i == 0) {
                        emitStore(lastValue, alloca);
                    } else {
                        auto *offset = module->createConstantInt(i * elemSize, 64);
                        auto *addr = emitGEP(alloca, offset, nextTemp("init_idx"));
                        emitStore(lastValue, addr);
                    }
                }
            }
        } else {
            node.init->accept(*this);
            if (lastValue) {
                emitStore(lastValue, alloca);
            }
        }
    }
}

void IRLowering::visitBlockStmt(BlockStmt &node) {
    pushScope();
    for (auto &stmt : node.statements) {
        stmt->accept(*this);
        // 如果当前块已经有终结指令，后续语句需要新块
        if (currentBB->getTerminator()) {
            auto *nextBB = createBlock(nextTemp("cont"));
            setInsertBlock(nextBB);
        }
    }
    popScope();
}

void IRLowering::visitIfStmt(IfStmt &node) {
    auto *thenBB = createBlock(nextTemp("if_then"));
    auto *elseBB = node.elseBranch ? createBlock(nextTemp("if_else")) : nullptr;
    auto *mergeBB = createBlock(nextTemp("if_end"));

    node.condition->accept(*this);
    auto *cond = lastValue;
    auto *zero = module->createConstantInt(0, cond->type.bitWidth);
    currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::NE, cond, zero, nextTemp("tobool")));
    auto *condBool = &currentBB->instructions.back();

    emitCondBr(condBool, thenBB, elseBB ? elseBB : mergeBB);

    setInsertBlock(thenBB);
    node.thenBranch->accept(*this);
    if (!currentBB->getTerminator()) emitBr(mergeBB);

    if (elseBB) {
        setInsertBlock(elseBB);
        node.elseBranch->accept(*this);
        if (!currentBB->getTerminator()) emitBr(mergeBB);
    }

    setInsertBlock(mergeBB);
}

void IRLowering::visitWhileStmt(WhileStmt &node) {
    auto *headerBB = createBlock(nextTemp("while_header"));
    auto *bodyBB = createBlock(nextTemp("while_body"));
    auto *exitBB = createBlock(nextTemp("while_exit"));

    breakTargets.push_back(exitBB);
    continueTargets.push_back(headerBB);

    emitBr(headerBB);
    setInsertBlock(headerBB);

    node.condition->accept(*this);
    auto *cond = lastValue;
    auto *zero = module->createConstantInt(0, cond->type.bitWidth);
    currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::NE, cond, zero, nextTemp("tobool")));
    auto *condBool = &currentBB->instructions.back();
    emitCondBr(condBool, bodyBB, exitBB);

    setInsertBlock(bodyBB);
    node.body->accept(*this);
    if (!currentBB->getTerminator()) emitBr(headerBB);

    breakTargets.pop_back();
    continueTargets.pop_back();

    setInsertBlock(exitBB);
}

void IRLowering::visitForStmt(ForStmt &node) {
    pushScope();

    // init
    if (node.init) node.init->accept(*this);

    auto *headerBB = createBlock(nextTemp("for_header"));
    auto *bodyBB = createBlock(nextTemp("for_body"));
    auto *updateBB = createBlock(nextTemp("for_update"));
    auto *exitBB = createBlock(nextTemp("for_exit"));

    breakTargets.push_back(exitBB);
    continueTargets.push_back(updateBB);

    emitBr(headerBB);
    setInsertBlock(headerBB);

    // condition
    if (node.condition) {
        node.condition->accept(*this);
        auto *cond = lastValue;
        auto *zero = module->createConstantInt(0, cond->type.bitWidth);
        currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::NE, cond, zero, nextTemp("tobool")));
        auto *condBool = &currentBB->instructions.back();
        emitCondBr(condBool, bodyBB, exitBB);
    } else {
        emitBr(bodyBB);
    }

    setInsertBlock(bodyBB);
    node.body->accept(*this);
    if (!currentBB->getTerminator()) emitBr(updateBB);

    setInsertBlock(updateBB);
    if (node.update) node.update->accept(*this);
    emitBr(headerBB);

    breakTargets.pop_back();
    continueTargets.pop_back();

    popScope();
    setInsertBlock(exitBB);
}

void IRLowering::visitBreakStmt(BreakStmt &node) {
    if (!breakTargets.empty()) {
        emitBr(breakTargets.back());
    }
}

void IRLowering::visitContinueStmt(ContinueStmt &node) {
    if (!continueTargets.empty()) {
        emitBr(continueTargets.back());
    }
}

void IRLowering::visitDoWhileStmt(DoWhileStmt &node) {
    auto *bodyBB = createBlock(nextTemp("dowhile_body"));
    auto *headerBB = createBlock(nextTemp("dowhile_header"));
    auto *exitBB = createBlock(nextTemp("dowhile_exit"));

    breakTargets.push_back(exitBB);
    continueTargets.push_back(headerBB);

    emitBr(bodyBB);

    setInsertBlock(bodyBB);
    node.body->accept(*this);
    if (!currentBB->getTerminator()) emitBr(headerBB);

    setInsertBlock(headerBB);
    node.condition->accept(*this);
    auto *cond = lastValue;
    auto *zero = module->createConstantInt(0, cond->type.bitWidth);
    currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::NE, cond, zero, nextTemp("tobool")));
    auto *condBool = &currentBB->instructions.back();
    emitCondBr(condBool, bodyBB, exitBB);

    breakTargets.pop_back();
    continueTargets.pop_back();

    setInsertBlock(exitBB);
}

void IRLowering::visitSwitchStmt(SwitchStmt &node) {
    node.scrutinee->accept(*this);
    auto *scrutinee = lastValue;

    auto *defaultBB = node.defaultBody ? createBlock(nextTemp("switch_default")) : createBlock(nextTemp("switch_exit"));
    auto *exitBB = createBlock(nextTemp("switch_exit"));

    breakTargets.push_back(exitBB);
    switchDefaultTargets.push_back(defaultBB);

    // 预创建所有 case body 块
    std::vector<IRBasicBlock *> caseBodyBBs;
    for (size_t i = 0; i < node.cases.size(); ++i) {
        caseBodyBBs.push_back(createBlock(nextTemp("case")));
    }

    // 生成 case 比较链（if-else 链）
    for (size_t i = 0; i < node.cases.size(); ++i) {
        auto *nextBB = (i + 1 < node.cases.size()) ? createBlock(nextTemp("case_next")) : defaultBB;

        node.cases[i].label->accept(*this);
        auto *caseVal = lastValue;
        currentBB->appendInstruction(IRInstruction::createICmp(ICmpKind::EQ, scrutinee, caseVal, nextTemp("case_cmp")));
        auto *cmp = &currentBB->instructions.back();
        emitCondBr(cmp, caseBodyBBs[i], nextBB);

        if (i + 1 < node.cases.size()) {
            setInsertBlock(nextBB);
        }
    }

    // 生成 case body 块，fall-through 连接到下一个 case body
    for (size_t i = 0; i < node.cases.size(); ++i) {
        setInsertBlock(caseBodyBBs[i]);
        node.cases[i].body->accept(*this);
        // fall-through：如果没有 break，跳到下一个 case body（或 default/exit）
        if (!currentBB->getTerminator()) {
            if (i + 1 < node.cases.size()) {
                emitBr(caseBodyBBs[i + 1]);
            } else {
                emitBr(node.defaultBody ? defaultBB : exitBB);
            }
        }
    }

    if (node.defaultBody) {
        setInsertBlock(defaultBB);
        node.defaultBody->accept(*this);
        if (!currentBB->getTerminator()) emitBr(exitBB);
    }

    breakTargets.pop_back();
    switchDefaultTargets.pop_back();

    setInsertBlock(exitBB);
}

void IRLowering::visitGotoStmt(GotoStmt &node) {
    auto it = labelMap.find(node.targetName);
    IRBasicBlock *target = nullptr;
    if (it != labelMap.end()) {
        target = it->second;
    } else {
        target = createBlock("label_" + node.targetName);
        labelMap[node.targetName] = target;
    }
    emitBr(target);
    // goto 之后创建一个不可达块
    auto *unreachable = createBlock(nextTemp("unreachable"));
    setInsertBlock(unreachable);
}

void IRLowering::visitLabelStmt(LabelStmt &node) {
    auto it = labelMap.find(node.name);
    IRBasicBlock *labelBB = nullptr;
    if (it != labelMap.end()) {
        labelBB = it->second;
    } else {
        labelBB = createBlock("label_" + node.name);
        labelMap[node.name] = labelBB;
    }
    // 从前一个块跳转到 label 块
    if (!currentBB->getTerminator()) {
        emitBr(labelBB);
    }
    setInsertBlock(labelBB);
    if (node.body) node.body->accept(*this);
}

void IRLowering::visitStaticAssertStmt(StaticAssertStmt &node) {
    // 静态断言：编译时已验证，无需生成代码
}

} // namespace ir
