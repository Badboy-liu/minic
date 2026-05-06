#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class TypeKind {
    Struct,
    Union,
    Char,
    Short,
    Int,
    Long,
    LongLong,
    Float,
    Double,
    Bool,
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
    int bitWidth = 0;       // 0 表示非位域
    int bitOffset = 0;      // 位域在包含单元中的位偏移
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
    bool isUnsigned = false;
    bool isConst = false;
    bool isVolatile = false;
    bool isVariadic = false;
    bool isRestrict = false;
    bool isAtomic = false;
    int alignAs = 0;
    bool isVla = false;     // 变长数组

    static int alignTo(int value, int alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }

    static TypePtr makeStruct(std::string name, std::vector<StructMember> membersValue) {
        int size = 0;
        int alignment = 1;
        int bitFieldCursor = 0;        // 当前位域组中的可用位数
        int bitFieldContainerOffset = 0; // 当前位域容器的字节偏移
        for (auto &member : membersValue) {
            const int memberAlignment = member.type->alignment();
            if (memberAlignment > alignment) {
                alignment = memberAlignment;
            }
            if (member.bitWidth > 0) {
                // 位域成员
                const int containerSize = member.type->valueSize() * 8; // 容器位数
                if (bitFieldCursor < member.bitWidth) {
                    // 需要新的容器
                    size = alignTo(size, memberAlignment);
                    bitFieldContainerOffset = size;
                    member.offset = size;
                    member.bitOffset = 0;
                    bitFieldCursor = containerSize - member.bitWidth;
                    size += member.type->valueSize();
                } else {
                    // 填充到当前容器
                    member.offset = bitFieldContainerOffset;
                    member.bitOffset = containerSize - bitFieldCursor;
                    bitFieldCursor -= member.bitWidth;
                }
            } else {
                // 非位域成员：结束当前位域组
                bitFieldCursor = 0;
                size = alignTo(size, memberAlignment);
                member.offset = size;
                size += member.type->valueSize();
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

    // 联合体：所有成员偏移为 0，大小 = max(成员大小)
    static TypePtr makeUnion(std::string name, std::vector<StructMember> membersValue) {
        int size = 0;
        int alignment = 1;
        for (auto &member : membersValue) {
            const int memberAlignment = member.type->alignment();
            member.offset = 0;  // 联合体所有成员偏移为 0
            const int memberSize = member.type->valueSize();
            if (memberSize > size) {
                size = memberSize;
            }
            if (memberAlignment > alignment) {
                alignment = memberAlignment;
            }
        }
        size = alignTo(size, alignment);
        return std::make_shared<Type>(Type{
            TypeKind::Union,
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

    static TypePtr makeULong() {
        return std::make_shared<Type>(Type{TypeKind::Long, nullptr, 0, {}, "", {}, 0, 1, true});
    }

    static TypePtr makeLongLong() {
        return std::make_shared<Type>(Type{TypeKind::LongLong, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeFloat() {
        return std::make_shared<Type>(Type{TypeKind::Float, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeDouble() {
        return std::make_shared<Type>(Type{TypeKind::Double, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeVoid() {
        return std::make_shared<Type>(Type{TypeKind::Void, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeBool() {
        return std::make_shared<Type>(Type{TypeKind::Bool, nullptr, 0, {}, "", {}, 0, 1});
    }

    static TypePtr makeFunction(TypePtr returnType, std::vector<TypePtr> parameterTypesValue, bool isVariadicValue = false) {
        auto t = std::make_shared<Type>(Type{
            TypeKind::Function,
            std::move(returnType),
            0,
            std::move(parameterTypesValue),
            "",
            {},
            0,
            1});
        t->isVariadic = isVariadicValue;
        return t;
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
        if (isUnsigned != other.isUnsigned) {
            return false;
        }
        if (isVariadic != other.isVariadic) {
            return false;
        }
        if (kind == TypeKind::Struct || kind == TypeKind::Union) {
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
            kind == TypeKind::LongLong ||
            kind == TypeKind::Bool;
    }

    bool isFloatingPoint() const {
        return kind == TypeKind::Float || kind == TypeKind::Double;
    }

    bool isStruct() const {
        return kind == TypeKind::Struct || kind == TypeKind::Union;
    }

    bool isUnion() const {
        return kind == TypeKind::Union;
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
        return isInteger() || isPointer() || isFloatingPoint();
    }

    const StructMember *findMember(const std::string &name) const {
        for (const auto &member : members) {
            if (member.name == name) {
                return &member;
            }
        }
        return nullptr;
    }

    // 递归查找成员（支持匿名结构体/联合体成员）
    // 返回找到的成员指针，outOffset 为累积偏移量
    const StructMember *findMemberRecursive(const std::string &name, int baseOffset, int &outOffset) const {
        for (const auto &member : members) {
            if (member.name == name) {
                outOffset = member.offset + baseOffset;
                return &member;
            }
            // 匿名成员：name 为空且为 struct/union 类型，递归搜索
            if (member.name.empty() && member.type && (member.type->isStruct() || member.type->isUnion())) {
                const StructMember *found = member.type->findMemberRecursive(name, baseOffset + member.offset, outOffset);
                if (found) {
                    return found;
                }
            }
        }
        return nullptr;
    }

    int valueSize() const {
        switch (kind) {
        case TypeKind::Struct:
        case TypeKind::Union:
            return structSize;
        case TypeKind::Char:
        case TypeKind::Bool:
            return 1;
        case TypeKind::Short:
            return 2;
        case TypeKind::Int:
            return 4;
        case TypeKind::Long:
        case TypeKind::LongLong:
        case TypeKind::Double:
            return 8;
        case TypeKind::Float:
            return 4;
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
        if (alignAs > 0) return alignAs;
        switch (kind) {
        case TypeKind::Struct:
        case TypeKind::Union:
            return structAlignment;
        case TypeKind::Array:
            return elementType->alignment();
        case TypeKind::Pointer:
        case TypeKind::Long:
        case TypeKind::LongLong:
        case TypeKind::Double:
            return 8;
        case TypeKind::Short:
            return 2;
        case TypeKind::Char:
            return 1;
        case TypeKind::Int:
        case TypeKind::Float:
            return 4;
        case TypeKind::Void:
        case TypeKind::Function:
            return 1;
        }
        return 1;
    }

    int storageSize() const {
        if (kind == TypeKind::Array || kind == TypeKind::Struct || kind == TypeKind::Union) {
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
    Dereference,
    BitwiseNot,
    PreIncrement,
    PreDecrement,
    PostIncrement,
    PostDecrement,
    Sizeof,
    Alignof
};

enum class BinaryOp {
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Equal,
    NotEqual,
    LogicalAnd,
    LogicalOr,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    ShiftLeft,
    ShiftRight,
    BitwiseAnd,
    BitwiseXor,
    BitwiseOr,
    Comma
};

struct Expr {
    enum class Kind {
        Number,
        FloatLiteral,
        String,
        Variable,
        Unary,
        Binary,
        InitializerList,
        Assign,
        Call,
        Index,
        MemberAccess,
        Ternary,
        Cast,
        BuiltinVaStart,
        BuiltinVaArg,
        BuiltinVaEnd,
        Generic,
        CompoundLiteral,
        StmtExpr  // GNU 语句表达式 ({ stmt; ...; expr; })，用于内联多语句函数
    };

    explicit Expr(Kind kindValue) : kind(kindValue) {}
    virtual ~Expr() = default;

    Kind kind;
    TypePtr type;
    bool isLValue = false;
    int line = 0;
    int column = 0;
};

struct NumberExpr : Expr {
    explicit NumberExpr(long long valueValue) : Expr(Kind::Number), value(valueValue) {}
    long long value;
};

struct FloatLiteralExpr : Expr {
    explicit FloatLiteralExpr(double valueValue) : Expr(Kind::FloatLiteral), value(valueValue) {}
    double value;
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
    bool isPostfix = false;
    TypePtr sizeofType;
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
    bool isCompound = false;
    BinaryOp compoundOp = BinaryOp::Add;
};

// 指定初始化器的指示符
struct Designator {
    enum Kind { Field, Index };
    Kind kind;
    std::string fieldName;   // .field 使用
    int index = 0;           // [index] 使用
};

struct InitializerListExpr : Expr {
    explicit InitializerListExpr(std::vector<std::unique_ptr<Expr>> elementsValue)
        : Expr(Kind::InitializerList), elements(std::move(elementsValue)) {}

    std::vector<std::unique_ptr<Expr>> elements;
    std::vector<std::vector<Designator>> designators; // 每个元素的指示符链（可为空）
};

struct CompoundLiteralExpr : Expr {
    CompoundLiteralExpr(TypePtr typeValue, std::unique_ptr<InitializerListExpr> initValue)
        : Expr(Kind::CompoundLiteral), compoundType(std::move(typeValue)), init(std::move(initValue)) {}

    TypePtr compoundType;
    std::unique_ptr<InitializerListExpr> init;
    int stackOffset = 0;  // 在栈上的偏移
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
    int bitWidth = 0;       // 0 表示非位域
    int bitOffset = 0;      // 位域在包含单元中的位偏移
};

struct TernaryExpr : Expr {
    TernaryExpr(
        std::unique_ptr<Expr> conditionValue,
        std::unique_ptr<Expr> thenValue,
        std::unique_ptr<Expr> elseValue)
        : Expr(Kind::Ternary),
          condition(std::move(conditionValue)),
          thenExpr(std::move(thenValue)),
          elseExpr(std::move(elseValue)) {}

    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> thenExpr;
    std::unique_ptr<Expr> elseExpr;
};

struct CastExpr : Expr {
    CastExpr(TypePtr targetTypeValue, std::unique_ptr<Expr> operandValue)
        : Expr(Kind::Cast), targetType(std::move(targetTypeValue)), operand(std::move(operandValue)) {}

    TypePtr targetType;
    std::unique_ptr<Expr> operand;
};

struct BuiltinVaStartExpr : Expr {
    BuiltinVaStartExpr(std::unique_ptr<Expr> apExpr, std::string lastParamNameValue)
        : Expr(Kind::BuiltinVaStart), ap(std::move(apExpr)), lastParamName(std::move(lastParamNameValue)) {}

    std::unique_ptr<Expr> ap;
    std::string lastParamName;
    int paramIndex = -1;
};

struct BuiltinVaArgExpr : Expr {
    BuiltinVaArgExpr(std::unique_ptr<Expr> apExpr, TypePtr argTypeValue)
        : Expr(Kind::BuiltinVaArg), ap(std::move(apExpr)), argType(std::move(argTypeValue)) {}

    std::unique_ptr<Expr> ap;
    TypePtr argType;
};

struct BuiltinVaEndExpr : Expr {
    explicit BuiltinVaEndExpr(std::unique_ptr<Expr> apExpr)
        : Expr(Kind::BuiltinVaEnd), ap(std::move(apExpr)) {}

    std::unique_ptr<Expr> ap;
};

struct GenericAssociation {
    TypePtr type;  // nullptr 表示 default
    std::unique_ptr<Expr> expr;
};

struct GenericExpr : Expr {
    GenericExpr(std::unique_ptr<Expr> controllingExprValue, std::vector<GenericAssociation> associationsValue)
        : Expr(Kind::Generic), controllingExpr(std::move(controllingExprValue)), associations(std::move(associationsValue)) {}

    std::unique_ptr<Expr> controllingExpr;
    std::vector<GenericAssociation> associations;
    Expr *selectedExpr = nullptr;  // 语义分析时解析出的选中表达式
};

struct Stmt;  // 前向声明

// GNU 语句表达式 ({ stmt; ...; expr; })，用于优化器内联多语句函数
struct StmtExpr : Expr {
    StmtExpr(std::vector<std::unique_ptr<Stmt>> stmtsValue, std::unique_ptr<Expr> resultValue)
        : Expr(Kind::StmtExpr), statements(std::move(stmtsValue)), result(std::move(resultValue)) {}

    std::vector<std::unique_ptr<Stmt>> statements;
    std::unique_ptr<Expr> result;
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
        Continue,
        DoWhile,
        Switch,
        Goto,
        Label,
        StaticAssert
    };

    explicit Stmt(Kind kindValue) : kind(kindValue) {}
    virtual ~Stmt() = default;

    Kind kind;
    int line = 0;
    int column = 0;
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
    bool isStatic = false;
    std::string staticSymbolName;  // 静态局部变量的全局符号名
    std::unique_ptr<Expr> vlaSizeExpr;  // VLA 的运行时大小表达式
    int line = 0;
    int column = 0;
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

struct DoWhileStmt : Stmt {
    DoWhileStmt(std::unique_ptr<Stmt> bodyValue, std::unique_ptr<Expr> conditionValue)
        : Stmt(Kind::DoWhile), body(std::move(bodyValue)), condition(std::move(conditionValue)) {}

    std::unique_ptr<Stmt> body;
    std::unique_ptr<Expr> condition;
};

struct SwitchCase {
    std::unique_ptr<Expr> label;
    std::unique_ptr<Stmt> body;
};

struct SwitchStmt : Stmt {
    SwitchStmt(
        std::unique_ptr<Expr> scrutineeValue,
        std::vector<SwitchCase> casesValue,
        std::unique_ptr<Stmt> defaultBodyValue)
        : Stmt(Kind::Switch),
          scrutinee(std::move(scrutineeValue)),
          cases(std::move(casesValue)),
          defaultBody(std::move(defaultBodyValue)) {}

    std::unique_ptr<Expr> scrutinee;
    std::vector<SwitchCase> cases;
    std::unique_ptr<Stmt> defaultBody;
};

struct GotoStmt : Stmt {
    explicit GotoStmt(std::string targetName)
        : Stmt(Kind::Goto), targetName(std::move(targetName)) {}

    std::string targetName;
};

struct LabelStmt : Stmt {
    LabelStmt(std::string nameValue, std::unique_ptr<Stmt> bodyValue)
        : Stmt(Kind::Label), name(std::move(nameValue)), body(std::move(bodyValue)) {}

    std::string name;
    std::unique_ptr<Stmt> body;
};

struct StaticAssertStmt : Stmt {
    StaticAssertStmt(std::unique_ptr<Expr> conditionValue, std::string messageValue)
        : Stmt(Kind::StaticAssert), condition(std::move(conditionValue)), message(std::move(messageValue)) {}

    std::unique_ptr<Expr> condition;
    std::string message;
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
    bool isVariadic = false;
    bool isInline = false;
    bool isNoreturn = false;

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
    bool isThreadLocal = false;
    std::string symbolName;
};

struct Program {
    std::vector<Function> functions;
    std::vector<GlobalVar> globals;
    std::vector<GlobalVar> staticLocals;  // static 局部变量，存储在 .data/.bss 节
    std::vector<std::unique_ptr<StaticAssertStmt>> globalStaticAsserts;  // 全局 _Static_assert

    Program() = default;
    Program(const Program &) = delete;
    Program &operator=(const Program &) = delete;
    Program(Program &&) = default;
    Program &operator=(Program &&) = default;
};
