#include "CodeGenerator.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

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
    case Stmt::Kind::DoWhile: {
        const auto &doWhileStmt = static_cast<const DoWhileStmt &>(stmt);
        return std::max(
            maxLargeStructCallResultSizeStmt(*doWhileStmt.body),
            maxLargeStructCallResultSizeExpr(*doWhileStmt.condition));
    }
    case Stmt::Kind::Switch: {
        const auto &sw = static_cast<const SwitchStmt &>(stmt);
        int maxSize = maxLargeStructCallResultSizeExpr(*sw.scrutinee);
        for (const auto &c : sw.cases) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*c.label));
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeStmt(*c.body));
        }
        if (sw.defaultBody) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeStmt(*sw.defaultBody));
        }
        return maxSize;
    }
    case Stmt::Kind::Break:
    case Stmt::Kind::Continue:
        return 0;
    case Stmt::Kind::Goto:
        return 0;
    case Stmt::Kind::Label:
        return maxLargeStructCallResultSizeStmt(*static_cast<const LabelStmt &>(stmt).body);
    case Stmt::Kind::StaticAssert:
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
    case Expr::Kind::FloatLiteral:
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
    case Expr::Kind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        if (unary.operand) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*unary.operand));
        }
        return maxSize;
    }
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
    case Expr::Kind::Ternary: {
        const auto &ternary = static_cast<const TernaryExpr &>(expr);
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*ternary.condition));
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*ternary.thenExpr));
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*ternary.elseExpr));
        return maxSize;
    }
    case Expr::Kind::Cast:
        return std::max(maxSize, maxLargeStructCallResultSizeExpr(*static_cast<const CastExpr &>(expr).operand));
    case Expr::Kind::BuiltinVaStart:
    case Expr::Kind::BuiltinVaArg:
    case Expr::Kind::BuiltinVaEnd:
        return maxSize;
    case Expr::Kind::Generic: {
        const auto &generic = static_cast<const GenericExpr &>(expr);
        maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*generic.controllingExpr));
        for (const auto &assoc : generic.associations) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*assoc.expr));
        }
        return maxSize;
    }
    case Expr::Kind::CompoundLiteral: {
        const auto &compound = static_cast<const CompoundLiteralExpr &>(expr);
        if (compound.init) {
            for (const auto &element : compound.init->elements) {
                maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*element));
            }
        }
        return maxSize;
    }
    case Expr::Kind::StmtExpr: {
        const auto &se = static_cast<const StmtExpr &>(expr);
        for (const auto &s : se.statements) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeStmt(*s));
        }
        if (se.result) {
            maxSize = std::max(maxSize, maxLargeStructCallResultSizeExpr(*se.result));
        }
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
    floatLabels.clear();
    labelCounter = 0;
    loopContinueLabels.clear();
    loopBreakLabels.clear();
    staticLocalVars.clear();
    functionUnwindInfos.clear();
    emitProgramEntryPoint = emitEntryPointValue;
    currentProgram = &program;

    // 收集所有函数体中的 static 局部变量
    for (const auto &function : program.functions) {
        if (function.body) {
            collectStaticLocals(*function.body);
        }
    }

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
    emitDwarfSections();
    emitPdataSection();
    emitXdataSection();

    std::string assembly = out.str();
    assembly = applyPeepholeRegAlloc(assembly);
    return assembly;
}

