#pragma once
#include "IRType.h"
#include <string>
#include <vector>

namespace ir {

class IRInstruction;
class IRBasicBlock;

// 使用记录：哪个指令的第几个操作数引用了这个值
struct IRUse {
    IRInstruction *user;
    int operandIndex;
};

enum class IRValueKind {
    Constant,
    Instruction,
    Argument,
    GlobalVariable,
    BasicBlock,
};

class IRValue {
public:
    IRValueKind valueKind;
    IRType type;
    std::string name;
    std::vector<IRUse> uses;

    IRValue(IRValueKind k, IRType t, std::string n = "")
        : valueKind(k), type(std::move(t)), name(std::move(n)) {}
    virtual ~IRValue() = default;

    bool isConstant() const { return valueKind == IRValueKind::Constant; }
    bool isInstruction() const { return valueKind == IRValueKind::Instruction; }
    bool isArgument() const { return valueKind == IRValueKind::Argument; }
    bool isGlobal() const { return valueKind == IRValueKind::GlobalVariable; }
    bool isBasicBlock() const { return valueKind == IRValueKind::BasicBlock; }

    void addUse(IRInstruction *user, int idx);
    void replaceAllUsesWith(IRValue *newValue);

    virtual std::string toString() const;
};

// 常量整数
class IRConstantInt : public IRValue {
public:
    int64_t value;

    IRConstantInt(int64_t v, int bitWidth = 32)
        : IRValue(IRValueKind::Constant, IRType(IRTypeKind::Int32, bitWidth)), value(v) {
        if (bitWidth == 1) type = IRType::i1();
        else if (bitWidth == 8) type = IRType::i8();
        else if (bitWidth == 16) type = IRType::i16();
        else if (bitWidth == 64) type = IRType::i64();
    }

    std::string toString() const override;
};

// 常量浮点
class IRConstantFloat : public IRValue {
public:
    double value;

    IRConstantFloat(double v, bool isDouble = true)
        : IRValue(IRValueKind::Constant, isDouble ? IRType::f64() : IRType::f32()), value(v) {}

    std::string toString() const override;
};

// null 指针
class IRConstantNull : public IRValue {
public:
    IRConstantNull() : IRValue(IRValueKind::Constant, IRType::ptr(IRType::i8())) {}
    std::string toString() const override { return "null"; }
};

// 函数参数
class IRArgument : public IRValue {
public:
    int argIndex;

    IRArgument(IRType t, int idx, std::string name = "")
        : IRValue(IRValueKind::Argument, std::move(t), std::move(name)), argIndex(idx) {}

    std::string toString() const override;
};

// 全局变量
class IRGlobalVariable : public IRValue {
public:
    bool isConstant;
    std::string initializer; // 初始值的文本表示

    IRGlobalVariable(IRType elemType, bool isConst, std::string name)
        : IRValue(IRValueKind::GlobalVariable, IRType::ptr(std::move(elemType)), std::move(name)),
          isConstant(isConst) {}
};

} // namespace ir
