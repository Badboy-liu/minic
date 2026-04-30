#include "WindowsObjectEmitter.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t TextSectionCharacteristics = 0x60000020;
constexpr std::uint32_t DataSectionCharacteristics = 0xC0000040;
constexpr std::uint32_t ReadOnlyDataSectionCharacteristics = 0x40000040;
constexpr std::uint32_t BssSectionCharacteristics = 0xC0000080;

constexpr int TextSectionIndex = 1;
constexpr int DataSectionIndex = 2;
constexpr int ReadOnlyDataSectionIndex = 3;
constexpr int BssSectionIndex = 4;

std::string functionSymbolName(const std::string &name) {
    return "fn_" + name;
}

ObjectSection makeTextSectionValue() {
    ObjectSection section;
    section.name = ".text";
    section.characteristics = TextSectionCharacteristics;
    section.alignment = 16;
    section.kind = ObjectSectionKind::Text;
    return section;
}

ObjectSection makeEmptyDataSectionValue() {
    ObjectSection section;
    section.name = ".data";
    section.characteristics = DataSectionCharacteristics;
    section.alignment = 8;
    section.kind = ObjectSectionKind::Data;
    return section;
}

ObjectSection makeEmptyReadOnlyDataSectionValue() {
    ObjectSection section;
    section.name = ".rdata";
    section.characteristics = ReadOnlyDataSectionCharacteristics;
    section.alignment = 8;
    section.kind = ObjectSectionKind::ReadOnlyData;
    return section;
}

ObjectSection makeEmptyBssSectionValue() {
    ObjectSection section;
    section.name = ".bss";
    section.characteristics = BssSectionCharacteristics;
    section.alignment = 8;
    section.virtualSize = 0;
    section.isBss = true;
    section.kind = ObjectSectionKind::Bss;
    return section;
}

enum class ConditionCode : std::uint8_t {
    Equal = 0x94,
    NotEqual = 0x95,
    Less = 0x9C,
    LessEqual = 0x9E,
    Greater = 0x9F,
    GreaterEqual = 0x9D,
    Below = 0x92,
    BelowEqual = 0x96,
    Above = 0x97,
    AboveEqual = 0x93
};

enum class RegisterCode : std::uint8_t {
    Rax = 0,
    Rcx = 1,
    Rdx = 2,
    Rbx = 3,
    Rsp = 4,
    Rbp = 5,
    Rsi = 6,
    Rdi = 7,
    R8 = 8,
    R9 = 9,
    R10 = 10,
    R11 = 11
};

struct PendingJumpPatch {
    std::uint32_t displacementOffset = 0;
    int labelId = 0;
};

struct GlobalStorageLocation {
    int sectionIndex = 0;
    std::uint32_t offset = 0;
};

class WindowsObjectModelEmitter {
public:
    ObjectFileModel emit(const Program &program, bool emitEntryPoint) {
        initializeSections();
        declareFunctionSymbols(program);
        emitGlobals(program);

        for (const auto &function : program.functions) {
            if (!function.isDeclaration()) {
                emitFunction(function);
            }
        }

        if (emitEntryPoint) {
            addOrUpdateSymbol("mainCRTStartup", TextSectionIndex, currentTextOffset(), ObjectSymbolBinding::Global);
            emitEntryPointBody();
            addOrUpdateSymbol("ExitProcess", 0, 0, ObjectSymbolBinding::Undefined);
        }

        if (model.sections[0].bytes.empty()) {
            throw std::runtime_error("direct COFF backend internal error: emitted empty .text section");
        }

        return std::move(model);
    }

private:
    void initializeSections() {
        model.sections.push_back(makeTextSectionValue());
        model.sections.push_back(makeEmptyDataSectionValue());
        model.sections.push_back(makeEmptyReadOnlyDataSectionValue());
        model.sections.push_back(makeEmptyBssSectionValue());
        text = &model.sections[0];
        data = &model.sections[1];
        rdata = &model.sections[2];
        bss = &model.sections[3];
    }

    void declareFunctionSymbols(const Program &program) {
        for (const auto &function : program.functions) {
            addOrUpdateSymbol(
                functionSymbolName(function.name),
                function.isDeclaration() ? 0 : TextSectionIndex,
                0,
                function.isDeclaration() ? ObjectSymbolBinding::Undefined : ObjectSymbolBinding::Global);
        }
    }

    void emitGlobals(const Program &program) {
        for (const auto &global : program.globals) {
            if (global.isExternal) {
                addOrUpdateSymbol(global.symbolName, 0, 0, ObjectSymbolBinding::Undefined);
                continue;
            }
            if (!global.emitStorage) {
                continue;
            }
            ensureSupportedGlobalType(*global.type);
            emitGlobalStorage(global);
        }
    }

    void ensureSupportedGlobalType(const Type &type) const {
        if (type.isPointer()) {
            return;
        }
        if (type.isInteger() && type.valueSize() <= 4) {
            return;
        }
        if (type.isArray()) {
            if (type.elementType->isPointer()) {
                return;
            }
            if (!type.elementType->isInteger() || type.elementType->valueSize() > 4) {
                throw std::runtime_error("direct COFF backend currently supports only integer global arrays");
            }
            return;
        }
        throw std::runtime_error("direct COFF backend encountered unsupported global type");
    }

    void ensureSupportedFunction(const Function &function) const {
        ensureSupportedScalarType(*function.returnType, "function return");
        for (const auto &parameter : function.parameters) {
            ensureSupportedScalarType(*parameter.type, "function parameter");
        }
    }

    void ensureSupportedLocalDecl(const DeclStmt &decl) const {
        if (decl.type->isArray()) {
            ensureSupportedLocalArrayType(*decl.type);
            return;
        }
        ensureSupportedScalarType(*decl.type, "local variable");
    }

    void ensureSupportedLocalArrayType(const Type &type) const {
        if (!type.isArray()) {
            throw std::runtime_error("direct COFF backend internal error: expected local array type");
        }
        if (type.elementType->isPointer()) {
            return;
        }
        if (!type.elementType->isInteger() || type.elementType->valueSize() > 4) {
            throw std::runtime_error("direct COFF backend currently supports only local integer or pointer arrays");
        }
    }

    void ensureSupportedScalarType(const Type &type, const char *context) const {
        if (type.isPointer()) {
            return;
        }
        if (type.isInteger() && type.valueSize() <= 8) {
            return;
        }
        throw std::runtime_error(std::string("direct COFF backend encountered unsupported ") + context + " type");
    }

