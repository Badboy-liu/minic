#include "CodeGenerator.h"

#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <stdexcept>

CodeGenerator::CodeGenerator(TargetKind targetValue)
    : target(targetSpec(targetValue)) {}

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

    emitTargetExternPrelude();
    if (emitProgramEntryPoint) {
        emitLine(std::string("global ") + target.entrySymbol);
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

void CodeGenerator::emitTargetExternPrelude() {
    if (target.runtimeEntryFlavor == RuntimeEntryFlavor::ExitProcessStub) {
        emitLine("extern ExitProcess");
    }
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
            if (stringExpr) {
                line << global.symbolName << ": db ";
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
            } else if (global.init && global.init->kind == Expr::Kind::InitializerList) {
                const auto &list = static_cast<const InitializerListExpr &>(*global.init);
                line << global.symbolName << ": ";
                line << dataDirectiveForSize(global.type->elementType->valueSize()) << " ";
                bool first = true;
                if (global.type->elementType->isPointer()) {
                    for (const auto &element : list.elements) {
                        if (!first) {
                            line << ", ";
                        }
                        line << globalAddressInitializer(*element);
                        first = false;
                    }
                } else {
                    for (const auto &element : list.elements) {
                        if (!first) {
                            line << ", ";
                        }
                        line << evaluateStaticIntegerInitializer(*element);
                        first = false;
                    }
                }
                for (std::size_t i = list.elements.size(); i < static_cast<std::size_t>(global.type->arrayLength); ++i) {
                    if (!first) {
                        line << ", ";
                    }
                    line << "0";
                    first = false;
                }
            } else {
                line << global.symbolName << ": db ";
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

        if (global.type->isPointer() && global.init) {
            std::ostringstream line;
            line << global.symbolName << ": dq ";
            line << globalAddressInitializer(*global.init);
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
    emitLine(std::string(target.entrySymbol) + ":");
    emitTargetEntryBody();
}

void CodeGenerator::emitTargetEntryBody() {
    switch (target.runtimeEntryFlavor) {
    case RuntimeEntryFlavor::ExitProcessStub:
        emitLine("    sub rsp, 40");
        emitLine("    call " + functionSymbol("main"));
        emitLine("    mov ecx, eax");
        emitLine("    call ExitProcess");
        return;
    case RuntimeEntryFlavor::LinuxSyscall:
        emitLine("    call " + functionSymbol("main"));
        emitLine("    mov edi, eax");
        emitLine("    mov eax, 60");
        emitLine("    syscall");
        return;
    }
}

void CodeGenerator::emitFunction(const Function &function) {
    currentReturnLabel = makeLabel(function.name + "_return");
    const std::string symbol = functionSymbol(function.name);
    const int registerCount = argumentRegisterCount();

    emitLine(symbol + ":");
    emitLine("    push rbp");
    emitLine("    mov rbp, rsp");
    if (function.stackSize > 0) {
        emitLine("    sub rsp, " + std::to_string(function.stackSize));
    }
    for (int i = 0; i < static_cast<int>(function.parameters.size()); ++i) {
        const Type &type = *function.parameters[i].type;
        const std::string localAddress = "[rbp-" + std::to_string(function.parameters[i].stackOffset) + "]";
        if (i < registerCount) {
            if (type.valueSize() == 1) {
                emitLine("    mov byte " + localAddress + ", " + argumentRegister(i).r8);
            } else if (type.valueSize() == 2) {
                emitLine("    mov word " + localAddress + ", " + argumentRegister(i).r16);
            } else if (type.valueSize() <= 4) {
                emitLine("    mov dword " + localAddress + ", " + argumentRegister(i).r32);
            } else {
                emitLine("    mov qword " + localAddress + ", " + argumentRegister(i).r64);
            }
        } else {
            const std::string sourceAddress = "[rbp+" + std::to_string(stackArgumentOffset(i)) + "]";
            if (type.valueSize() == 1) {
                emitLine("    mov al, byte " + sourceAddress);
                emitLine("    mov byte " + localAddress + ", al");
            } else if (type.valueSize() == 2) {
                emitLine("    mov ax, word " + sourceAddress);
                emitLine("    mov word " + localAddress + ", ax");
            } else if (type.valueSize() <= 4) {
                emitLine("    mov eax, dword " + sourceAddress);
                emitLine("    mov dword " + localAddress + ", eax");
            } else {
                emitLine("    mov rax, qword " + sourceAddress);
                emitLine("    mov qword " + localAddress + ", rax");
            }
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
            if (decl.type->isArray() && decl.init->kind == Expr::Kind::String &&
                decl.type->elementType->equals(*Type::makeChar())) {
                emitLocalStringInitializer(decl, static_cast<const StringExpr &>(*decl.init));
            } else if (decl.type->isArray() && decl.init->kind == Expr::Kind::InitializerList) {
                emitLocalArrayInitializer(decl, static_cast<const InitializerListExpr &>(*decl.init));
            } else {
                emitLine("    lea rax, [rbp-" + std::to_string(decl.stackOffset) + "]");
                emitLine("    push rax");
                emitExpr(*decl.init);
                emitLine("    pop rcx");
                emitStore(*decl.type);
            }
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
        if (expr.type->isArray() || expr.type->isFunction()) {
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
            if (expr.type->valueSize() > 4) {
                emitLine("    neg rax");
            } else {
                emitLine("    neg eax");
            }
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
            if (expr.type->isFunction()) {
                return;
            }
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
        emitCallExpr(call);
        return;
    }
    case Expr::Kind::Index:
        emitAddress(expr);
        if (expr.type->isFunction()) {
            return;
        }
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
            } else if (expr.type->valueSize() > 4) {
                emitLine("    add rax, rcx");
            } else {
                emitLine("    add eax, ecx");
            }
            return;
        case BinaryOp::Subtract:
            if (expr.type->isPointer()) {
                emitLine("    imul rcx, " + std::to_string(pointeeSize(*binary.left->type->decay())));
                emitLine("    sub rax, rcx");
            } else if (expr.type->valueSize() > 4) {
                emitLine("    sub rax, rcx");
            } else {
                emitLine("    sub eax, ecx");
            }
            return;
        case BinaryOp::Multiply:
            if (expr.type->valueSize() > 4) {
                emitLine("    imul rax, rcx");
            } else {
                emitLine("    imul eax, ecx");
            }
            return;
        case BinaryOp::Divide:
            if (expr.type->valueSize() > 4) {
                emitLine("    cqo");
                emitLine("    idiv rcx");
            } else {
                emitLine("    cdq");
                emitLine("    idiv ecx");
            }
            return;
        case BinaryOp::Equal:
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
            emitLine("    sete al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::NotEqual:
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
            emitLine("    setne al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::Less:
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
            emitLine("    setl al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::LessEqual:
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
            emitLine("    setle al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::Greater:
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
            emitLine("    setg al");
            emitLine("    movzx eax, al");
            return;
        case BinaryOp::GreaterEqual:
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
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

void CodeGenerator::emitCallExpr(const CallExpr &call) {
    if (usesWindowsAbi()) {
        emitWindowsCallExpr(call);
        return;
    }
    emitSystemVCallExpr(call);
}

void CodeGenerator::emitWindowsCallExpr(const CallExpr &call) {
    const int registerCount = argumentRegisterCount();
    const int slotCount = std::max<int>(registerCount, static_cast<int>(call.arguments.size()));
    const int alignedSlotCount = (slotCount % 2 == 0) ? slotCount : slotCount + 1;
    emitLine("    sub rsp, " + std::to_string(alignedSlotCount * 8));
    for (int i = static_cast<int>(call.arguments.size()) - 1; i >= 0; --i) {
        emitExpr(*call.arguments[i]);
        emitLine("    mov qword [rsp+" + std::to_string(i * 8) + "], rax");
    }
    for (int i = 0; i < std::min<int>(registerCount, static_cast<int>(call.arguments.size())); ++i) {
        if (call.parameterTypes[i]->valueSize() <= 4) {
            emitLine("    mov " + std::string(argumentRegister(i).r32) +
                     ", dword [rsp+" + std::to_string(i * 8) + "]");
        } else {
            emitLine("    mov " + std::string(argumentRegister(i).r64) +
                     ", qword [rsp+" + std::to_string(i * 8) + "]");
        }
    }
    emitExpr(*call.callee);
    emitLine("    mov r11, rax");
    emitLine("    call r11");
    emitLine("    add rsp, " + std::to_string(alignedSlotCount * 8));
}

void CodeGenerator::emitSystemVCallExpr(const CallExpr &call) {
    const int registerCount = argumentRegisterCount();
    const int stackArgumentCount = std::max<int>(0, static_cast<int>(call.arguments.size()) - registerCount);
    const int temporaryRegisterSlots = std::min<int>(registerCount, static_cast<int>(call.arguments.size()));
    const int totalSlots = stackArgumentCount + temporaryRegisterSlots;
    const int alignedSlotCount = (totalSlots % 2 == 0) ? totalSlots : totalSlots + 1;
    if (alignedSlotCount > 0) {
        emitLine("    sub rsp, " + std::to_string(alignedSlotCount * 8));
    }
    for (int i = static_cast<int>(call.arguments.size()) - 1; i >= 0; --i) {
        emitExpr(*call.arguments[i]);
        const int slotIndex = i < registerCount ? stackArgumentCount + i : i - registerCount;
        emitLine("    mov qword [rsp+" + std::to_string(slotIndex * 8) + "], rax");
    }
    for (int i = 0; i < temporaryRegisterSlots; ++i) {
        if (call.parameterTypes[i]->valueSize() <= 4) {
            emitLine("    mov " + std::string(argumentRegister(i).r32) +
                     ", dword [rsp+" + std::to_string((stackArgumentCount + i) * 8) + "]");
        } else {
            emitLine("    mov " + std::string(argumentRegister(i).r64) +
                     ", qword [rsp+" + std::to_string((stackArgumentCount + i) * 8) + "]");
        }
    }
    emitExpr(*call.callee);
    emitLine("    mov r11, rax");
    emitLine("    call r11");
    if (alignedSlotCount > 0) {
        emitLine("    add rsp, " + std::to_string(alignedSlotCount * 8));
    }
}

void CodeGenerator::emitLocalStringInitializer(const DeclStmt &decl, const StringExpr &stringExpr) {
    const int baseOffset = decl.stackOffset;
    for (std::size_t i = 0; i < stringExpr.value.size(); ++i) {
        emitLine(
            "    mov byte [rbp-" + std::to_string(baseOffset - static_cast<int>(i)) + "], " +
            std::to_string(static_cast<int>(static_cast<unsigned char>(stringExpr.value[i]))));
    }
    emitLine("    mov byte [rbp-" + std::to_string(baseOffset - static_cast<int>(stringExpr.value.size())) + "], 0");
    for (int i = static_cast<int>(stringExpr.value.size()) + 1; i < decl.type->arrayLength; ++i) {
        emitLine("    mov byte [rbp-" + std::to_string(baseOffset - i) + "], 0");
    }
}

void CodeGenerator::emitLocalArrayInitializer(const DeclStmt &decl, const InitializerListExpr &list) {
    const int elementSize = decl.type->elementType->valueSize();
    for (std::size_t i = 0; i < list.elements.size(); ++i) {
        const int elementOffset = static_cast<int>(i) * elementSize;
        emitExpr(*list.elements[i]);
        emitStoreToLocalSlot(*decl.type->elementType, decl.stackOffset - elementOffset);
    }
    emitZeroLocalArrayElements(*decl.type, decl.stackOffset, list.elements.size());
}

void CodeGenerator::emitZeroLocalArrayElements(const Type &arrayType, int baseOffset, std::size_t startIndex) {
    const int elementSize = arrayType.elementType->valueSize();
    for (std::size_t i = startIndex; i < static_cast<std::size_t>(arrayType.arrayLength); ++i) {
        const int addressOffset = baseOffset - static_cast<int>(i) * elementSize;
        emitLine("    xor eax, eax");
        emitLine("    xor edx, edx");
        emitStoreToLocalSlot(*arrayType.elementType, addressOffset);
    }
}

void CodeGenerator::emitStoreToLocalSlot(const Type &type, int addressOffset) {
    const std::string address = "[rbp-" + std::to_string(addressOffset) + "]";
    if (type.valueSize() == 1) {
        emitLine("    mov byte " + address + ", al");
    } else if (type.valueSize() == 2) {
        emitLine("    mov word " + address + ", ax");
    } else if (type.valueSize() <= 4) {
        emitLine("    mov dword " + address + ", eax");
    } else {
        emitLine("    mov qword " + address + ", rax");
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

std::string CodeGenerator::dataDirectiveForSize(int size) const {
    switch (size) {
    case 1:
        return "db";
    case 2:
        return "dw";
    case 4:
        return "dd";
    default:
        return "dq";
    }
}

long long CodeGenerator::evaluateStaticIntegerInitializer(const Expr &expr) const {
    if (expr.kind == Expr::Kind::Number) {
        return static_cast<const NumberExpr &>(expr).value;
    }
    const auto &unary = static_cast<const UnaryExpr &>(expr);
    const long long value = static_cast<const NumberExpr &>(*unary.operand).value;
    return unary.op == UnaryOp::Minus ? -value : value;
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
        {"cl", "cx", "ecx", "rcx"},
        {"r8b", "r8w", "r8d", "r8"},
        {"r9b", "r9w", "r9d", "r9"}
    };

    if (index < 0 || index >= argumentRegisterCount()) {
        throw std::runtime_error("internal code generation error: unsupported argument register");
    }
    return usesWindowsAbi() ? windows[index] : linux[index];
}

bool CodeGenerator::usesWindowsAbi() const {
    return target.abiFlavor == AbiFlavor::WindowsX64;
}

int CodeGenerator::argumentRegisterCount() const {
    return target.integerRegisterArgumentCount;
}

int CodeGenerator::stackArgumentOffset(int index) const {
    if (usesWindowsAbi()) {
        return 16 + index * 8;
    }
    return 16 + (index - argumentRegisterCount()) * 8;
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
