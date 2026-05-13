#include "CodeGenerator.h"

#include <iomanip>
#include <stdexcept>
#include <unordered_map>

void CodeGenerator::emitExpr(Expr &expr) {
    expr.accept(*this);
}

void CodeGenerator::visitNumberExpr(NumberExpr &node) {
    emitLine("    mov rax, " + std::to_string(node.value));
}

void CodeGenerator::visitFloatLiteralExpr(FloatLiteralExpr &node) {
    emitLine("    movsd xmm0, [rel " + floatLiteralLabel(node.value) + "]");
}

void CodeGenerator::visitStringExpr(StringExpr &node) {
    emitLine("    lea rax, [rel " + stringLabel(node.value) + "]");
}

void CodeGenerator::visitVariableExpr(VariableExpr &node) {
    emitAddress(node);
    if (node.type->isArray() || node.type->isFunction() || node.type->isStruct()) {
        return;
    }
    emitLoad(*node.type);
}

void CodeGenerator::visitUnaryExpr(UnaryExpr &node) {
    switch (node.op) {
    case UnaryOp::Plus:
        emitExpr(*node.operand);
        return;
    case UnaryOp::Minus:
        emitExpr(*node.operand);
        if (node.type->isFloatingPoint()) {
            emitLine("    movsd xmm1, xmm0");
            emitLine("    xorpd xmm0, xmm0");
            emitLine("    subsd xmm0, xmm1");
        } else if (node.type->valueSize() > 4) {
            emitLine("    neg rax");
        } else {
            emitLine("    neg eax");
        }
        return;
    case UnaryOp::LogicalNot:
        emitExpr(*node.operand);
        if (node.operand->type && node.operand->type->isFloatingPoint()) {
            emitFloatToBool();
        }
        emitLine("    cmp rax, 0");
        emitLine("    sete al");
        emitLine("    movzx eax, al");
        return;
    case UnaryOp::AddressOf:
        emitAddress(*node.operand);
        return;
    case UnaryOp::Dereference:
        emitAddress(node);
        if (node.type->isFunction()) {
            return;
        }
        emitLoad(*node.type);
        return;
    case UnaryOp::BitwiseNot:
        emitExpr(*node.operand);
        emitLine("    not rax");
        return;
    case UnaryOp::PreIncrement:
    case UnaryOp::PreDecrement: {
        emitAddress(*node.operand);
        if (node.operand->type->isFloatingPoint()) {
            emitLine("    movsd xmm0, qword [rax]");
            emitLine("    movsd xmm1, [rel " + floatLiteralLabel(1.0) + "]");
            if (node.op == UnaryOp::PreIncrement) {
                emitLine("    addsd xmm0, xmm1");
            } else {
                emitLine("    subsd xmm0, xmm1");
            }
            emitLine("    movsd qword [rax], xmm0");
            return;
        }
        const char *op = (node.op == UnaryOp::PreIncrement) ? "inc" : "dec";
        int sz = node.operand->type->valueSize();
        if (sz == 1) {
            emitLine(std::string("    ") + op + " byte [rax]");
        } else if (sz == 2) {
            emitLine(std::string("    ") + op + " word [rax]");
        } else if (sz <= 4) {
            emitLine(std::string("    ") + op + " dword [rax]");
        } else {
            emitLine(std::string("    ") + op + " qword [rax]");
        }
        emitLoad(*node.operand->type);
        return;
    }
    case UnaryOp::PostIncrement:
    case UnaryOp::PostDecrement: {
        emitAddress(*node.operand);
        if (node.operand->type->isFloatingPoint()) {
            emitLine("    mov rcx, rax");
            emitLine("    movsd xmm0, qword [rax]");
            emitLine("    movsd xmm1, [rel " + floatLiteralLabel(1.0) + "]");
            emitLine("    movsd xmm2, xmm0");
            if (node.op == UnaryOp::PostIncrement) {
                emitLine("    addsd xmm0, xmm1");
            } else {
                emitLine("    subsd xmm0, xmm1");
            }
            emitLine("    movsd qword [rcx], xmm0");
            emitLine("    movsd xmm0, xmm2");
            return;
        }
        emitLine("    mov rcx, rax");
        emitLoad(*node.operand->type);
        const char *op = (node.op == UnaryOp::PostIncrement) ? "inc" : "dec";
        int sz = node.operand->type->valueSize();
        if (sz == 1) {
            emitLine(std::string("    ") + op + " byte [rcx]");
        } else if (sz == 2) {
            emitLine(std::string("    ") + op + " word [rcx]");
        } else if (sz <= 4) {
            emitLine(std::string("    ") + op + " dword [rcx]");
        } else {
            emitLine(std::string("    ") + op + " qword [rcx]");
        }
        return;
    }
    case UnaryOp::Sizeof:
        if (node.sizeofType) {
            emitLine("    mov eax, " + std::to_string(node.sizeofType->valueSize()));
        } else {
            emitLine("    mov eax, " + std::to_string(node.operand->type->valueSize()));
        }
        return;
    case UnaryOp::Alignof:
        if (node.sizeofType) {
            emitLine("    mov eax, " + std::to_string(node.sizeofType->alignment()));
        } else {
            emitLine("    mov eax, 1");
        }
        return;
    }
}

