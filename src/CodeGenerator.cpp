#include "CodeGenerator.h"

#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <stdexcept>

CodeGenerator::CodeGenerator(TargetKind targetValue)
    : target(targetValue) {}

std::string CodeGenerator::generate(const Program &program) {
    return generate(program, true);
}

std::string CodeGenerator::generate(const Program &program, bool emitEntryPointValue) {
    out.str("");
    out.clear();
    dataLines.clear();
    bssLines.clear();
    rdataLines.clear();
    stringLabels.clear();
    labelCounter = 0;
    loopContinueLabels.clear();
    loopBreakLabels.clear();
    emitProgramEntryPoint = emitEntryPointValue;
    currentProgram = &program;

    emitPrologue();

    for (const auto &function : program.functions) {
        if (!function.isDeclaration()) {
            emitFunction(function);
            emitLine("");
        }
    }

    if (emitProgramEntryPoint) {
        emitEntryPoint();
    }

    emitGlobals();
    emitStringLiterals();

    return out.str();
}

void CodeGenerator::emitPrologue() {
    emitLine("default rel");
    std::unordered_set<std::string> definedFunctions;
    for (const auto &function : currentProgram->functions) {
        if (!function.isDeclaration()) {
            definedFunctions.insert(function.name);
        }
    }

    if (target == TargetKind::WindowsX64) {
        emitLine("extern ExitProcess");
        if (emitProgramEntryPoint) {
            emitLine("global mainCRTStartup");
        }
    } else {
        if (emitProgramEntryPoint) {
            emitLine("global _start");
        }
    }
    for (const auto &function : currentProgram->functions) {
        if (function.isDeclaration()) {
            if (definedFunctions.find(function.name) == definedFunctions.end()) {
                emitLine("extern " + functionSymbol(function.name));
            }
        } else {
            emitLine("global " + functionSymbol(function.name));
        }
    }
    for (const auto &global : currentProgram->globals) {
        if (global.isExternal) {
            emitLine("extern " + global.symbolName);
        } else {
            emitLine("global " + global.symbolName);
        }
    }
    emitLine("");
    emitLine("section .text");
    emitLine("");
}

void CodeGenerator::emitGlobals() {
    if (currentProgram->globals.empty()) {
        return;
    }

    for (const auto &global : currentProgram->globals) {
        if (global.isExternal || !global.emitStorage) {
            continue;
        }

        if (global.isBss) {
            std::ostringstream line;
            if (global.type->isArray()) {
                line << global.symbolName << ": resb " << global.type->valueSize();
            } else {
                switch (global.type->valueSize()) {
                case 1:
                    line << global.symbolName << ": resb 1";
                    break;
                case 2:
                    line << global.symbolName << ": resw 1";
                    break;
                case 4:
                    line << global.symbolName << ": resd 1";
                    break;
                default:
                    line << global.symbolName << ": resq 1";
                    break;
                }
            }
            emitBssLine(line.str());
            continue;
        }

        if (global.type->isArray()) {
            const auto *stringExpr = global.init ? dynamic_cast<const StringExpr *>(global.init.get()) : nullptr;
            std::ostringstream line;
            line << global.symbolName << ": db ";
            if (stringExpr) {
                for (std::size_t i = 0; i < stringExpr->value.size(); ++i) {
                    if (i > 0) {
                        line << ", ";
                    }
                    line << static_cast<int>(static_cast<unsigned char>(stringExpr->value[i]));
                }
                if (!stringExpr->value.empty()) {
                    line << ", ";
                }
                line << "0";
                const int padding = global.type->arrayLength - static_cast<int>(stringExpr->value.size()) - 1;
                for (int i = 0; i < padding; ++i) {
                    line << ", 0";
                }
            } else {
                for (int i = 0; i < global.type->arrayLength; ++i) {
                    if (i > 0) {
                        line << ", ";
                    }
                    line << "0";
                }
            }
            emitDataLine(line.str());
            continue;
        }

        const int value = global.init ? static_cast<const NumberExpr &>(*global.init).value : 0;
        std::ostringstream line;
        switch (global.type->valueSize()) {
        case 1:
            line << global.symbolName << ": db " << value;
            break;
        case 2:
            line << global.symbolName << ": dw " << value;
            break;
        case 4:
            line << global.symbolName << ": dd " << value;
            break;
        default:
            line << global.symbolName << ": dq " << value;
            break;
        }
        emitDataLine(line.str());
    }

    if (dataLines.empty()) {
        if (bssLines.empty()) {
            return;
        }
    }

    if (!dataLines.empty()) {
        emitLine("");
        emitLine("section .data");
        for (const auto &line : dataLines) {
            emitLine(line);
        }
    }

    if (!bssLines.empty()) {
        emitLine("");
        emitLine("section .bss");
        for (const auto &line : bssLines) {
            emitLine(line);
        }
    }
}

void CodeGenerator::emitStringLiterals() {
    if (rdataLines.empty()) {
        return;
    }

    emitLine("");
    emitLine("section .rdata");
    for (const auto &line : rdataLines) {
        emitLine(line);
    }
}

