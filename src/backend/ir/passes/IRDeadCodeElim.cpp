#include "IRDeadCodeElim.h"
#include <queue>

namespace ir {

bool IRDeadCodeElim::runOnFunction(IRFunction &func, IRModule &) {
    changed = false;
    removeUnreachableBlocks(func);
    removeUnusedInstructions(func);
    return changed;
}

void IRDeadCodeElim::removeUnusedInstructions(IRFunction &func) {
    for (auto &bb : func.basicBlocks) {
        auto it = bb.instructions.begin();
        while (it != bb.instructions.end()) {
            auto &inst = *it;

            if (hasSideEffects(inst)) {
                ++it;
                continue;
            }

            if (inst.uses.empty()) {
                std::cerr << "[DCE] removing: op=" << static_cast<int>(inst.opcode)
                          << " name=" << inst.name << " bb=" << bb.name << std::endl;
                inst.cleanupOperands();
                it = bb.instructions.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
}

void IRDeadCodeElim::removeUnreachableBlocks(IRFunction &func) {
    auto reachable = computeReachableBlocks(func);

    auto it = func.basicBlocks.begin();
    while (it != func.basicBlocks.end()) {
        if (reachable.count(&*it) == 0) {
            // 清理块内所有指令的操作数 use 列表
            for (auto &inst : it->instructions) {
                inst.cleanupOperands();
            }
            // 清理前驱/后继关系
            for (auto *succ : it->successors) {
                succ->removePredecessor(&*it);
            }
            for (auto *pred : it->predecessors) {
                pred->removeSuccessor(&*it);
            }
            it = func.basicBlocks.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
}

std::unordered_set<IRBasicBlock *> IRDeadCodeElim::computeReachableBlocks(IRFunction &func) {
    std::unordered_set<IRBasicBlock *> reachable;
    if (!func.entryBlock) return reachable;

    std::queue<IRBasicBlock *> worklist;
    worklist.push(func.entryBlock);
    reachable.insert(func.entryBlock);

    while (!worklist.empty()) {
        auto *bb = worklist.front();
        worklist.pop();

        for (auto *succ : bb->successors) {
            if (reachable.count(succ) == 0) {
                reachable.insert(succ);
                worklist.push(succ);
            }
        }
    }

    return reachable;
}

bool IRDeadCodeElim::hasSideEffects(const IRInstruction &inst) {
    switch (inst.opcode) {
        // 终结指令
        case IROpcode::Br:
        case IROpcode::CondBr:
        case IROpcode::Ret:
        case IROpcode::Switch:
            return true;

        // 内存写入
        case IROpcode::Store:
            return true;

        // 函数调用（保守认为有副作用）
        case IROpcode::Call:
            return true;

        // alloca 只有在有 uses 时才算有副作用（无 uses 的 alloca 可安全删除）
        case IROpcode::Alloca:
            return !inst.uses.empty();

        // 内联汇编有副作用
        case IROpcode::InlineAsm:
            return true;

        // Phi 节点不算副作用，但如果无 uses 可以删除
        case IROpcode::Phi:
            return false;

        default:
            return false;
    }
}

} // namespace ir
