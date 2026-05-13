#pragma once

#include <memory>
#include <vector>

#include "CFG.h"

class Optimizer;
struct Function;

// 基础 pass 接口
class OptimizationPass {
public:
    virtual ~OptimizationPass() = default;
    virtual const char *name() const = 0;
    virtual void run(Function &function, Optimizer &optimizer) = 0;
};

// CFG 级 pass：额外接收 CFG 信息
class CFGPass : public OptimizationPass {
public:
    virtual void runOnCFG(Function &function, CFG &cfg, Optimizer &optimizer) = 0;
    void run(Function &function, Optimizer &optimizer) override;
};

class PassManager {
public:
    void addPass(std::unique_ptr<OptimizationPass> pass);
    void runFunction(Function &function, Optimizer &optimizer);

private:
    std::vector<std::unique_ptr<OptimizationPass>> passes;
    bool needsCFG() const;
};

// --- 现有优化 pass 包装类 ---

// 常量传播 + 常量折叠 + 算术恒等式 + 强度削减 + 死代码消除 + 循环不变量外提
class ConstantFoldingPass : public OptimizationPass {
public:
    const char *name() const override { return "ConstantFolding"; }
    void run(Function &function, Optimizer &optimizer) override;
};

// 常量传播
class ConstantPropagationPass : public OptimizationPass {
public:
    const char *name() const override { return "ConstantPropagation"; }
    void run(Function &function, Optimizer &optimizer) override;
};

// 尾递归消除
class TailRecursionPass : public OptimizationPass {
public:
    const char *name() const override { return "TailRecursion"; }
    void run(Function &function, Optimizer &optimizer) override;
};

// 公共子表达式消除
class CSEPass : public OptimizationPass {
public:
    const char *name() const override { return "CSE"; }
    void run(Function &function, Optimizer &optimizer) override;
};

// 复制传播
class CopyPropagationPass : public OptimizationPass {
public:
    const char *name() const override { return "CopyPropagation"; }
    void run(Function &function, Optimizer &optimizer) override;
};

// 死存储消除
class DeadStoreElimPass : public OptimizationPass {
public:
    const char *name() const override { return "DeadStoreElim"; }
    void run(Function &function, Optimizer &optimizer) override;
};

// 循环展开：将常数次 for 循环展开为顺序语句
class LoopUnrollPass : public OptimizationPass {
public:
    const char *name() const override { return "LoopUnroll"; }
    void run(Function &function, Optimizer &optimizer) override;
};
