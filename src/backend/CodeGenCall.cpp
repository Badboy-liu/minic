#include "CodeGenerator.h"

#include <algorithm>
#include <stdexcept>

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
        if (member.type->isArray()) {
            return false;
        }
        // 允许嵌套结构体成员
    }
    return true;
}

bool CodeGenerator::isRegisterPassedStruct(const Type &type) const {
    return type.isStruct() && type.valueSize() <= 8;
}

// System V AMD64 ABI: 结构体按 8 字节八字节分类。
// 简化规则：所有成员为整数/指针且总大小 <=16 字节时，按 INTEGER 类通过寄存器传递。
bool CodeGenerator::isSystemVRegisterStruct(const Type &type) const {
    if (!type.isStruct()) {
        return false;
    }
    if (type.valueSize() > 16) {
        return false;
    }
    for (const auto &member : type.members) {
        if (!member.type->isInteger() && !member.type->isPointer()) {
            return false;
        }
    }
    return true;
}

// 返回 System V ABI 下传递该结构体所需的寄存器数量（1 或 2）
int CodeGenerator::systemVStructRegisterCount(const Type &type) const {
    if (!isSystemVRegisterStruct(type)) {
        return 0;
    }
    return type.valueSize() <= 8 ? 1 : 2;
}

// 判断函数是否需要隐藏返回指针（System V ABI 下 >16 字节结构体返回值）
bool CodeGenerator::usesHiddenSystemVReturnPointer(const Function &function) const {
    if (usesWindowsAbi()) {
        return false;
    }
    if (!function.returnType->isStruct()) {
        return false;
    }
    return !isSystemVRegisterStruct(*function.returnType);
}

int CodeGenerator::alignStackSize(int size) const {
    return ((size + 7) / 8) * 8;
}

