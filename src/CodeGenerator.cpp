#include "CodeGenerator.h"

#include <stdexcept>

std::string CodeGenerator::generate(const Program &program) {
    out.str("");
    out.clear();
    labelCounter = 0;
    loopContinueLabels.clear();
    loopBreakLabels.clear();

    emitLine("option casemap:none");
    emitLine("EXTERN ExitProcess:PROC");
    emitLine("");
    emitLine(".code");
    emitLine("");

    for (const auto &function : program.functions) {
        if (!function.isDeclaration()) {
            emitFunction(function);
            emitLine("");
        }
    }

    emitLine("mainCRTStartup PROC");
    emitLine("    sub rsp, 40");
    emitLine("    call " + functionSymbol("main"));
    emitLine("    mov ecx, eax");
    emitLine("    call ExitProcess");
    emitLine("mainCRTStartup ENDP");
    emitLine("");
    emitLine("END");

    return out.str();
}

void CodeGenerator::emitFunction(const Function &function) {
    currentReturnLabel = makeLabel(function.name + "_return");
    const std::string symbol = functionSymbol(function.name);

    emitLine(symbol + " PROC");
    emitLine("    push rbp");
    emitLine("    mov rbp, rsp");
    if (function.stackSize > 0) {
        emitLine("    sub rsp, " + std::to_string(function.stackSize));
    }
    for (int i = 0; i < static_cast<int>(function.parameters.size()); ++i) {
        const Type &type = *function.parameters[i].type;
        if (type.valueSize() <= 4) {
            emitLine(
                "    mov DWORD PTR [rbp-" + std::to_string(function.parameters[i].stackOffset) + "], " +
                argumentRegister32(i));
        } else {
            emitLine(
                "    mov QWORD PTR [rbp-" + std::to_string(function.parameters[i].stackOffset) + "], " +
                argumentRegister64(i));
        }
    }

    for (const auto &statement : function.body->statements) {
        emitStatement(*statement);
    }

    emitLine("    xor eax, eax");
    emitLine(currentReturnLabel + ":");
    if (function.stackSize > 0) {
        emitLine("    add rsp, " + std::to_string(function.stackSize));
    }
    emitLine("    pop rbp");
    emitLine("    ret");
    emitLine(symbol + " ENDP");
}

void CodeGenerator::emitStatement(const Stmt &stmt) {
    switch (stmt.kind) {
    case Stmt::Kind::Return: {
        const auto &returnStmt = static_cast<const ReturnStmt &>(stmt);
        if (returnStmt.expr) {
            emitExpr(*returnStmt.expr);
        }
        emitLine("    jmp " + currentReturnLabel);
        break;
    }
    case Stmt::Kind::Expr:
        emitExpr(*static_cast<const ExprStmt &>(stmt).expr);
        break;
    case Stmt::Kind::Decl: {
        const auto &decl = static_cast<const DeclStmt &>(stmt);
        if (decl.init) {
            emitLine("    lea rax, [rbp-" + std::to_string(decl.stackOffset) + "]");
            emitLine("    push rax");
            emitExpr(*decl.init);
            emitLine("    pop rcx");
            emitStore(*decl.type);
        }
        break;
    }
    case Stmt::Kind::Block: {
        const auto &block = static_cast<const BlockStmt &>(stmt);
        for (const auto &nested : block.statements) {
            emitStatement(*nested);
        }
        break;
    }
    case Stmt::Kind::If: {
        const auto &ifStmt = static_cast<const IfStmt &>(stmt);
        const std::string elseLabel = makeLabel("if_else");
        const std::string endLabel = makeLabel("if_end");

        emitExpr(*ifStmt.condition);
        emitLine("    cmp rax, 0");
        emitLine("    je " + elseLabel);
        emitStatement(*ifStmt.thenBranch);
        emitLine("    jmp " + endLabel);
        emitLine(elseLabel + ":");
        if (ifStmt.elseBranch) {
            emitStatement(*ifStmt.elseBranch);
        }
        emitLine(endLabel + ":");
        break;
    }
    case Stmt::Kind::While: {
        const auto &whileStmt = static_cast<const WhileStmt &>(stmt);
        const std::string loopLabel = makeLabel("while_begin");
        const std::string endLabel = makeLabel("while_end");

        loopContinueLabels.push_back(loopLabel);
        loopBreakLabels.push_back(endLabel);
        emitLine(loopLabel + ":");
        emitExpr(*whileStmt.condition);
        emitLine("    cmp rax, 0");
        emitLine("    je " + endLabel);
        emitStatement(*whileStmt.body);
        emitLine("    jmp " + loopLabel);
        emitLine(endLabel + ":");
        loopContinueLabels.pop_back();
        loopBreakLabels.pop_back();
        break;
    }
    case Stmt::Kind::For: {
        const auto &forStmt = static_cast<const ForStmt &>(stmt);
        const std::string conditionLabel = makeLabel("for_cond");
        const std::string updateLabel = makeLabel("for_update");
        const std::string endLabel = makeLabel("for_end");

        if (forStmt.init) {
            emitStatement(*forStmt.init);
        }
        loopContinueLabels.push_back(updateLabel);
        loopBreakLabels.push_back(endLabel);
        emitLine(conditionLabel + ":");
        if (forStmt.condition) {
            emitExpr(*forStmt.condition);
            emitLine("    cmp rax, 0");
            emitLine("    je " + endLabel);
        }
        emitStatement(*forStmt.body);
        emitLine(updateLabel + ":");
        if (forStmt.update) {
            emitExpr(*forStmt.update);
        }
        emitLine("    jmp " + conditionLabel);
        emitLine(endLabel + ":");
        loopContinueLabels.pop_back();
        loopBreakLabels.pop_back();
        break;
    }
    case Stmt::Kind::Break:
        emitLine("    jmp " + loopBreakLabels.back());
        break;
    case Stmt::Kind::Continue:
        emitLine("    jmp " + loopContinueLabels.back());
        break;
    }
}

