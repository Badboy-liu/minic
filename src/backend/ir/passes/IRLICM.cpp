#include "IRLICM.h"
#include <algorithm>
#include <queue>

namespace ir {

std::vector<IRLICM::Loop> IRLICM::findLoops(IRFunction &func) {
    std::vector<Loop> loops;

    // 计算 RPO 顺序
    std::vector<IRBasicBlock *> rpo;
    std::unordered_set<int> visited;
    std::vector<IRBasicBlock *> stack;
    if (func.entryBlock) stack.push_back(func.entryBlock);
    while (!stack.empty()) {
        auto *bb = stack.back();
        stack.pop_back();
        if (visited.count(bb->id)) continue;
        visited.insert(bb->id);
        rpo.push_back(bb);
        for (auto it = bb->successors.rbegin(); it != bb->successors.rend(); ++it) {
            if (!visited.count((*it)->id)) stack.push_back(*it);
        }
    }
    std::reverse(rpo.begin(), rpo.end());

    std::unordered_map<int, int> rpoIndex;
    for (int i = 0; i < static_cast<int>(rpo.size()); ++i) {
        rpoIndex[rpo[i]->id] = i;
    }

    // 找后边
    for (auto &bb : func.basicBlocks) {
        for (auto *succ : bb.successors) {
            auto tgtIt = rpoIndex.find(succ->id);
            auto srcIt = rpoIndex.find(bb.id);
            if (tgtIt == rpoIndex.end() || srcIt == rpoIndex.end()) continue;
            if (tgtIt->second <= srcIt->second) {
                // 后边：bb → succ，succ 是 header
                Loop loop;
                loop.header = succ;
                loop.body.insert(succ->id);
                loop.body.insert(bb.id);

                // 从 bb 沿前驱回到 header
                std::vector<IRBasicBlock *> worklist = {&bb};
                while (!worklist.empty()) {
                    auto *cur = worklist.back();
                    worklist.pop_back();
                    for (auto *pred : cur->predecessors) {
                        if (loop.body.insert(pred->id).second) {
                            worklist.push_back(pred);
                        }
                    }
                }
                loops.push_back(std::move(loop));
            }
        }
    }
    return loops;
}

IRBasicBlock *IRLICM::getPreheader(const Loop &loop) {
    // header 的前驱中，不在循环内的那个就是 preheader
    for (auto *pred : loop.header->predecessors) {
        if (!loop.body.count(pred->id)) {
            return pred;
        }
    }
    return nullptr;
}

bool IRLICM::isSafeToHoist(const IRInstruction &inst) {
    switch (inst.opcode) {
    case IROpcode::Add: case IROpcode::Sub: case IROpcode::Mul:
    case IROpcode::SDiv: case IROpcode::UDiv: case IROpcode::SRem:
    case IROpcode::URem: case IROpcode::And: case IROpcode::Or:
    case IROpcode::Xor: case IROpcode::Shl: case IROpcode::LShr:
    case IROpcode::AShr: case IROpcode::ICmp: case IROpcode::ZExt:
    case IROpcode::SExt: case IROpcode::Trunc: case IROpcode::BitCast:
    case IROpcode::IntToPtr: case IROpcode::PtrToInt:
    case IROpcode::FPToUI: case IROpcode::FPToSI:
    case IROpcode::UIToFP: case IROpcode::SIToFP:
    case IROpcode::Select:
        return true;
    default:
        return false;
    }
}

bool IRLICM::isLoopInvariant(const IRInstruction &inst, const Loop &loop,
                              const std::unordered_set<int> &invariantVregs) {
    if (inst.opcode == IROpcode::Phi) return false; // phi 不外提
    if (inst.opcode == IROpcode::Call) return false; // 调用有副作用

    for (auto *op : inst.operands) {
        if (!op) continue;
        if (reinterpret_cast<uintptr_t>(op) < 100) continue;

        // 常量操作数 → 不变量
        if (op->isConstant()) continue;

        // IRArgument（函数参数）→ 在循环外定义，是不变量
        if (dynamic_cast<IRArgument *>(op)) continue;

        // IRInstruction 操作数
        if (auto *defInst = dynamic_cast<IRInstruction *>(op)) {
            // 定义在循环外 → 不变量
            if (defInst->parentBB && !loop.body.count(defInst->parentBB->id)) continue;
            // 定义在循环内但已被标记为不变量
            if (invariantVregs.count(defInst->id)) continue;
            // 否则不是不变量
            return false;
        }
    }
    return true;
}

bool IRLICM::runOnFunction(IRFunction &func, IRModule &module) {
    changed = false;
    auto loops = findLoops(func);

    for (auto &loop : loops) {
        auto *preheader = getPreheader(loop);
        if (!preheader) continue; // 没有 preheader，跳过

        // 迭代找不变量（不动点）
        std::unordered_set<int> invariantVregs;
        bool localChanged = true;
        while (localChanged) {
            localChanged = false;
            for (auto &bb : func.basicBlocks) {
                if (!loop.body.count(bb.id)) continue;
                for (auto &inst : bb.instructions) {
                    if (inst.id < 0) continue;
                    if (invariantVregs.count(inst.id)) continue;
                    if (!isSafeToHoist(inst)) continue;
                    if (!isLoopInvariant(inst, loop, invariantVregs)) continue;

                    // 检查：指令在循环中只有一个定义点（SSA 保证）
                    // 检查：外提后所有使用点仍在支配范围内
                    // 简化：preheader 支配 header，header 支配循环内所有块
                    // （对于自然循环成立）
                    invariantVregs.insert(inst.id);
                    localChanged = true;
                    changed = true;
                }
            }
        }

        if (invariantVregs.empty()) continue;

        // 外提不变量指令到 preheader 末尾（在终结指令之前）
        for (auto &bb : func.basicBlocks) {
            if (!loop.body.count(bb.id)) continue;

            auto it = bb.instructions.begin();
            while (it != bb.instructions.end()) {
                if (it->id >= 0 && invariantVregs.count(it->id) && isSafeToHoist(*it)) {
                    // 移动到 preheader
                    auto &preInsts = preheader->instructions;
                    auto next = std::next(it);
                    if (!preInsts.empty() &&
                        (preInsts.back().opcode == IROpcode::Br ||
                         preInsts.back().opcode == IROpcode::CondBr ||
                         preInsts.back().opcode == IROpcode::Ret)) {
                        // splice 到终结指令之前
                        preInsts.splice(std::prev(preInsts.end()),
                                        bb.instructions, it, next);
                    } else {
                        preInsts.splice(preInsts.end(),
                                        bb.instructions, it, next);
                    }
                    it = next;
                } else {
                    ++it;
                }
            }
        }
    }

    return changed;
}

} // namespace ir