std::vector<CodeGenerator::WindowsAbiArgument> CodeGenerator::buildWindowsAbiArguments(
    const std::vector<TypePtr> &parameterTypes,
    bool includeHiddenReturnPointer) const {
    std::vector<WindowsAbiArgument> arguments;
    int nextIntRegisterIndex = 0;
    int nextFloatRegisterIndex = 0;
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
        } else if (!hiddenReturnPointer && argument.type->isFloatingPoint() &&
                   nextFloatRegisterIndex < floatArgumentRegisterCount()) {
            argument.inRegister = true;
            argument.registerIndex = nextFloatRegisterIndex++;
            argument.isFloatRegister = true;
        } else if (nextIntRegisterIndex < argumentRegisterCount()) {
            argument.inRegister = true;
            argument.registerIndex = nextIntRegisterIndex++;
            argument.isFloatRegister = false;
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

int CodeGenerator::currentFunctionFrameSize(const Function &function) const {
    int size = function.stackSize;
    const bool usesHiddenReturnPointer =
        usesWindowsAbi()
        ? (function.returnType->isStruct() && function.returnType->valueSize() > 8)
        : usesHiddenSystemVReturnPointer(function);
    if (usesHiddenReturnPointer) {
        size = std::max(size, findHiddenReturnPointerLocalOffset(function));
    }
    size = std::max(size, findLargeStructCallResultLocalOffset(function));
    return ((size + 15) / 16) * 16;
}

void CodeGenerator::emitCallExpr(CallExpr &call) {
    if (usesWindowsAbi()) {
        emitWindowsCallExpr(call);
        return;
    }
    emitSystemVCallExpr(call);
}

bool CodeGenerator::canTailCall() const {
    // VLA 函数不支持尾调用优化（需要特殊的栈清理逻辑）
    return !functionHasVla;
}

void CodeGenerator::emitTailCall(CallExpr &call) {
    // 尾调用优化：在 return 处直接内联 epilogue，避免跳转到统一的返回标签
    emitCallExpr(call);
    emitLine("    mov rsp, rbp");
    emitLine("    pop rbp");
    emitLine("    ret");
}

void CodeGenerator::emitWindowsCallExpr(CallExpr &call) {
    std::vector<TypePtr> abiParameterTypes = call.parameterTypes;
    // 可变参数函数：将额外的实参类型添加到参数类型列表中
    if (abiParameterTypes.size() < call.arguments.size()) {
        for (std::size_t i = abiParameterTypes.size(); i < call.arguments.size(); ++i) {
            abiParameterTypes.push_back(call.arguments[i]->type);
        }
    }
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
            throw std::runtime_error("internal code generation error: missing scratch space for hidden struct return pointer");
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
        if (type.isFloatingPoint()) {
            emitLine("    movsd qword [" + slotAddress + "], xmm0");
        } else if (type.valueSize() == 1) {
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
        if (placement.isFloatRegister) {
            // 浮点参数加载到 xmm 寄存器
            const std::string xmmReg = "xmm" + std::to_string(placement.registerIndex);
            if (placement.type->kind == TypeKind::Float) {
                emitLine("    movss " + xmmReg + ", dword [rsp+" + std::to_string(placement.homeOffset) + "]");
            } else {
                emitLine("    movsd " + xmmReg + ", qword [rsp+" + std::to_string(placement.homeOffset) + "]");
            }
        } else if (placement.type->isStruct()) {
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

void CodeGenerator::emitSystemVCallExpr(CallExpr &call) {
    const int registerCount = argumentRegisterCount();

    // 是否需要隐藏返回指针（MEMORY 类结构体返回）
    const bool needsHiddenReturnPointer =
        call.type->isStruct() && !isSystemVRegisterStruct(*call.type);

    // System V ABI 参数布局
    struct SysVCallArg {
        enum Kind { REG_SCALAR, REG_FLOAT, REG_STRUCT_1, REG_STRUCT_2, STACK_SCALAR, STACK_STRUCT };
        Kind kind;
        int registerIndex = -1;    // REG_* 使用的起始寄存器索引
        int stackOffset = -1;      // STACK_* 在栈参数区域的偏移
        int structSize = 0;        // STACK_STRUCT 的字节大小（对齐到 8）
    };
    std::vector<SysVCallArg> argPlacements;
    int nextIntReg = 0;
    int nextFloatReg = 0;
    int stackArgsBytes = 0;

    // 隐藏返回指针占用 rdi
    if (needsHiddenReturnPointer) {
        nextIntReg = 1;
    }

    for (int i = 0; i < static_cast<int>(call.arguments.size()); ++i) {
        const Type &argType = *call.arguments[i]->type;
        SysVCallArg placement;

        if (argType.isStruct()) {
            int regCount = systemVStructRegisterCount(argType);
            if (regCount == 1 && nextIntReg < registerCount) {
                // INTEGER 类 <=8 字节，一个寄存器
                placement.kind = SysVCallArg::REG_STRUCT_1;
                placement.registerIndex = nextIntReg;
                nextIntReg++;
            } else if (regCount == 2 && nextIntReg + 2 <= registerCount) {
                // INTEGER 类 9-16 字节，两个寄存器
                placement.kind = SysVCallArg::REG_STRUCT_2;
                placement.registerIndex = nextIntReg;
                nextIntReg += 2;
            } else {
                // MEMORY 类或寄存器不足，通过栈传递
                placement.kind = SysVCallArg::STACK_STRUCT;
                stackArgsBytes = alignStackSize(stackArgsBytes);
                placement.stackOffset = stackArgsBytes;
                placement.structSize = alignStackSize(argType.valueSize());
                stackArgsBytes += placement.structSize;
            }
        } else if (argType.isFloatingPoint()) {
            if (nextFloatReg < floatArgumentRegisterCount()) {
                placement.kind = SysVCallArg::REG_FLOAT;
                placement.registerIndex = nextFloatReg;
                nextFloatReg++;
            } else {
                placement.kind = SysVCallArg::STACK_SCALAR;
                placement.stackOffset = stackArgsBytes;
                stackArgsBytes += 8;
            }
        } else {
            if (nextIntReg < registerCount) {
                placement.kind = SysVCallArg::REG_SCALAR;
                placement.registerIndex = nextIntReg;
                nextIntReg++;
            } else {
                placement.kind = SysVCallArg::STACK_SCALAR;
                placement.stackOffset = stackArgsBytes;
                stackArgsBytes += 8;
            }
        }
        argPlacements.push_back(placement);
    }

    // 寄存器参数的临时存储区域（在栈参数区域之上）
    const int tempRegAreaOffset = stackArgsBytes;
    const int tempRegAreaSize = (nextIntReg + nextFloatReg) * 8;
    // 隐藏返回指针的临时存储（在临时寄存器区域之上）
    const int hiddenRetPtrTempOffset = tempRegAreaOffset + tempRegAreaSize;
    // 大结构体返回结果的临时存储
    const int largeStructResultOffset = needsHiddenReturnPointer
        ? hiddenRetPtrTempOffset + 8
        : hiddenRetPtrTempOffset;
    // 总栈分配大小
    int totalAlloc = largeStructResultOffset;
    // 为 INTEGER 类 9-16 字节返回值分配额外空间
    bool returnsStructInRegisters = call.type->isStruct() && isSystemVRegisterStruct(*call.type) && call.type->valueSize() > 8;
    if (returnsStructInRegisters) {
        totalAlloc += 16;
    }
    if (call.type->isStruct() && !isSystemVRegisterStruct(*call.type)) {
        // MEMORY 类返回需要 scratch space
        if (activeLargeStructCallResultOffset == 0) {
            throw std::runtime_error("internal code generation error: no scratch space for large struct call result");
        }
    }
    totalAlloc = ((totalAlloc + 15) / 16) * 16;
    if (totalAlloc == 0) {
        totalAlloc = 16;  // 至少 16 字节对齐
    }

    emitLine("    sub rsp, " + std::to_string(totalAlloc));

    // 设置隐藏返回指针（如果需要）
    if (needsHiddenReturnPointer) {
        emitLine("    lea rax, [rbp-" + std::to_string(activeLargeStructCallResultOffset) + "]");
        emitLine("    mov qword [rsp+" + std::to_string(hiddenRetPtrTempOffset) + "], rax");
    }

    // 求值每个参数并存储到相应位置
    for (int i = static_cast<int>(call.arguments.size()) - 1; i >= 0; --i) {
        const SysVCallArg &placement = argPlacements[static_cast<std::size_t>(i)];
        const Type &argType = *call.arguments[i]->type;

        switch (placement.kind) {
        case SysVCallArg::REG_SCALAR: {
            emitExpr(*call.arguments[i]);
            const std::string tempAddr = "[rsp+" + std::to_string(tempRegAreaOffset + placement.registerIndex * 8) + "]";
            emitLine("    mov qword " + tempAddr + ", rax");
            break;
        }
        case SysVCallArg::REG_FLOAT: {
            emitExpr(*call.arguments[i]);
            const std::string tempAddr = "[rsp+" + std::to_string(tempRegAreaOffset + placement.registerIndex * 8) + "]";
            emitLine("    movsd qword " + tempAddr + ", xmm0");
            break;
        }
        case SysVCallArg::REG_STRUCT_1: {
            // <=8 字节 INTEGER 类结构体
            emitLoadStructValueToRax(*call.arguments[i]);
            const std::string tempAddr = "[rsp+" + std::to_string(tempRegAreaOffset + placement.registerIndex * 8) + "]";
            emitLine("    mov qword " + tempAddr + ", rax");
            break;
        }
        case SysVCallArg::REG_STRUCT_2: {
            // 9-16 字节 INTEGER 类结构体，需要两个寄存器
            emitAddress(*call.arguments[i]);
            emitLine("    mov rcx, qword [rax]");
            emitLine("    mov rdx, qword [rax+8]");
            const std::string tempAddr1 = "[rsp+" + std::to_string(tempRegAreaOffset + placement.registerIndex * 8) + "]";
            const std::string tempAddr2 = "[rsp+" + std::to_string(tempRegAreaOffset + (placement.registerIndex + 1) * 8) + "]";
            emitLine("    mov qword " + tempAddr1 + ", rcx");
            emitLine("    mov qword " + tempAddr2 + ", rdx");
            break;
        }
        case SysVCallArg::STACK_SCALAR: {
            emitExpr(*call.arguments[i]);
            const std::string stackAddr = "[rsp+" + std::to_string(placement.stackOffset) + "]";
            if (argType.isFloatingPoint()) {
                emitLine("    movsd qword " + stackAddr + ", xmm0");
            } else if (argType.valueSize() == 1) {
                emitLine("    mov byte " + stackAddr + ", al");
            } else if (argType.valueSize() == 2) {
                emitLine("    mov word " + stackAddr + ", ax");
            } else if (argType.valueSize() <= 4) {
                emitLine("    mov dword " + stackAddr + ", eax");
            } else {
                emitLine("    mov qword " + stackAddr + ", rax");
            }
            break;
        }
        case SysVCallArg::STACK_STRUCT: {
            // MEMORY 类结构体：拷贝字节到栈参数区域
            emitAddress(*call.arguments[i]);
            emitLine("    mov rdx, rax");
            emitLine("    lea rcx, [rsp+" + std::to_string(placement.stackOffset) + "]");
            emitCopyStructValue(argType, "rcx", "rdx");
            break;
        }
        }
    }

    // 将临时存储加载到实际寄存器
    // 注意：寄存器顺序可能被栈参数区覆盖，所以先加载所有寄存器参数
    // 加载顺序要小心：不能覆盖还在临时区域的参数
    // 从最后一个寄存器参数开始加载（从高索引到低索引）以避免覆盖问题

    // 首先加载隐藏返回指针到 rdi
    if (needsHiddenReturnPointer) {
        emitLine("    mov rdi, qword [rsp+" + std::to_string(hiddenRetPtrTempOffset) + "]");
    }

    // 从高到低加载寄存器参数（因为高索引寄存器不依赖低索引区域）
    // 但这里所有数据都在栈上不同的临时位置，所以可以直接按顺序加载
    for (int i = 0; i < static_cast<int>(call.arguments.size()); ++i) {
        const SysVCallArg &placement = argPlacements[static_cast<std::size_t>(i)];
        if (placement.kind == SysVCallArg::REG_SCALAR) {
            const Type &argType = *call.arguments[i]->type;
            const std::string tempAddr = "[rsp+" + std::to_string(tempRegAreaOffset + placement.registerIndex * 8) + "]";
            if (argType.valueSize() <= 4) {
                emitLine("    mov " + std::string(argumentRegister(placement.registerIndex).r32) + ", dword " + tempAddr);
            } else {
                emitLine("    mov " + std::string(argumentRegister(placement.registerIndex).r64) + ", qword " + tempAddr);
            }
        } else if (placement.kind == SysVCallArg::REG_FLOAT) {
            const std::string tempAddr = "[rsp+" + std::to_string(tempRegAreaOffset + placement.registerIndex * 8) + "]";
            emitLine("    movsd xmm" + std::to_string(placement.registerIndex) + ", qword " + tempAddr);
        } else if (placement.kind == SysVCallArg::REG_STRUCT_1) {
            const std::string tempAddr = "[rsp+" + std::to_string(tempRegAreaOffset + placement.registerIndex * 8) + "]";
            emitLine("    mov " + std::string(argumentRegister(placement.registerIndex).r64) + ", qword " + tempAddr);
        } else if (placement.kind == SysVCallArg::REG_STRUCT_2) {
            const std::string tempAddr1 = "[rsp+" + std::to_string(tempRegAreaOffset + placement.registerIndex * 8) + "]";
            const std::string tempAddr2 = "[rsp+" + std::to_string(tempRegAreaOffset + (placement.registerIndex + 1) * 8) + "]";
            emitLine("    mov " + std::string(argumentRegister(placement.registerIndex).r64) + ", qword " + tempAddr1);
            emitLine("    mov " + std::string(argumentRegister(placement.registerIndex + 1).r64) + ", qword " + tempAddr2);
        }
    }

    // 调用
    emitExpr(*call.callee);
    emitLine("    mov r11, rax");
    emitLine("    call r11");

    // 处理返回值
    if (call.type->isStruct()) {
        if (needsHiddenReturnPointer) {
            // MEMORY 类返回：rax 已经包含隐藏返回指针，结构体数据已在 scratch 区
            // 将 rax 指向的 scratch 地址加载到 rax（让后续代码知道结构体在哪）
            emitLine("    lea rax, [rbp-" + std::to_string(activeLargeStructCallResultOffset) + "]");
        } else if (call.type->valueSize() > 8) {
            // INTEGER 类 9-16 字节返回：rax = 低 8 字节, rdx = 高 8 字节
            // 存储到 scratch 区域
            emitLine("    mov qword [rbp-" + std::to_string(activeLargeStructCallResultOffset) + "], rax");
            emitLine("    mov qword [rbp-" + std::to_string(activeLargeStructCallResultOffset - 8) + "], rdx");
            emitLine("    lea rax, [rbp-" + std::to_string(activeLargeStructCallResultOffset) + "]");
        }
        // <=8 字节 INTEGER 类：rax 已包含返回值，无需额外处理
    }

    emitLine("    add rsp, " + std::to_string(totalAlloc));
}

void CodeGenerator::emitStoreToLocalSlot(const Type &type, int addressOffset) {
    const std::string address = "[rbp-" + std::to_string(addressOffset) + "]";
    if (type.kind == TypeKind::Float) {
        emitLine("    cvtsd2ss xmm0, xmm0");
        emitLine("    movss dword " + address + ", xmm0");
        return;
    }
    if (type.kind == TypeKind::Double) {
        emitLine("    movsd qword " + address + ", xmm0");
        return;
    }
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
    if (type.kind == TypeKind::Float) {
        emitLine("    movss xmm0, dword [rax]");
        emitLine("    cvtss2sd xmm0, xmm0");
        return;
    }
    if (type.kind == TypeKind::Double) {
        emitLine("    movsd xmm0, qword [rax]");
        return;
    }
    if (type.valueSize() == 1) {
        // unsigned char 使用 movzx，signed char 使用 movsx
        if (type.isUnsigned) {
            emitLine("    movzx eax, byte [rax]");
        } else {
            emitLine("    movsx eax, byte [rax]");
        }
    } else if (type.valueSize() == 2) {
        // unsigned short 使用 movzx，signed short 使用 movsx
        if (type.isUnsigned) {
            emitLine("    movzx eax, word [rax]");
        } else {
            emitLine("    movsx eax, word [rax]");
        }
    } else if (type.valueSize() <= 4) {
        emitLine("    mov eax, dword [rax]");
    } else {
        emitLine("    mov rax, qword [rax]");
    }
}

void CodeGenerator::emitStore(const Type &type) {
    if (type.kind == TypeKind::Float) {
        emitLine("    cvtsd2ss xmm0, xmm0");
        emitLine("    movss dword [rcx], xmm0");
        return;
    }
    if (type.kind == TypeKind::Double) {
        emitLine("    movsd qword [rcx], xmm0");
        return;
    }
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
        throw std::runtime_error("internal code generation error: pointeeSize called on non-pointer type");
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

int CodeGenerator::floatArgumentRegisterCount() const {
    return usesWindowsAbi() ? 4 : 8;
}

int CodeGenerator::stackArgumentOffset(int index) const {
    if (usesWindowsAbi()) {
        return 16 + index * 8;
    }
    return 16 + (index - argumentRegisterCount()) * 8;
}
