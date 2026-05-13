#pragma once

#include <unordered_map>
#include <unordered_set>

#include "Ast.h"

class Optimizer {
public:
    // 优化级别：0=不优化，1=基础优化（常量折叠+死代码消除），2=全部优化
    void optimize(Program &program, int level = 2);

    // --- PassManager 可调用的 pass 方法（public） ---

    // 常量折叠 + 算术恒等式 + 强度削减 + 死代码消除 + 循环不变量外提
    void optimizeBlock(BlockStmt &block);

    // 常量传播
    void propagateBlock(BlockStmt &block);

    // 尾递归消除
    static void eliminateTailRecursion(Function &function);

    // 公共子表达式消除
    void applyCSE(Function &function);

    // 复制传播
    void applyCopyPropagation(Function &function);

    // 死存储消除
    void applyDeadStoreElimination(Function &function);

    // 循环展开
    void applyLoopUnrolling(Function &function);
    void applyLoopUnrollBlock(BlockStmt &block);

    int getOptLevel() const { return optLevel; }

private:
    int optLevel = 2;
    void optimizeFunction(Function &function);
    void optimizeStatement(Stmt &stmt);
    void optimizeExpr(std::unique_ptr<Expr> &expr);
    static bool tryEvaluateIntegerConstant(const Expr &expr, long long &value);
    static bool tryEvaluateFloatConstant(const Expr &expr, double &value);
    static std::unique_ptr<Expr> makeFoldedNumber(long long value, const Expr &original);
    static std::unique_ptr<Expr> makeFoldedFloat(double value, const Expr &original);

    // 常量传播：跟踪当前作用域中已知的常量变量
    std::unordered_map<std::string, long long> constantEnv;
    std::unordered_map<std::string, double> floatConstantEnv;

    // 常量传播遍
    void propagateStatement(Stmt &stmt);
    void propagateExpr(std::unique_ptr<Expr> &expr);

    // 复制传播：x = y → 后续 x 替换为 y
    // 存储源 VariableExpr 指针以保留 stackOffset/isGlobal/symbolName
    std::unordered_map<std::string, VariableExpr *> copyEnv;
    void copyPropagateBlock(BlockStmt &block);
    void copyPropagateStatement(Stmt &stmt);
    void copyPropagateExpr(std::unique_ptr<Expr> &expr);

    // 死存储消除辅助
    static bool eliminateDeadStoresInBlock(BlockStmt &block);
    static void collectExprUses(const Expr &expr, std::unordered_set<std::string> &uses);
    static bool hasSideEffects(const Expr &expr);

    // 死代码消除
    bool eliminateDeadCode(BlockStmt &block);

    // 强度削减
    static void applyStrengthReduction(std::unique_ptr<Expr> &expr);

    // 算术恒等式消除：x+0→x, x*1→x, x*0→0, x-0→x, x/1→x, x&-1→x, x|0→x, x^0→x, x<<0→x, x>>0→x
    static bool applyArithmeticIdentity(std::unique_ptr<Expr> &expr);

    // 循环不变量外提
    void hoistInvariantsFromBlock(BlockStmt &block);
    static bool isLoopInvariant(const Expr &expr, const std::unordered_set<std::string> &modifiedVars);
    static void collectModifiedVars(const Stmt &stmt, std::unordered_set<std::string> &vars);
    static void collectUsedVars(const Expr &expr, std::unordered_set<std::string> &vars);

    // 函数内联
    struct InlineCandidate {
        const Function *func;  // 指向原始函数（不拥有所有权）
    };
    std::unordered_map<std::string, InlineCandidate> inlineCandidates;
    void collectInlineCandidates(Program &program);
    static bool isInlinableFunction(const Function &func);
    void tryInlineCall(std::unique_ptr<Expr> &expr);
    static std::unique_ptr<Expr> cloneExpr(const Expr &expr);
    static std::unique_ptr<Stmt> cloneStmt(const Stmt &stmt);

    // 公共子表达式消除（CSE）
    struct CSEEntry {
        std::string tempName;
        int stackOffset;
        std::unordered_set<std::string> usedVars;  // 表达式依赖的变量
    };
    static std::string computeExprKey(const Expr &expr);
    static bool isPureExpression(const Expr &expr);
    static void collectExprVars(const Expr &expr, std::unordered_set<std::string> &vars);
    void cseExpr(std::unique_ptr<Expr> &expr,
                 std::unordered_map<std::string, CSEEntry> &availableExprs,
                 std::vector<std::unique_ptr<Stmt>> &newStmts,
                 int &tempCounter,
                 int &stackSize);
    static void invalidateCSE(std::unordered_map<std::string, CSEEntry> &availableExprs,
                              const std::string &varName);
};
