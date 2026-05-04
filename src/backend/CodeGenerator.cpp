#include "CodeGenerator.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <stdexcept>

namespace {

int maxLargeStructCallResultSizeExpr(const Expr &expr);

int maxLargeStructCallResultSizeStmt(const Stmt &stmt) {
    switch (stmt.kind) {
    case Stmt::Kind::Return: {
        const auto &returnStmt = static_cast<const ReturnStmt &>(stmt);
        return returnStmt.expr ? maxLargeStructCallResultSizeExpr(*returnStmt.expr) : 0;
    }
    case Stmt::Kind::Expr:
        return maxLargeStructCallResultSizeExpr(*static_cast<const ExprStmt &>(stmt).expr);
    case Stmt::Kind::Decl: {
        const auto &decl = static_cast<const DeclStmt &>(stmt);
        return decl.init ? maxLargeStructCallResultSizeExpr(*decl.init) : 0;
    }
    case Stmt::Kind::Block: {
        int maxSize = 0;
        for (const auto &nested : static_cast<const BlockStmt &>(stmt).statements) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeStmt(*nested));
        }
        return maxSize;
    }
    case Stmt::Kind::If: {
        const auto &ifStmt = static_cast<const IfStmt &>(stmt);
        int maxSize = maxLargeStructCallResultSizeExpr(*ifStmt.condition);
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeStmt(*ifStmt.thenBranch));
        if (ifStmt.elseBranch) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeStmt(*ifStmt.elseBranch));
        }
        return maxSize;
    }
    case Stmt::Kind::While: {
        const auto &whileStmt = static_cast<const WhileStmt &>(stmt);
        return std::max(
            maxLargeStructCallResultSizeExpr(*whileStmt.condition),
            maxLargeStructCallResultSizeStmt(*whileStmt.body));
    }
    case Stmt::Kind::For: {
        const auto &forStmt = static_cast<const ForStmt &>(stmt);
        int maxSize = 0;
        if (forStmt.init) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeStmt(*forStmt.init));
        }
        if (forStmt.condition) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*forStmt.condition));
        }
        if (forStmt.update) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*forStmt.update));
        }
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeStmt(*forStmt.body));
        return maxSize;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
        return 0;
    }
    return 0;
}

int maxLargeStructCallResultSizeExpr(const Expr &expr) {
    int maxSize = 0;
    if (expr.kind == Expr::Kind::Call && expr.type && expr.type->isStruct() && expr.type->valueSize() > 8) {
        maxSize = expr.type->valueSize();
    }

    switch (expr.kind) {
    case Expr::Kind::Number:
    case Expr::Kind::String:
    case Expr::Kind::Variable:
        return maxSize;
    case Expr::Kind::InitializerList: {
        const auto &list = static_cast<const InitializerListExpr &>(expr);
        for (const auto &element : list.elements) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*element));
        }
        return maxSize;
    }
    case Expr::Kind::Call: {
        const auto &call = static_cast<const CallExpr &>(expr);
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*call.callee));
        for (const auto &argument : call.arguments) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*argument));
        }
        return maxSize;
    }
    case Expr::Kind::Index: {
        const auto &index = static_cast<const IndexExpr &>(expr);
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*index.base));
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*index.index));
        return maxSize;
    }
    case Expr::Kind::MemberAccess:
        return std::max(maxSize, maxLargeStructCallResultSizeExpr(*static_cast<const MemberAccessExpr &>(expr).base));
    case Expr::Kind::Unary:
        return std::max(maxSize, maxLargeStructCallResultSizeExpr(*static_cast<const UnaryExpr &>(expr).operand));
    case Expr::Kind::Assign: {
        const auto &assign = static_cast<const AssignExpr &>(expr);
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*assign.target));
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*assign.value));
        return maxSize;
    }
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*binary.left));
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*binary.right));
        return maxSize;
    }
    }
    return maxSize;
}