    void emitGlobalStorage(const GlobalVar &global) {
        const int alignment = std::max(1, std::min(8, storageSize(*global.type)));
        if (global.isBss) {
            const std::uint32_t offset = alignSection(*bss, alignment);
            bss->virtualSize += static_cast<std::uint32_t>(global.type->valueSize());
            addOrUpdateSymbol(global.symbolName, BssSectionIndex, offset, ObjectSymbolBinding::Global);
            return;
        }

        if (global.type->isArray()) {
            if (global.type->elementType->isPointer()) {
                emitGlobalPointerArray(global);
                return;
            }
            if (global.init && global.init->kind == Expr::Kind::String) {
                emitGlobalStringArray(global);
                return;
            }
            emitGlobalIntegerArray(global);
            return;
        }

        if (global.type->isPointer()) {
            emitGlobalPointer(global);
            return;
        }

        emitGlobalIntegerScalar(global);
    }

    void emitGlobalIntegerScalar(const GlobalVar &global) {
        const std::uint32_t offset = appendSectionBytes(*data, static_cast<std::uint32_t>(valueSize(*global.type)), storageSize(*global.type));
        addOrUpdateSymbol(global.symbolName, DataSectionIndex, offset, ObjectSymbolBinding::Global);
        const long long value = global.init ? evaluateStaticInteger(*global.init) : 0;
        patchLittleEndian(data->bytes, offset, value, valueSize(*global.type));
    }

    void emitGlobalIntegerArray(const GlobalVar &global) {
        const int elementSize = valueSize(*global.type->elementType);
        const std::uint32_t offset = appendSectionBytes(*data, static_cast<std::uint32_t>(global.type->valueSize()), std::max(1, std::min(8, elementSize)));
        addOrUpdateSymbol(global.symbolName, DataSectionIndex, offset, ObjectSymbolBinding::Global);

        std::size_t elementIndex = 0;
        if (global.init && global.init->kind == Expr::Kind::InitializerList) {
            const auto &list = static_cast<const InitializerListExpr &>(*global.init);
            for (const auto &element : list.elements) {
                patchLittleEndian(
                    data->bytes,
                    offset + static_cast<std::uint32_t>(elementIndex * elementSize),
                    evaluateStaticInteger(*element),
                    elementSize);
                ++elementIndex;
            }
        }
    }

    void emitGlobalStringArray(const GlobalVar &global) {
        const auto &stringExpr = static_cast<const StringExpr &>(*global.init);
        const std::uint32_t offset = appendSectionBytes(*data, static_cast<std::uint32_t>(global.type->valueSize()), 1);
        addOrUpdateSymbol(global.symbolName, DataSectionIndex, offset, ObjectSymbolBinding::Global);
        for (std::size_t i = 0; i < stringExpr.value.size(); ++i) {
            data->bytes[offset + i] = static_cast<std::uint8_t>(stringExpr.value[i]);
        }
    }

    void emitGlobalPointer(const GlobalVar &global) {
        const std::uint32_t offset = appendSectionBytes(*data, 8, 8);
        addOrUpdateSymbol(global.symbolName, DataSectionIndex, offset, ObjectSymbolBinding::Global);
        const std::string target = resolveStaticAddressSymbol(*global.init);
        model.relocations.push_back(ObjectRelocation{
            DataSectionIndex,
            offset,
            target,
            ObjectRelocationKind::Addr64});
    }

    void emitGlobalPointerArray(const GlobalVar &global) {
        const std::uint32_t offset = appendSectionBytes(*data, static_cast<std::uint32_t>(global.type->valueSize()), 8);
        addOrUpdateSymbol(global.symbolName, DataSectionIndex, offset, ObjectSymbolBinding::Global);

        if (!global.init) {
            return;
        }
        if (global.init->kind != Expr::Kind::InitializerList) {
            throw std::runtime_error("direct COFF backend currently supports global pointer arrays only with initializer lists");
        }

        const auto &list = static_cast<const InitializerListExpr &>(*global.init);
        for (std::size_t i = 0; i < list.elements.size(); ++i) {
            const std::string target = resolveStaticAddressSymbol(*list.elements[i]);
            model.relocations.push_back(ObjectRelocation{
                DataSectionIndex,
                offset + static_cast<std::uint32_t>(i * 8),
                target,
                ObjectRelocationKind::Addr64});
        }
    }

    std::string resolveStaticAddressSymbol(const Expr &expr) {
        if (expr.kind == Expr::Kind::String) {
            return stringLiteralSymbol(static_cast<const StringExpr &>(expr).value);
        }
        if (expr.kind == Expr::Kind::Variable) {
            return static_cast<const VariableExpr &>(expr).symbolName;
        }
        if (expr.kind == Expr::Kind::Unary) {
            const auto &unary = static_cast<const UnaryExpr &>(expr);
            if (unary.op == UnaryOp::AddressOf && unary.operand->kind == Expr::Kind::Variable) {
                return static_cast<const VariableExpr &>(*unary.operand).symbolName;
            }
        }
        throw std::runtime_error("direct COFF backend encountered unsupported static pointer initializer");
    }

    long long evaluateStaticInteger(const Expr &expr) const {
        if (expr.kind != Expr::Kind::Number) {
            throw std::runtime_error("direct COFF backend encountered unsupported static integer initializer");
        }
        return static_cast<const NumberExpr &>(expr).value;
    }

    std::string stringLiteralSymbol(const std::string &value) {
        const auto found = stringSymbols.find(value);
        if (found != stringSymbols.end()) {
            return found->second;
        }

        const std::string symbol = ".rstr_" + std::to_string(nextStringId++);
        const std::uint32_t offset = appendSectionBytes(*rdata, static_cast<std::uint32_t>(value.size() + 1), 1);
        addOrUpdateSymbol(symbol, ReadOnlyDataSectionIndex, offset, ObjectSymbolBinding::Local);
        for (std::size_t i = 0; i < value.size(); ++i) {
            rdata->bytes[offset + i] = static_cast<std::uint8_t>(value[i]);
        }
        rdata->bytes[offset + value.size()] = 0;
        stringSymbols.emplace(value, symbol);
        return symbol;
    }

