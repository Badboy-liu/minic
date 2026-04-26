#pragma once

#include <memory>
#include <string>
#include <vector>

enum class TypeKind {
    Int,
    Void,
    Pointer,
    Array
};

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type {
    TypeKind kind;
    TypePtr elementType;
    int arrayLength = 0;

    static TypePtr makeInt() {
        return std::make_shared<Type>(Type{TypeKind::Int, nullptr, 0});
    }

    static TypePtr makeVoid() {
        return std::make_shared<Type>(Type{TypeKind::Void, nullptr, 0});
    }

    static TypePtr makePointer(TypePtr element) {
        return std::make_shared<Type>(Type{TypeKind::Pointer, std::move(element), 0});
    }

    static TypePtr makeArray(TypePtr element, int length) {
        return std::make_shared<Type>(Type{TypeKind::Array, std::move(element), length});
    }

    bool equals(const Type &other) const {
        if (kind != other.kind || arrayLength != other.arrayLength) {
            return false;
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
        return kind == TypeKind::Int;
    }

    bool isVoid() const {
        return kind == TypeKind::Void;
    }

    bool isPointer() const {
        return kind == TypeKind::Pointer;
    }

    bool isArray() const {
        return kind == TypeKind::Array;
    }

    bool isScalar() const {
        return isInteger() || isPointer();
    }

    int valueSize() const {
        switch (kind) {
        case TypeKind::Int:
            return 4;
        case TypeKind::Void:
            return 0;
        case TypeKind::Pointer:
            return 8;
        case TypeKind::Array:
            return elementType->valueSize() * arrayLength;
        }
        return 0;
    }

    int storageSize() const {
        if (kind == TypeKind::Array) {
            return valueSize();
        }
        return valueSize() <= 4 ? 8 : valueSize();
    }

    TypePtr decay() const {
        if (kind == TypeKind::Array) {
            return makePointer(elementType);
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
        Variable,
        Unary,
        Binary,
        Assign,
        Call,
        Index
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

struct VariableExpr : Expr {
    explicit VariableExpr(std::string nameValue)
        : Expr(Kind::Variable), name(std::move(nameValue)), stackOffset(0) {}

    std::string name;
    int stackOffset;
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

struct CallExpr : Expr {
    CallExpr(std::string calleeValue, std::vector<std::unique_ptr<Expr>> argumentsValue)
        : Expr(Kind::Call), callee(std::move(calleeValue)), arguments(std::move(argumentsValue)) {}

    std::string callee;
    std::vector<std::unique_ptr<Expr>> arguments;
    std::vector<TypePtr> parameterTypes;
};

struct IndexExpr : Expr {
    IndexExpr(std::unique_ptr<Expr> baseValue, std::unique_ptr<Expr> indexValue)
        : Expr(Kind::Index), base(std::move(baseValue)), index(std::move(indexValue)) {}

    std::unique_ptr<Expr> base;
    std::unique_ptr<Expr> index;
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

struct Program {
    std::vector<Function> functions;
};