void CodeGenerator::emitEntryPoint() {
    if (target == TargetKind::WindowsX64) {
        emitLine("mainCRTStartup:");
        emitLine("    sub rsp, 40");
        emitLine("    call " + functionSymbol("main"));
        emitLine("    mov ecx, eax");
        emitLine("    call ExitProcess");
        return;
    }

    emitLine("_start:");
    emitLine("    call " + functionSymbol("main"));
    emitLine("    mov edi, eax");
    emitLine("    mov eax, 60");
    emitLine("    syscall");
}

void CodeGenerator::emitFunction(const Function &function) {
    currentReturnLabel = makeLabel(function.name + "_return");
    const std::string symbol = functionSymbol(function.name);

    emitLine(symbol + ":");
    emitLine("    push rbp");
    emitLine("    mov rbp, rsp");
    if (function.stackSize > 0) {
        emitLine("    sub rsp, " + std::to_string(function.stackSize));
    }
    for (int i = 0; i < static_cast<int>(function.parameters.size()); ++i) {
        const Type &type = *function.parameters[i].type;
        if (type.valueSize() == 1) {
            emitLine(
                "    mov byte [rbp-" + std::to_string(function.parameters[i].stackOffset) + "], " +
                argumentRegister(i).r8);
        } else if (type.valueSize() == 2) {
            emitLine(
                "    mov word [rbp-" + std::to_string(function.parameters[i].stackOffset) + "], " +
                argumentRegister(i).r16);
        } else if (type.valueSize() <= 4) {
            emitLine(
                "    mov dword [rbp-" + std::to_string(function.parameters[i].stackOffset) + "], " +
                argumentRegister(i).r32);
        } else {
            emitLine(
                "    mov qword [rbp-" + std::to_string(function.parameters[i].stackOffset) + "], " +
                argumentRegister(i).r64);
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
    case Expr::Kind::String: {
        const auto &stringExpr = static_cast<const StringExpr &>(expr);
        emitLine("    lea rax, [rel " + stringLabel(stringExpr.value) + "]");
        return;
    }
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
                emitLine(std::string("    mov ") + argumentRegister(i).r32 + ", eax");
            } else {
                emitLine(std::string("    mov ") + argumentRegister(i).r64 + ", rax");
            }
        }
        if (target == TargetKind::WindowsX64) {
            emitLine("    sub rsp, 32");
        }
        emitLine("    call " + functionSymbol(call.callee));
        if (target == TargetKind::WindowsX64) {
            emitLine("    add rsp, 32");
        }
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
        if (variable.isGlobal) {
            emitLine("    lea rax, [rel " + variable.symbolName + "]");
            return;
        }
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
    if (type.valueSize() == 1) {
        emitLine("    movsx eax, byte [rax]");
    } else if (type.valueSize() == 2) {
        emitLine("    movsx eax, word [rax]");
    } else if (type.valueSize() <= 4) {
        emitLine("    mov eax, dword [rax]");
    } else {
        emitLine("    mov rax, qword [rax]");
    }
}

void CodeGenerator::emitStore(const Type &type) {
    if (type.valueSize() == 1) {
        emitLine("    mov byte [rcx], al");
    } else if (type.valueSize() == 2) {
        emitLine("    mov word [rcx], ax");
    } else if (type.valueSize() <= 4) {
        emitLine("    mov dword [rcx], eax");
    } else {
        emitLine("    mov qword [rcx], rax");
    }
}

int CodeGenerator::pointeeSize(const Type &type) const {
    if (!type.isPointer()) {
        throw std::runtime_error("internal code generation error");
    }
    return type.elementType->valueSize();
}

const CodeGenerator::RegisterSet &CodeGenerator::argumentRegister(int index) const {
    static const RegisterSet windows[] = {
        {"cl", "cx", "ecx", "rcx"},
        {"dl", "dx", "edx", "rdx"},
        {"r8b", "r8w", "r8d", "r8"},
        {"r9b", "r9w", "r9d", "r9"}
    };
    static const RegisterSet linux[] = {
        {"dil", "di", "edi", "rdi"},
        {"sil", "si", "esi", "rsi"},
        {"dl", "dx", "edx", "rdx"},
        {"cl", "cx", "ecx", "rcx"}
    };

    if (index < 0 || index >= 4) {
        throw std::runtime_error("internal code generation error: unsupported argument register");
    }
    return target == TargetKind::WindowsX64 ? windows[index] : linux[index];
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

std::string CodeGenerator::globalSymbol(const std::string &name) {
    return "gv_" + name;
}

void CodeGenerator::emitLine(const std::string &text) {
    out << text << '\n';
}

void CodeGenerator::emitDataLine(std::string text) {
    dataLines.push_back(std::move(text));
}

void CodeGenerator::emitBssLine(std::string text) {
    bssLines.push_back(std::move(text));
}

void CodeGenerator::emitRdataLine(std::string text) {
    rdataLines.push_back(std::move(text));
}

std::string CodeGenerator::makeLabel(const std::string &prefix) {
    return prefix + "_" + std::to_string(labelCounter++);
}