void CodeGenerator::emitExpr(const Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Number:
        emitLine("    mov eax, " + std::to_string(static_cast<const NumberExpr &>(expr).value));
        return;
    case Expr::Kind::Variable:
        emitAddress(expr);
        if (expr.type->isArray()) {
            return;
        }
        emitLoad(*expr.type);
        return;
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        switch (unary.op) {
        case UnaryOp::Plus:
            emitExpr(*unary.operand);
            return;
        case UnaryOp::Minus:
            emitExpr(*unary.operand);
            emitLine("    neg eax");
            return;
        case UnaryOp::LogicalNot:
            emitExpr(*unary.operand);
            emitLine("    cmp rax, 0");
            emitLine("    sete al");
            emitLine("    movzx eax, al");
            return;
        case UnaryOp::AddressOf:
            emitAddress(*unary.operand);
            return;
        case UnaryOp::Dereference:
            emitAddress(expr);
            emitLoad(*expr.type);
            return;
        }
        return;
    }
    case Expr::Kind::Assign: {
        const auto &assign = static_cast<const AssignExpr &>(expr);
        emitAddress(*assign.target);
        emitLine("    push rax");
        emitExpr(*assign.value);
        emitLine("    pop rcx");
        emitStore(*assign.target->type);
        return;
    }
    case Expr::Kind::Call: {
        const auto &call = static_cast<const CallExpr &>(expr);
        for (const auto &argument : call.arguments) {
            emitExpr(*argument);
            emitLine("    push rax");
        }
        for (int i = static_cast<int>(call.arguments.size()) - 1; i >= 0; --i) {
            emitLine("    pop rax");
            if (call.parameterTypes[i]->valueSize() <= 4) {
                emitLine("    mov " + argumentRegister32(i) + ", eax");
            } else {
                emitLine("    mov " + argumentRegister64(i) + ", rax");
            }
        }
        emitLine("    sub rsp, 32");
        emitLine("    call " + functionSymbol(call.callee));
        emitLine("    add rsp, 32");
        return;
    }
    case Expr::Kind::Index:
        emitAddress(expr);
        emitLoad(*expr.type);
        return;
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        if (binary.op == BinaryOp::LogicalAnd) {
            const std::string falseLabel = makeLabel("and_false");
            const std::string endLabel = makeLabel("and_end");
            emitExpr(*binary.left);
            emitLine("    cmp rax, 0");
            emitLine("    je " + falseLabel);
            emitExpr(*binary.right);
            emitLine("    cmp rax, 0");
            emitLine("    je " + falseLabel);
            emitLine("    mov eax, 1");
            emitLine("    jmp " + endLabel);
            emitLine(falseLabel + ":");
            emitLine("    xor eax, eax");
            emitLine(endLabel + ":");
            return;
        }
        if (binary.op == BinaryOp::LogicalOr) {
            const std::string trueLabel = makeLabel("or_true");
            const std::string endLabel = makeLabel("or_end");
            emitExpr(*binary.left);
            emitLine("    cmp rax, 0");
            emitLine("    jne " + trueLabel);
            emitExpr(*binary.right);
            emitLine("    cmp rax, 0");
            emitLine("    jne " + trueLabel);
            emitLine("    xor eax, eax");
            emitLine("    jmp " + endLabel);
            emitLine(trueLabel + ":");
            emitLine("    mov eax, 1");
            emitLine(endLabel + ":");
            return;
        }

        emitExpr(*binary.left);
        emitLine("    push rax");
        emitExpr(*binary.right);
        emitLine("    mov rcx, rax");
        emitLine("    pop rax");

        switch (binary.op) {
        case BinaryOp::Add:
            if (expr.type->isPointer()) {
                const bool leftPointer = binary.left->type->decay()->isPointer();
                if (leftPointer) {
                    emitLine("    imul rcx, " + std::to_string(pointeeSize(*binary.left->type->decay())));
                    emitLine("    add rax, rcx");
                } else {
                    emitLine("    imul rax, " + std::to_string(pointeeSize(*binary.right->type->decay())));
                    emitLine("    add rax, rcx");
                }
            } else {
                emitLine("    add eax, ecx");
            }
            return;
        case BinaryOp::Subtract:
            if (expr.type->isPointer()) {
                emitLine("    imul rcx, " + std::to_string(pointeeSize(*binary.left->type->decay())));
                emitLine("    sub rax, rcx");
            } else {
                emitLine("    sub eax, ecx");
            }
            return;
        case BinaryOp::Multiply:
            emitLine("    imul eax, ecx");
            return;
        case BinaryOp::Divide:
            emitLine("    cdq");
            emitLine("    idiv ecx");
            return;
        case BinaryOp::Equal:
            emitLine("    cmp rax, rcx");
            emitLine("    sete al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::NotEqual:
            emitLine("    cmp rax, rcx");
            emitLine("    setne al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::Less:
            emitLine("    cmp eax, ecx");
            emitLine("    setl al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::LessEqual:
            emitLine("    cmp eax, ecx");
            emitLine("    setle al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::Greater:
            emitLine("    cmp eax, ecx");
            emitLine("    setg al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::GreaterEqual:
            emitLine("    cmp eax, ecx");
            emitLine("    setge al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::LogicalAnd:
        case BinaryOp::LogicalOr:
            break;
        }
        throw std::runtime_error("internal code generation error");
    }
    }

    throw std::runtime_error("internal code generation error");
}

void CodeGenerator::emitAddress(const Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Variable: {
        const auto &variable = static_cast<const VariableExpr &>(expr);
        emitLine("    lea rax, [rbp-" + std::to_string(variable.stackOffset) + "]");
        return;
    }
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        if (unary.op != UnaryOp::Dereference) {
            throw std::runtime_error("internal code generation error");
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
    default:
        throw std::runtime_error("internal code generation error");
    }
}

void CodeGenerator::emitLoad(const Type &type) {
    if (type.valueSize() <= 4) {
        emitLine("    mov eax, DWORD PTR [rax]");
    } else {
        emitLine("    mov rax, QWORD PTR [rax]");
    }
}

void CodeGenerator::emitStore(const Type &type) {
    if (type.valueSize() <= 4) {
        emitLine("    mov DWORD PTR [rcx], eax");
    } else {
        emitLine("    mov QWORD PTR [rcx], rax");
    }
}

int CodeGenerator::pointeeSize(const Type &type) const {
    if (!type.isPointer()) {
        throw std::runtime_error("internal code generation error");
    }
    return type.elementType->valueSize();
}

std::string CodeGenerator::argumentRegister32(int index) {
    switch (index) {
    case 0:
        return "ecx";
    case 1:
        return "edx";
    case 2:
        return "r8d";
    case 3:
        return "r9d";
    default:
        throw std::runtime_error("internal code generation error: unsupported argument register");
    }
}

std::string CodeGenerator::argumentRegister64(int index) {
    switch (index) {
    case 0:
        return "rcx";
    case 1:
        return "rdx";
    case 2:
        return "r8";
    case 3:
        return "r9";
    default:
        throw std::runtime_error("internal code generation error: unsupported argument register");
    }
}

std::string CodeGenerator::functionSymbol(const std::string &name) {
    return "fn_" + name;
}

void CodeGenerator::emitLine(const std::string &text) {
    out << text << '\n';
}

std::string CodeGenerator::makeLabel(const std::string &prefix) {
    return prefix + "_" + std::to_string(labelCounter++);
}