    void emitFunction(const Function &function) {
        ensureSupportedFunction(function);
        addOrUpdateSymbol(functionSymbolName(function.name), TextSectionIndex, currentTextOffset(), ObjectSymbolBinding::Global);

        labelOffsets.clear();
        pendingJumps.clear();
        loopBreakLabels.clear();
        loopContinueLabels.clear();
        nextLabelId = 1;
        currentReturnLabel = createLabel();

        emitByte(0x55);
        emitBytes({0x48, 0x89, 0xE5});
        if (function.stackSize > 0) {
            emitAdjustRsp(false, function.stackSize);
        }
        spillParameters(function);
        emitStatement(*function.body);
        emitZeroResult(*function.returnType);
        bindLabel(currentReturnLabel);
        if (function.stackSize > 0) {
            emitAdjustRsp(true, function.stackSize);
        }
        emitByte(0x5D);
        emitByte(0xC3);

        patchPendingJumps();
    }

    void spillParameters(const Function &function) {
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i < 4) {
                emitStoreToLocalSlot(*function.parameters[i].type, function.parameters[i].stackOffset, parameterRegister(i));
                continue;
            }
            emitLoadRegisterFromStackArgument(*function.parameters[i].type, RegisterCode::Rax, static_cast<int>(i));
            if (function.parameters[i].type->kind == TypeKind::Bool) {
                emitNormalizeBoolInRax();
            }
            emitStoreToLocalSlot(*function.parameters[i].type, function.parameters[i].stackOffset, RegisterCode::Rax);
        }
    }

    void emitEntryPointBody() {
        emitAdjustRsp(false, 40);
        emitCallSymbol(functionSymbolName("main"));
        emitBytes({0x89, 0xC1});
        emitCallSymbol("ExitProcess");
    }

    void emitStatement(const Stmt &stmt) {
        switch (stmt.kind) {
        case Stmt::Kind::Return: {
            const auto &returnStmt = static_cast<const ReturnStmt &>(stmt);
            if (returnStmt.expr) {
                emitExpr(*returnStmt.expr);
            } else {
                emitBytes({0x31, 0xC0});
            }
            emitJump(currentReturnLabel);
            return;
        }
        case Stmt::Kind::Expr:
            emitExpr(*static_cast<const ExprStmt &>(stmt).expr);
            return;
        case Stmt::Kind::Decl: {
            const auto &decl = static_cast<const DeclStmt &>(stmt);
            ensureSupportedLocalDecl(decl);
            if (decl.type->isArray()) {
                emitLocalArrayDecl(decl);
                return;
            }
            if (decl.init) {
                emitExpr(*decl.init);
            } else {
                emitZeroResult(*decl.type);
            }
            if (decl.type->kind == TypeKind::Bool) {
                emitNormalizeBoolInRax();
            }
            emitStoreToLocalSlot(*decl.type, decl.stackOffset, RegisterCode::Rax);
            return;
        }
        case Stmt::Kind::Block: {
            const auto &block = static_cast<const BlockStmt &>(stmt);
            for (const auto &nested : block.statements) {
                emitStatement(*nested);
            }
            return;
        }
        case Stmt::Kind::If: {
            const auto &ifStmt = static_cast<const IfStmt &>(stmt);
            const int elseLabel = createLabel();
            const int endLabel = createLabel();
            emitExpr(*ifStmt.condition);
            emitZeroJump(*ifStmt.condition->type, elseLabel, true);
            emitStatement(*ifStmt.thenBranch);
            emitJump(endLabel);
            bindLabel(elseLabel);
            if (ifStmt.elseBranch) {
                emitStatement(*ifStmt.elseBranch);
            }
            bindLabel(endLabel);
            return;
        }
        case Stmt::Kind::While: {
            const auto &whileStmt = static_cast<const WhileStmt &>(stmt);
            const int beginLabel = createLabel();
            const int endLabel = createLabel();
            loopContinueLabels.push_back(beginLabel);
            loopBreakLabels.push_back(endLabel);
            bindLabel(beginLabel);
            emitExpr(*whileStmt.condition);
            emitZeroJump(*whileStmt.condition->type, endLabel, true);
            emitStatement(*whileStmt.body);
            emitJump(beginLabel);
            bindLabel(endLabel);
            loopContinueLabels.pop_back();
            loopBreakLabels.pop_back();
            return;
        }
        case Stmt::Kind::For: {
            const auto &forStmt = static_cast<const ForStmt &>(stmt);
            const int condLabel = createLabel();
            const int updateLabel = createLabel();
            const int endLabel = createLabel();
            if (forStmt.init) {
                emitStatement(*forStmt.init);
            }
            loopContinueLabels.push_back(updateLabel);
            loopBreakLabels.push_back(endLabel);
            bindLabel(condLabel);
            if (forStmt.condition) {
                emitExpr(*forStmt.condition);
                emitZeroJump(*forStmt.condition->type, endLabel, true);
            }
            emitStatement(*forStmt.body);
            bindLabel(updateLabel);
            if (forStmt.update) {
                emitExpr(*forStmt.update);
            }
            emitJump(condLabel);
            bindLabel(endLabel);
            loopContinueLabels.pop_back();
            loopBreakLabels.pop_back();
            return;
        }
        case Stmt::Kind::Break:
            if (loopBreakLabels.empty()) {
                throw std::runtime_error("direct COFF backend encountered break outside loop");
            }
            emitJump(loopBreakLabels.back());
            return;
        case Stmt::Kind::Continue:
            if (loopContinueLabels.empty()) {
                throw std::runtime_error("direct COFF backend encountered continue outside loop");
            }
            emitJump(loopContinueLabels.back());
            return;
        }
    }

    void emitExpr(const Expr &expr) {
        ensureSupportedExprType(*expr.type);
        switch (expr.kind) {
        case Expr::Kind::Number:
            emitMovRegImm32(RegisterCode::Rax, static_cast<const NumberExpr &>(expr).value);
            return;
        case Expr::Kind::String:
            emitLeaSymbolAddress(stringLiteralSymbol(static_cast<const StringExpr &>(expr).value));
            return;
        case Expr::Kind::Variable:
            emitVariableExpr(static_cast<const VariableExpr &>(expr));
            return;
        case Expr::Kind::Unary:
            emitUnaryExpr(static_cast<const UnaryExpr &>(expr));
            return;
        case Expr::Kind::Binary:
            emitBinaryExpr(static_cast<const BinaryExpr &>(expr));
            return;
        case Expr::Kind::Assign:
            emitAssignExpr(static_cast<const AssignExpr &>(expr));
            return;
        case Expr::Kind::Call:
            emitCallExpr(static_cast<const CallExpr &>(expr));
            return;
        case Expr::Kind::Index:
            emitIndexExpr(static_cast<const IndexExpr &>(expr));
            return;
        case Expr::Kind::FloatNumber:
        case Expr::Kind::InitializerList:
            throw std::runtime_error("direct COFF backend encountered unsupported expression");
        }
    }

    void ensureSupportedExprType(const Type &type) const {
        if (type.isArray()) {
            ensureSupportedLocalArrayType(type);
            return;
        }
        if (type.isFunction()) {
            return;
        }
        ensureSupportedScalarType(type, "expression");
    }

    void emitVariableExpr(const VariableExpr &variable) {
        if (variable.type->isArray() || variable.type->isFunction()) {
            emitAddressOfVariable(variable);
            return;
        }
        emitAddressOfVariable(variable);
        emitLoadFromAddress(*variable.type);
    }

    void emitUnaryExpr(const UnaryExpr &expr) {
        switch (expr.op) {
        case UnaryOp::Plus:
            emitExpr(*expr.operand);
            return;
        case UnaryOp::Minus:
            emitExpr(*expr.operand);
            if (expr.type->isPointer()) {
                throw std::runtime_error("direct COFF backend does not support negating pointers");
            }
            emitNegForType(*expr.type);
            return;
        case UnaryOp::LogicalNot:
            emitExpr(*expr.operand);
            emitCompareZero(*expr.operand->type);
            emitSetCcToAl(ConditionCode::Equal);
            emitMovzxEaxAl();
            return;
        case UnaryOp::AddressOf:
            emitAddress(*expr.operand);
            return;
        case UnaryOp::Dereference:
            emitAddress(expr);
            emitLoadFromAddress(*expr.type);
            return;
        }
    }

    void emitBinaryExpr(const BinaryExpr &expr) {
        if (expr.op == BinaryOp::LogicalAnd) {
            const int falseLabel = createLabel();
            const int endLabel = createLabel();
            emitExpr(*expr.left);
            emitZeroJump(*expr.left->type, falseLabel, true);
            emitExpr(*expr.right);
            emitZeroJump(*expr.right->type, falseLabel, true);
            emitMovRegImm32(RegisterCode::Rax, 1);
            emitJump(endLabel);
            bindLabel(falseLabel);
            emitBytes({0x31, 0xC0});
            bindLabel(endLabel);
            return;
        }
        if (expr.op == BinaryOp::LogicalOr) {
            const int trueLabel = createLabel();
            const int endLabel = createLabel();
            emitExpr(*expr.left);
            emitZeroJump(*expr.left->type, trueLabel, false);
            emitExpr(*expr.right);
            emitZeroJump(*expr.right->type, trueLabel, false);
            emitBytes({0x31, 0xC0});
            emitJump(endLabel);
            bindLabel(trueLabel);
            emitMovRegImm32(RegisterCode::Rax, 1);
            bindLabel(endLabel);
            return;
        }

        emitExpr(*expr.left);
        emitPush(RegisterCode::Rax);
        emitExpr(*expr.right);
        emitMovRegReg(RegisterCode::Rcx, RegisterCode::Rax, 8);
        emitPop(RegisterCode::Rax);

        switch (expr.op) {
        case BinaryOp::Add:
            emitAddForType(*expr.type);
            return;
        case BinaryOp::Subtract:
            emitSubtractForType(*expr.type);
            return;
        case BinaryOp::Multiply:
            emitMultiplyForType(*expr.type);
            return;
        case BinaryOp::Divide:
            emitDivideForType(*expr.type);
            return;
        case BinaryOp::Equal:
            emitCompareRegisters(*expr.left->type->decay());
            emitSetCcToAl(ConditionCode::Equal);
            emitMovzxEaxAl();
            return;
        case BinaryOp::NotEqual:
            emitCompareRegisters(*expr.left->type->decay());
            emitSetCcToAl(ConditionCode::NotEqual);
            emitMovzxEaxAl();
            return;
        case BinaryOp::Less:
            emitCompareRegisters(*expr.left->type->decay());
            emitSetCcToAl(comparisonCondition(expr, BinaryOp::Less));
            emitMovzxEaxAl();
            return;
        case BinaryOp::LessEqual:
            emitCompareRegisters(*expr.left->type->decay());
            emitSetCcToAl(comparisonCondition(expr, BinaryOp::LessEqual));
            emitMovzxEaxAl();
            return;
        case BinaryOp::Greater:
            emitCompareRegisters(*expr.left->type->decay());
            emitSetCcToAl(comparisonCondition(expr, BinaryOp::Greater));
            emitMovzxEaxAl();
            return;
        case BinaryOp::GreaterEqual:
            emitCompareRegisters(*expr.left->type->decay());
            emitSetCcToAl(comparisonCondition(expr, BinaryOp::GreaterEqual));
            emitMovzxEaxAl();
            return;
        case BinaryOp::LogicalAnd:
        case BinaryOp::LogicalOr:
            return;
        }
    }

    ConditionCode comparisonCondition(const BinaryExpr &expr, BinaryOp op) const {
        const bool unsignedComparison =
            expr.left->type->decay()->isPointer() ||
            expr.right->type->decay()->isPointer() ||
            expr.left->type->decay()->isUnsignedInteger() ||
            expr.right->type->decay()->isUnsignedInteger();

        if (!unsignedComparison) {
            switch (op) {
            case BinaryOp::Less:
                return ConditionCode::Less;
            case BinaryOp::LessEqual:
                return ConditionCode::LessEqual;
            case BinaryOp::Greater:
                return ConditionCode::Greater;
            case BinaryOp::GreaterEqual:
                return ConditionCode::GreaterEqual;
            default:
                break;
            }
        } else {
            switch (op) {
            case BinaryOp::Less:
                return ConditionCode::Below;
            case BinaryOp::LessEqual:
                return ConditionCode::BelowEqual;
            case BinaryOp::Greater:
                return ConditionCode::Above;
            case BinaryOp::GreaterEqual:
                return ConditionCode::AboveEqual;
            default:
                break;
            }
        }

        throw std::runtime_error("internal direct COFF backend comparison error");
    }

    void emitAssignExpr(const AssignExpr &expr) {
        emitAddress(*expr.target);
        emitPush(RegisterCode::Rax);
        emitExpr(*expr.value);
        if (expr.target->type->kind == TypeKind::Bool) {
            emitNormalizeBoolInRax();
        }
        emitPop(RegisterCode::Rcx);
        emitStoreToAddress(*expr.target->type, RegisterCode::Rcx, RegisterCode::Rax);
    }

    void emitCallExpr(const CallExpr &expr) {
        const int slotCount = std::max<int>(4, static_cast<int>(expr.arguments.size()));
        const int alignedSlotCount = (slotCount % 2 == 0) ? slotCount : slotCount + 1;
        emitAdjustRsp(false, alignedSlotCount * 8);
        for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
            ensureSupportedScalarType(*expr.arguments[i]->type->decay(), "call argument");
            emitExpr(*expr.arguments[i]);
            if (expr.parameterTypes[i]->kind == TypeKind::Bool) {
                emitNormalizeBoolInRax();
            }
            emitStoreToRspSlot(*expr.parameterTypes[i], static_cast<int>(i * 8));
        }
        for (std::size_t i = 0; i < expr.arguments.size(); ++i) {
            if (i >= 4) {
                break;
            }
            emitLoadRegisterFromRspSlot(*expr.parameterTypes[i], parameterRegister(i), static_cast<int>(i * 8));
        }
        if (expr.callee->kind == Expr::Kind::Variable) {
            const auto &callee = static_cast<const VariableExpr &>(*expr.callee);
            if (callee.type && callee.type->isFunction()) {
                emitCallSymbol(callee.symbolName);
            } else {
                emitExpr(*expr.callee);
                emitMovRegReg(RegisterCode::R11, RegisterCode::Rax, 8);
                emitCallRegister(RegisterCode::R11);
            }
        } else {
            emitExpr(*expr.callee);
            emitMovRegReg(RegisterCode::R11, RegisterCode::Rax, 8);
            emitCallRegister(RegisterCode::R11);
        }
        emitAdjustRsp(true, alignedSlotCount * 8);
    }

    void emitIndexExpr(const IndexExpr &expr) {
        emitAddress(expr);
        emitLoadFromAddress(*expr.type);
    }

    void emitLocalArrayDecl(const DeclStmt &decl) {
        const Type &arrayType = *decl.type;
        if (!decl.init) {
            return;
        }

        if (decl.init->kind == Expr::Kind::String && arrayType.elementType->equals(*Type::makeChar())) {
            emitLocalStringInitializer(decl, static_cast<const StringExpr &>(*decl.init));
            return;
        }

        if (decl.init->kind == Expr::Kind::InitializerList) {
            emitLocalArrayInitializer(decl, static_cast<const InitializerListExpr &>(*decl.init));
            return;
        }

        throw std::runtime_error(
            "direct COFF backend currently supports local arrays only with string literals or initializer lists");
    }

    void emitLocalStringInitializer(const DeclStmt &decl, const StringExpr &stringExpr) {
        for (std::size_t i = 0; i < stringExpr.value.size(); ++i) {
            emitMovRegImm32(RegisterCode::Rax, static_cast<unsigned char>(stringExpr.value[i]));
            emitStoreToLocalArrayElement(*decl.type->elementType, decl.stackOffset, static_cast<int>(i), RegisterCode::Rax);
        }
        emitMovRegImm32(RegisterCode::Rax, 0);
        emitStoreToLocalArrayElement(*decl.type->elementType, decl.stackOffset, static_cast<int>(stringExpr.value.size()), RegisterCode::Rax);
        for (int i = static_cast<int>(stringExpr.value.size()) + 1; i < decl.type->arrayLength; ++i) {
            emitMovRegImm32(RegisterCode::Rax, 0);
            emitStoreToLocalArrayElement(*decl.type->elementType, decl.stackOffset, i, RegisterCode::Rax);
        }
    }

    void emitLocalArrayInitializer(const DeclStmt &decl, const InitializerListExpr &list) {
        const int declaredLength = decl.type->arrayLength;
        for (std::size_t i = 0; i < list.elements.size(); ++i) {
            if (decl.type->elementType->isPointer()) {
                emitRuntimeAddressValue(*list.elements[i]);
            } else {
                emitExpr(*list.elements[i]);
                if (decl.type->elementType->kind == TypeKind::Bool) {
                    emitNormalizeBoolInRax();
                }
            }
            emitStoreToLocalArrayElement(*decl.type->elementType, decl.stackOffset, static_cast<int>(i), RegisterCode::Rax);
        }
        for (int i = static_cast<int>(list.elements.size()); i < declaredLength; ++i) {
            emitZeroResult(*decl.type->elementType);
            emitStoreToLocalArrayElement(*decl.type->elementType, decl.stackOffset, i, RegisterCode::Rax);
        }
    }

    void emitRuntimeAddressValue(const Expr &expr) {
        if (expr.kind == Expr::Kind::String) {
            emitLeaSymbolAddress(stringLiteralSymbol(static_cast<const StringExpr &>(expr).value));
            return;
        }
        if (expr.kind == Expr::Kind::Variable) {
            const auto &variable = static_cast<const VariableExpr &>(expr);
            if (variable.type->isFunction()) {
                emitLeaSymbolAddress(variable.symbolName);
                return;
            }
            emitAddressOfVariable(variable);
            return;
        }
        if (expr.kind == Expr::Kind::Unary) {
            const auto &unary = static_cast<const UnaryExpr &>(expr);
            if (unary.op == UnaryOp::AddressOf && unary.operand->kind == Expr::Kind::Variable) {
                const auto &variable = static_cast<const VariableExpr &>(*unary.operand);
                if (variable.type->isFunction()) {
                    emitLeaSymbolAddress(variable.symbolName);
                    return;
                }
                emitAddressOfVariable(variable);
                return;
            }
        }
        throw std::runtime_error(
            "direct COFF backend currently supports pointer array initializers only with function names, '&function', '&global', or string literals");
    }

    void emitAddress(const Expr &expr) {
        switch (expr.kind) {
        case Expr::Kind::Variable:
            emitAddressOfVariable(static_cast<const VariableExpr &>(expr));
            return;
        case Expr::Kind::Unary: {
            const auto &unary = static_cast<const UnaryExpr &>(expr);
            if (unary.op != UnaryOp::Dereference) {
                throw std::runtime_error("direct COFF backend encountered unsupported lvalue unary expression");
            }
            emitExpr(*unary.operand);
            return;
        }
        case Expr::Kind::Index: {
            const auto &index = static_cast<const IndexExpr &>(expr);
            emitExpr(*index.base);
            emitPush(RegisterCode::Rax);
            emitExpr(*index.index);
            emitScaleIndex(index.base->type->decay()->elementType->valueSize());
            emitMovRegReg(RegisterCode::Rcx, RegisterCode::Rax, 8);
            emitPop(RegisterCode::Rax);
            emitAddForPointerArithmetic();
            return;
        }
        default:
            throw std::runtime_error("direct COFF backend encountered unsupported lvalue expression");
        }
    }

    void emitAddressOfVariable(const VariableExpr &variable) {
        if (variable.isGlobal) {
            emitLeaSymbolAddress(variable.symbolName);
            return;
        }
        emitLeaLocalAddress(variable.stackOffset);
    }

    void emitZeroJump(const Type &type, int labelId, bool jumpWhenZero) {
        emitCompareZero(type);
        if (jumpWhenZero) {
            emitJe(labelId);
        } else {
            emitJne(labelId);
        }
    }

    void emitCompareZero(const Type &type) {
        if (type.isPointer()) {
            emitBytes({0x48, 0x83, 0xF8, 0x00});
            return;
        }
        if (type.valueSize() == 8) {
            emitBytes({0x48, 0x83, 0xF8, 0x00});
            return;
        }
        if (type.valueSize() == 1 || type.valueSize() == 2 || type.valueSize() == 4) {
            emitBytes({0x83, 0xF8, 0x00});
            return;
        }
        throw std::runtime_error("direct COFF backend encountered unsupported zero comparison");
    }

    void emitCompareRegisters(const Type &type) {
        if (type.isPointer() || type.valueSize() > 4) {
            emitBytes({0x48, 0x39, 0xC8});
            return;
        }
        emitBytes({0x39, 0xC8});
    }

    void emitAddForType(const Type &type) {
        if (type.isPointer() || type.valueSize() > 4) {
            emitAddForPointerArithmetic();
            return;
        }
        emitBytes({0x01, 0xC8});
    }

    void emitAddForPointerArithmetic() {
        emitBytes({0x48, 0x01, 0xC8});
    }

    void emitSubtractForType(const Type &type) {
        if (type.isPointer() || type.valueSize() > 4) {
            emitBytes({0x48, 0x29, 0xC8});
            return;
        }
        emitBytes({0x29, 0xC8});
    }

    void emitMultiplyForType(const Type &type) {
        if (type.isPointer()) {
            throw std::runtime_error("direct COFF backend does not support pointer multiplication");
        }
        if (type.valueSize() > 4) {
            emitBytes({0x48, 0x0F, 0xAF, 0xC1});
            return;
        }
        emitBytes({0x0F, 0xAF, 0xC1});
    }

    void emitDivideForType(const Type &type) {
        if (type.isPointer()) {
            throw std::runtime_error("direct COFF backend does not support pointer division");
        }
        if (type.isUnsignedInteger()) {
            emitBytes({0x31, 0xD2});
            emitBytes({0xF7, 0xF1});
            return;
        }
        emitByte(0x99);
        emitBytes({0xF7, 0xF9});
    }

    void emitScaleIndex(int pointeeSize) {
        if (pointeeSize <= 1) {
            return;
        }
        if (pointeeSize >= -128 && pointeeSize <= 127) {
            emitBytes({0x6B, 0xC0, static_cast<std::uint8_t>(static_cast<std::int8_t>(pointeeSize))});
            return;
        }
        emitBytes({0x69, 0xC0});
        emitI32(pointeeSize);
    }

    void emitZeroResult(const Type &type) {
        if (type.isPointer() || type.valueSize() > 4) {
            emitBytes({0x48, 0x31, 0xC0});
            return;
        }
        emitBytes({0x31, 0xC0});
    }

    void emitNormalizeBoolInRax() {
        emitCompareZero(*Type::makeInt());
        emitSetCcToAl(ConditionCode::NotEqual);
        emitMovzxEaxAl();
    }

    void emitNegForType(const Type &type) {
        if (type.isPointer() || type.valueSize() > 4) {
            emitBytes({0x48, 0xF7, 0xD8});
            return;
        }
        emitBytes({0xF7, 0xD8});
    }

    void emitLoadFromAddress(const Type &type) {
        if (type.isPointer()) {
            emitLoadRegisterFromMemory(RegisterCode::Rax, RegisterCode::Rax, 8, false);
            return;
        }

        switch (type.valueSize()) {
        case 1:
            emitLoadByteLike(type);
            return;
        case 2:
            emitLoadWordLike(type);
            return;
        case 4:
            emitLoadRegisterFromMemory(RegisterCode::Rax, RegisterCode::Rax, 4, false);
            return;
        case 8:
            emitLoadRegisterFromMemory(RegisterCode::Rax, RegisterCode::Rax, 8, false);
            return;
        default:
            throw std::runtime_error("direct COFF backend encountered unsupported load size");
        }
    }

    void emitLoadByteLike(const Type &type) {
        if (type.kind == TypeKind::Bool || type.kind == TypeKind::UnsignedChar) {
            emitBytes({0x0F, 0xB6, 0x00});
            return;
        }
        emitBytes({0x0F, 0xBE, 0x00});
    }

    void emitLoadWordLike(const Type &type) {
        if (type.kind == TypeKind::UnsignedShort) {
            emitBytes({0x0F, 0xB7, 0x00});
            return;
        }
        emitBytes({0x0F, 0xBF, 0x00});
    }

    void emitStoreToLocalSlot(const Type &type, int stackOffset, RegisterCode source) {
        emitStoreToMemory(type, RegisterCode::Rbp, -stackOffset, source);
    }

    void emitStoreToLocalArrayElement(const Type &type, int baseOffset, int index, RegisterCode source) {
        const int elementOffset = index * type.valueSize();
        emitStoreToMemory(type, RegisterCode::Rbp, -(baseOffset - elementOffset), source);
    }

    void emitStoreToRspSlot(const Type &type, int offset) {
        emitStoreToMemory(type, RegisterCode::Rsp, offset, RegisterCode::Rax);
    }

    void emitLoadRegisterFromRspSlot(const Type &type, RegisterCode target, int offset) {
        emitLoadRegisterFromMemory(target, RegisterCode::Rsp, storageSize(type), false, offset);
    }

    void emitLoadRegisterFromStackArgument(const Type &type, RegisterCode target, int argumentIndex) {
        emitLoadRegisterFromMemory(target, RegisterCode::Rbp, storageSize(type), false, 16 + argumentIndex * 8);
    }

    void emitStoreToAddress(const Type &type, RegisterCode addressRegister, RegisterCode source) {
        emitStoreToMemory(type, addressRegister, 0, source);
    }

    void emitStoreToMemory(const Type &type, RegisterCode base, int displacement, RegisterCode source) {
        if (type.kind == TypeKind::Bool) {
            emitNormalizeBoolInRax();
        }
        const int bytes = storageSize(type);
        const std::uint8_t opcode = bytes == 1 ? 0x88 : 0x89;
        emitMemoryRegInstruction(opcode, base, displacement, source, bytes == 8);
    }

    void emitLoadRegisterFromMemory(RegisterCode target, RegisterCode base, int bytes, bool signedLoad, int displacement = 0) {
        if (bytes == 8) {
            emitRegMemoryInstruction(0x8B, target, base, displacement, true);
            return;
        }
        if (bytes == 4) {
            emitRegMemoryInstruction(0x8B, target, base, displacement, false);
            return;
        }
        if (bytes == 2) {
            emitExtendedLoad(target, base, displacement, signedLoad ? 0xBF : 0xB7);
            return;
        }
        if (bytes == 1) {
            emitExtendedLoad(target, base, displacement, signedLoad ? 0xBE : 0xB6);
            return;
        }
        throw std::runtime_error("direct COFF backend encountered unsupported load width");
    }

    void emitExtendedLoad(RegisterCode target, RegisterCode base, int displacement, std::uint8_t opcodeTail) {
        emitOptionalRex(false, needsRex(target), false, needsRex(base));
        emitByte(0x0F);
        emitByte(opcodeTail);
        emitAddressing(base, displacement, lowCode(target));
    }

    void emitLeaLocalAddress(int stackOffset) {
        emitBytes({0x48, 0x8D});
        emitAddressing(RegisterCode::Rbp, -stackOffset, lowCode(RegisterCode::Rax));
    }

    void emitLeaSymbolAddress(const std::string &symbolName) {
        emitBytes({0x48, 0x8D, 0x05});
        const std::uint32_t displacementOffset = currentTextOffset();
        emitU32(0);
        model.relocations.push_back(ObjectRelocation{
            TextSectionIndex,
            displacementOffset,
            symbolName,
            ObjectRelocationKind::Rel32});
    }

    int createLabel() {
        return nextLabelId++;
    }

    void bindLabel(int labelId) {
        labelOffsets[labelId] = currentTextOffset();
    }

    void emitJump(int labelId) {
        emitByte(0xE9);
        const std::uint32_t displacementOffset = currentTextOffset();
        emitU32(0);
        pendingJumps.push_back(PendingJumpPatch{displacementOffset, labelId});
    }

    void emitJe(int labelId) {
        emitBytes({0x0F, 0x84});
        const std::uint32_t displacementOffset = currentTextOffset();
        emitU32(0);
        pendingJumps.push_back(PendingJumpPatch{displacementOffset, labelId});
    }

    void emitJne(int labelId) {
        emitBytes({0x0F, 0x85});
        const std::uint32_t displacementOffset = currentTextOffset();
        emitU32(0);
        pendingJumps.push_back(PendingJumpPatch{displacementOffset, labelId});
    }

    void patchPendingJumps() {
        for (const auto &patch : pendingJumps) {
            const auto label = labelOffsets.find(patch.labelId);
            if (label == labelOffsets.end()) {
                throw std::runtime_error("direct COFF backend internal error: unresolved jump label");
            }
            const std::int32_t displacement =
                static_cast<std::int32_t>(label->second) - static_cast<std::int32_t>(patch.displacementOffset + 4);
            patchI32(text->bytes, patch.displacementOffset, displacement);
        }
    }

    void emitCallSymbol(const std::string &symbolName) {
        emitByte(0xE8);
        const std::uint32_t displacementOffset = currentTextOffset();
        emitU32(0);
        model.relocations.push_back(ObjectRelocation{
            TextSectionIndex,
            displacementOffset,
            symbolName,
            ObjectRelocationKind::Rel32});
    }

    void emitCallRegister(RegisterCode reg) {
        emitOptionalRex(false, false, false, needsRex(reg));
        emitByte(0xFF);
        emitByte(modRm(0x3, 0x2, lowCode(reg)));
    }

    void emitMovRegImm32(RegisterCode target, std::int32_t value) {
        if (target == RegisterCode::Rax) {
            emitBytes({0x48, 0xC7, 0xC0});
            emitI32(value);
            return;
        }
        emitOptionalRex(false, false, false, needsRex(target));
        emitByte(static_cast<std::uint8_t>(0xB8 + lowCode(target)));
        emitI32(value);
    }

    void emitMovRegReg(RegisterCode target, RegisterCode source, int bytes) {
        emitOptionalRex(bytes == 8, needsRex(source), false, needsRex(target));
        emitByte(0x89);
        emitByte(modRm(0x3, lowCode(source), lowCode(target)));
    }

    void emitPush(RegisterCode reg) {
        if (needsRex(reg)) {
            emitByte(0x41);
        }
        emitByte(static_cast<std::uint8_t>(0x50 + lowCode(reg)));
    }

    void emitPop(RegisterCode reg) {
        if (needsRex(reg)) {
            emitByte(0x41);
        }
        emitByte(static_cast<std::uint8_t>(0x58 + lowCode(reg)));
    }

    void emitAdjustRsp(bool add, int amount) {
        const std::uint8_t opcode = add ? 0xC4 : 0xEC;
        if (amount >= 0 && amount <= 127) {
            emitBytes({0x48, 0x83, opcode, static_cast<std::uint8_t>(amount)});
            return;
        }
        emitBytes({0x48, 0x81, opcode});
        emitI32(amount);
    }

    void emitSetCcToAl(ConditionCode code) {
        emitByte(0x0F);
        emitByte(static_cast<std::uint8_t>(code));
        emitByte(0xC0);
    }

    void emitMovzxEaxAl() {
        emitBytes({0x0F, 0xB6, 0xC0});
    }

    void emitMemoryRegInstruction(
        std::uint8_t opcode,
        RegisterCode base,
        int displacement,
        RegisterCode source,
        bool wide) {
        emitOptionalRex(wide, needsRex(source), false, needsRex(base));
        emitByte(opcode);
        emitAddressing(base, displacement, lowCode(source));
    }

    void emitRegMemoryInstruction(
        std::uint8_t opcode,
        RegisterCode target,
        RegisterCode base,
        int displacement,
        bool wide) {
        emitOptionalRex(wide, needsRex(target), false, needsRex(base));
        emitByte(opcode);
        emitAddressing(base, displacement, lowCode(target));
    }

    void emitAddressing(RegisterCode base, int displacement, std::uint8_t regField) {
        const std::uint8_t rm = lowCode(base);
        const bool hasSib = rm == lowCode(RegisterCode::Rsp);
        if (displacement >= -128 && displacement <= 127) {
            emitByte(modRm(0x1, regField, rm));
            if (hasSib) {
                emitByte(0x24);
            }
            emitByte(static_cast<std::uint8_t>(static_cast<std::int8_t>(displacement)));
            return;
        }

        emitByte(modRm(0x2, regField, rm));
        if (hasSib) {
            emitByte(0x24);
        }
        emitI32(displacement);
    }

    void emitOptionalRex(bool wide, bool reg, bool index, bool base) {
        const std::uint8_t rex =
            static_cast<std::uint8_t>(0x40 | (wide ? 0x08 : 0x00) | (reg ? 0x04 : 0x00) |
                                      (index ? 0x02 : 0x00) | (base ? 0x01 : 0x00));
        if (rex != 0x40) {
            emitByte(rex);
        }
    }

    static std::uint8_t lowCode(RegisterCode reg) {
        return static_cast<std::uint8_t>(reg) & 0x7;
    }

    static bool needsRex(RegisterCode reg) {
        return static_cast<std::uint8_t>(reg) >= 8;
    }

    static std::uint8_t modRm(std::uint8_t mod, std::uint8_t reg, std::uint8_t rm) {
        return static_cast<std::uint8_t>((mod << 6) | ((reg & 0x7) << 3) | (rm & 0x7));
    }

    static RegisterCode parameterRegister(std::size_t index) {
        switch (index) {
        case 0:
            return RegisterCode::Rcx;
        case 1:
            return RegisterCode::Rdx;
        case 2:
            return RegisterCode::R8;
        case 3:
            return RegisterCode::R9;
        default:
            throw std::runtime_error("direct COFF backend internal error: invalid parameter register");
        }
    }

    static int storageSize(const Type &type) {
        if (type.isPointer()) {
            return 8;
        }
        return type.valueSize();
    }

    static int valueSize(const Type &type) {
        return type.valueSize();
    }

    std::uint32_t appendSectionBytes(ObjectSection &section, std::uint32_t size, int alignment) {
        const std::uint32_t offset = alignSection(section, alignment);
        section.bytes.resize(offset + size, 0);
        return offset;
    }

    std::uint32_t alignSection(ObjectSection &section, int alignment) {
        const std::uint32_t mask = static_cast<std::uint32_t>(alignment - 1);
        if (section.isBss) {
            const std::uint32_t aligned = (section.virtualSize + mask) & ~mask;
            section.virtualSize = aligned;
            return aligned;
        }

        const std::uint32_t size = static_cast<std::uint32_t>(section.bytes.size());
        const std::uint32_t aligned = (size + mask) & ~mask;
        if (aligned > size) {
            section.bytes.resize(aligned, 0);
        }
        return aligned;
    }

    void addOrUpdateSymbol(
        const std::string &name,
        int sectionIndex,
        std::uint32_t value,
        ObjectSymbolBinding binding) {
        const auto found = symbolIndexes.find(name);
        if (found == symbolIndexes.end()) {
            symbolIndexes.emplace(name, model.symbols.size());
            model.symbols.push_back(ObjectSymbol{name, sectionIndex, value, binding});
            return;
        }

        auto &symbol = model.symbols[found->second];
        symbol.sectionIndex = sectionIndex;
        symbol.value = value;
        symbol.binding = binding;
    }

    std::uint32_t currentTextOffset() const {
        return static_cast<std::uint32_t>(text->bytes.size());
    }

    void emitByte(std::uint8_t byte) {
        text->bytes.push_back(byte);
    }

    void emitBytes(std::initializer_list<std::uint8_t> bytes) {
        text->bytes.insert(text->bytes.end(), bytes.begin(), bytes.end());
    }

    void emitU32(std::uint32_t value) {
        emitByte(static_cast<std::uint8_t>(value & 0xFF));
        emitByte(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        emitByte(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        emitByte(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    }

    void emitI32(std::int32_t value) {
        emitU32(static_cast<std::uint32_t>(value));
    }

    static void patchI32(std::vector<std::uint8_t> &bytes, std::uint32_t offset, std::int32_t value) {
        const std::uint32_t encoded = static_cast<std::uint32_t>(value);
        bytes[offset + 0] = static_cast<std::uint8_t>(encoded & 0xFF);
        bytes[offset + 1] = static_cast<std::uint8_t>((encoded >> 8) & 0xFF);
        bytes[offset + 2] = static_cast<std::uint8_t>((encoded >> 16) & 0xFF);
        bytes[offset + 3] = static_cast<std::uint8_t>((encoded >> 24) & 0xFF);
    }

    static void patchLittleEndian(std::vector<std::uint8_t> &bytes, std::uint32_t offset, long long value, int width) {
        for (int i = 0; i < width; ++i) {
            bytes[offset + static_cast<std::uint32_t>(i)] =
                static_cast<std::uint8_t>((static_cast<unsigned long long>(value) >> (i * 8)) & 0xFF);
        }
    }

    ObjectFileModel model;
    ObjectSection *text = nullptr;
    ObjectSection *data = nullptr;
    ObjectSection *rdata = nullptr;
    ObjectSection *bss = nullptr;
    std::unordered_map<std::string, std::size_t> symbolIndexes;
    std::unordered_map<std::string, std::string> stringSymbols;
    std::unordered_map<int, std::uint32_t> labelOffsets;
    std::vector<PendingJumpPatch> pendingJumps;
    std::vector<int> loopBreakLabels;
    std::vector<int> loopContinueLabels;
    int nextLabelId = 1;
    int currentReturnLabel = 0;
    int nextStringId = 0;
};

} // namespace

ObjectFileModel WindowsObjectEmitter::emit(const Program &program, bool emitEntryPoint) {
    WindowsObjectModelEmitter emitter;
    return emitter.emit(program, emitEntryPoint);
}

ObjectSection WindowsObjectEmitter::makeTextSection() {
    return makeTextSectionValue();
}

ObjectSection WindowsObjectEmitter::makeEmptyDataSection() {
    return makeEmptyDataSectionValue();
}

ObjectSection WindowsObjectEmitter::makeEmptyReadOnlyDataSection() {
    return makeEmptyReadOnlyDataSectionValue();
}

ObjectSection WindowsObjectEmitter::makeEmptyBssSection() {
    return makeEmptyBssSectionValue();
}
