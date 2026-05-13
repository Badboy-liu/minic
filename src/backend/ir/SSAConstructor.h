#pragma once
#include "IRFunction.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ir {

class SSAConstructor {
public:
    // 对函数执行 mem2reg 转换
    // 将 alloca/load/store 模式提升为 SSA 寄存器值
    void run(IRFunction &func, const std::unordered_set<const void *> &constantPool = {});

private:
    IRFunction *func = nullptr;
    std::unordered_set<const void *> constPool;

    // 支配树：block id → 直接支配者 block id
    std::unordered_map<int, int> idomMap;

    // 支配边界：block id → 支配边界 block id 集合
    std::unordered_map<int, std::unordered_set<int>> domFrontiers;

    // 需要提升的 alloca 集合
    std::unordered_set<const IRInstruction *> promotableAllocas;

    // 每个 alloca 的定义块（包含 store 的块）
    std::unordered_map<const IRInstruction *, std::unordered_set<int>> allocaDefs;

    // 变量重命名栈：alloca → 值栈
    std::unordered_map<const IRInstruction *, std::vector<IRValue *>> renameStack;

    // 步骤 1：支配性分析
    void computeDominatorTree();
    std::unordered_set<int> computeDom(int blockId);

    // 步骤 2：支配边界
    void computeDominanceFrontiers();

    // 步骤 3：识别可提升的 alloca
    void findPromotableAllocas();

    // 步骤 4：收集每个 alloca 的定义块
    void collectAllocaDefs();

    // 步骤 5：Phi 插入
    void placePhiFunctions();

    // 步骤 6：变量重命名
    void renameVariables();
    void renameBlock(IRBasicBlock *bb, std::unordered_set<int> &visited);

    // phi 指令 → 对应 alloca 的映射
    std::unordered_map<const IRInstruction *, const IRInstruction *> phiToAlloca;

    // 工具方法
    IRBasicBlock *getBlockById(int id);
    int getBlockId(const IRBasicBlock *bb);
    void removeTrivialPhis();
    void removeDeadStores();
};

} // namespace ir
