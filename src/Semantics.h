#pragma once

#include "Ast.h"

#include <string>
#include <unordered_map>
#include <vector>

struct VariableSymbol {
    TypePtr type;
    int stackOffset = 0;
};

struct FunctionSignature {
    TypePtr returnType;
    std::vector<TypePtr> parameterTypes;
    bool hasDefinition = false;
};

class SemanticAnalyzer {
public:
    void analyze(Program &program);

private:
    void analyzeFunction(Function &function);
    void analyzeBlock(BlockStmt &block);
    void analyzeStatement(Stmt &stmt);
    void analyzeExpr(Expr &expr);
    void enterScope();
    void leaveScope();
    void declareVariable(DeclStmt &decl);
    VariableSymbol resolveVariable(const std::string &name) const;
    bool canAssign(const TypePtr &target, const TypePtr &value) const;
    bool sameType(const TypePtr &left, const TypePtr &right) const;
    bool isEquivalentArgumentType(const TypePtr &param, const TypePtr &arg) const;
    TypePtr decayType(const TypePtr &type) const;
    std::string typeName(const TypePtr &type) const;
    [[noreturn]] void fail(const std::string &message) const;
    static int alignTo(int value, int alignment);

    std::vector<std::unordered_map<std::string, VariableSymbol>> scopes;
    std::unordered_map<std::string, FunctionSignature> functions;
    int nextStackOffset = 0;
    int loopDepth = 0;
    TypePtr currentReturnType;
};
