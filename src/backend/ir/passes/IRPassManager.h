#pragma once
#include "../IRFunction.h"
#include "../IRModule.h"
#include <memory>
#include <vector>

namespace ir {

// IR 优化 Pass 基类
class IRPass {
public:
    virtual ~IRPass() = default;
    virtual const char *name() const = 0;
    virtual bool runOnFunction(IRFunction &func, IRModule &module) = 0;
};

// IR Pass 管理器：按顺序运行 pass，支持重复运行至不动点
class IRPassManager {
public:
    void addPass(std::unique_ptr<IRPass> pass);
    bool runOnFunction(IRFunction &func, IRModule &module);

    // 运行所有 pass 直到没有变化（不动点）
    bool runToFixpoint(IRFunction &func, IRModule &module, int maxIterations = 10);

private:
    std::vector<std::unique_ptr<IRPass>> passes;
};

} // namespace ir
