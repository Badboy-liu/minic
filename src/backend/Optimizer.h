#pragma once

#include "Ast.h"

class Optimizer {
public:
    void optimize(Program &program);

private:
    void optimizeFunction(Function &function);
    void optimizeBlock(BlockStmt &block);
    void optimizeStatement(Stmt &stmt);
    void optimizeExpr(std::unique_ptr<Expr> &expr);
    static bool tryEvaluateIntegerConstant(const Expr &expr, int &value);
    static std::unique_ptr<Expr> makeFoldedNumber(int value, const Expr &original);
};
