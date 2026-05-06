#include "CodeGenerator.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_map>
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
        if (i + 1 < result.size()) out << '\n';
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
    for (const auto &global : currentProgram->globals) {
        if (global.isExternal) {
            emitLine("extern " + global.symbolName);
        } else {
            emitLine("global " + global.symbolName);
        }
    }
    // 声明静态局部变量的全局符号
    for (const auto &var : staticLocalVars) {
        emitLine("global " + var.symbolName);
    }
    emitLine("");
    emitLine("section .text");
    emitLine("");
}

void CodeGenerator::emitTargetExternPrelude() {
    if (target.runtimeEntryFlavor == RuntimeEntryFlavor::ExitProcessStub) {
        emitLine("extern ExitProcess");
    }
    // 浮点取模运算需要 fmod
    emitLine("extern fmod");
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

    currentReturnLabel = makeLabel(function.name + "_return");
    currentFunctionName = function.name;
    const std::string symbol = functionSymbol(function.name);
    const int registerCount = argumentRegisterCount();
    const bool usesHiddenReturnPointer =
        usesWindowsAbi()
        ? (function.returnType->isStruct() && function.returnType->valueSize() > 8)
        : usesHiddenSystemVReturnPointer(function);

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

    // System V ABI: 构建参数布局
    struct SystemVArgPlacement {
        bool inRegister = false;
        int registerIndex = -1;
        int registerCount = 0;  // 结构体可能占 2 个寄存器
        bool isFloatRegister = false;
        int stackSlotIndex = -1;  // 栈参数索引
    };
    std::vector<SystemVArgPlacement> systemVPlacements;
    if (!usesWindowsAbi()) {
        int nextIntReg = 0;
        int nextFloatReg = 0;
        int stackSlot = 0;
        if (usesHiddenReturnPointer) {
            // 隐藏返回指针占用第一个整数寄存器 rdi
            nextIntReg = 1;
        }
        for (const auto &parameter : function.parameters) {
            SystemVArgPlacement placement;
            if (parameter.type->isStruct()) {
                int regCount = systemVStructRegisterCount(*parameter.type);
                if (regCount > 0 && nextIntReg + regCount <= registerCount) {
                    placement.inRegister = true;
                    placement.registerIndex = nextIntReg;
                    placement.registerCount = regCount;
                    nextIntReg += regCount;
                } else {
                    // MEMORY 类或寄存器不足，通过栈传递
                    placement.inRegister = false;
                    placement.stackSlotIndex = stackSlot;
                    stackSlot += parameter.type->valueSize() <= 8 ? 1 : 2;
                }
            } else if (parameter.type->isFloatingPoint()) {
                if (nextFloatReg < floatArgumentRegisterCount()) {
                    placement.inRegister = true;
                    placement.registerIndex = nextFloatReg;
                    placement.registerCount = 1;
                    placement.isFloatRegister = true;
                    nextFloatReg++;
                } else {
                    placement.inRegister = false;
                    placement.stackSlotIndex = stackSlot;
                    stackSlot++;
                }
            } else {
                if (nextIntReg < registerCount) {
                    placement.inRegister = true;
                    placement.registerIndex = nextIntReg;
                    placement.registerCount = 1;
                    nextIntReg++;
                } else {
                    placement.inRegister = false;
                    placement.stackSlotIndex = stackSlot;
                    stackSlot++;
                }
            }
            systemVPlacements.push_back(placement);
        }
    }

    const int frameSize = currentFunctionFrameSize(function);
    functionHasVla = false;
    activeHiddenReturnPointerOffset = usesHiddenReturnPointer
        ? findHiddenReturnPointerLocalOffset(function)
        : 0;
    activeLargeStructCallResultOffset = findLargeStructCallResultLocalOffset(function);

    emitLine(symbol + ":");
    emitLine("    push rbp");
    emitLine("    mov rbp, rsp");

    // 记录函数调试信息
    DebugFuncInfo funcInfo;
    funcInfo.name = function.name;
    funcInfo.startLabel = symbol;
    funcInfo.startLine = 0;
    debugFuncInfos.push_back(funcInfo);
    if (frameSize > 0) {
        emitLine("    sub rsp, " + std::to_string(frameSize));
    }

    // 保存隐藏返回指针
    if (usesHiddenReturnPointer) {
        const std::string hiddenLocalAddress = "[rbp-" + std::to_string(activeHiddenReturnPointerOffset) + "]";
        if (usesWindowsAbi()) {
            const WindowsAbiArgument &hiddenArg = abiArguments.front();
            if (hiddenArg.inRegister) {
                emitLine("    mov qword " + hiddenLocalAddress + ", " + argumentRegister(hiddenArg.registerIndex).r64);
            } else {
                emitLine("    mov rax, qword [rbp+" + std::to_string(16 + hiddenArg.homeOffset) + "]");
                emitLine("    mov qword " + hiddenLocalAddress + ", rax");
            }
        } else {
            // System V ABI: 隐藏返回指针在 rdi（第一个参数寄存器）
            emitLine("    mov qword " + hiddenLocalAddress + ", rdi");
        }
    }

    // 可变参数函数：将所有 4 个寄存器参数保存到 shadow space（供 va_start 使用）
    if (function.isVariadic && usesWindowsAbi()) {
        const int regCount = argumentRegisterCount();
        for (int i = 0; i < regCount; ++i) {
            const std::string shadowAddress = "[rbp+" + std::to_string(16 + i * 8) + "]";
            emitLine("    mov qword " + shadowAddress + ", " + argumentRegister(i).r64);
        }
    }

    for (int i = 0; i < static_cast<int>(function.parameters.size()); ++i) {
        const Type &type = *function.parameters[i].type;
        const std::string localAddress = formatStackAddress(function.parameters[i].stackOffset);

        if (usesWindowsAbi()) {
            const WindowsAbiArgument &placement = abiArguments[static_cast<std::size_t>(i + (usesHiddenReturnPointer ? 1 : 0))];
            if (placement.inRegister) {
                if (placement.isFloatRegister) {
                    // 浮点参数从 xmm 寄存器保存到局部栈
                    const std::string xmmReg = "xmm" + std::to_string(placement.registerIndex);
                    if (type.kind == TypeKind::Float) {
                        emitLine("    movss dword " + localAddress + ", " + xmmReg);
                    } else {
                        emitLine("    movsd qword " + localAddress + ", " + xmmReg);
                    }
                } else if (type.isStruct()) {
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

        // System V ABI 参数加载
        const SystemVArgPlacement &placement = systemVPlacements[static_cast<std::size_t>(i)];
        if (placement.inRegister) {
            if (placement.isFloatRegister) {
                // 浮点参数从 xmm 寄存器保存到局部栈
                const std::string xmmReg = "xmm" + std::to_string(placement.registerIndex);
                if (type.kind == TypeKind::Float) {
                    emitLine("    movss dword " + localAddress + ", " + xmmReg);
                } else {
                    emitLine("    movsd qword " + localAddress + ", " + xmmReg);
                }
            } else if (type.isStruct()) {
                // INTEGER 类结构体：从寄存器保存到局部栈
                if (placement.registerCount == 1) {
                    // <=8 字节：一个寄存器
                    emitLine("    mov qword " + localAddress + ", " + argumentRegister(placement.registerIndex).r64);
                } else {
                    // 9-16 字节：两个寄存器
                    // 低 8 字节在第一个寄存器，高 8 字节在第二个寄存器
                    // 结构体起始地址为 [rbp-stackOffset]
                    emitLine("    mov qword [rbp-" + std::to_string(function.parameters[i].stackOffset) + "], " +
                             argumentRegister(placement.registerIndex).r64);
                    emitLine("    mov qword [rbp-" + std::to_string(function.parameters[i].stackOffset - 8) + "], " +
                             argumentRegister(placement.registerIndex + 1).r64);
                }
            } else if (type.valueSize() == 1) {
                emitLine("    mov byte " + localAddress + ", " + argumentRegister(placement.registerIndex).r8);
            } else if (type.valueSize() == 2) {
                emitLine("    mov word " + localAddress + ", " + argumentRegister(placement.registerIndex).r16);
            } else if (type.valueSize() <= 4) {
                emitLine("    mov dword " + localAddress + ", " + argumentRegister(placement.registerIndex).r32);
            } else {
                emitLine("    mov qword " + localAddress + ", " + argumentRegister(placement.registerIndex).r64);
            }
        } else {
            // 栈传递的参数
            const int sourceOffset = 16 + placement.stackSlotIndex * 8;
            const std::string sourceAddress = "[rbp+" + std::to_string(sourceOffset) + "]";
            if (type.isStruct()) {
                // MEMORY 类结构体：从调用者栈帧拷贝
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
    }

    for (const auto &statement : function.body->statements) {
        emitStatement(*statement);
    }

    if (!function.returnType->isStruct()) {
        if (function.returnType->isFloatingPoint()) {
            emitLine("    xorpd xmm0, xmm0");
        } else {
            emitLine("    xor eax, eax");
        }
    }
    emitLine(currentReturnLabel + ":");
    if (!debugFuncInfos.empty()) {
        debugFuncInfos.back().endLabel = currentReturnLabel;
    }
    if (function.isNoreturn) {
        // _Noreturn 函数：到达 return 是未定义行为，用 ud2 捕获
        emitLine("    ud2");
    } else if (functionHasVla) {
        // VLA 动态分配了额外栈空间，必须用 mov rsp, rbp 恢复
        emitLine("    mov rsp, rbp");
    } else if (frameSize > 0) {
        emitLine("    add rsp, " + std::to_string(frameSize));
    }
    emitLine("    pop rbp");
    emitLine("    ret");
    activeHiddenReturnPointerOffset = 0;
    activeLargeStructCallResultOffset = 0;
}

void CodeGenerator::emitStatement(const Stmt &stmt) {
    // 记录语句行号用于 DWARF 调试信息
    if (stmt.line > 0) {
        std::string label = makeLabel("dbg_line");
        emitLine(label + ":");
        debugLineEntries.push_back({label, stmt.line});
    }

    switch (stmt.kind) {
    case Stmt::Kind::Return: {
        const auto &returnStmt = static_cast<const ReturnStmt &>(stmt);
        if (returnStmt.expr) {
            // 尾调用优化：当 return 直接返回函数调用结果时，内联 epilogue 避免额外跳转
            if (returnStmt.expr->kind == Expr::Kind::Call &&
                !returnStmt.expr->type->isStruct() && canTailCall()) {
                emitTailCall(static_cast<const CallExpr &>(*returnStmt.expr));
                break;
            }
            if (returnStmt.expr->type->isStruct()) {
                if (usesWindowsAbi() && returnStmt.expr->type->valueSize() > 8) {
                    // Windows: >8 字节结构体通过隐藏返回指针
                    emitLine("    mov rcx, qword [rbp-" + std::to_string(activeHiddenReturnPointerOffset) + "]");
                    emitAddress(*returnStmt.expr);
                    emitLine("    mov rdx, rax");
                    emitCopyStructValue(*returnStmt.expr->type, "rcx", "rdx");
                } else if (!usesWindowsAbi() && !isSystemVRegisterStruct(*returnStmt.expr->type)) {
                    // System V: MEMORY 类结构体通过隐藏返回指针
                    emitLine("    mov rcx, qword [rbp-" + std::to_string(activeHiddenReturnPointerOffset) + "]");
                    emitAddress(*returnStmt.expr);
                    emitLine("    mov rdx, rax");
                    emitCopyStructValue(*returnStmt.expr->type, "rcx", "rdx");
                    // 隐藏返回指针已在 rcx 中，把它放到 rax 中作为返回值
                    emitLine("    mov rax, rcx");
                } else if (!usesWindowsAbi() && returnStmt.expr->type->valueSize() > 8) {
                    // System V: INTEGER 类 9-16 字节结构体，通过 rax:rdx 返回
                    emitAddress(*returnStmt.expr);
                    emitLine("    mov rax, qword [rax]");
                    emitLine("    mov rdx, qword [rax+8]");
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
        if (decl.isStatic) {
            // static 局部变量：数据在 .data/.bss 节，不需要栈分配或初始化代码
            // 全局符号已在 prologue 中声明
            break;
        }
        if (decl.type->isVla && decl.vlaSizeExpr) {
            // VLA：运行时栈分配
            functionHasVla = true;
            emitExpr(*decl.vlaSizeExpr);  // rax = 元素数量
            int elemSize = decl.type->elementType->valueSize();
            if (elemSize > 1) {
                emitLine("    imul rax, " + std::to_string(elemSize));
            }
            // 对齐到 16 字节
            emitLine("    add rax, 15");
            emitLine("    and rax, ~15");
            // 动态分配栈空间
            emitLine("    sub rsp, rax");
            emitLine("    mov rax, rsp");
            // 存储 VLA 基地址到指针槽位
            emitLine("    mov qword [rbp-" + std::to_string(decl.stackOffset) + "], rax");
            break;
        }
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
        if (ifStmt.condition->type && ifStmt.condition->type->isFloatingPoint()) {
            emitFloatToBool();
        }
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
        if (whileStmt.condition->type && whileStmt.condition->type->isFloatingPoint()) {
            emitFloatToBool();
        }
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

        // 循环展开检测：for (i = 0; i < N; i++) 且 N 是小常量 (2-4)
        int unrollCount = 0;
        if (forStmt.condition && forStmt.condition->kind == Expr::Kind::Binary) {
            auto &cond = static_cast<const BinaryExpr &>(*forStmt.condition);
            if (cond.op == BinaryOp::Less && cond.right->kind == Expr::Kind::Number) {
                long long n = static_cast<const NumberExpr &>(*cond.right).value;
                if (n >= 2 && n <= 4 && forStmt.update) {
                    unrollCount = static_cast<int>(n);
                }
            }
        }

        if (unrollCount > 0) {
            // 展开循环：生成 N 份循环体
            loopContinueLabels.push_back(updateLabel);
            loopBreakLabels.push_back(endLabel);
            for (int u = 0; u < unrollCount; ++u) {
                emitStatement(*forStmt.body);
                if (forStmt.update && u < unrollCount - 1) {
                    emitExpr(*forStmt.update);
                }
            }
            if (forStmt.update) {
                emitExpr(*forStmt.update);
            }
            loopContinueLabels.pop_back();
            loopBreakLabels.pop_back();
        } else {
            loopContinueLabels.push_back(updateLabel);
            loopBreakLabels.push_back(endLabel);
            emitLine(conditionLabel + ":");
            if (forStmt.condition) {
                emitExpr(*forStmt.condition);
                if (forStmt.condition->type && forStmt.condition->type->isFloatingPoint()) {
                    emitFloatToBool();
                }
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
        }
        break;
    }
    case Stmt::Kind::DoWhile: {
        const auto &doWhileStmt = static_cast<const DoWhileStmt &>(stmt);
        const std::string bodyLabel = makeLabel("do_body");
        const std::string endLabel = makeLabel("do_end");

        loopContinueLabels.push_back(bodyLabel);
        loopBreakLabels.push_back(endLabel);
        emitLine(bodyLabel + ":");
        emitStatement(*doWhileStmt.body);
        emitExpr(*doWhileStmt.condition);
        if (doWhileStmt.condition->type && doWhileStmt.condition->type->isFloatingPoint()) {
            emitFloatToBool();
        }
        emitLine("    cmp rax, 0");
        emitLine("    jne " + bodyLabel);
        emitLine(endLabel + ":");
        loopContinueLabels.pop_back();
        loopBreakLabels.pop_back();
        break;
    }
    case Stmt::Kind::Switch: {
        const auto &sw = static_cast<const SwitchStmt &>(stmt);
        const std::string endLabel = makeLabel("switch_end");

        // 尝试跳转表优化：当所有 case 标签为整数常量且分布足够密集时使用
        if (emitSwitchJumpTable(sw, endLabel)) {
            break;
        }

        // 回退到线性比较链
        std::vector<std::string> caseLabels;
        emitExpr(*sw.scrutinee);
        emitLine("    push rax");

        for (const auto &c : sw.cases) {
            caseLabels.push_back(makeLabel("case"));
            emitExpr(*c.label);
            emitLine("    pop rcx");
            emitLine("    cmp eax, ecx");
            emitLine("    je " + caseLabels.back());
            emitLine("    push rcx");
        }

        emitLine("    pop rcx");

        if (sw.defaultBody) {
            loopBreakLabels.push_back(endLabel);
            emitStatement(*sw.defaultBody);
            loopBreakLabels.pop_back();
            emitLine("    jmp " + endLabel);
        } else {
            emitLine("    jmp " + endLabel);
        }

        loopBreakLabels.push_back(endLabel);
        for (std::size_t i = 0; i < sw.cases.size(); ++i) {
            emitLine(caseLabels[i] + ":");
            emitStatement(*sw.cases[i].body);
        }
        loopBreakLabels.pop_back();

        emitLine(endLabel + ":");
        break;
    }
    case Stmt::Kind::Break:
        emitLine("    jmp " + loopBreakLabels.back());
        break;
    case Stmt::Kind::Continue:
        emitLine("    jmp " + loopContinueLabels.back());
        break;
    case Stmt::Kind::Goto: {
        const auto &gotoStmt = static_cast<const GotoStmt &>(stmt);
        emitLine("    jmp label_" + gotoStmt.targetName);
        break;
    }
    case Stmt::Kind::Label: {
        const auto &labelStmt = static_cast<const LabelStmt &>(stmt);
        emitLine("label_" + labelStmt.name + ":");
        emitStatement(*labelStmt.body);
        break;
    }
    case Stmt::Kind::StaticAssert:
        // 编译时断言，代码生成阶段无需处理
        break;
    }
}

void CodeGenerator::emitExpr(const Expr &expr) {
    switch (expr.kind) {
    case Expr::Kind::Number:
        emitLine("    mov rax, " + std::to_string(static_cast<const NumberExpr &>(expr).value));
        return;
    case Expr::Kind::FloatLiteral: {
        const auto &floatExpr = static_cast<const FloatLiteralExpr &>(expr);
        emitLine("    movsd xmm0, [rel " + floatLiteralLabel(floatExpr.value) + "]");
        return;
    }
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
            if (expr.type->isFloatingPoint()) {
                // 浮点取反：0.0 - xmm0
                emitLine("    movsd xmm1, xmm0");
                emitLine("    xorpd xmm0, xmm0");
                emitLine("    subsd xmm0, xmm1");
            } else if (expr.type->valueSize() > 4) {
                emitLine("    neg rax");
            } else {
                emitLine("    neg eax");
            }
            return;
        case UnaryOp::LogicalNot:
            emitExpr(*unary.operand);
            if (unary.operand->type && unary.operand->type->isFloatingPoint()) {
                emitFloatToBool();
            }
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
        case UnaryOp::BitwiseNot:
            emitExpr(*unary.operand);
            emitLine("    not rax");
            return;
        case UnaryOp::PreIncrement:
        case UnaryOp::PreDecrement: {
            emitAddress(*unary.operand);
            if (unary.operand->type->isFloatingPoint()) {
                // 浮点前缀自增/自减
                emitLine("    movsd xmm0, qword [rax]");
                emitLine("    movsd xmm1, [rel " + floatLiteralLabel(1.0) + "]");
                if (unary.op == UnaryOp::PreIncrement) {
                    emitLine("    addsd xmm0, xmm1");
                } else {
                    emitLine("    subsd xmm0, xmm1");
                }
                emitLine("    movsd qword [rax], xmm0");
                return;
            }
            const char *op = (unary.op == UnaryOp::PreIncrement) ? "inc" : "dec";
            int sz = unary.operand->type->valueSize();
            if (sz == 1) {
                emitLine(std::string("    ") + op + " byte [rax]");
            } else if (sz == 2) {
                emitLine(std::string("    ") + op + " word [rax]");
            } else if (sz <= 4) {
                emitLine(std::string("    ") + op + " dword [rax]");
            } else {
                emitLine(std::string("    ") + op + " qword [rax]");
            }
            emitLoad(*unary.operand->type);
            return;
        }
        case UnaryOp::PostIncrement:
        case UnaryOp::PostDecrement: {
            emitAddress(*unary.operand);
            if (unary.operand->type->isFloatingPoint()) {
                // 浮点后缀自增/自减：返回旧值
                emitLine("    mov rcx, rax");
                emitLine("    movsd xmm0, qword [rax]");
                emitLine("    movsd xmm1, [rel " + floatLiteralLabel(1.0) + "]");
                emitLine("    movsd xmm2, xmm0");
                if (unary.op == UnaryOp::PostIncrement) {
                    emitLine("    addsd xmm0, xmm1");
                } else {
                    emitLine("    subsd xmm0, xmm1");
                }
                emitLine("    movsd qword [rcx], xmm0");
                emitLine("    movsd xmm0, xmm2");
                return;
            }
            emitLine("    mov rcx, rax");
            emitLoad(*unary.operand->type);
            const char *op = (unary.op == UnaryOp::PostIncrement) ? "inc" : "dec";
            int sz = unary.operand->type->valueSize();
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
            if (unary.sizeofType) {
                emitLine("    mov eax, " + std::to_string(unary.sizeofType->valueSize()));
            } else {
                emitLine("    mov eax, " + std::to_string(unary.operand->type->valueSize()));
            }
            return;
        case UnaryOp::Alignof:
            if (unary.sizeofType) {
                emitLine("    mov eax, " + std::to_string(unary.sizeofType->alignment()));
            } else {
                emitLine("    mov eax, 1");
            }
            return;
        }
        return;
    }
    case Expr::Kind::Assign: {
        const auto &assign = static_cast<const AssignExpr &>(expr);
        // 检查位域赋值
        if (assign.target->kind == Expr::Kind::MemberAccess) {
            const auto &memberAccess = static_cast<const MemberAccessExpr &>(*assign.target);
            if (memberAccess.bitWidth > 0) {
                // 位域写入：加载包含字 → 清除目标位 → 或上新值 → 存回
                emitAddress(*assign.target);
                emitLine("    push rax");  // 保存包含字地址
                emitLine("    mov ecx, dword [rax]");  // 加载包含字
                // 清除目标位
                int mask = (1 << memberAccess.bitWidth) - 1;
                int clearMask = ~(mask << memberAccess.bitOffset);
                emitLine("    and ecx, " + std::to_string(clearMask));
                // 计算新值
                emitExpr(*assign.value);
                emitLine("    and eax, " + std::to_string(mask));  // 截断到位宽
                if (memberAccess.bitOffset > 0) {
                    emitLine("    shl eax, " + std::to_string(memberAccess.bitOffset));
                }
                emitLine("    or ecx, eax");  // 合并
                emitLine("    pop rax");  // 恢复地址
                emitLine("    mov dword [rax], ecx");  // 存回
                return;
            }
        }
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
        if (assign.isCompound) {
            // 复合赋值：加载当前值，计算，存储
            emitLine("    pop rcx");  // rcx = target 地址
            emitLine("    push rcx");

            // 浮点复合赋值
            if (assign.target->type->isFloatingPoint()) {
                emitLoad(*assign.target->type);  // xmm0 = 当前值
                emitLine("    sub rsp, 8");
                emitLine("    movsd [rsp], xmm0");
                emitExpr(*assign.value);  // xmm0 = 右值
                emitLine("    movsd xmm1, xmm0");
                emitLine("    movsd xmm0, [rsp]");
                emitLine("    add rsp, 8");
                switch (assign.compoundOp) {
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
                emitStore(*assign.target->type);
                return;
            }

            emitLoad(*assign.target->type);  // rax = 当前值
            emitLine("    push rax");
            emitExpr(*assign.value);  // rax = 右值
            emitLine("    mov rcx, rax");
            emitLine("    pop rax");
            // 执行操作 a op b
            switch (assign.compoundOp) {
            case BinaryOp::Add:
                emitLine("    add eax, ecx");
                break;
            case BinaryOp::Subtract:
                emitLine("    sub eax, ecx");
                break;
            case BinaryOp::Multiply:
                emitLine("    imul eax, ecx");
                break;
            case BinaryOp::Divide:
                emitLine("    cdq");
                emitLine("    idiv ecx");
                break;
            case BinaryOp::Modulo:
                emitLine("    cdq");
                emitLine("    idiv ecx");
                emitLine("    mov eax, edx");
                break;
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
            emitStore(*assign.target->type);
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
        // 数组类型不加载值（数组到指针衰减）
        if (!expr.type->isArray()) {
            emitLoad(*expr.type);
        }
        return;
    case Expr::Kind::MemberAccess: {
        const auto &memberAccess = static_cast<const MemberAccessExpr &>(expr);
        if (memberAccess.bitWidth > 0) {
            // 位域读取：加载包含字 → 右移 → 掩码
            emitAddress(expr);
            // 加载包含字（使用包含单元的类型大小）
            int containerSize = expr.type->valueSize();
            if (containerSize == 1) {
                emitLine("    movzx eax, byte [rax]");
            } else if (containerSize == 2) {
                emitLine("    movzx eax, word [rax]");
            } else {
                emitLine("    mov eax, dword [rax]");
            }
            // 右移 bitOffset 位
            if (memberAccess.bitOffset > 0) {
                emitLine("    shr eax, " + std::to_string(memberAccess.bitOffset));
            }
            // 掩码：保留低 bitWidth 位
            int mask = (1 << memberAccess.bitWidth) - 1;
            emitLine("    and eax, " + std::to_string(mask));
            return;
        }
        emitAddress(expr);
        if (expr.type->isFunction() || expr.type->isStruct()) {
            return;
        }
        emitLoad(*expr.type);
        return;
    }
    case Expr::Kind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        if (binary.op == BinaryOp::LogicalAnd) {
            const std::string falseLabel = makeLabel("and_false");
            const std::string endLabel = makeLabel("and_end");
            emitExpr(*binary.left);
            if (binary.left->type && binary.left->type->isFloatingPoint()) {
                emitFloatToBool();
            }
            emitLine("    cmp rax, 0");
            emitLine("    je " + falseLabel);
            emitExpr(*binary.right);
            if (binary.right->type && binary.right->type->isFloatingPoint()) {
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
        if (binary.op == BinaryOp::LogicalOr) {
            const std::string trueLabel = makeLabel("or_true");
            const std::string endLabel = makeLabel("or_end");
            emitExpr(*binary.left);
            if (binary.left->type && binary.left->type->isFloatingPoint()) {
                emitFloatToBool();
            }
            emitLine("    cmp rax, 0");
            emitLine("    jne " + trueLabel);
            emitExpr(*binary.right);
            if (binary.right->type && binary.right->type->isFloatingPoint()) {
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
        if (binary.op == BinaryOp::Comma) {
            // 逗号运算符：求值左侧（丢弃结果），求值右侧（结果在 rax）
            emitExpr(*binary.left);
            emitExpr(*binary.right);
            return;
        }

        // 浮点二元运算（结果类型或操作数类型为浮点）
        if ((expr.type && expr.type->isFloatingPoint()) ||
            (binary.left->type && binary.left->type->isFloatingPoint())) {
            emitExpr(*binary.left);
            emitLine("    sub rsp, 8");
            emitLine("    movsd [rsp], xmm0");
            emitExpr(*binary.right);
            emitLine("    movsd xmm1, xmm0");
            emitLine("    movsd xmm0, [rsp]");
            emitLine("    add rsp, 8");

            switch (binary.op) {
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
                // fmod 是外部 C 库调用，需要确保栈 16 字节对齐
                // 调用前：rsp 已经是 16 字节对齐的（来自 sub rsp, 8 + call 的 push rbp）
                emitLine("    sub rsp, 8");   // 对齐
                emitLine("    call fmod");
                emitLine("    add rsp, 8");   // 恢复
                return;
            default:
                throw std::runtime_error("internal code generation error: unsupported float binary operation");
            }
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
            if (binary.left->type->decay()->isPointer() && binary.right->type->decay()->isPointer()) {
                // 指针减法：rax=right, rcx=left → (rax - rcx) / sizeof(*ptr)
                emitLine("    sub rax, rcx");
                const int elemSize = pointeeSize(*binary.left->type->decay());
                if (elemSize > 1) {
                    emitLine("    mov rcx, " + std::to_string(elemSize));
                    emitLine("    cqo");
                    emitLine("    idiv rcx");
                }
            } else if (expr.type->isPointer()) {
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
        case BinaryOp::Less: {
            const bool isUnsigned = binary.left->type->decay()->isUnsigned || binary.right->type->decay()->isUnsigned;
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
            emitLine(isUnsigned ? "    setb al" : "    setl al");
            emitLine("    movzx eax, al");
            return;
        }
        case BinaryOp::LessEqual: {
            const bool isUnsigned = binary.left->type->decay()->isUnsigned || binary.right->type->decay()->isUnsigned;
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
            emitLine(isUnsigned ? "    setbe al" : "    setle al");
            emitLine("    movzx eax, al");
            return;
        }
        case BinaryOp::Greater: {
            const bool isUnsigned = binary.left->type->decay()->isUnsigned || binary.right->type->decay()->isUnsigned;
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
            emitLine(isUnsigned ? "    seta al" : "    setg al");
            emitLine("    movzx eax, al");
            return;
        }
        case BinaryOp::GreaterEqual: {
            const bool isUnsigned = binary.left->type->decay()->isUnsigned || binary.right->type->decay()->isUnsigned;
            if (binary.left->type->decay()->valueSize() > 4 || binary.right->type->decay()->valueSize() > 4) {
                emitLine("    cmp rax, rcx");
            } else {
                emitLine("    cmp eax, ecx");
            }
            emitLine(isUnsigned ? "    setae al" : "    setge al");
            emitLine("    movzx eax, al");
            return;
        }
        case BinaryOp::Modulo:
            if (expr.type->valueSize() > 4) {
                emitLine("    cqo");
                emitLine("    idiv rcx");
                emitLine("    mov rax, rdx");
            } else {
                emitLine("    cdq");
                emitLine("    idiv ecx");
                emitLine("    mov eax, edx");
            }
            return;
        case BinaryOp::ShiftLeft: {
            // 通用设置后：rax=right(移位量), rcx=left(被移位值)
            // 需要交换，使 cl=移位量, rax=被移位值
            emitLine("    xchg rax, rcx");
            if (expr.type->valueSize() > 4) {
                emitLine("    shl rax, cl");
            } else {
                emitLine("    shl eax, cl");
            }
            return;
        }
        case BinaryOp::ShiftRight: {
            emitLine("    xchg rax, rcx");
            if (expr.type->valueSize() > 4) {
                emitLine("    sar rax, cl");
            } else {
                emitLine("    sar eax, cl");
            }
            return;
        }
        case BinaryOp::BitwiseAnd:
            if (expr.type->valueSize() > 4) {
                emitLine("    and rax, rcx");
            } else {
                emitLine("    and eax, ecx");
            }
            return;
        case BinaryOp::BitwiseXor:
            if (expr.type->valueSize() > 4) {
                emitLine("    xor rax, rcx");
            } else {
                emitLine("    xor eax, ecx");
            }
            return;
        case BinaryOp::BitwiseOr:
            if (expr.type->valueSize() > 4) {
                emitLine("    or rax, rcx");
            } else {
                emitLine("    or eax, ecx");
            }
            return;
        default:
            throw std::runtime_error("internal code generation error: unhandled integer binary operator");
        }
    }
    case Expr::Kind::Ternary: {
        const auto &ternary = static_cast<const TernaryExpr &>(expr);
        const std::string falseLabel = makeLabel("ternary_false");
        const std::string endLabel = makeLabel("ternary_end");
        emitExpr(*ternary.condition);
        if (ternary.condition->type && ternary.condition->type->isFloatingPoint()) {
            emitFloatToBool();
        }
        emitLine("    cmp rax, 0");
        emitLine("    je " + falseLabel);
        emitExpr(*ternary.thenExpr);
        emitLine("    jmp " + endLabel);
        emitLine(falseLabel + ":");
        emitExpr(*ternary.elseExpr);
        emitLine(endLabel + ":");
        return;
    }
    case Expr::Kind::Cast: {
        const auto &cast = static_cast<const CastExpr &>(expr);
        emitExpr(*cast.operand);
        const int srcSize = cast.operand->type ? cast.operand->type->valueSize() : 4;
        const int dstSize = cast.targetType->valueSize();
        const bool srcFloat = cast.operand->type && cast.operand->type->isFloatingPoint();
        const bool dstFloat = cast.targetType->isFloatingPoint();

        // 指针 -> 指针 或 整数 <-> 指针：直接赋值，无需转换
        if (cast.targetType->isPointer() || (cast.operand->type && cast.operand->type->isPointer())) {
            return;
        }

        // 整数 -> 浮点
        if (!srcFloat && dstFloat) {
            if (srcSize <= 4) {
                emitLine("    cvtsi2sd xmm0, eax");
            } else {
                emitLine("    cvtsi2sd xmm0, rax");
            }
            if (cast.targetType->kind == TypeKind::Float) {
                // 目标是 float，保持为 double（内部统一用 double 运算）
            }
            return;
        }

        // 浮点 -> 整数
        if (srcFloat && !dstFloat) {
            if (dstSize <= 4) {
                emitLine("    cvttsd2si eax, xmm0");
            } else {
                emitLine("    cvttsd2si rax, xmm0");
            }
            return;
        }

        // 浮点 -> 浮点（float ↔ double）
        if (srcFloat && dstFloat) {
            // 内部统一用 double 运算，无需转换
            return;
        }

        // 同大小整数：无需转换
        if (srcSize == dstSize) {
            return;
        }

        // 目标是 char (1 字节)：取低 8 位
        if (dstSize == 1) {
            emitLine("    movzx eax, al");
            return;
        }

        // 目标是 short (2 字节)：取低 16 位
        if (dstSize == 2) {
            emitLine("    movzx eax, ax");
            return;
        }

        // 目标是 int (4 字节)：如果源是 char/short，符号扩展到 eax
        if (dstSize <= 4) {
            // 源已经是 4 字节或更大，截断到 eax
            return;
        }

        // 目标是 long long (8 字节)：符号扩展到 rax
        if (dstSize == 8) {
            if (srcSize <= 4) {
                emitLine("    movsxd rax, eax");
            }
            return;
        }

        return;
    }
    case Expr::Kind::BuiltinVaStart: {
        const auto &vaStart = static_cast<const BuiltinVaStartExpr &>(expr);
        // va_start(ap, lastParam): 设置 ap 指向第一个可变参数
        // ap 是 char** 局部变量，*ap = &lastParam + 8（第一个可变参数的地址）
        const int numParams = vaStart.paramIndex + 1;
        emitLine("    lea rcx, [rbp+" + std::to_string(16 + numParams * 8) + "]");
        emitAddress(*vaStart.ap);
        emitLine("    mov qword [rax], rcx");
        emitLine("    xor eax, eax");
        return;
    }
    case Expr::Kind::BuiltinVaArg: {
        const auto &vaArg = static_cast<const BuiltinVaArgExpr &>(expr);
        // va_arg(ap, type): 从 ap 读取值，然后推进指针
        // 先获取 ap 变量的地址（存到 r10，后续不被 emitExpr 覆盖）
        emitAddress(*vaArg.ap);
        emitLine("    mov r10, rax");  // r10 = &ap
        emitExpr(*vaArg.ap);  // rax = ap 的值（指针）
        emitLine("    mov rcx, rax");  // rcx = ap 指针
        const int argSize = vaArg.argType->valueSize();
        if (vaArg.argType->isFloatingPoint()) {
            if (vaArg.argType->kind == TypeKind::Float) {
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
        return;
    }
    case Expr::Kind::BuiltinVaEnd: {
        // va_end 是空操作
        emitLine("    xor eax, eax");
        return;
    }
    case Expr::Kind::Generic: {
        const auto &generic = static_cast<const GenericExpr &>(expr);
        if (generic.selectedExpr) {
            emitExpr(*generic.selectedExpr);
            return;
        }
        // 如果没有选中表达式（不应该发生），生成默认值
        emitLine("    xor eax, eax");
        return;
    }
    case Expr::Kind::CompoundLiteral: {
        // 复合字面量：使用预分配的栈帧空间
        const auto &compound = static_cast<const CompoundLiteralExpr &>(expr);
        // 初始化数据
        if (compound.compoundType->isArray() && compound.init) {
            const auto &list = *compound.init;
            const int elemSize = compound.compoundType->elementType->valueSize();
            for (std::size_t i = 0; i < list.elements.size(); ++i) {
                emitExpr(*list.elements[i]);
                const int offset = compound.stackOffset - static_cast<int>(i) * elemSize;
                emitStoreToLocalSlot(*compound.compoundType->elementType, offset);
            }
        } else if (compound.compoundType->isStruct() && compound.init) {
            const auto &list = *compound.init;
            // 检查是否有指定初始化器
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
                for (std::size_t mi = 0; mi < compound.compoundType->members.size(); ++mi) {
                    const auto &member = compound.compoundType->members[mi];
                    auto it = fieldToElement.find(member.name);
                    if (it == fieldToElement.end()) continue;
                    emitExpr(*list.elements[it->second]);
                    const int memberStackOffset = compound.stackOffset - member.offset;
                    emitStoreToLocalSlot(*member.type, memberStackOffset);
                }
            } else {
                for (std::size_t i = 0; i < list.elements.size() && i < compound.compoundType->members.size(); ++i) {
                    const auto &member = compound.compoundType->members[i];
                    emitExpr(*list.elements[i]);
                    const int memberStackOffset = compound.stackOffset - member.offset;
                    emitStoreToLocalSlot(*member.type, memberStackOffset);
                }
            }
        }
        // 返回复合字面量的基地址
        emitLine("    lea rax, " + formatStackAddress(compound.stackOffset));
        return;
    }
    case Expr::Kind::StmtExpr: {
        // GNU 语句表达式：执行所有语句，然后求值结果表达式
        const auto &se = static_cast<const StmtExpr &>(expr);
        for (const auto &stmt : se.statements) {
            emitStatement(*stmt);
        }
        if (se.result) {
            emitExpr(*se.result);
        } else {
            emitLine("    xor eax, eax");
        }
        return;
    }
    default:
        break;
    }

    throw std::runtime_error("internal code generation error: unhandled expression kind " +
                             std::to_string(static_cast<int>(expr.kind)));
}

void CodeGenerator::emitAddress(const Expr &expr) {
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

void CodeGenerator::emitCopyBytes(const std::string &destAddressExpr, const std::string &srcAddressExpr, int size) {
    if (size <= 0) return;
    // 小拷贝直接用 mov（避免 rep movsb 的开销）
    if (size <= 8) {
        const char *reg = size == 1 ? "al" : size <= 2 ? "ax" : size <= 4 ? "eax" : "rax";
        emitLine("    mov " + std::string(reg) + ", [" + srcAddressExpr + "]");
        emitLine("    mov [" + destAddressExpr + "], " + std::string(reg));
        return;
    }
    // 使用 rep movsb 进行批量拷贝
    emitLine("    lea rdi, [" + destAddressExpr + "]");
    emitLine("    lea rsi, [" + srcAddressExpr + "]");
    emitLine("    mov ecx, " + std::to_string(size));
    emitLine("    rep movsb");
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

    // 递归处理结构体成员的辅助函数
    std::function<void(const Type &, const InitializerListExpr *, int &)> emitStructMembers;
    emitStructMembers = [&](const Type &structType, const InitializerListExpr *initList, int &cur) {
        for (std::size_t i = 0; i < structType.members.size(); ++i) {
            const StructMember &member = structType.members[i];
            const int memberPadding = member.offset - cur;
            if (memberPadding > 0) {
                std::ostringstream padding;
                padding << "db ";
                bool firstValue = true;
                emitZeroFillBytes(padding, memberPadding, firstValue);
                emitLabeledLine(padding.str());
            }
            cur = member.offset;

            const Expr *memberExpr = (initList && i < initList->elements.size()) ? initList->elements[i].get() : nullptr;

            if (member.type->isStruct()) {
                // 嵌套结构体成员
                const InitializerListExpr *nestedList = nullptr;
                if (memberExpr && memberExpr->kind == Expr::Kind::InitializerList) {
                    nestedList = static_cast<const InitializerListExpr *>(memberExpr);
                }
                emitStructMembers(*member.type, nestedList, cur);
            } else if (memberExpr) {
                std::ostringstream memberLine;
                memberLine << dataDirectiveForSize(member.type->valueSize()) << " ";
                bool firstValue = true;
                emitGlobalStructMemberValue(memberLine, *member.type, *memberExpr, firstValue);
                emitLabeledLine(memberLine.str());
                cur += member.type->valueSize();
            } else {
                std::ostringstream zeroLine;
                zeroLine << "db ";
                bool firstValue = true;
                emitZeroFillBytes(zeroLine, member.type->valueSize(), firstValue);
                emitLabeledLine(zeroLine.str());
                cur += member.type->valueSize();
            }
        }
    };

    emitStructMembers(*global.type, list, cursor);

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
    // 递归初始化辅助函数
    // structStackOffset: 结构体第一个字节的 rbp 偏移量（即 [rbp-structStackOffset] 是结构体起始地址）
    std::function<void(int, const Type &, const InitializerListExpr &)> emitRecursive;
    emitRecursive = [&](int structStackOffset, const Type &structType, const InitializerListExpr &initList) {
        // 先将整个结构体区域清零
        const int baseStart = structStackOffset - structType.valueSize() + 1;
        for (int i = 0; i < structType.valueSize(); ++i) {
            emitLine("    mov byte [rbp-" + std::to_string(baseStart + i) + "], 0");
        }

        // 检查是否使用了指定初始化器（designated initializers）
        bool hasDesignators = false;
        for (const auto &desig : initList.designators) {
            if (!desig.empty()) {
                hasDesignators = true;
                break;
            }
        }

        if (hasDesignators) {
            // 指定初始化器：按成员名称匹配元素
            // 构建成员名 -> 元素索引的映射
            std::unordered_map<std::string, std::size_t> fieldToElement;
            for (std::size_t ei = 0; ei < initList.designators.size(); ++ei) {
                if (!initList.designators[ei].empty() && initList.designators[ei][0].kind == Designator::Field) {
                    fieldToElement[initList.designators[ei][0].fieldName] = ei;
                }
            }
            // 按结构体成员顺序初始化
            for (std::size_t mi = 0; mi < structType.members.size(); ++mi) {
                const StructMember &member = structType.members[mi];
                const int memberStackOffset = structStackOffset - member.offset;
                auto it = fieldToElement.find(member.name);
                if (it == fieldToElement.end()) {
                    continue; // 未指定的成员保持零初始化
                }
                const auto &elem = *initList.elements[it->second];
                if (member.type->isStruct() && elem.kind == Expr::Kind::InitializerList) {
                    emitRecursive(memberStackOffset, *member.type,
                        static_cast<const InitializerListExpr &>(elem));
                } else {
                    emitExpr(elem);
                    emitStoreToLocalSlot(*member.type, memberStackOffset);
                }
            }
        } else {
            // 无指定初始化器：按位置匹配
            for (std::size_t i = 0; i < initList.elements.size(); ++i) {
                const StructMember &member = structType.members[i];
                const int memberStackOffset = structStackOffset - member.offset;

                if (member.type->isStruct() && initList.elements[i]->kind == Expr::Kind::InitializerList) {
                    emitRecursive(memberStackOffset, *member.type,
                        static_cast<const InitializerListExpr &>(*initList.elements[i]));
                } else {
                    emitExpr(*initList.elements[i]);
                    emitStoreToLocalSlot(*member.type, memberStackOffset);
                }
            }
        }
    };

    emitRecursive(decl.stackOffset, *decl.type, list);
}

void CodeGenerator::emitCallExpr(const CallExpr &call) {
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

void CodeGenerator::emitTailCall(const CallExpr &call) {
    // 尾调用优化：在 return 处直接内联 epilogue，避免跳转到统一的返回标签
    emitCallExpr(call);
    emitLine("    mov rsp, rbp");
    emitLine("    pop rbp");
    emitLine("    ret");
}

void CodeGenerator::emitWindowsCallExpr(const CallExpr &call) {
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

void CodeGenerator::emitSystemVCallExpr(const CallExpr &call) {
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
        // 多维数组：子元素为嵌套初始化列表时，递归处理
        if (decl.type->elementType->isArray() && list.elements[i]->kind == Expr::Kind::InitializerList) {
            emitNestedArrayValues(
                *decl.type->elementType,
                decl.stackOffset - elementOffset,
                static_cast<const InitializerListExpr &>(*list.elements[i]));
        } else {
            emitExpr(*list.elements[i]);
            emitStoreToLocalSlot(*decl.type->elementType, decl.stackOffset - elementOffset);
        }
    }
    emitZeroLocalArrayElements(*decl.type, decl.stackOffset, list.elements.size());
}

void CodeGenerator::emitNestedArrayValues(const Type &arrayType, int baseOffset, const InitializerListExpr &list) {
    const int elementSize = arrayType.elementType->valueSize();
    for (std::size_t i = 0; i < list.elements.size(); ++i) {
        const int elementOffset = static_cast<int>(i) * elementSize;
        if (arrayType.elementType->isArray() && list.elements[i]->kind == Expr::Kind::InitializerList) {
            emitNestedArrayValues(
                *arrayType.elementType,
                baseOffset - elementOffset,
                static_cast<const InitializerListExpr &>(*list.elements[i]));
        } else {
            emitExpr(*list.elements[i]);
            emitStoreToLocalSlot(*arrayType.elementType, baseOffset - elementOffset);
        }
    }
    emitZeroLocalArrayElements(arrayType, baseOffset, list.elements.size());
}

void CodeGenerator::emitZeroLocalArrayElements(const Type &arrayType, int baseOffset, std::size_t startIndex) {
    const int elementSize = arrayType.elementType->valueSize();
    const int totalBytes = (arrayType.arrayLength - static_cast<int>(startIndex)) * elementSize;

    // 大数组使用 rep stosb 优化（>= 32 字节）
    if (!arrayType.elementType->isArray() && !arrayType.elementType->isFloatingPoint() && totalBytes >= 32) {
        const int startByteOffset = baseOffset - static_cast<int>(startIndex) * elementSize;
        emitLine("    lea rdi, [rbp-" + std::to_string(startByteOffset) + "]");
        emitLine("    xor eax, eax");
        emitLine("    mov ecx, " + std::to_string(totalBytes));
        emitLine("    rep stosb");
        return;
    }

    for (std::size_t i = startIndex; i < static_cast<std::size_t>(arrayType.arrayLength); ++i) {
        const int addressOffset = baseOffset - static_cast<int>(i) * elementSize;
        if (arrayType.elementType->isArray()) {
            // 递归零初始化嵌套子数组（从索引 0 开始）
            emitZeroLocalArrayElements(*arrayType.elementType, addressOffset, 0);
        } else if (arrayType.elementType->isFloatingPoint()) {
            emitLine("    xorpd xmm0, xmm0");
            emitStoreToLocalSlot(*arrayType.elementType, addressOffset);
        } else {
            emitLine("    xor eax, eax");
            emitLine("    xor edx, edx");
            emitStoreToLocalSlot(*arrayType.elementType, addressOffset);
        }
    }
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
    // NASM 的 __float64__() 要求必须有小数点
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

void CodeGenerator::emitFloatToBool() {
    // xmm0 中的浮点值转为 rax 中的布尔值（非零为真）
    emitLine("    xorpd xmm1, xmm1");
    emitLine("    comisd xmm0, xmm1");
    emitLine("    setne al");
    emitLine("    movzx eax, al");
}

std::string CodeGenerator::globalSymbol(const std::string &name) {
    return "gv_" + name;
}

bool CodeGenerator::emitSwitchJumpTable(const SwitchStmt &sw, const std::string &endLabel) {
    // 收集所有 case 标签的整数值
    std::map<long long, std::size_t> valueToCase;
    for (std::size_t i = 0; i < sw.cases.size(); ++i) {
        if (sw.cases[i].label->kind != Expr::Kind::Number) {
            return false;
        }
        long long val = static_cast<const NumberExpr &>(*sw.cases[i].label).value;
        if (val < -2147483648LL || val > 2147483647LL) {
            return false;
        }
        if (!valueToCase.emplace(val, i).second) {
            return false;  // 重复的 case 值
        }
    }

    if (valueToCase.empty()) {
        return false;
    }

    const long long minVal = valueToCase.begin()->first;
    const long long maxVal = valueToCase.rbegin()->first;
    const long long range = maxVal - minVal + 1;

    // 范围太大或太稀疏时回退到线性比较
    if (range > 256 || range > static_cast<long long>(sw.cases.size()) * 2) {
        return false;
    }

    // 预先生成所有标签
    const std::string tableLabel = makeLabel("switch_table");
    const std::string defaultBodyLabel = sw.defaultBody ? makeLabel("default_body") : std::string();

    // 为每个 case 值生成标签，为空槽生成目标标签
    std::map<long long, std::string> caseValueLabels;
    for (const auto &[val, idx] : valueToCase) {
        caseValueLabels[val] = makeLabel("case");
    }

    std::vector<std::string> slotLabels(static_cast<std::size_t>(range));
    for (long long v = minVal; v <= maxVal; ++v) {
        auto caseIt = caseValueLabels.find(v);
        if (caseIt != caseValueLabels.end()) {
            slotLabels[static_cast<std::size_t>(v - minVal)] = caseIt->second;
        } else {
            slotLabels[static_cast<std::size_t>(v - minVal)] =
                sw.defaultBody ? defaultBodyLabel : endLabel;
        }
    }

    // 生成跳转表数据段（标签 + 数据）
    emitRdataLine(tableLabel + ":");
    for (const auto &label : slotLabels) {
        emitRdataLine("    dq " + label);
    }

    // 生成 switch 分派代码
    emitExpr(*sw.scrutinee);
    if (minVal != 0) {
        emitLine("    sub eax, " + std::to_string(minVal));
    }
    emitLine("    cmp eax, " + std::to_string(maxVal - minVal));
    emitLine("    ja " + (sw.defaultBody ? defaultBodyLabel : endLabel));
    emitLine("    lea rcx, [rel " + tableLabel + "]");
    emitLine("    movsxd rax, eax");
    emitLine("    jmp [rcx + rax * 8]");

    // 生成各 case 分支体
    loopBreakLabels.push_back(endLabel);
    for (std::size_t i = 0; i < sw.cases.size(); ++i) {
        long long val = static_cast<const NumberExpr &>(*sw.cases[i].label).value;
        emitLine(caseValueLabels[val] + ":");
        emitStatement(*sw.cases[i].body);
    }

    // 生成 default 分支体
    if (sw.defaultBody) {
        emitLine(defaultBodyLabel + ":");
        emitStatement(*sw.defaultBody);
    }
    loopBreakLabels.pop_back();

    emitLine(endLabel + ":");
    return true;
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

void CodeGenerator::collectStaticLocals(const BlockStmt &block) {
    for (const auto &stmt : block.statements) {
        if (stmt->kind == Stmt::Kind::Decl) {
            const auto &decl = static_cast<const DeclStmt &>(*stmt);
            if (decl.isStatic) {
                staticLocalVars.push_back({decl.type, decl.staticSymbolName, decl.init.get()});
            }
        } else if (stmt->kind == Stmt::Kind::Block) {
            collectStaticLocals(static_cast<const BlockStmt &>(*stmt));
        } else if (stmt->kind == Stmt::Kind::If) {
            const auto &ifStmt = static_cast<const IfStmt &>(*stmt);
            if (ifStmt.thenBranch->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*ifStmt.thenBranch));
            }
            if (ifStmt.elseBranch && ifStmt.elseBranch->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*ifStmt.elseBranch));
            }
        } else if (stmt->kind == Stmt::Kind::While) {
            const auto &whileStmt = static_cast<const WhileStmt &>(*stmt);
            if (whileStmt.body->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*whileStmt.body));
            }
        } else if (stmt->kind == Stmt::Kind::For) {
            const auto &forStmt = static_cast<const ForStmt &>(*stmt);
            if (forStmt.init && forStmt.init->kind == Stmt::Kind::Decl) {
                const auto &initDecl = static_cast<const DeclStmt &>(*forStmt.init);
                if (initDecl.isStatic) {
                    staticLocalVars.push_back({initDecl.type, initDecl.staticSymbolName, initDecl.init.get()});
                }
            }
            if (forStmt.body->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*forStmt.body));
            }
        } else if (stmt->kind == Stmt::Kind::DoWhile) {
            const auto &doWhileStmt = static_cast<const DoWhileStmt &>(*stmt);
            if (doWhileStmt.body->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*doWhileStmt.body));
            }
        } else if (stmt->kind == Stmt::Kind::Switch) {
            const auto &sw = static_cast<const SwitchStmt &>(*stmt);
            for (const auto &c : sw.cases) {
                if (c.body->kind == Stmt::Kind::Block) {
                    collectStaticLocals(static_cast<const BlockStmt &>(*c.body));
                }
            }
            if (sw.defaultBody && sw.defaultBody->kind == Stmt::Kind::Block) {
                collectStaticLocals(static_cast<const BlockStmt &>(*sw.defaultBody));
            }
        }
    }
}

void CodeGenerator::emitStaticLocals() {
    for (const auto &var : staticLocalVars) {
        if (var.init && var.init->kind == Expr::Kind::Number) {
            // 初始化的静态局部变量 -> .data 节
            const long long value = static_cast<const NumberExpr &>(*var.init).value;
            std::ostringstream line;
            switch (var.type->valueSize()) {
            case 1:
                line << var.symbolName << ": db " << value;
                break;
            case 2:
                line << var.symbolName << ": dw " << value;
                break;
            case 4:
                line << var.symbolName << ": dd " << value;
                break;
            default:
                line << var.symbolName << ": dq " << value;
                break;
            }
            emitDataLine(line.str());
        } else if (var.init && var.init->kind == Expr::Kind::FloatLiteral) {
            // 浮点初始化的静态局部变量 -> .data 节
            const double value = static_cast<const FloatLiteralExpr &>(*var.init).value;
            std::ostringstream valStream;
            valStream << std::setprecision(17) << value;
            std::string valStr = valStream.str();
            if (valStr.find('.') == std::string::npos &&
                valStr.find('e') == std::string::npos &&
                valStr.find('E') == std::string::npos) {
                valStr += ".0";
            }
            std::ostringstream line;
            if (var.type->kind == TypeKind::Float) {
                line << var.symbolName << ": dd __float32__(" << valStr << ")";
            } else {
                line << var.symbolName << ": dq __float64__(" << valStr << ")";
            }
            emitDataLine(line.str());
        } else if (var.init && var.init->kind == Expr::Kind::String) {
            // 字符串初始化
            const auto &stringExpr = static_cast<const StringExpr &>(*var.init);
            std::ostringstream line;
            line << var.symbolName << ": db ";
            for (std::size_t i = 0; i < stringExpr.value.size(); ++i) {
                if (i > 0) {
                    line << ", ";
                }
                line << static_cast<int>(static_cast<unsigned char>(stringExpr.value[i]));
            }
            if (!stringExpr.value.empty()) {
                line << ", ";
            }
            line << "0";
            emitDataLine(line.str());
        } else {
            // 未初始化或零初始化 -> .bss 节
            std::ostringstream line;
            if (var.type->isArray() || var.type->isStruct()) {
                line << var.symbolName << ": resb " << var.type->valueSize();
            } else {
                switch (var.type->valueSize()) {
                case 1:
                    line << var.symbolName << ": resb 1";
                    break;
                case 2:
                    line << var.symbolName << ": resw 1";
                    break;
                case 4:
                    line << var.symbolName << ": resd 1";
                    break;
                default:
                    line << var.symbolName << ": resq 1";
                    break;
                }
            }
            emitBssLine(line.str());
        }
    }
}

// LEB128 编码辅助
std::string CodeGenerator::encodeULEB128(int value) {
    std::string result;
    unsigned int v = static_cast<unsigned int>(value);
    do {
        uint8_t byte = v & 0x7f;
        v >>= 7;
        if (v != 0) byte |= 0x80;
        result += "0x";
        char buf[4];
        snprintf(buf, sizeof(buf), "%02x", byte);
        result += buf;
        result += ",";
    } while (v != 0);
    return result;
}

std::string CodeGenerator::encodeSLEB128(int value) {
    std::string result;
    int v = value;
    bool more = true;
    while (more) {
        uint8_t byte = v & 0x7f;
        v >>= 7;
        if ((v == 0 && (byte & 0x40) == 0) || (v == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        result += "0x";
        char buf[4];
        snprintf(buf, sizeof(buf), "%02x", byte);
        result += buf;
        result += ",";
    }
    return result;
}

void CodeGenerator::emitDwarfSections() {
    if (debugFuncInfos.empty()) return;
    emitDebugStrSection();
    emitDebugAbbrevSection();
    emitDebugLineSection();
    emitDebugInfoSection();
}

void CodeGenerator::emitDebugStrSection() {
    emitLine("section .debug_str");
    // 文件名
    std::string producer = "minic compiler";
    emitLine("    db \"" + producer + "\",0");
    if (currentProgram && !currentProgram->functions.empty()) {
        // 使用第一个函数的文件名（简化）
        emitLine("    db \"input.c\",0");
    }
}

void CodeGenerator::emitDebugAbbrevSection() {
    emitLine("section .debug_abbrev");
    // 编译单元 DIE (tag=0x11 DW_TAG_compile_unit, children=yes)
    emitLine("    ; abbreviation 1: DW_TAG_compile_unit");
    emitLine("    db 1");  // abbreviation code
    emitLine("    db 0x11");  // DW_TAG_compile_unit
    emitLine("    db 1");  // DW_CHILDREN_yes
    // DW_AT_producer (DW_FORM_strp)
    emitLine("    db 0x25");  // DW_AT_producer
    emitLine("    db 0x0e");  // DW_FORM_strp
    // DW_AT_language (DW_FORM_data1)
    emitLine("    db 0x13");  // DW_AT_language
    emitLine("    db 0x0b");  // DW_FORM_data1
    // DW_AT_name (DW_FORM_strp)
    emitLine("    db 0x03");  // DW_AT_name
    emitLine("    db 0x0e");  // DW_FORM_strp
    // DW_AT_stmt_list (DW_FORM_sec_offset)
    emitLine("    db 0x10");  // DW_AT_stmt_list
    emitLine("    db 0x17");  // DW_FORM_sec_offset
    // 结束属性
    emitLine("    db 0,0");

    // 子程序 DIE (tag=0x2e DW_TAG_subprogram, children=yes)
    emitLine("    ; abbreviation 2: DW_TAG_subprogram");
    emitLine("    db 2");
    emitLine("    db 0x2e");  // DW_TAG_subprogram
    emitLine("    db 1");  // DW_CHILDREN_yes
    // DW_AT_name (DW_FORM_strp)
    emitLine("    db 0x03");
    emitLine("    db 0x0e");
    // DW_AT_low_pc (DW_FORM_addr)
    emitLine("    db 0x11");  // DW_AT_low_pc
    emitLine("    db 0x01");  // DW_FORM_addr
    // DW_AT_high_pc (DW_FORM_data4)
    emitLine("    db 0x12");  // DW_AT_high_pc
    emitLine("    db 0x06");  // DW_FORM_data4
    emitLine("    db 0,0");

    // 结束子程序 DIE
    emitLine("    ; abbreviation 3: (null - end of children)");
    emitLine("    db 0");

    // 结束编译单元 DIE
    emitLine("    db 0");
}

void CodeGenerator::emitDebugInfoSection() {
    emitLine("section .debug_info");

    // 编译单元头
    emitLine("    ; compilation unit header");
    // unit_length (4字节，后面填写)
    std::string lengthLabel = makeLabel("debug_info_length");
    emitLine("    dd " + lengthLabel + " - $ - 4");
    emitLine(lengthLabel + ":");
    // version
    emitLine("    dw 4");
    // debug_abbrev_offset
    emitLine("    dd 0");
    // address size
    emitLine("    db 8");

    // 编译单元 DIE (abbreviation 1)
    emitLine("    ; compile unit DIE");
    emitLine("    db 1");  // abbreviation code
    // DW_AT_producer -> .debug_str 偏移 0
    emitLine("    dd 0");
    // DW_AT_language -> C99 (0x0c)
    emitLine("    db 0x0c");
    // DW_AT_name -> .debug_str 偏移
    std::string producer = "minic compiler";
    int nameOffset = static_cast<int>(producer.size()) + 1;  // 跳过 producer 字符串
    emitLine("    dd " + std::to_string(nameOffset));
    // DW_AT_stmt_list -> .debug_line 节偏移（用0表示从头开始）
    emitLine("    dd 0");

    // 子程序 DIE
    for (const auto &fi : debugFuncInfos) {
        emitLine("    ; subprogram DIE: " + fi.name);
        emitLine("    db 2");  // abbreviation code
        // DW_AT_name
        int funcNameOffset = nameOffset + 8;  // "input.c" + null = 8
        emitLine("    dd " + std::to_string(funcNameOffset));
        // DW_AT_low_pc
        emitLine("    dq " + fi.startLabel);
        // DW_AT_high_pc (size)
        emitLine("    dd " + fi.endLabel + " - " + fi.startLabel);
    }

    // 结束编译单元子节点
    emitLine("    db 0");
}

void CodeGenerator::emitDebugLineSection() {
    emitLine("section .debug_line");

    // 行号程序头
    emitLine("    ; line number program header");
    std::string headerEnd = makeLabel("debug_line_header_end");
    emitLine("    dd " + headerEnd + " - $ - 4");  // unit_length
    emitLine(headerEnd + ":");
    emitLine("    dw 4");   // version
    emitLine("    dd 0");   // header_length (简化，填0)
    emitLine("    db 1");   // minimum_instruction_length
    emitLine("    db 1");   // default_is_stmt
    emitLine("    db 0");   // line_base
    emitLine("    db 1");   // line_range
    emitLine("    db 10");  // opcode_base (标准: 10个标准操作码)
    // 标准操作码长度表
    emitLine("    db 0,1,1,1,1,0,0,0,1,0");  // 对应 opcode 1-10 的参数数量

    // 包含目录表（空，用0终止）
    emitLine("    db 0");
    // 文件名表
    emitLine("    db \"input.c\",0,1,0,0");  // 文件名,目录索引,最后修改时间,大小
    emitLine("    db 0");  // 文件名表结束

    // 行号程序
    emitLine("    ; line number program");

    // DW_LNS_extended_op: 设置初始地址
    emitLine("    db 0,9,2");  // extended op, length=9, DW_LNE_set_address
    emitLine("    dq " + debugFuncInfos[0].startLabel);

    bool isStmt = true;
    int currentLine = 1;
    int currentAddr = 0;

    for (const auto &entry : debugLineEntries) {
        // 先输出特殊操作码设置行号
        int lineDelta = entry.line - currentLine;

        // 检查是否需要切换函数（设置新地址）
        bool needNewAddress = false;
        for (const auto &fi : debugFuncInfos) {
            if (entry.label == fi.startLabel) {
                needNewAddress = true;
                break;
            }
        }

        if (needNewAddress) {
            // DW_LNS_extended_op: 设置新地址
            emitLine("    db 0,9,2");
            emitLine("    dq " + entry.label);
            currentAddr = 0;
        }

        if (lineDelta != 0) {
            // DW_LNS_advance_line
            std::string leb = encodeSLEB128(lineDelta);
            if (!leb.empty() && leb.back() == ',') leb.pop_back();
            emitLine("    db 3");
            emitLine("    db " + leb);
            currentLine = entry.line;
        }

        // DW_LNS_copy (输出一行)
        emitLine("    db 1");
    }

    // DW_LNS_extended_op: 序列结束
    emitLine("    db 0,1,1");  // extended op, length=1, DW_LNE_end_sequence
}