// 简单的窥孔寄存器分配：消除冗余的栈读写
// 模式：mov [rbp-N], rax + ... + mov rax, [rbp-N] → mov [rbp-N], rax + ... + mov rax, rax (消除)
// 当紧邻 store 后的 load 到同一寄存器时完全消除
std::string CodeGenerator::applyPeepholeRegAlloc(const std::string &assembly) {
    std::istringstream in(assembly);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }

    // 第一遍：消除紧邻的 store-load 模式
    // 模式1：mov [rbp-N], rax + mov rax, [rbp-N] → mov [rbp-N], rax（删除冗余 load）
    // 模式2：mov [rbp-N], rax + mov rcx, [rbp-N] → mov [rbp-N], rax + mov rcx, rax（寄存器间传递）
    auto parseStore = [](const std::string &s, std::string &offset, std::string &reg) -> bool {
        std::size_t p = s.find("mov qword [rbp-");
        if (p == std::string::npos) p = s.find("mov dword [rbp-");
        if (p == std::string::npos) return false;
        p += 15;
        std::size_t end = s.find(']', p);
        if (end == std::string::npos) return false;
        offset = s.substr(p, end - p);
        std::size_t comma = s.find(',', end);
        if (comma == std::string::npos) return false;
        reg = s.substr(comma + 1);
        while (!reg.empty() && reg.front() == ' ') reg.erase(reg.begin());
        while (!reg.empty() && reg.back() == ' ') reg.pop_back();
        return true;
    };
    auto parseLoad = [](const std::string &s, std::string &reg, std::string &offset) -> bool {
        if (s.find("mov ") != 0) return false;
        std::size_t comma = s.find(',');
        if (comma == std::string::npos) return false;
        reg = s.substr(4, comma - 4);
        while (!reg.empty() && reg.front() == ' ') reg.erase(reg.begin());
        while (!reg.empty() && reg.back() == ' ') reg.pop_back();
        std::string rest = s.substr(comma + 1);
        while (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
        std::size_t p = rest.find("[rbp-");
        if (p == std::string::npos) return false;
        p += 5;
        std::size_t end = rest.find(']', p);
        if (end == std::string::npos) return false;
        offset = rest.substr(p, end - p);
        return true;
    };

    std::vector<std::string> result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        bool skip = false;
        if (i + 1 < lines.size()) {
            const std::string &cur = lines[i];
            const std::string &nxt = lines[i + 1];

            std::string storeOffset, storeReg;
            std::string loadReg, loadOffset;
            if (parseStore(cur, storeOffset, storeReg) && parseLoad(nxt, loadReg, loadOffset)) {
                if (storeOffset == loadOffset) {
                    if (storeReg == loadReg) {
                        // 同一寄存器：删除冗余 load
                        result.push_back(cur);
                        ++i;
                        skip = true;
                    } else {
                        // 不同寄存器：用 mov 替代内存 load
                        result.push_back(cur);
                        // 保留大小写一致的寄存器名
                        std::string sizePrefix;
                        if (nxt.find("movzx") != std::string::npos || nxt.find("movsx") != std::string::npos) {
                            // 零扩展/符号扩展不能简单替换为寄存器间移动
                            result.push_back(nxt);
                        } else {
                            result.push_back("    mov " + loadReg + ", " + storeReg);
                        }
                        ++i;
                        skip = true;
                    }
                }
            }
        }
        if (!skip) {
            result.push_back(lines[i]);
        }
    }

    // 重新组装
    std::ostringstream out;
    for (std::size_t i = 0; i < result.size(); ++i) {
        out << result[i];
        if (i + 1 < result.size()) {
            out << '\n';
        }
    }
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
    // 声明静态局部变量的全局符号
    for (const auto &var : staticLocalVars) {
        emitLine("global " + var.symbolName);
    }
    // 声明全局变量的全局符号
    for (const auto &global : currentProgram->globals) {
        if (!global.isExternal && !global.isExternStorage) {
            emitLine("global " + global.symbolName);
        }
    }
    emitLine("");
    emitLine("section .text");
    emitLine("");
}

void CodeGenerator::emitEntryPoint() {
    emitLine(std::string(target.entrySymbol) + ":");
    emitTargetEntryBody();
}