int maxLargeStructCallResultSizeFunction(const Function &function) {
    int maxSize = 0;
    if (!function.body) {
        return 0;
    }
    for (const auto &statement : function.body->statements) {
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeStmt(*statement));
    }
    return maxSize;
}

}

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
            if (global.type->isArray() || global.type->isStruct()) {
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

        if (global.type->isStruct()) {
            const auto *list = global.init && global.init->kind == Expr::Kind::InitializerList
                ? static_cast<const InitializerListExpr *>(global.init.get())
                : nullptr;
            emitGlobalStructInitializer(global, list);
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
    if (!usesWindowsAbi()) {
        if (function.returnType->isStruct()) {
            throw std::runtime_error("by-value struct returns are not supported yet on " + std::string(target.name));
        }
        for (const auto &parameter : function.parameters) {
            if (parameter.type->isStruct()) {
                throw std::runtime_error("by-value struct parameters are not supported yet on " + std::string(target.name));
            }
        }
    }

    currentReturnLabel = makeLabel(function.name + "_return");
    const std::string symbol = functionSymbol(function.name);
    const int registerCount = argumentRegisterCount();
    const bool usesHiddenReturnPointer =
        usesWindowsAbi() && function.returnType->isStruct() && function.returnType->valueSize() > 8;
    const std::vector<WindowsAbiArgument> abiArguments =
        usesWindowsAbi()
        ? buildWindowsAbiArguments(
              [&]() {
                  std::vector<TypePtr> parameterTypes;
                  parameterTypes.reserve(function.parameters.size());
                  for (const auto &parameter : function.parameters) {
                      parameterTypes.push_back(parameter.type);
                  }
                  return parameterTypes;
              }(),
              usesHiddenReturnPointer)
        : std::vector<WindowsAbiArgument>{};
    const int frameSize = currentFunctionFrameSize(function);
    activeHiddenReturnPointerOffset = usesHiddenReturnPointer
        ? findHiddenReturnPointerLocalOffset(function)
        : 0;
    activeLargeStructCallResultOffset = findLargeStructCallResultLocalOffset(function);

    emitLine(symbol + ":");
    emitLine("    push rbp");
    emitLine("    mov rbp, rsp");
    if (frameSize > 0) {
        emitLine("    sub rsp, " + std::to_string(frameSize));
    }
    if (usesHiddenReturnPointer) {
        const WindowsAbiArgument &hiddenArg = abiArguments.front();
        const std::string hiddenLocalAddress = "[rbp-" + std::to_string(activeHiddenReturnPointerOffset) + "]";
        if (hiddenArg.inRegister) {
            emitLine("    mov qword " + hiddenLocalAddress + ", " + argumentRegister(hiddenArg.registerIndex).r64);
        } else {
            emitLine("    mov rax, qword [rbp+" + std::to_string(16 + hiddenArg.homeOffset) + "]");
            emitLine("    mov qword " + hiddenLocalAddress + ", rax");
        }
    }
    for (int i = 0; i < static_cast<int>(function.parameters.size()); ++i) {
        const Type &type = *function.parameters[i].type;
        const std::string localAddress = "[rbp-" + std::to_string(function.parameters[i].stackOffset) + "]";
        if (usesWindowsAbi()) {
            const WindowsAbiArgument &placement = abiArguments[static_cast<std::size_t>(i + (usesHiddenReturnPointer ? 1 : 0))];
            if (placement.inRegister) {
                if (type.isStruct()) {
                    emitLine("    mov qword " + localAddress + ", " + argumentRegister(placement.registerIndex).r64);
                } else if (type.valueSize() == 1) {
                    emitLine("    mov byte " + localAddress + ", " + argumentRegister(placement.registerIndex).r8);
                } else if (type.valueSize() == 2) {
                    emitLine("    mov word " + localAddress + ", " + argumentRegister(placement.registerIndex).r16);
                } else if (type.valueSize() <= 4) {
                    emitLine("    mov dword " + localAddress + ", " + argumentRegister(placement.registerIndex).r32);
                } else {
                    emitLine("    mov qword " + localAddress + ", " + argumentRegister(placement.registerIndex).r64);
                }
            }
            else {
                const std::string sourceAddress = "[rbp+" + std::to_string(16 + placement.homeOffset) + "]";
                if (type.isStruct()) {
                    emitLine("    lea rcx, " + localAddress);
                    emitLine("    lea rdx, " + sourceAddress);
                    emitCopyStructValue(type, "rcx", "rdx");
                } else if (type.valueSize() == 1) {
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
            continue;
        }

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

    if (!function.returnType->isStruct()) {
        emitLine("    xor eax, eax");
    }
    emitLine(currentReturnLabel + ":");
    if (frameSize > 0) {
        emitLine("    add rsp, " + std::to_string(frameSize));
    }
    emitLine("    pop rbp");
    emitLine("    ret");
    activeHiddenReturnPointerOffset = 0;
    activeLargeStructCallResultOffset = 0;
}

void CodeGenerator::emitStatement(const Stmt &stmt) {
    switch (stmt.kind) {
    case Stmt::Kind::Return: {
        const auto &returnStmt = static_cast<const ReturnStmt &>(stmt);
        if (returnStmt.expr) {
            if (returnStmt.expr->type->isStruct()) {
                if (usesWindowsAbi() && returnStmt.expr->type->valueSize() > 8) {
                    emitLine("    mov rcx, qword [rbp-" + std::to_string(activeHiddenReturnPointerOffset) + "]");
                    emitAddress(*returnStmt.expr);
                    emitLine("    mov rdx, rax");
                    emitCopyStructValue(*returnStmt.expr->type, "rcx", "rdx");
                } else {
                    emitLoadStructValueToRax(*returnStmt.expr);
                }
            } else {
                emitExpr(*returnStmt.expr);
            }
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
            if (decl.type->isStruct() && decl.init->kind == Expr::Kind::InitializerList) {
                emitLocalStructInitializer(decl, static_cast<const InitializerListExpr &>(*decl.init));
            } else if (decl.type->isArray() && decl.init->kind == Expr::Kind::String &&
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
        if (expr.type->isArray() || expr.type->isFunction() || expr.type->isStruct()) {
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
        if (assign.target->type->isStruct()) {
            if (assign.value->type->valueSize() <= 8 && assign.value->kind == Expr::Kind::Call) {
                emitExpr(*assign.value);
                emitLine("    pop rcx");
                emitStoreStructValueFromRax(*assign.target->type, "rcx");
            } else {
                emitAddress(*assign.value);
                emitLine("    pop rcx");
                emitLine("    mov rdx, rax");
                emitCopyStructValue(*assign.target->type, "rcx", "rdx");
                emitLine("    mov rax, rcx");
            }
            return;
        }
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
    case Expr::Kind::MemberAccess:
        emitAddress(expr);
        if (expr.type->isFunction() || expr.type->isStruct()) {
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
        throw std::runtime_error("internal code generation error");
    }
    throw std::runtime_error("internal code generation error");
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

bool CodeGenerator::supportsCurrentByValueStruct(const Type &type) const {
    if (!type.isStruct()) {
        return true;
    }
    for (const auto &member : type.members) {
        if (!member.type->isInteger() && !member.type->isPointer()) {
            return false;
        }
    }
    return true;
}

bool CodeGenerator::supportsCurrentStructInitializer(const Type &type) const {
    if (!type.isStruct()) {
        return true;
    }
    for (const auto &member : type.members) {
        if (member.type->isArray() || member.type->isStruct()) {
            return false;
        }
    }
    return true;
}

bool CodeGenerator::isRegisterPassedStruct(const Type &type) const {
    return type.isStruct() && type.valueSize() <= 8;
}

int CodeGenerator::alignStackSize(int size) const {
    return ((size + 7) / 8) * 8;
}

std::vector<CodeGenerator::WindowsAbiArgument> CodeGenerator::buildWindowsAbiArguments(
    const std::vector<TypePtr> &parameterTypes,
    bool includeHiddenReturnPointer) const {
    std::vector<WindowsAbiArgument> arguments;
    int nextRegisterIndex = 0;
    int homeOffset = 0;

    auto appendArgument = [&](TypePtr type, bool hiddenReturnPointer) {
        WindowsAbiArgument argument;
        argument.type = std::move(type);
        argument.hiddenReturnPointer = hiddenReturnPointer;
        argument.stackSize = hiddenReturnPointer
            ? 8
            : (argument.type->isStruct() && argument.type->valueSize() > 8
                   ? alignStackSize(argument.type->valueSize())
                   : 8);
        argument.homeOffset = homeOffset;
        if (!hiddenReturnPointer && argument.type->isStruct() && argument.type->valueSize() > 8) {
            argument.inRegister = false;
            argument.registerIndex = -1;
        } else if (nextRegisterIndex < argumentRegisterCount()) {
            argument.inRegister = true;
            argument.registerIndex = nextRegisterIndex++;
        } else {
            argument.inRegister = false;
            argument.registerIndex = -1;
        }
        homeOffset += argument.stackSize;
        arguments.push_back(std::move(argument));
    };

    if (includeHiddenReturnPointer) {
        appendArgument(Type::makePointer(Type::makeVoid()), true);
    }
    for (const auto &parameterType : parameterTypes) {
        appendArgument(parameterType, false);
    }

    return arguments;
}

int CodeGenerator::findHiddenReturnPointerLocalOffset(const Function &function) const {
    return alignStackSize(function.stackSize) + 8;
}

int CodeGenerator::findLargeStructCallResultLocalOffset(const Function &function) const {
    const int scratchSize = maxLargeStructCallResultSizeFunction(function);
    if (scratchSize == 0) {
        return 0;
    }
    int offset = alignStackSize(function.stackSize);
    if (usesWindowsAbi() && function.returnType->isStruct() && function.returnType->valueSize() > 8) {
        offset += 8;
    }
    return offset + scratchSize;
}

int CodeGenerator::currentFunctionFrameSize(const Function &function) const {
    int size = function.stackSize;
    if (usesWindowsAbi() && function.returnType->isStruct() && function.returnType->valueSize() > 8) {
        size = std::max(size, findHiddenReturnPointerLocalOffset(function));
    }
    size = std::max(size, findLargeStructCallResultLocalOffset(function));
    return ((size + 15) / 16) * 16;
}

void CodeGenerator::emitCopyBytes(const std::string &destAddressExpr, const std::string &srcAddressExpr, int size) {
    for (int offset = 0; offset < size; ++offset) {
        emitLine("    mov al, byte [" + srcAddressExpr + (offset == 0 ? "" : "+" + std::to_string(offset)) + "]");
        emitLine("    mov byte [" + destAddressExpr + (offset == 0 ? "" : "+" + std::to_string(offset)) + "], al");
    }
}

void CodeGenerator::emitCopyStructValue(const Type &type, const std::string &destAddressExpr, const std::string &srcAddressExpr) {
    emitCopyBytes(destAddressExpr, srcAddressExpr, type.valueSize());
}

void CodeGenerator::emitLoadStructValueToRax(const Expr &expr) {
    if (!expr.type->isStruct()) {
        emitExpr(expr);
        return;
    }
    if (expr.kind == Expr::Kind::Call) {
        emitExpr(expr);
        return;
    }
    emitAddress(expr);
    if (expr.type->valueSize() == 1) {
        emitLine("    movzx eax, byte [rax]");
    } else if (expr.type->valueSize() == 2) {
        emitLine("    movzx eax, word [rax]");
    } else if (expr.type->valueSize() <= 4) {
        emitLine("    mov eax, dword [rax]");
    } else {
        emitLine("    mov rax, qword [rax]");
    }
}

void CodeGenerator::emitStoreStructValueFromRax(const Type &type, const std::string &destAddressExpr) {
    if (type.valueSize() == 1) {
        emitLine("    mov byte [" + destAddressExpr + "], al");
    } else if (type.valueSize() == 2) {
        emitLine("    mov word [" + destAddressExpr + "], ax");
    } else if (type.valueSize() <= 4) {
        emitLine("    mov dword [" + destAddressExpr + "], eax");
    } else {
        emitLine("    mov qword [" + destAddressExpr + "], rax");
    }
}

void CodeGenerator::emitZeroFillBytes(std::ostringstream &line, int count, bool &first) const {
    for (int i = 0; i < count; ++i) {
        if (!first) {
            line << ", ";
        }
        line << "0";
        first = false;
    }
}

void CodeGenerator::emitGlobalStructMemberValue(
    std::ostringstream &line,
    const Type &type,
    const Expr &expr,
    bool &first) {
    if (!first) {
        line << ", ";
    }
    if (type.isPointer()) {
        line << globalAddressInitializer(expr);
    } else {
        line << evaluateStaticIntegerInitializer(expr);
    }
    first = false;
}

void CodeGenerator::emitGlobalStructInitializer(const GlobalVar &global, const InitializerListExpr *list) {
    bool firstLine = true;
    int cursor = 0;
    auto emitLabeledLine = [&](const std::string &body) {
        if (firstLine) {
            emitDataLine(global.symbolName + ": " + body);
            firstLine = false;
        } else {
            emitDataLine("    " + body);
        }
    };
    for (std::size_t i = 0; i < global.type->members.size(); ++i) {
        const StructMember &member = global.type->members[i];
        const int memberPadding = member.offset - cursor;
        if (memberPadding > 0) {
            std::ostringstream padding;
            padding << "db ";
            bool firstValue = true;
            emitZeroFillBytes(padding, memberPadding, firstValue);
            emitLabeledLine(padding.str());
        }
        cursor = member.offset;
        if (list && i < list->elements.size()) {
            const Expr &expr = *list->elements[i];
            std::ostringstream memberLine;
            memberLine << dataDirectiveForSize(member.type->valueSize()) << " ";
            bool firstValue = true;
            emitGlobalStructMemberValue(memberLine, *member.type, expr, firstValue);
            emitLabeledLine(memberLine.str());
        } else {
            std::ostringstream zeroLine;
            zeroLine << "db ";
            bool firstValue = true;
            emitZeroFillBytes(zeroLine, member.type->valueSize(), firstValue);
            emitLabeledLine(zeroLine.str());
        }
        cursor += member.type->valueSize();
    }
    const int trailingPadding = global.type->valueSize() - cursor;
    if (trailingPadding > 0 || firstLine) {
        std::ostringstream padding;
        padding << "db ";
        bool firstValue = true;
        emitZeroFillBytes(padding, std::max(1, trailingPadding), firstValue);
        emitLabeledLine(padding.str());
    }
}

void CodeGenerator::emitLocalStructInitializer(const DeclStmt &decl, const InitializerListExpr &list) {
    const int baseStart = decl.stackOffset - decl.type->valueSize() + 1;
    for (int i = 0; i < decl.type->valueSize(); ++i) {
        emitLine("    mov byte [rbp-" + std::to_string(baseStart + i) + "], 0");
    }
    for (std::size_t i = 0; i < list.elements.size(); ++i) {
        const StructMember &member = decl.type->members[i];
        const int memberOffset = decl.stackOffset - member.offset;
        emitExpr(*list.elements[i]);
        emitStoreToLocalSlot(*member.type, memberOffset);
    }
}

void CodeGenerator::emitCallExpr(const CallExpr &call) {
    if (usesWindowsAbi()) {
        emitWindowsCallExpr(call);
        return;
    }
    emitSystemVCallExpr(call);
}

void CodeGenerator::emitWindowsCallExpr(const CallExpr &call) {
    std::vector<TypePtr> abiParameterTypes = call.parameterTypes;
    const bool usesHiddenReturnPointer = call.type->isStruct() && call.type->valueSize() > 8;
    const std::vector<WindowsAbiArgument> placements =
        buildWindowsAbiArguments(abiParameterTypes, usesHiddenReturnPointer);
    int totalStackBytes = 32;
    for (const auto &placement : placements) {
        totalStackBytes = std::max(totalStackBytes, placement.homeOffset + placement.stackSize);
    }
    const int hiddenPlacementOffset = usesHiddenReturnPointer ? 1 : 0;
    if (totalStackBytes % 16 != 0) {
        totalStackBytes += 8;
    }

    emitLine("    sub rsp, " + std::to_string(totalStackBytes));
    if (usesHiddenReturnPointer) {
        if (activeLargeStructCallResultOffset == 0) {
            throw std::runtime_error("internal code generation error");
        }
        const WindowsAbiArgument &hiddenPlacement = placements.front();
        emitLine("    lea rax, [rbp-" + std::to_string(activeLargeStructCallResultOffset) + "]");
        emitLine("    mov qword [rsp+" + std::to_string(hiddenPlacement.homeOffset) + "], rax");
    }

    for (int i = static_cast<int>(call.arguments.size()) - 1; i >= 0; --i) {
        const WindowsAbiArgument &placement = placements[static_cast<std::size_t>(i + hiddenPlacementOffset)];
        const Type &type = *placement.type;
        const std::string slotAddress = "rsp+" + std::to_string(placement.homeOffset);
        if (type.isStruct()) {
            if (type.valueSize() <= 8 && call.arguments[i]->kind == Expr::Kind::Call) {
                emitExpr(*call.arguments[i]);
                emitStoreStructValueFromRax(type, slotAddress);
            } else if (type.valueSize() <= 8) {
                emitLoadStructValueToRax(*call.arguments[i]);
                emitStoreStructValueFromRax(type, slotAddress);
            } else {
                emitAddress(*call.arguments[i]);
                emitLine("    mov rdx, rax");
                emitLine("    lea rcx, [" + slotAddress + "]");
                emitCopyStructValue(type, "rcx", "rdx");
            }
            continue;
        }
        emitExpr(*call.arguments[i]);
        if (type.valueSize() == 1) {
            emitLine("    mov byte [" + slotAddress + "], al");
        } else if (type.valueSize() == 2) {
            emitLine("    mov word [" + slotAddress + "], ax");
        } else if (type.valueSize() <= 4) {
            emitLine("    mov dword [" + slotAddress + "], eax");
        } else {
            emitLine("    mov qword [" + slotAddress + "], rax");
        }
    }
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const WindowsAbiArgument &placement = placements[i];
        if (!placement.inRegister) {
            continue;
        }
        if (placement.type->isStruct()) {
            emitLine("    mov " + std::string(argumentRegister(placement.registerIndex).r64) +
                     ", qword [rsp+" + std::to_string(placement.homeOffset) + "]");
        } else if (placement.type->valueSize() <= 4) {
            emitLine("    mov " + std::string(argumentRegister(placement.registerIndex).r32) +
                     ", dword [rsp+" + std::to_string(placement.homeOffset) + "]");
        } else {
            emitLine("    mov " + std::string(argumentRegister(placement.registerIndex).r64) +
                     ", qword [rsp+" + std::to_string(placement.homeOffset) + "]");
        }
    }
    emitExpr(*call.callee);
    emitLine("    mov r11, rax");
    emitLine("    call r11");
    emitLine("    add rsp, " + std::to_string(totalStackBytes));
    if (usesHiddenReturnPointer) {
        emitLine("    lea rax, [rbp-" + std::to_string(activeLargeStructCallResultOffset) + "]");
    }
}

void CodeGenerator::emitSystemVCallExpr(const CallExpr &call) {
    for (const auto &parameterType : call.parameterTypes) {
        if (parameterType->isStruct()) {
            throw std::runtime_error("by-value struct arguments are not supported yet on x86_64-linux");
        }
    }
    if (call.type->isStruct()) {
        throw std::runtime_error("by-value struct returns are not supported yet on x86_64-linux");
    }
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
