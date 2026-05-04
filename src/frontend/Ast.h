#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class TypeKind {
    Struct,
    Char,
    Short,
    Int,
    Long,
    LongLong,
    Void,
    Function,
    Pointer,
    Array
};

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct StructMember {
    std::string name;
    TypePtr type;
    int offset = 0;
};

struct Type {
    TypeKind kind;
    TypePtr elementType;
    int arrayLength = 0;
    std::vector<TypePtr> parameterTypes;
    std::string structName;
    std::vector<StructMember> members;
    int structSize = 0;
    int structAlignment = 1;

    static int alignTo(int value, int alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

    static TypePtr makeStruct(std::string name, std::vector<StructMember> membersValue) {
        int size = 0;
        int alignment = 1;
        for (auto &member : membersValue) {
            const int memberAlignment = member.type->alignment();
            size = alignTo(size, memberAlignment);
            member.offset = size;
            size += member.type->valueSize();
            if (memberAlignment > alignment) {
                alignment = memberAlignment;
            }
        }
        size = alignTo(size, alignment);
        return std::make_shared<Type>(Type{
            TypeKind::Struct,
            nullptr,
            0,
            {},
            std::move(name),
            std::move(membersValue),
            size,
            alignment});
    }

    static TypePtr makeInt() {
        return std::make_shared<Type>(Type{TypeKind::Int, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeChar() {
        return std::make_shared<Type>(Type{TypeKind::Char, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeShort() {
        return std::make_shared<Type>(Type{TypeKind::Short, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeLong() {
        return std::make_shared<Type>(Type{TypeKind::Long, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeLongLong() {
        return std::make_shared<Type>(Type{TypeKind::LongLong, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeVoid() {
        return std::make_shared<Type>(Type{TypeKind::Void, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeFunction(TypePtr returnType, std::vector<TypePtr> parameterTypesValue) {
        return std::make_shared<Type>(Type{
            TypeKind::Function,
            std::move(returnType),
            0,
            std::move(parameterTypesValue),
            "",
            {},
            0,
            1});
    }

    static TypePtr makePointer(TypePtr element) {
        return std::make_shared<Type>(Type{TypeKind::Pointer, std::move(element), 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeArray(TypePtr element, int length) {
        return std::make_shared<Type>(Type{TypeKind::Array, std::move(element), length, {}, "", {}, 0, 1});
    }

    bool equals(const Type &other) const {
        if (kind != other.kind || arrayLength != other.arrayLength || parameterTypes.size() != other.parameterTypes.size()) {
            return false;
        }
        if (kind == TypeKind::Struct) {
            return structName == other.structName;
        }
        for (std::size_t i = 0; i < parameterTypes.size(); ++i) {
            if (!parameterTypes[i] || !other.parameterTypes[i]) {
                if (parameterTypes[i] || other.parameterTypes[i]) {
                    return false;
                }
                continue;
            }
            if (!parameterTypes[i]->equals(*other.parameterTypes[i])) {
                return false;
            }
        }
        if (!elementType && !other.elementType) {
            return true;
        }
        if (!elementType || !other.elementType) {
            return false;
        }
        return elementType->equals(*other.elementType);
    }

    bool isInteger() const {
        return kind == TypeKind::Char ||
            kind == TypeKind::Short ||
            kind == TypeKind::Int ||
            kind == TypeKind::Long ||
            kind == TypeKind::LongLong;
    }

    bool isStruct() const {
        return kind == TypeKind::Struct;
    }

    bool isVoid() const {
        return kind == TypeKind::Void;
    }

    bool isPointer() const {
        return kind == TypeKind::Pointer;
    }

    bool isFunction() const {
        return kind == TypeKind::Function;
    }

    bool isArray() const {
        return kind == TypeKind::Array;
    }

    bool isScalar() const {
        return isInteger() || isPointer();
    }

    const StructMember *findMember(const std::string &name) const {
        for (const auto &member : members) {
            if (member.name == name) {
                return &member;
            }
        }
        return nullptr;
    }

    int valueSize() const {
        switch (kind) {
        case TypeKind::Struct:
            return structSize;
        case TypeKind::Char:
            return 1;
        case TypeKind::Short:
            return 2;
        case TypeKind::Int:
        case TypeKind::Long:
            return 4;
        case TypeKind::LongLong:
            return 8;
        case TypeKind::Void:
        case TypeKind::Function:
            return 0;
        case TypeKind::Pointer:
            return 8;
        case TypeKind::Array:
            return elementType->valueSize() * arrayLength;
        }
        return 0;
    }

    int alignment() const {
        switch (kind) {
        case TypeKind::Struct:
            return structAlignment;
        case TypeKind::Array:
            return elementType->alignment();
        case TypeKind::Pointer:
        case TypeKind::LongLong:
            return 8;
        case TypeKind::Short:
            return 2;
        case TypeKind::Char:
            return 1;
        case TypeKind::Int:
        case TypeKind::Long:
            return 4;
        case TypeKind::Void:
        case TypeKind::Function:
            return 1;
        }
        return 1;
    }

    int storageSize() const {
        if (kind == TypeKind::Array || kind == TypeKind::Struct) {
            return valueSize();
        }
        return valueSize() <= 4 ? 8 : valueSize();
    }

    TypePtr decay() const {
        if (kind == TypeKind::Array) {
            return makePointer(elementType);
        }
        if (kind == TypeKind::Function) {
            return makePointer(std::make_shared<Type>(*this));
        }
        return std::make_shared<Type>(*this);
    }
};

enum class UnaryOp {
    Plus,
    Minus,
    LogicalNot,
    AddressOf,
    Dereference
};

enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
    Equal,
    NotEqual,
    LogicalAnd,
    LogicalOr,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};

struct Expr {
    enum class Kind {
        Number,
        String,
        Variable,
        Unary,
        Binary,
        InitializerList,
        Assign,
        Call,
        Index,
        MemberAccess
    };

    explicit Expr(Kind kindValue) : kind(kindValue) {}
    virtual ~Expr() = default;

    Kind kind;
    TypePtr type;
    bool isLValue = false;
};

struct NumberExpr : Expr {
    explicit NumberExpr(int valueValue) : Expr(Kind::Number), value(valueValue) {}
    int value;
};

struct StringExpr : Expr {
    explicit StringExpr(std::string valueValue)
        : Expr(Kind::String), value(std::move(valueValue)) {}

    std::string value;
    std::string label;
};

struct VariableExpr : Expr {
    explicit VariableExpr(std::string nameValue)
        : Expr(Kind::Variable), name(std::move(nameValue)), stackOffset(0) {}

    std::string name;
    int stackOffset;
    bool isGlobal = false;
    std::string symbolName;
};

struct UnaryExpr : Expr {
    UnaryExpr(UnaryOp opValue, std::unique_ptr<Expr> operandValue)
        : Expr(Kind::Unary), op(opValue), operand(std::move(operandValue)) {}

    UnaryOp op;
    std::unique_ptr<Expr> operand;
};

struct BinaryExpr : Expr {
    BinaryExpr(BinaryOp opValue, std::unique_ptr<Expr> leftValue, std::unique_ptr<Expr> rightValue)
        : Expr(Kind::Binary), op(opValue), left(std::move(leftValue)), right(std::move(rightValue)) {}

    BinaryOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct AssignExpr : Expr {
    AssignExpr(std::unique_ptr<Expr> targetValue, std::unique_ptr<Expr> valueValue)
        : Expr(Kind::Assign), target(std::move(targetValue)), value(std::move(valueValue)) {}

    std::unique_ptr<Expr> target;
    std::unique_ptr<Expr> value;
};

struct InitializerListExpr : Expr {
    explicit InitializerListExpr(std::vector<std::unique_ptr<Expr>> elementsValue)
        : Expr(Kind::InitializerList), elements(std::move(elementsValue)) {}

    std::vector<std::unique_ptr<Expr>> elements;
};

struct CallExpr : Expr {
    CallExpr(std::unique_ptr<Expr> calleeValue, std::vector<std::unique_ptr<Expr>> argumentsValue)
        : Expr(Kind::Call), callee(std::move(calleeValue)), arguments(std::move(argumentsValue)) {}

    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> arguments;
    std::vector<TypePtr> parameterTypes;
};

struct IndexExpr : Expr {
    IndexExpr(std::unique_ptr<Expr> baseValue, std::unique_ptr<Expr> indexValue)
        : Expr(Kind::Index), base(std::move(baseValue)), index(std::move(indexValue)) {}

    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
};

struct MemberAccessExpr : Expr {
    MemberAccessExpr(std::unique_ptr<Expr> baseValue, std::string memberNameValue)
        : Expr(Kind::MemberAccess), base(std::move(baseValue)), memberName(std::move(memberNameValue)) {}

    std::unique_ptr<Expr> base;
    std::string memberName;
    int memberOffset = 0;
};

struct Stmt {
    enum class Kind {
        Return,
        Expr,
        Decl,
        Block,
        If,
        While,
        For,
        Break,
        Continue
    };

    explicit Stmt(Kind kindValue) : kind(kindValue) {}
    virtual ~Stmt() = default;

    Kind kind;
};

struct ReturnStmt : Stmt {
    explicit ReturnStmt(std::unique_ptr<Expr> exprValue)
        : Stmt(Kind::Return), expr(std::move(exprValue)) {}

    std::unique_ptr<Expr> expr;
};

struct ExprStmt : Stmt {
    explicit ExprStmt(std::unique_ptr<Expr> exprValue)
        : Stmt(Kind::Expr), expr(std::move(exprValue)) {}

    std::unique_ptr<Expr> expr;
};

struct DeclStmt : Stmt {
    DeclStmt(TypePtr typeValue, std::string nameValue, std::unique_ptr<Expr> initValue)
        : Stmt(Kind::Decl), type(std::move(typeValue)), name(std::move(nameValue)), init(std::move(initValue)) {}

    TypePtr type;
    std::string name;
    std::unique_ptr<Expr> init;
    int stackOffset = 0;
};

struct BlockStmt : Stmt {
    BlockStmt() : Stmt(Kind::Block) {}
    std::vector<std::unique_ptr<Stmt>> statements;
};

struct IfStmt : Stmt {
    IfStmt(
        std::unique_ptr<Expr> conditionValue,
        std::unique_ptr<Stmt> thenBranchValue,
        std::unique_ptr<Stmt> elseBranchValue)
        : Stmt(Kind::If),
          condition(std::move(conditionValue)),
          thenBranch(std::move(thenBranchValue)),
          elseBranch(std::move(elseBranchValue)) {}

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
};

struct WhileStmt : Stmt {
    WhileStmt(std::unique_ptr<Expr> conditionValue, std::unique_ptr<Stmt> bodyValue)
        : Stmt(Kind::While), condition(std::move(conditionValue)), body(std::move(bodyValue)) {}

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};

struct ForStmt : Stmt {
    ForStmt(
        std::unique_ptr<Stmt> initValue,
        std::unique_ptr<Expr> conditionValue,
        std::unique_ptr<Expr> updateValue,
        std::unique_ptr<Stmt> bodyValue)
        : Stmt(Kind::For),
          init(std::move(initValue)),
          condition(std::move(conditionValue)),
          update(std::move(updateValue)),
          body(std::move(bodyValue)) {}

    std::unique_ptr<Stmt> init;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> update;
    std::unique_ptr<Stmt> body;
};

struct BreakStmt : Stmt {
    BreakStmt() : Stmt(Kind::Break) {}
};

struct ContinueStmt : Stmt {
    ContinueStmt() : Stmt(Kind::Continue) {}
};

struct Parameter {
    TypePtr type;
    std::string name;
    int stackOffset = 0;
};

struct Function {
    std::string name;
    TypePtr returnType;
    std::vector<Parameter> parameters;
    std::unique_ptr<BlockStmt> body;
    int stackSize = 0;

    bool isDeclaration() const {
        return body == nullptr;
    }
};

struct GlobalVar {
    TypePtr type;
    std::string name;
    std::unique_ptr<Expr> init;
    bool isExternStorage = false;
    bool isExternal = false;
    bool emitStorage = true;
    bool isBss = false;
    std::string symbolName;
};

struct Program {
    std::vector<Function> functions;
    std::vector<GlobalVar> globals;
};
