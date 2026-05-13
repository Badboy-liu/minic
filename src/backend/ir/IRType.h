#pragma once
#include <memory>
#include <string>
#include <vector>

namespace ir {

enum class IRTypeKind {
    Void,
    Int1,
    Int8,
    Int16,
    Int32,
    Int64,
    Float,
    Double,
    Pointer,
    Array,
    Struct,
    Function,
};

struct IRType {
    IRTypeKind kind;
    int bitWidth = 0;                    // 整数/浮点位宽
    int arraySize = 0;                   // 数组元素数
    std::shared_ptr<IRType> elementType; // 指针/数组的元素类型
    std::vector<IRType> structMembers;   // 结构体成员类型
    std::shared_ptr<IRType> returnType;  // 函数返回类型
    std::vector<IRType> paramTypes;      // 函数参数类型
    bool isVarArg = false;               // 函数是否变参

    IRType() : kind(IRTypeKind::Void) {}
    explicit IRType(IRTypeKind k, int bw = 0) : kind(k), bitWidth(bw) {}

    bool isVoid() const { return kind == IRTypeKind::Void; }
    bool isInt() const {
        return kind == IRTypeKind::Int1 || kind == IRTypeKind::Int8 ||
               kind == IRTypeKind::Int16 || kind == IRTypeKind::Int32 ||
               kind == IRTypeKind::Int64;
    }
    bool isFloat() const { return kind == IRTypeKind::Float || kind == IRTypeKind::Double; }
    bool isPointer() const { return kind == IRTypeKind::Pointer; }
    bool isArray() const { return kind == IRTypeKind::Array; }
    bool isStruct() const { return kind == IRTypeKind::Struct; }
    bool isFunction() const { return kind == IRTypeKind::Function; }

    // 返回值的字节大小
    int byteSize() const {
        return bitWidth / 8;
    }

    // 工厂方法
    static IRType voidTy() { return IRType(IRTypeKind::Void); }
    static IRType i1() { return IRType(IRTypeKind::Int1, 1); }
    static IRType i8() { return IRType(IRTypeKind::Int8, 8); }
    static IRType i16() { return IRType(IRTypeKind::Int16, 16); }
    static IRType i32() { return IRType(IRTypeKind::Int32, 32); }
    static IRType i64() { return IRType(IRTypeKind::Int64, 64); }
    static IRType f32() { return IRType(IRTypeKind::Float, 32); }
    static IRType f64() { return IRType(IRTypeKind::Double, 64); }

    static IRType ptr(IRType elem) {
        IRType t(IRTypeKind::Pointer, 64);
        t.elementType = std::make_shared<IRType>(std::move(elem));
        return t;
    }

    static IRType array(IRType elem, int size) {
        IRType t(IRTypeKind::Array, elem.bitWidth * size);
        t.arraySize = size;
        t.elementType = std::make_shared<IRType>(std::move(elem));
        return t;
    }

    static IRType structTy(std::vector<IRType> members) {
        int totalBits = 0;
        for (auto &m : members) totalBits += m.bitWidth;
        IRType t(IRTypeKind::Struct, totalBits);
        t.structMembers = std::move(members);
        return t;
    }

    static IRType function(IRType ret, std::vector<IRType> params, bool varArg = false) {
        IRType t(IRTypeKind::Function);
        t.returnType = std::make_shared<IRType>(std::move(ret));
        t.paramTypes = std::move(params);
        t.isVarArg = varArg;
        return t;
    }

    std::string toString() const;
};

} // namespace ir