void CodeGenerator::emitTargetExternPrelude() {
    if (target.runtimeEntryFlavor == RuntimeEntryFlavor::ExitProcessStub) {
        emitLine("extern ExitProcess");
    }
    // 浮点取模运算需要 fmod
    emitLine("extern fmod");
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

void CodeGenerator::emitGlobals() {
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
                // 确定最内层标量类型，用于数据指令
                TypePtr scalarType = global.type->elementType;
                while (scalarType->isArray()) scalarType = scalarType->elementType;
                line << dataDirectiveForSize(scalarType->valueSize()) << " ";
                bool first = true;
                // 递归展开嵌套初始化器为扁平的标量值序列
                std::function<void(const Type &, const InitializerListExpr &)> emitGlobalNested;
                emitGlobalNested = [&](const Type &arrType, const InitializerListExpr &initList) {
                    for (std::size_t i = 0; i < initList.elements.size(); ++i) {
                        if (arrType.elementType->isArray() && initList.elements[i]->kind == Expr::Kind::InitializerList) {
                            const auto &inner = static_cast<const InitializerListExpr &>(*initList.elements[i]);
                            emitGlobalNested(*arrType.elementType, inner);
                            // 零填充子数组的剩余元素
                            for (std::size_t j = inner.elements.size(); j < static_cast<std::size_t>(arrType.elementType->arrayLength); ++j) {
                                if (!first) line << ", ";
                                line << "0";
                                first = false;
                            }
                        } else {
                            if (!first) line << ", ";
                            if (scalarType->isPointer()) {
                                line << globalAddressInitializer(*initList.elements[i]);
                            } else {
                                line << evaluateStaticIntegerInitializer(*initList.elements[i]);
                            }
                            first = false;
                        }
                    }
                };
                emitGlobalNested(*global.type, list);
                // 计算总的标量元素数量，用于零填充外层剩余子数组
                int totalScalars = 1;
                {
                    TypePtr t = global.type;
                    while (t->isArray()) {
                        totalScalars *= t->arrayLength;
                        t = t->elementType;
                    }
                }
                // 计算已输出的标量元素数量
                int emittedCount = 0;
                {
                    std::function<int(const Type &, const InitializerListExpr &)> countEmitted;
                    countEmitted = [&](const Type &arrType, const InitializerListExpr &initList) -> int {
                        int count = 0;
                        for (std::size_t i = 0; i < initList.elements.size(); ++i) {
                            if (arrType.elementType->isArray() && initList.elements[i]->kind == Expr::Kind::InitializerList) {
                                count += countEmitted(*arrType.elementType, static_cast<const InitializerListExpr &>(*initList.elements[i]));
                                const auto &inner = static_cast<const InitializerListExpr &>(*initList.elements[i]);
                                count += static_cast<int>(arrType.elementType->arrayLength) - static_cast<int>(inner.elements.size());
                            } else {
                                ++count;
                            }
                        }
                        return count;
                    };
                    emittedCount = countEmitted(*global.type, list);
                }
                for (int i = emittedCount; i < totalScalars; ++i) {
                    if (!first) line << ", ";
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

        if (global.type->isFloatingPoint()) {
            std::ostringstream line;
            if (global.init && global.init->kind == Expr::Kind::FloatLiteral) {
                const double value = static_cast<const FloatLiteralExpr &>(*global.init).value;
                std::ostringstream valStream;
                valStream << std::setprecision(17) << value;
                std::string valStr = valStream.str();
                if (valStr.find('.') == std::string::npos &&
                    valStr.find('e') == std::string::npos &&
                    valStr.find('E') == std::string::npos) {
                    valStr += ".0";
                }
                if (global.type->kind == TypeKind::Float) {
                    line << global.symbolName << ": dd __float32__(" << valStr << ")";
                } else {
                    line << global.symbolName << ": dq __float64__(" << valStr << ")";
                }
            } else {
                if (global.type->kind == TypeKind::Float) {
                    line << global.symbolName << ": dd __float32__(0.0)";
                } else {
                    line << global.symbolName << ": dq __float64__(0.0)";
                }
            }
            emitDataLine(line.str());
            continue;
        }

        const long long value = global.init ? static_cast<const NumberExpr &>(*global.init).value : 0;
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

    // 发射静态局部变量到 .data/.bss 节
    emitStaticLocals();

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

void CodeGenerator::emitFunction(const Function &function) {
    // System V ABI 下检查结构体类型是否支持
    if (!usesWindowsAbi()) {
        if (function.returnType->isStruct()) {
            // 支持 INTEGER 类（<=16 字节全整数成员）和 MEMORY 类（>16 字节或含非整数成员）
            // 两者都已实现支持
        }
        for (const auto &parameter : function.parameters) {
            if (parameter.type->isStruct()) {
                // 支持 INTEGER 类和 MEMORY 类结构体参数
            }
        }
    }

    currentFunctionName = function.name;
    const std::string functionLabel = functionSymbol(function.name);
    emitLine(functionLabel + ":");

    // 记录 DWARF 函数信息
    DebugFuncInfo funcInfo;
    funcInfo.name = function.name;
    funcInfo.startLabel = functionLabel;
    funcInfo.startLine = 0;
    debugFuncInfos.push_back(funcInfo);

    // 保存栈帧
    emitLine("    push rbp");
    emitLine("    mov rbp, rsp");

    // 预计算帧大小：包含局部变量 + 对齐 + 隐藏返回指针 + 大结构体返回 scratch
    const int frameSize = currentFunctionFrameSize(function);
    if (frameSize > 0) {
        emitLine("    sub rsp, " + std::to_string(frameSize));
    }

    // 记录 PE 异常处理信息（仅 Windows x64）
    FunctionUnwindInfo unwindInfo;
    unwindInfo.startLabel = functionLabel;
    unwindInfo.frameSize = frameSize;
    unwindInfo.prologueSize = computePrologueSize(frameSize);
    unwindInfo.hasVla = false;  // 将在后面更新

    // System V ABI：隐藏返回指针存储到已知位置
    if (!usesWindowsAbi() && usesHiddenSystemVReturnPointer(function)) {
        activeHiddenReturnPointerOffset = findHiddenReturnPointerLocalOffset(function);
        emitLine("    mov qword [rbp-" + std::to_string(activeHiddenReturnPointerOffset) + "], rdi");
    }

    // 大结构体返回结果 scratch 空间
    activeLargeStructCallResultOffset = findLargeStructCallResultLocalOffset(function);
    activeHiddenReturnPointerOffset = 0;
    if (usesWindowsAbi() && function.returnType->isStruct() && function.returnType->valueSize() > 8) {
        activeHiddenReturnPointerOffset = findHiddenReturnPointerLocalOffset(function);
    }
    if (!usesWindowsAbi() && usesHiddenSystemVReturnPointer(function)) {
        activeHiddenReturnPointerOffset = findHiddenReturnPointerLocalOffset(function);
    }

    functionHasVla = false;

    // 加载参数到局部变量槽位
    if (usesWindowsAbi()) {
        // Windows x64 ABI：前 4 个整数参数通过 rcx, rdx, r8, r9 传递
        // 前 4 个浮点参数通过 xmm0-xmm3 传递
        const std::vector<WindowsAbiArgument> placements =
            buildWindowsAbiArguments(
                [&]() {
                    std::vector<TypePtr> parameterTypes;
                    parameterTypes.reserve(function.parameters.size());
                    for (const auto &parameter : function.parameters) {
                        parameterTypes.push_back(parameter.type);
                    }
                    return parameterTypes;
                }(),
                function.returnType->isStruct() && function.returnType->valueSize() > 8);
        int argIdx = 0;
        if (function.returnType->isStruct() && function.returnType->valueSize() > 8) {
            // 隐藏返回指针：存储到已知位置
            activeHiddenReturnPointerOffset = findHiddenReturnPointerLocalOffset(function);
            emitLine("    mov qword [rbp-" + std::to_string(activeHiddenReturnPointerOffset) + "], rcx");
            argIdx = 1;
        }
        for (std::size_t i = 0; i < function.parameters.size(); ++i, ++argIdx) {
            const auto &param = function.parameters[i];
            const WindowsAbiArgument &placement = placements[static_cast<std::size_t>(argIdx)];
            const int offset = param.stackOffset;
            if (param.type->isStruct()) {
                if (placement.inRegister) {
                    // 小结构体（<=8 字节）通过寄存器传递
                    emitLine("    mov qword [rbp-" + std::to_string(offset) + "], " +
                             std::string(argumentRegister(placement.registerIndex).r64));
                } else {
                    // 大结构体（>8 字节）通过栈传递：拷贝到局部变量区
                    // 栈参数从 rbp+16 开始
                    const int stackArgOffset = 16 + placement.homeOffset;
                    emitLine("    lea rsi, [rbp+" + std::to_string(stackArgOffset) + "]");
                    emitLine("    lea rdi, [rbp-" + std::to_string(offset) + "]");
                    emitLine("    mov ecx, " + std::to_string(param.type->valueSize()));
                    emitLine("    rep movsb");
                }
            } else if (placement.isFloatRegister) {
                if (param.type->kind == TypeKind::Float) {
                    emitLine("    movss dword [rbp-" + std::to_string(offset) + "], xmm" +
                             std::to_string(placement.registerIndex));
                } else {
                    emitLine("    movsd qword [rbp-" + std::to_string(offset) + "], xmm" +
                             std::to_string(placement.registerIndex));
                }
            } else if (placement.inRegister) {
                if (param.type->valueSize() <= 4) {
                    emitLine("    mov dword [rbp-" + std::to_string(offset) + "], " +
                             std::string(argumentRegister(placement.registerIndex).r32));
                } else {
                    emitLine("    mov qword [rbp-" + std::to_string(offset) + "], " +
                             std::string(argumentRegister(placement.registerIndex).r64));
                }
            } else {
                // 栈传递的参数：从 rbp+16 开始
                const int stackArgOffset = 16 + placement.homeOffset;
                if (param.type->valueSize() <= 4) {
                    emitLine("    mov eax, dword [rbp+" + std::to_string(stackArgOffset) + "]");
                    emitLine("    mov dword [rbp-" + std::to_string(offset) + "], eax");
                } else {
                    emitLine("    mov rax, qword [rbp+" + std::to_string(stackArgOffset) + "]");
                    emitLine("    mov qword [rbp-" + std::to_string(offset) + "], rax");
                }
            }
        }
    } else {
        // System V AMD64 ABI：前 6 个整数参数通过 rdi, rsi, rdx, rcx, r8, r9 传递
        // 前 8 个浮点参数通过 xmm0-xmm7 传递
        int nextIntReg = 0;
        int nextFloatReg = 0;
        if (usesHiddenSystemVReturnPointer(function)) {
            nextIntReg = 1;  // rdi 被隐藏返回指针占用
        }
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            const auto &param = function.parameters[i];
            const int offset = param.stackOffset;
            if (param.type->isStruct()) {
                int regCount = isSystemVRegisterStruct(*param.type) ? systemVStructRegisterCount(*param.type) : 0;
                if (regCount == 1 && nextIntReg < argumentRegisterCount()) {
                    // <=8 字节 INTEGER 类：一个寄存器
                    emitLine("    mov qword [rbp-" + std::to_string(offset) + "], " +
                             std::string(argumentRegister(nextIntReg).r64));
                    nextIntReg++;
                } else if (regCount == 2 && nextIntReg + 2 <= argumentRegisterCount()) {
                    // 9-16 字节 INTEGER 类：两个寄存器（低 8 字节在第一个寄存器）
                    emitLine("    mov qword [rbp-" + std::to_string(offset) + "], " +
                             std::string(argumentRegister(nextIntReg).r64));
                    emitLine("    mov qword [rbp-" + std::to_string(offset - 8) + "], " +
                             std::string(argumentRegister(nextIntReg + 1).r64));
                    nextIntReg += 2;
                } else {
                    // MEMORY 类：通过栈传递，需要从寄存器区域拷贝
                    // 隐藏返回指针已在 rdi 中
                    emitLine("    lea rsi, [rbp+" + std::to_string(16 + static_cast<int>(i) * 8) + "]");
                    emitLine("    lea rdi, [rbp-" + std::to_string(offset) + "]");
                    emitLine("    mov ecx, " + std::to_string(param.type->valueSize()));
                    emitLine("    rep movsb");
                }
            } else if (param.type->isFloatingPoint()) {
                if (nextFloatReg < floatArgumentRegisterCount()) {
                    if (param.type->kind == TypeKind::Float) {
                        emitLine("    movss dword [rbp-" + std::to_string(offset) + "], xmm" +
                                 std::to_string(nextFloatReg));
                    } else {
                        emitLine("    movsd qword [rbp-" + std::to_string(offset) + "], xmm" +
                                 std::to_string(nextFloatReg));
                    }
                    nextFloatReg++;
                } else {
                    // 栈传递的浮点参数
                    const int stackArgOffset = 16 + (static_cast<int>(i) - argumentRegisterCount()) * 8;
                    if (param.type->kind == TypeKind::Float) {
                        emitLine("    movss xmm0, dword [rbp+" + std::to_string(stackArgOffset) + "]");
                        emitLine("    movss dword [rbp-" + std::to_string(offset) + "], xmm0");
                    } else {
                        emitLine("    movsd xmm0, qword [rbp+" + std::to_string(stackArgOffset) + "]");
                        emitLine("    movsd qword [rbp-" + std::to_string(offset) + "], xmm0");
                    }
                }
            } else {
                if (nextIntReg < argumentRegisterCount()) {
                    if (param.type->valueSize() <= 4) {
                        emitLine("    mov dword [rbp-" + std::to_string(offset) + "], " +
                                 std::string(argumentRegister(nextIntReg).r32));
                    } else {
                        emitLine("    mov qword [rbp-" + std::to_string(offset) + "], " +
                                 std::string(argumentRegister(nextIntReg).r64));
                    }
                    nextIntReg++;
                } else {
                    // 栈传递的整数参数
                    const int stackArgOffset = 16 + (static_cast<int>(i) - argumentRegisterCount()) * 8;
                    if (param.type->valueSize() <= 4) {
                        emitLine("    mov eax, dword [rbp+" + std::to_string(stackArgOffset) + "]");
                        emitLine("    mov dword [rbp-" + std::to_string(offset) + "], eax");
                    } else {
                        emitLine("    mov rax, qword [rbp+" + std::to_string(stackArgOffset) + "]");
                        emitLine("    mov qword [rbp-" + std::to_string(offset) + "], rax");
                    }
                }
            }
        }
    }

    currentReturnLabel = makeLabel("ret");
    emitStatement(*function.body);
    emitLine(currentReturnLabel + ":");
    if (!debugFuncInfos.empty()) {
        debugFuncInfos.back().endLabel = currentReturnLabel;
    }

    // 恢复栈帧
    if (functionHasVla) {
        // VLA 动态分配了额外栈空间，必须用 mov rsp, rbp 恢复
        emitLine("    mov rsp, rbp");
    } else if (frameSize > 0) {
        emitLine("    add rsp, " + std::to_string(frameSize));
    }
    emitLine("    pop rbp");
    emitLine("    ret");

    // 记录 PE 异常处理信息（仅 Windows x64）
    if (usesWindowsAbi()) {
        unwindInfo.endLabel = currentReturnLabel;
        unwindInfo.hasVla = functionHasVla;
        unwindInfo.xdataLabel = makeLabel("xdata_" + function.name);
        functionUnwindInfos.push_back(unwindInfo);
    }

    activeHiddenReturnPointerOffset = 0;
    activeLargeStructCallResultOffset = 0;
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

std::string CodeGenerator::formatStackAddress(int offset) {
    if (offset >= 0) {
        return "[rbp-" + std::to_string(offset) + "]";
    }
    return "[rbp+" + std::to_string(-offset) + "]";
}

int CodeGenerator::findLargeStructCallResultLocalOffset(const Function &function) const {
    const int rawScratchSize = maxLargeStructCallResultSizeFunction(function);
    if (rawScratchSize == 0) {
        return 0;
    }
    // System V ABI 下，INTEGER 类 9-16 字节结构体返回需要 16 字节 scratch（rax:rdx）
    const int scratchSize = std::max(rawScratchSize, 16);
    int offset = alignStackSize(function.stackSize);
    if (usesWindowsAbi() && function.returnType->isStruct() && function.returnType->valueSize() > 8) {
        offset += 8;
    }
    if (!usesWindowsAbi() && usesHiddenSystemVReturnPointer(function)) {
        offset += 8;  // 隐藏返回指针存储空间
    }
    return offset + scratchSize;
}

// 计算前言字节数：push rbp(1) + mov rbp,rsp(3) + sub rsp,N
int CodeGenerator::computePrologueSize(int frameSize) {
    int size = 1 + 3;  // push rbp + mov rbp, rsp
    if (frameSize > 0) {
        // sub rsp, N：REX.W + 83 EC xx（N<128）= 4 字节，或 REX.W + 81 EC xx xx xx xx = 7 字节
        size += (frameSize < 128) ? 4 : 7;
    }
    return size;
}

void CodeGenerator::emitPdataSection() {
    if (functionUnwindInfos.empty()) return;
    if (!usesWindowsAbi()) return;

    emitLine("section .pdata");
    for (const auto &fi : functionUnwindInfos) {
        emitLine("    dd " + fi.startLabel + " wrt ..imagebase");  // BeginAddress RVA
        emitLine("    dd " + fi.endLabel + " wrt ..imagebase");    // EndAddress RVA
        emitLine("    dd " + fi.xdataLabel + " wrt ..imagebase");  // UnwindInfoAddress RVA
    }
}

void CodeGenerator::emitXdataSection() {
    if (functionUnwindInfos.empty()) return;
    if (!usesWindowsAbi()) return;

    emitLine("section .xdata");
    for (const auto &fi : functionUnwindInfos) {
        emitLine(fi.xdataLabel + ":");

        // UNWIND_INFO header (4 bytes)
        // Version=1, Flags=0 (无异常处理程序)
        int countOfCodes = 0;
        // push rbp → UWOP_PUSH_NONVOL rbp
        countOfCodes++;
        // sub rsp, N → UWOP_ALLOC_SMALL 或 UWOP_ALLOC_LARGE
        if (fi.frameSize > 0) countOfCodes++;

        emitLine("    db 0x01");                                    // Version=1, Flags=0
        emitLine("    db " + std::to_string(fi.prologueSize));     // SizeOfProlog
        emitLine("    db " + std::to_string(countOfCodes));        // CountOfCodes
        emitLine("    db 0x00");                                    // FrameRegister=0, FrameOffset=0

        // UNWIND_CODE entries（逆序：先 sub rsp，后 push rbp）
        if (fi.frameSize > 0) {
            int subRspOffset = 4;  // push rbp(1) + mov rbp,rsp(3) = 4 字节后是 sub rsp
            if (fi.frameSize <= 124 && fi.frameSize % 8 == 0) {
                // UWOP_ALLOC_SMALL：OpInfo = size/8 - 1
                // 字节格式：UnwindOp(低4位) | OpInfo(高4位) = (info << 4) | 2
                int info = fi.frameSize / 8 - 1;
                int byte1 = (info << 4) | 2;
                emitLine("    db " + std::to_string(subRspOffset));  // CodeOffset
                emitLine("    db " + std::to_string(byte1));         // UnwindOp=2, OpInfo=info
            } else {
                // UWOP_ALLOC_LARGE (form 0)：2 字节 unwind code + 2 字节 size
                emitLine("    db " + std::to_string(subRspOffset));  // CodeOffset
                emitLine("    db 0x02");                              // UnwindOp=2 (ALLOC_LARGE), OpInfo=0
                emitLine("    dw " + std::to_string(fi.frameSize));  // 分配大小（16 位）
            }
        }

        // push rbp → UWOP_PUSH_NONVOL rbp
        // CodeOffset=0（push rbp 是第一条指令）
        // 字节格式：UnwindOp=0 (PUSH_NONVOL), OpInfo=5 (rbp) → (5 << 4) | 0 = 0x50
        emitLine("    db 0x00");  // CodeOffset=0
        emitLine("    db 0x50");  // UnwindOp=0 (PUSH_NONVOL), OpInfo=5 (rbp)
    }
}
