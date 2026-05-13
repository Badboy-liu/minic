#include "Liveness.h"
#include <algorithm>
#include <queue>

namespace ir {

void LivenessAnalysis::compute(const IRFunction &func) {
    instPositions.clear();
    intervals.clear();
    callPositions.clear();
    argVregMap.clear();

    // 构建 IRArgument* → vreg id 映射
    if (argVregStart >= 0) {
        for (size_t i = 0; i < func.arguments.size(); ++i) {
            argVregMap[func.arguments[i].get()] = argVregStart + static_cast<int>(i);
        }
    }

    linearizeBlocks(func);
    computeLiveness(func);
    buildIntervals(func);
}

// RPO 线性化：为每条指令分配位置号
void LivenessAnalysis::linearizeBlocks(const IRFunction &func) {
    int pos = 0;

    // 计算 RPO（逆后序）
    // 简化：使用 DFS 后序的逆序
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
        // 反向推入后继，这样 DFS 会先处理第一个后继
        for (auto it = bb->successors.rbegin(); it != bb->successors.rend(); ++it) {
            if (!visited.count((*it)->id)) {
                stack.push_back(*it);
            }
        }
    }
    // RPO = 后序的逆序
    std::reverse(rpo.begin(), rpo.end());

    // 为每条指令分配位置
    for (auto *bb : rpo) {
        for (auto &inst : bb->instructions) {
            instPositions[&inst] = pos++;
            if (inst.opcode == IROpcode::Call) {
                callPositions.push_back(pos - 1);
            }
        }
    }
}

void LivenessAnalysis::computeLiveness(const IRFunction &func) {
    // 收集所有块
    std::unordered_map<int, const IRBasicBlock *> blockMap;
    for (auto &bb : func.basicBlocks) {
        blockMap[bb.id] = &bb;
    }

    // 计算每块的 def/use 集合
    std::unordered_map<int, std::unordered_set<int>> blockDef;
    std::unordered_map<int, std::unordered_set<int>> blockUse;

    for (auto &[id, bb] : blockMap) {
        for (auto &inst : bb->instructions) {
            if (inst.type.kind == IRTypeKind::Void) continue;
            int vreg = inst.id;
            if (vreg < 0) continue;

            if (inst.opcode == IROpcode::Phi) {
                // phi 结果在本块定义
                blockDef[id].insert(vreg);
                // phi 操作数在前驱块末尾使用（特殊处理）
                for (size_t i = 0; i + 1 < inst.operands.size(); i += 2) {
                    auto *val = inst.getOperand(i);
                    if (auto *defInst = dynamic_cast<IRInstruction *>(val)) {
                        if (defInst->id >= 0) {
                            auto *predBB = reinterpret_cast<IRBasicBlock *>(inst.getOperand(i + 1));
                            if (predBB) {
                                blockUse[predBB->id].insert(defInst->id);
                            }
                        }
                    } else if (auto *defArg = dynamic_cast<IRArgument *>(val)) {
                        auto ait = argVregMap.find(defArg);
                        if (ait != argVregMap.end()) {
                            auto *predBB = reinterpret_cast<IRBasicBlock *>(inst.getOperand(i + 1));
                            if (predBB) {
                                blockUse[predBB->id].insert(ait->second);
                            }
                        }
                    }
                }
                continue;
            }

            // 非 phi 指令：先 use 后 def
            for (auto *op : inst.operands) {
                if (!op) continue;
                if (reinterpret_cast<uintptr_t>(op) < 100) continue;
                if (auto *defInst = dynamic_cast<IRInstruction *>(op)) {
                    if (defInst->id >= 0 && !blockDef[id].count(defInst->id)) {
                        blockUse[id].insert(defInst->id);
                    }
                } else if (auto *defArg = dynamic_cast<IRArgument *>(op)) {
                    auto it = argVregMap.find(defArg);
                    if (it != argVregMap.end() && !blockDef[id].count(it->second)) {
                        blockUse[id].insert(it->second);
                    }
                }
            }
            blockDef[id].insert(vreg);
        }
    }

    // 迭代数据流求 liveIn/liveOut
    std::unordered_map<int, std::unordered_set<int>> liveIn;
    std::unordered_map<int, std::unordered_set<int>> liveOut;

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto &[id, bb] : blockMap) {
            // liveOut[B] = ∪ liveIn[S] for successors S
            std::unordered_set<int> newLiveOut;
            for (auto *succ : bb->successors) {
                for (int v : liveIn[succ->id]) {
                    newLiveOut.insert(v);
                }
            }
            // 对于 phi 指令，只在对应的前驱块中使用 phi 操作数
            // 已经在 blockUse 中处理了

            if (newLiveOut != liveOut[id]) {
                liveOut[id] = std::move(newLiveOut);
                changed = true;
            }

            // liveIn[B] = use[B] ∪ (liveOut[B] - def[B])
            std::unordered_set<int> newLiveIn = blockUse[id];
            for (int v : liveOut[id]) {
                if (!blockDef[id].count(v)) {
                    newLiveIn.insert(v);
                }
            }
            if (newLiveIn != liveIn[id]) {
                liveIn[id] = std::move(newLiveIn);
                changed = true;
            }
        }
    }

    // 存储 liveIn/liveOut 供 buildIntervals 使用
    // 这里直接在 buildIntervals 中重新计算
    // 为了简化，我们将 liveIn/liveOut 保存为成员变量
    // 但实际上我们只需要在 buildIntervals 中使用它们
    // 所以我们把它们存到临时结构中

    // 由于我们需要在 buildIntervals 中使用这些数据，
    // 让我们直接在 buildIntervals 中重新计算
    // 但为了效率，我们可以将结果保存下来

    // 实际上，让我们改变策略：直接在 buildIntervals 中
    // 使用 blockMap、liveIn、liveOut 来构建区间
    // 但 buildIntervals 需要访问这些数据...

    // 最简单的方案：把 liveIn/liveOut 存为临时成员
    // 但为了保持接口简洁，我们在 buildIntervals 中直接重新计算

    // 保存到成员变量（使用 static 或临时方案）
    // 这里我们选择在 buildIntervals 中重新计算
    (void)liveIn;
    (void)liveOut;
    (void)blockMap;
}