void CodeGenerator::visitBinaryExpr(BinaryExpr &node) {
    if (node.op == BinaryOp::LogicalAnd) {
        const std::string falseLabel = makeLabel("and_false");
        const std::string endLabel = makeLabel("and_end");
        emitExpr(*node.left);
        if (node.left->type && node.left->type->isFloatingPoint()) {
            emitFloatToBool();
        }
        emitLine("    cmp rax, 0");
        emitLine("    je " + falseLabel);
        emitExpr(*node.right);
        if (node.right->type && node.right->type->isFloatingPoint()) {
            emitFloatToBool();
        }
        emitLine("    cmp rax, 0");
        emitLine("    je " + falseLabel);
        emitLine("    mov eax, 1");
        emitLine("    jmp " + endLabel);
        emitLine(falseLabel + ":");
        emitLine("    xor eax, eax");
        emitLine(endLabel + ":");
        return;
    }
    if (node.op == BinaryOp::LogicalOr) {
        const std::string trueLabel = makeLabel("or_true");
        const std::string endLabel = makeLabel("or_end");
        emitExpr(*node.left);
        if (node.left->type && node.left->type->isFloatingPoint()) {
            emitFloatToBool();
        }
        emitLine("    cmp rax, 0");
        emitLine("    jne " + trueLabel);
        emitExpr(*node.right);
        if (node.right->type && node.right->type->isFloatingPoint()) {
            emitFloatToBool();
        }
        emitLine("    cmp rax, 0");
        emitLine("    jne " + trueLabel);
        emitLine("    xor eax, eax");
        emitLine("    jmp " + endLabel);
        emitLine(trueLabel + ":");
        emitLine("    mov eax, 1");
        emitLine(endLabel + ":");
        return;
    }
    if (node.op == BinaryOp::Comma) {
        emitExpr(*node.left);
        emitExpr(*node.right);
        return;
    }

    // 浮点二元运算
    if ((node.type && node.type->isFloatingPoint()) ||
        (node.left->type && node.left->type->isFloatingPoint())) {
        emitExpr(*node.left);
        emitLine("    sub rsp, 8");
        emitLine("    movsd [rsp], xmm0");
        emitExpr(*node.right);
        emitLine("    movsd xmm1, xmm0");
        emitLine("    movsd xmm0, [rsp]");
        emitLine("    add rsp, 8");

        switch (node.op) {
        case BinaryOp::Add:
            emitLine("    addsd xmm0, xmm1");
            return;
        case BinaryOp::Subtract:
            emitLine("    subsd xmm0, xmm1");
            return;
        case BinaryOp::Multiply:
            emitLine("    mulsd xmm0, xmm1");
            return;
        case BinaryOp::Divide:
            emitLine("    divsd xmm0, xmm1");
            return;
        case BinaryOp::Equal:
            emitLine("    comisd xmm0, xmm1");
            emitLine("    sete al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::NotEqual:
            emitLine("    comisd xmm0, xmm1");
            emitLine("    setne al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::Less:
            emitLine("    comisd xmm0, xmm1");
            emitLine("    setb al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::LessEqual:
            emitLine("    comisd xmm0, xmm1");
            emitLine("    setbe al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::Greater:
            emitLine("    comisd xmm0, xmm1");
            emitLine("    seta al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::GreaterEqual:
            emitLine("    comisd xmm0, xmm1");
            emitLine("    setae al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::Modulo:
            emitLine("    sub rsp, 8");
            emitLine("    call fmod");
            emitLine("    add rsp, 8");
            return;
        default:
            throw std::runtime_error("internal code generation error: unsupported float binary operation");
        }
    }

    emitExpr(*node.left);
    emitLine("    push rax");
    emitExpr(*node.right);
    emitLine("    mov rcx, rax");
    emitLine("    pop rax");

    switch (node.op) {
    case BinaryOp::Add:
        if (node.type->isPointer()) {
            const bool leftPointer = node.left->type->decay()->isPointer();
            if (leftPointer) {
                emitLine("    imul rcx, " + std::to_string(pointeeSize(*node.left->type->decay())));
                emitLine("    add rax, rcx");
            } else {
                emitLine("    imul rax, " + std::to_string(pointeeSize(*node.right->type->decay())));
                emitLine("    add rax, rcx");
            }
        } else if (node.type->valueSize() > 4) {
            emitLine("    add rax, rcx");
        } else {
            emitLine("    add eax, ecx");
        }
        return;
    case BinaryOp::Subtract:
        if (node.left->type->decay()->isPointer() && node.right->type->decay()->isPointer()) {
            emitLine("    sub rax, rcx");
            const int elemSize = pointeeSize(*node.left->type->decay());
            if (elemSize > 1) {
                emitLine("    mov rcx, " + std::to_string(elemSize));
                emitLine("    cqo");
                emitLine("    idiv rcx");
            }
        } else if (node.type->isPointer()) {
            emitLine("    imul rcx, " + std::to_string(pointeeSize(*node.left->type->decay())));
            emitLine("    sub rax, rcx");
        } else if (node.type->valueSize() > 4) {
            emitLine("    sub rax, rcx");
        } else {
            emitLine("    sub eax, ecx");
        }
        return;
    case BinaryOp::Multiply:
        if (node.type->valueSize() > 4) {
            emitLine("    imul rax, rcx");
        } else {
            emitLine("    imul eax, ecx");
        }
        return;
    case BinaryOp::Divide: {
        // 除零保护
        std::string divOk = ".div_ok_" + std::to_string(labelCounter++);
        emitLine("    test rcx, rcx");
        emitLine("    jnz " + divOk);
        emitLine("    ud2");
        emitLine(divOk + ":");
        if (node.type->valueSize() > 4) {
            emitLine("    cqo");
            emitLine("    idiv rcx");
        } else {
            emitLine("    cdq");
            emitLine("    idiv ecx");
        }
        return;
    }
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual: {
        // 缓存 decay 结果，避免重复 shared_ptr 解引用
        auto leftType = node.left->type->decay();
        auto rightType = node.right->type->decay();
        const int leftSize = leftType->valueSize();
        const int rightSize = rightType->valueSize();
        const bool useWideCmp = leftSize > 4 || rightSize > 4;

        if (useWideCmp) {
            emitLine("    cmp rax, rcx");
        } else {
            emitLine("    cmp eax, ecx");
        }

        switch (node.op) {
        case BinaryOp::Equal:        emitLine("    sete al"); break;
        case BinaryOp::NotEqual:     emitLine("    setne al"); break;
        case BinaryOp::Less: {
            const bool isUnsigned = leftType->isUnsigned || rightType->isUnsigned;
            emitLine(isUnsigned ? "    setb al" : "    setl al");
            break;
        }
        case BinaryOp::LessEqual: {
            const bool isUnsigned = leftType->isUnsigned || rightType->isUnsigned;
            emitLine(isUnsigned ? "    setbe al" : "    setle al");
            break;
        }
        case BinaryOp::Greater: {
            const bool isUnsigned = leftType->isUnsigned || rightType->isUnsigned;
            emitLine(isUnsigned ? "    seta al" : "    setg al");
            break;
        }
        case BinaryOp::GreaterEqual: {
            const bool isUnsigned = leftType->isUnsigned || rightType->isUnsigned;
            emitLine(isUnsigned ? "    setae al" : "    setge al");
            break;
        }
        default: break;
        }
        emitLine("    movzx eax, al");
        return;
    }
    case BinaryOp::Modulo: {
        // 除零保护
        std::string modOk = ".mod_ok_" + std::to_string(labelCounter++);
        emitLine("    test rcx, rcx");
        emitLine("    jnz " + modOk);
        emitLine("    ud2");
        emitLine(modOk + ":");
        if (node.type->valueSize() > 4) {
            emitLine("    cqo");
            emitLine("    idiv rcx");
            emitLine("    mov rax, rdx");
        } else {
            emitLine("    cdq");
            emitLine("    idiv ecx");
            emitLine("    mov eax, edx");
        }
        return;
    }
    case BinaryOp::ShiftLeft: {
        emitLine("    xchg rax, rcx");
        if (node.type->valueSize() > 4) {
            emitLine("    shl rax, cl");
        } else {
            emitLine("    shl eax, cl");
        }
        return;
    }
    case BinaryOp::ShiftRight: {
        emitLine("    xchg rax, rcx");
        if (node.type->valueSize() > 4) {
            emitLine("    sar rax, cl");
        } else {
            emitLine("    sar eax, cl");
        }
        return;
    }
    case BinaryOp::BitwiseAnd:
        if (node.type->valueSize() > 4) {
            emitLine("    and rax, rcx");
        } else {
            emitLine("    and eax, ecx");
        }
        return;
    case BinaryOp::BitwiseXor:
        if (node.type->valueSize() > 4) {
            emitLine("    xor rax, rcx");
        } else {
            emitLine("    xor eax, ecx");
        }
        return;
    case BinaryOp::BitwiseOr:
        if (node.type->valueSize() > 4) {
            emitLine("    or rax, rcx");
        } else {
            emitLine("    or eax, ecx");
        }
        return;
    default:
        throw std::runtime_error("internal code generation error: unhandled integer binary operator");
    }
}

void CodeGenerator::visitInitializerListExpr(InitializerListExpr &node) {
    // 初始化列表在表达式上下文中不常见，生成默认值
    emitLine("    xor eax, eax");
}

void CodeGenerator::visitAssignExpr(AssignExpr &node) {
    // 检查位域赋值
    if (node.target->kind == Expr::Kind::MemberAccess) {
        const auto &memberAccess = static_cast<const MemberAccessExpr &>(*node.target);
        if (memberAccess.bitWidth > 0) {
            emitAddress(*node.target);
            emitLine("    push rax");
            emitLine("    mov ecx, dword [rax]");
            int mask = (1 << memberAccess.bitWidth) - 1;
            int clearMask = ~(mask << memberAccess.bitOffset);
            emitLine("    and ecx, " + std::to_string(clearMask));
            emitExpr(*node.value);
            emitLine("    and eax, " + std::to_string(mask));
            if (memberAccess.bitOffset > 0) {
                emitLine("    shl eax, " + std::to_string(memberAccess.bitOffset));
            }
            emitLine("    or ecx, eax");
            emitLine("    pop rax");
            emitLine("    mov dword [rax], ecx");
            return;
        }
    }
    emitAddress(*node.target);
    emitLine("    push rax");
    if (node.target->type->isStruct()) {
        if (node.value->type->valueSize() <= 8 && node.value->kind == Expr::Kind::Call) {
            emitExpr(*node.value);
            emitLine("    pop rcx");
            emitStoreStructValueFromRax(*node.target->type, "rcx");
        } else {
            emitAddress(*node.value);
            emitLine("    pop rcx");
            emitLine("    mov rdx, rax");
            emitCopyStructValue(*node.target->type, "rcx", "rdx");
            emitLine("    mov rax, rcx");
        }
        return;
    }
    if (node.isCompound) {
        emitLine("    pop rcx");
        emitLine("    push rcx");

        if (node.target->type->isFloatingPoint()) {
            emitLoad(*node.target->type);
            emitLine("    sub rsp, 8");
            emitLine("    movsd [rsp], xmm0");
            emitExpr(*node.value);
            emitLine("    movsd xmm1, xmm0");
            emitLine("    movsd xmm0, [rsp]");
            emitLine("    add rsp, 8");
            switch (node.compoundOp) {
            case BinaryOp::Add:
                emitLine("    addsd xmm0, xmm1");
                break;
            case BinaryOp::Subtract:
                emitLine("    subsd xmm0, xmm1");
                break;
            case BinaryOp::Multiply:
                emitLine("    mulsd xmm0, xmm1");
                break;
            case BinaryOp::Divide:
                emitLine("    divsd xmm0, xmm1");
                break;
            default:
                break;
            }
            emitLine("    pop rcx");
            emitStore(*node.target->type);
            return;
        }

        emitLoad(*node.target->type);
        emitLine("    push rax");
        emitExpr(*node.value);
        emitLine("    mov rcx, rax");
        emitLine("    pop rax");
        switch (node.compoundOp) {
        case BinaryOp::Add:
            emitLine("    add eax, ecx");
            break;
        case BinaryOp::Subtract:
            emitLine("    sub eax, ecx");
            break;
        case BinaryOp::Multiply:
            emitLine("    imul eax, ecx");
            break;
        case BinaryOp::Divide: {
            std::string ok = ".div_ok_" + std::to_string(labelCounter++);
            emitLine("    test ecx, ecx");
            emitLine("    jnz " + ok);
            emitLine("    ud2");
            emitLine(ok + ":");
            emitLine("    cdq");
            emitLine("    idiv ecx");
            break;
        }
        case BinaryOp::Modulo: {
            std::string ok = ".mod_ok_" + std::to_string(labelCounter++);
            emitLine("    test ecx, ecx");
            emitLine("    jnz " + ok);
            emitLine("    ud2");
            emitLine(ok + ":");
            emitLine("    cdq");
            emitLine("    idiv ecx");
            emitLine("    mov eax, edx");
            break;
        }
        case BinaryOp::ShiftLeft:
            emitLine("    shl eax, cl");
            break;
        case BinaryOp::ShiftRight:
            emitLine("    sar eax, cl");
            break;
        case BinaryOp::BitwiseAnd:
            emitLine("    and eax, ecx");
            break;
        case BinaryOp::BitwiseXor:
            emitLine("    xor eax, ecx");
            break;
        case BinaryOp::BitwiseOr:
            emitLine("    or eax, ecx");
            break;
        default:
            break;
        }
        emitLine("    pop rcx");
        emitStore(*node.target->type);
        return;
    }
    emitExpr(*node.value);
    emitLine("    pop rcx");
    emitStore(*node.target->type);
}

void CodeGenerator::visitCallExpr(CallExpr &node) {
    emitCallExpr(node);
}

void CodeGenerator::visitIndexExpr(IndexExpr &node) {
    emitAddress(node);
    if (node.type->isFunction()) {
        return;
    }
    if (!node.type->isArray()) {
        emitLoad(*node.type);
    }
}

void CodeGenerator::visitMemberAccessExpr(MemberAccessExpr &node) {
    if (node.bitWidth > 0) {
        emitAddress(node);
        int containerSize = node.type->valueSize();
        if (containerSize == 1) {
            emitLine("    movzx eax, byte [rax]");
        } else if (containerSize == 2) {
            emitLine("    movzx eax, word [rax]");
        } else {
            emitLine("    mov eax, dword [rax]");
        }
        if (node.bitOffset > 0) {
            emitLine("    shr eax, " + std::to_string(node.bitOffset));
        }
        int mask = (1 << node.bitWidth) - 1;
        emitLine("    and eax, " + std::to_string(mask));
        return;
    }
    emitAddress(node);
    if (node.type->isFunction() || node.type->isStruct()) {
        return;
    }
    emitLoad(*node.type);
}

void CodeGenerator::visitTernaryExpr(TernaryExpr &node) {
    const std::string falseLabel = makeLabel("ternary_false");
    const std::string endLabel = makeLabel("ternary_end");
    emitExpr(*node.condition);
    if (node.condition->type && node.condition->type->isFloatingPoint()) {
        emitFloatToBool();
    }
    emitLine("    cmp rax, 0");
    emitLine("    je " + falseLabel);
    emitExpr(*node.thenExpr);
    emitLine("    jmp " + endLabel);
    emitLine(falseLabel + ":");
    emitExpr(*node.elseExpr);
    emitLine(endLabel + ":");
}

void CodeGenerator::visitCastExpr(CastExpr &node) {
    emitExpr(*node.operand);
    const int srcSize = node.operand->type ? node.operand->type->valueSize() : 4;
    const int dstSize = node.targetType->valueSize();
    const bool srcFloat = node.operand->type && node.operand->type->isFloatingPoint();
    const bool dstFloat = node.targetType->isFloatingPoint();

    if (node.targetType->isPointer() || (node.operand->type && node.operand->type->isPointer())) {
        return;
    }

    if (!srcFloat && dstFloat) {
        if (srcSize <= 4) {
            emitLine("    cvtsi2sd xmm0, eax");
        } else {
            emitLine("    cvtsi2sd xmm0, rax");
        }
        return;
    }

    if (srcFloat && !dstFloat) {
        if (dstSize <= 4) {
            emitLine("    cvttsd2si eax, xmm0");
        } else {
            emitLine("    cvttsd2si rax, xmm0");
        }
        return;
    }

    if (srcFloat && dstFloat) {
        return;
    }

    if (srcSize == dstSize) {
        return;
    }

    if (dstSize == 1) {
        emitLine("    movzx eax, al");
        return;
    }

    if (dstSize == 2) {
        emitLine("    movzx eax, ax");
        return;
    }

    if (dstSize <= 4) {
        return;
    }

    if (dstSize == 8) {
        if (srcSize <= 4) {
            emitLine("    movsxd rax, eax");
        }
        return;
    }
}

void CodeGenerator::visitBuiltinVaStartExpr(BuiltinVaStartExpr &node) {
    const int numParams = node.paramIndex + 1;
    emitLine("    lea rcx, [rbp+" + std::to_string(16 + numParams * 8) + "]");
    emitAddress(*node.ap);
    emitLine("    mov qword [rax], rcx");
    emitLine("    xor eax, eax");
}

void CodeGenerator::visitBuiltinVaArgExpr(BuiltinVaArgExpr &node) {
    emitAddress(*node.ap);
    emitLine("    mov r10, rax");
    emitExpr(*node.ap);
    emitLine("    mov rcx, rax");
    const int argSize = node.argType->valueSize();
    if (node.argType->isFloatingPoint()) {
        if (node.argType->kind == TypeKind::Float) {
            emitLine("    movss xmm0, dword [rcx]");
            emitLine("    cvtss2sd xmm0, xmm0");
        } else {
            emitLine("    movsd xmm0, qword [rcx]");
        }
        emitLine("    add rcx, " + std::to_string(Type::alignTo(argSize, 8)));
        emitLine("    mov qword [r10], rcx");
    } else {
        if (argSize == 1) {
            emitLine("    movsx eax, byte [rcx]");
        } else if (argSize == 2) {
            emitLine("    movsx eax, word [rcx]");
        } else if (argSize <= 4) {
            emitLine("    mov eax, dword [rcx]");
        } else {
            emitLine("    mov rax, qword [rcx]");
        }
        emitLine("    add rcx, " + std::to_string(Type::alignTo(argSize, 8)));
        emitLine("    mov qword [r10], rcx");
    }
}

void CodeGenerator::visitBuiltinVaEndExpr(BuiltinVaEndExpr &) {
    emitLine("    xor eax, eax");
}

void CodeGenerator::visitGenericExpr(GenericExpr &node) {
    if (node.selectedExpr) {
        emitExpr(*node.selectedExpr);
        return;
    }
    emitLine("    xor eax, eax");
}

void CodeGenerator::visitCompoundLiteralExpr(CompoundLiteralExpr &node) {
    if (node.compoundType->isArray() && node.init) {
        const auto &list = *node.init;
        const int elemSize = node.compoundType->elementType->valueSize();
        for (std::size_t i = 0; i < list.elements.size(); ++i) {
            emitExpr(*list.elements[i]);
            const int offset = node.stackOffset - static_cast<int>(i) * elemSize;
            emitStoreToLocalSlot(*node.compoundType->elementType, offset);
        }
    } else if (node.compoundType->isStruct() && node.init) {
        const auto &list = *node.init;
        bool hasDesignators = false;
        for (const auto &desig : list.designators) {
            if (!desig.empty()) {
                hasDesignators = true;
                break;
            }
        }
        if (hasDesignators) {
            std::unordered_map<std::string, std::size_t> fieldToElement;
            for (std::size_t ei = 0; ei < list.designators.size(); ++ei) {
                if (!list.designators[ei].empty() && list.designators[ei][0].kind == Designator::Field) {
                    fieldToElement[list.designators[ei][0].fieldName] = ei;
                }
            }
            for (std::size_t mi = 0; mi < node.compoundType->members.size(); ++mi) {
                const auto &member = node.compoundType->members[mi];
                auto it = fieldToElement.find(member.name);
                if (it == fieldToElement.end()) continue;
                emitExpr(*list.elements[it->second]);
                const int memberStackOffset = node.stackOffset - member.offset;
                emitStoreToLocalSlot(*member.type, memberStackOffset);
            }
        } else {
            for (std::size_t i = 0; i < list.elements.size() && i < node.compoundType->members.size(); ++i) {
                const auto &member = node.compoundType->members[i];
                emitExpr(*list.elements[i]);
                const int memberStackOffset = node.stackOffset - member.offset;
                emitStoreToLocalSlot(*member.type, memberStackOffset);
            }
        }
    }
    emitLine("    lea rax, " + formatStackAddress(node.stackOffset));
}

void CodeGenerator::visitStmtExpr(StmtExpr &node) {
    for (const auto &stmt : node.statements) {
        emitStatement(*stmt);
    }
    if (node.result) {
        emitExpr(*node.result);
    } else {
        emitLine("    xor eax, eax");
    }
}

void CodeGenerator::emitAddress(Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Variable: {
        const auto &variable = static_cast<const VariableExpr &>(expr);
        if (variable.isGlobal) {
            emitLine("    lea rax, [rel " + variable.symbolName + "]");
            return;
        }
        emitLine("    lea rax, " + formatStackAddress(variable.stackOffset));
        return;
    }
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        if (unary.op != UnaryOp::Dereference) {
            throw std::runtime_error("internal code generation error: cannot take address of unary operator " +
                                     std::to_string(static_cast<int>(unary.op)));
        }
        emitExpr(*unary.operand);
        return;
    }
    case Expr::Kind::Index: {
        const auto &index = static_cast<const IndexExpr &>(expr);
        emitExpr(*index.base);
        emitLine("    push rax");
        emitExpr(*index.index);
        emitLine("    imul rax, " + std::to_string(pointeeSize(*index.base->type->decay())));
        emitLine("    mov rcx, rax");
        emitLine("    pop rax");
        emitLine("    add rax, rcx");
        return;
    }
    case Expr::Kind::MemberAccess: {
        const auto &member = static_cast<const MemberAccessExpr &>(expr);
        emitAddress(*member.base);
        if (member.memberOffset > 0) {
            emitLine("    add rax, " + std::to_string(member.memberOffset));
        }
        return;
    }
    case Expr::Kind::Call:
        if (expr.type->isStruct() && expr.type->valueSize() > 8) {
            emitExpr(expr);
            return;
        }
        break;
    default:
        throw std::runtime_error("internal code generation error: cannot take address of expression kind " +
                                 std::to_string(static_cast<int>(expr.kind)));
    }
    throw std::runtime_error("internal code generation error: cannot take address of non-struct call expression");
}

void CodeGenerator::emitFloatToBool() {
    emitLine("    xorpd xmm1, xmm1");
    emitLine("    comisd xmm0, xmm1");
    emitLine("    setne al");
    emitLine("    movzx eax, al");
}

std::string CodeGenerator::globalAddressInitializer(const Expr &expr) {
    if (expr.kind == Expr::Kind::String) {
        const auto &stringExpr = static_cast<const StringExpr &>(expr);
        return stringLabel(stringExpr.value);
    }
    if (expr.kind == Expr::Kind::Variable) {
        return static_cast<const VariableExpr &>(expr).symbolName;
    }
    const auto &unary = static_cast<const UnaryExpr &>(expr);
    const auto &variable = static_cast<const VariableExpr &>(*unary.operand);
    return variable.symbolName;
}

std::string CodeGenerator::functionSymbol(const std::string &name) {
    return "fn_" + name;
}

std::string CodeGenerator::stringLabel(const std::string &value) {
    const auto found = stringLabels.find(value);
    if (found != stringLabels.end()) {
        return found->second;
    }

    const std::string label = "str_" + std::to_string(stringLabels.size());
    stringLabels.emplace(value, label);

    std::ostringstream line;
    line << label << ": db ";
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i > 0) {
            line << ", ";
        }
        line << static_cast<int>(static_cast<unsigned char>(value[i]));
    }
    if (!value.empty()) {
        line << ", ";
    }
    line << "0";
    emitRdataLine(line.str());
    return label;
}

std::string CodeGenerator::floatLiteralLabel(double value) {
    const auto found = floatLabels.find(value);
    if (found != floatLabels.end()) {
        return found->second;
    }
    const std::string label = "flt_" + std::to_string(floatLabels.size());
    floatLabels.emplace(value, label);
    std::ostringstream valStream;
    valStream << std::setprecision(17) << value;
    std::string valStr = valStream.str();
    if (valStr.find('.') == std::string::npos &&
        valStr.find('e') == std::string::npos &&
        valStr.find('E') == std::string::npos) {
        valStr += ".0";
    }
    std::ostringstream line;
    line << label << ": dq __float64__(" << valStr << ")";
    emitRdataLine(line.str());
    return label;
}

std::string CodeGenerator::globalSymbol(const std::string &name) {
    return "gv_" + name;
}
