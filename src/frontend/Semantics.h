#pragma once

#include "Ast.h"
#include "Diagnostics.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct VariableSymbol {
    TypePtr type;
    int stackOffset = 0;
    bool isGlobal = false;
    std::string symbolName;
};

struct FunctionSignature {
    TypePtr returnType;
    std::vector<TypePtr> parameterTypes;
    bool hasDefinition = false;
    bool isVariadic = false;
};

struct GlobalSignature {
    TypePtr type;
    bool hasInitializerDefinition = false;
    bool hasTentativeDefinition = false;
    std::string symbolName;
};

class SemanticAnalyzer {
public:
    explicit SemanticAnalyzer(DiagnosticEngine *diag = nullptr);

    void analyze(Program &program, bool requireMain = true);

    // 警告开关
    void setShadowWarning(bool enabled) { warnShadow = enabled; }
    void setUnusedParamWarning(bool enabled) { warnUnusedParam = enabled; }

    // 是否有错误
    bool hasErrors() const;

private:
    void analyzeFunction(Function &function);
    void analyzeGlobal(GlobalVar &global);
    void validateArrayInitializer(const std::string &name, const TypePtr &arrayType, Expr &init, bool isGlobal);
    void validateStructInitializer(const std::string &name, const TypePtr &structType, Expr &init, bool isGlobal);
    bool isSupportedGlobalPointerInitializer(const GlobalVar &global) const;
    bool isSupportedGlobalPointerArrayInitializer(const GlobalVar &global) const;
    bool isSupportedStaticPointerInitializer(const Expr &expr) const;
    bool isSupportedPointerArrayElementInitializer(const TypePtr &elementType, const Expr &expr, bool isGlobal) const;
    bool isSupportedGlobalIntegerInitializer(const Expr &expr) const;
    bool isSupportedStructMemberType(const TypePtr &type) const;
    bool isSupportedByValueStructType(const TypePtr &type) const;
    void analyzeBlock(BlockStmt &block);
    void analyzeStatement(Stmt &stmt);
    void analyzeExpr(Expr &expr);
    void enterScope();
    void leaveScope();
    void declareVariable(DeclStmt &decl);
    VariableSymbol resolveVariable(const std::string &name, int line = 0, int column = 0) const;
    bool canAssign(const TypePtr &target, const TypePtr &value) const;
    bool sameType(const TypePtr &left, const TypePtr &right) const;
    bool isEquivalentArgumentType(const TypePtr &param, const TypePtr &arg) const;
    TypePtr decayType(const TypePtr &type) const;
    TypePtr promoteIntegerType(const TypePtr &type) const;
    TypePtr commonIntegerType(const TypePtr &left, const TypePtr &right) const;
    TypePtr commonFloatType(const TypePtr &left, const TypePtr &right) const;
    TypePtr usualArithmeticConversion(const TypePtr &left, const TypePtr &right) const;
    void insertImplicitCast(std::unique_ptr<Expr> &expr, const TypePtr &targetType);
    int integerRank(const TypePtr &type) const;
    std::string typeName(const TypePtr &type) const;
    [[noreturn]] void fail(const std::string &message) const;
    [[noreturn]] void failAt(int line, int column, const std::string &message) const;
    // 带位置信息的错误报告（非致命，收集到诊断引擎）
    void diagError(int line, int column, const std::string &message);
    // 带 AST 节点位置的错误报告
    void diagError(const Expr &expr, const std::string &message);
    void diagError(const Stmt &stmt, const std::string &message);
    static int alignTo(int value, int alignment);
    // 编译时常量表达式求值（用于 _Static_assert）
    // 返回 true 表示成功求值，结果存入 result；返回 false 表示不是常量表达式
    bool evaluateConstantExpr(const Expr &expr, long long &result) const;

    std::vector<std::unordered_map<std::string, VariableSymbol>> scopes;
    std::vector<std::unordered_set<std::string>> initializedVars; // 跟踪已初始化的变量
    std::vector<std::unordered_set<std::string>> usedVars; // 跟踪已使用的变量
    std::vector<std::string> scopeVarOrder; // 当前作用域中变量的声明顺序
    std::unordered_map<std::string, FunctionSignature> functions;
    std::unordered_map<std::string, VariableSymbol> globals;
    mutable std::unordered_set<std::string> reportedUndeclared; // 错误级联抑制：已报告的未声明名称
    std::unordered_map<std::string, GlobalSignature> globalSignatures;
    int nextStackOffset = 0;
    int loopDepth = 0;
    int switchDepth = 0;
    TypePtr currentReturnType;
    std::vector<Parameter> *currentFunctionParameters = nullptr;
    bool currentFunctionIsVariadic = false;
    DiagnosticEngine *diag;
    bool hasSemanticErrors = false;
    bool warnShadow = false;         // -Wshadow
    bool warnUnusedParam = false;    // -Wunused-parameter
};