void LivenessAnalysis::buildIntervals(const IRFunction &func) {
    // 重新计算 liveIn/liveOut（与 computeLiveness 相同的逻辑）
    std::unordered_map<int, const IRBasicBlock *> blockMap;
    for (auto &bb : func.basicBlocks) {
        blockMap[bb.id] = &bb;
    }

    std::unordered_map<int, std::unordered_set<int>> blockDef;
    std::unordered_map<int, std::unordered_set<int>> blockUse;

    for (auto &[id, bb] : blockMap) {
        for (auto &inst : bb->instructions) {
            if (inst.type.kind == IRTypeKind::Void) continue;
            int vreg = inst.id;
            if (vreg < 0) continue;

            if (inst.opcode == IROpcode::Phi) {
                blockDef[id].insert(vreg);
                for (size_t i = 0; i + 1 < inst.operands.size(); i += 2) {
                    auto *val = inst.getOperand(i);
                    if (auto *defInst = dynamic_cast<IRInstruction *>(val)) {
                        if (defInst->id >= 0) {
                            auto *predBB = reinterpret_cast<IRBasicBlock *>(inst.getOperand(i + 1));
                            if (predBB) blockUse[predBB->id].insert(defInst->id);
                        }
                    }
                }
                continue;
            }
            for (auto *op : inst.operands) {
                if (!op) continue;
                if (reinterpret_cast<uintptr_t>(op) < 100) continue;
                if (auto *defInst = dynamic_cast<IRInstruction *>(op)) {
                    if (defInst->id >= 0 && !blockDef[id].count(defInst->id)) {
                        blockUse[id].insert(defInst->id);
                    }
                }
            }
            blockDef[id].insert(vreg);
        }
    }

    std::unordered_map<int, std::unordered_set<int>> liveIn;
    std::unordered_map<int, std::unordered_set<int>> liveOut;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto &[id, bb] : blockMap) {
            std::unordered_set<int> newLiveOut;
            for (auto *succ : bb->successors) {
                for (int v : liveIn[succ->id]) newLiveOut.insert(v);
            }
            if (newLiveOut != liveOut[id]) { liveOut[id] = std::move(newLiveOut); changed = true; }

            std::unordered_set<int> newLiveIn = blockUse[id];
            for (int v : liveOut[id]) {
                if (!blockDef[id].count(v)) newLiveIn.insert(v);
            }
            if (newLiveIn != liveIn[id]) { liveIn[id] = std::move(newLiveIn); changed = true; }
        }
    }

    // 构建活跃区间
    // 对每个 vreg，找到所有它活跃的位置
    std::unordered_map<int, std::vector<int>> vregLivePoints;

    // 对每个块，vreg 在 liveIn 中 → 在块中所有位置活跃
    // vreg 在 liveOut 中 → 在块中所有位置活跃
    // vreg 被定义 → 从定义位置开始活跃
    // vreg 被使用 → 到使用位置结束活跃

    // 简化方案：对每个 vreg，找最早定义和最晚使用
    std::unordered_map<int, int> vregFirstDef;
    std::unordered_map<int, int> vregLastUse;

    for (auto &[id, bb] : blockMap) {
        // 如果 vreg 在 liveIn 中，活跃范围从块的第一条指令开始
        for (int v : liveIn[id]) {
            for (auto &inst : bb->instructions) {
                auto it = instPositions.find(&inst);
                if (it != instPositions.end()) {
                    if (!vregFirstDef.count(v) || it->second < vregFirstDef[v]) {
                        vregFirstDef[v] = it->second;
                    }
                    break;
                }
            }
        }

        for (auto &inst : bb->instructions) {
            auto posIt = instPositions.find(&inst);
            if (posIt == instPositions.end()) continue;
            int pos = posIt->second;

            // 定义
            if (inst.type.kind != IRTypeKind::Void && inst.id >= 0 && inst.opcode != IROpcode::Phi) {
                int vreg = inst.id;
                if (!vregFirstDef.count(vreg) || pos < vregFirstDef[vreg]) {
                    vregFirstDef[vreg] = pos;
                }
            }

            // 使用（非 phi）
            if (inst.opcode != IROpcode::Phi) {
                for (auto *op : inst.operands) {
                    if (!op) continue;
                    if (reinterpret_cast<uintptr_t>(op) < 100) continue;
                    if (auto *defInst = dynamic_cast<IRInstruction *>(op)) {
                        if (defInst->id >= 0) {
                            int vreg = defInst->id;
                            if (!vregLastUse.count(vreg) || pos > vregLastUse[vreg]) {
                                vregLastUse[vreg] = pos;
                            }
                        }
                    } else if (auto *defArg = dynamic_cast<IRArgument *>(op)) {
                        auto ait = argVregMap.find(defArg);
                        if (ait != argVregMap.end()) {
                            int vreg = ait->second;
                            if (!vregLastUse.count(vreg) || pos > vregLastUse[vreg]) {
                                vregLastUse[vreg] = pos;
                            }
                        }
                    }
                }
            }

            // phi 操作数的使用位置在前驱块末尾
            if (inst.opcode == IROpcode::Phi) {
                for (size_t i = 0; i + 1 < inst.operands.size(); i += 2) {
                    auto *val = inst.getOperand(i);
                    auto *predBB = reinterpret_cast<IRBasicBlock *>(inst.getOperand(i + 1));
                    if (!predBB || predBB->instructions.empty()) continue;
                    auto &lastInst = predBB->instructions.back();
                    auto lastIt = instPositions.find(&lastInst);
                    if (lastIt == instPositions.end()) continue;
                    int usePos = lastIt->second;

                    if (auto *defInst = dynamic_cast<IRInstruction *>(val)) {
                        if (defInst->id >= 0) {
                            int vreg = defInst->id;
                            if (!vregLastUse.count(vreg) || usePos > vregLastUse[vreg]) {
                                vregLastUse[vreg] = usePos;
                            }
                        }
                    } else if (auto *defArg = dynamic_cast<IRArgument *>(val)) {
                        auto ait = argVregMap.find(defArg);
                        if (ait != argVregMap.end()) {
                            int vreg = ait->second;
                            if (!vregLastUse.count(vreg) || usePos > vregLastUse[vreg]) {
                                vregLastUse[vreg] = usePos;
                            }
                        }
                    }
                }
            }
        }

        // 如果 vreg 在 liveOut 中，活跃范围到块的最后一条指令
        for (int v : liveOut[id]) {
            for (auto it = bb->instructions.rbegin(); it != bb->instructions.rend(); ++it) {
                auto posIt = instPositions.find(&*it);
                if (posIt != instPositions.end()) {
                    if (!vregLastUse.count(v) || posIt->second > vregLastUse[v]) {
                        vregLastUse[v] = posIt->second;
                    }
                    break;
                }
            }
        }
    }

    // 构建区间
    for (auto &[vreg, start] : vregFirstDef) {
        LiveInterval interval;
        interval.vreg = vreg;
        interval.start = start;
        interval.end = vregLastUse.count(vreg) ? vregLastUse[vreg] + 1 : start + 1;
        intervals.push_back(interval);
    }

    // 为参数创建区间（参数从位置 0 开始活跃）
    for (auto &[arg, vreg] : argVregMap) {
        if (vregLastUse.count(vreg)) {
            LiveInterval interval;
            interval.vreg = vreg;
            interval.start = 0;
            interval.end = vregLastUse[vreg] + 1;
            intervals.push_back(interval);
        }
    }

    // 按 start 排序
    std::sort(intervals.begin(), intervals.end(),
              [](const LiveInterval &a, const LiveInterval &b) { return a.start < b.start; });
}

} // namespace ir
