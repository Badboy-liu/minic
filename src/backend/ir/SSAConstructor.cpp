#include "SSAConstructor.h"
#include <algorithm>
#include <cassert>
#include <iostream>

namespace ir {

void SSAConstructor::run(IRFunction &f, const std::unordered_set<const void *> &constantPool) {
    func = &f;
    constPool = constantPool;
    if (!func->entryBlock) return;

    // 清理上一个函数的状态
    idomMap.clear();
    domFrontiers.clear();
    promotableAllocas.clear();
    allocaDefs.clear();
    renameStack.clear();
    phiToAlloca.clear();

    findPromotableAllocas();
    std::cerr << "[SSA] promotableAllocas=" << promotableAllocas.size() << std::endl;
    if (promotableAllocas.empty()) return;

    std::cerr << "[SSA] computeDominatorTree..." << std::endl;
    computeDominatorTree();
    std::cerr << "[SSA] computeDominanceFrontiers..." << std::endl;
    computeDominanceFrontiers();
    std::cerr << "[SSA] collectAllocaDefs..." << std::endl;
    collectAllocaDefs();
    std::cerr << "[SSA] placePhiFunctions..." << std::endl;
    placePhiFunctions();
    std::cerr << "[SSA] renameVariables..." << std::endl;
    renameVariables();
    std::cerr << "[SSA] removeTrivialPhis..." << std::endl;
    removeTrivialPhis();
    // 不在这里调用 rebuildUseLists（常量池问题），removeDeadStores 自己扫描
    std::cerr << "[SSA] removeDeadStores..." << std::endl;
    removeDeadStores();
    std::cerr << "[SSA] done." << std::endl;
}

// === 支配性分析 ===

void SSAConstructor::computeDominatorTree() {
    if (!func->entryBlock) return;

    // 收集所有块 ID
    std::vector<int> blockIds;
    for (auto &bb : func->basicBlocks) {
        blockIds.push_back(bb.id);
    }

    // 初始化：入口块只被自己支配，其他块被所有块支配
    std::unordered_map<int, std::unordered_set<int>> domSets;
    for (int id : blockIds) {
        domSets[id] = std::unordered_set<int>(blockIds.begin(), blockIds.end());
    }
    domSets[func->entryBlock->id] = {func->entryBlock->id};

    // 迭代求不动点
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto &bb : func->basicBlocks) {
            if (bb.id == func->entryBlock->id) continue;

            // 计算所有前驱支配集的交集
            std::unordered_set<int> newDom;
            bool first = true;
            for (auto *pred : bb.predecessors) {
                auto predIt = domSets.find(pred->id);
                if (predIt == domSets.end()) continue;
                if (first) {
                    newDom = predIt->second;
                    first = false;
                } else {
                    std::unordered_set<int> intersection;
                    for (int id : newDom) {
                        if (predIt->second.count(id)) {
                            intersection.insert(id);
                        }
                    }
                    newDom = std::move(intersection);
                }
            }
            newDom.insert(bb.id);

            if (newDom != domSets[bb.id]) {
                domSets[bb.id] = std::move(newDom);
                changed = true;
            }
        }
    }

    // 计算直接支配者（idom）
    // idom(b) = 在 Dom(b)-{b} 中不被其他支配者支配的节点
    for (auto &bb : func->basicBlocks) {
        if (bb.id == func->entryBlock->id) continue;
        auto &domSet = domSets[bb.id];

        for (int candidate : domSet) {
            if (candidate == bb.id) continue;
            bool isDirect = true;
            for (int other : domSet) {
                if (other == bb.id || other == candidate) continue;
                // 如果 other 支配 candidate，则 candidate 不是直接支配者
                if (domSets[other].count(candidate)) {
                    isDirect = false;
                    break;
                }
            }
            if (isDirect) {
                idomMap[bb.id] = candidate;
                break;
            }
        }
    }
}

// === 支配边界 ===

void SSAConstructor::computeDominanceFrontiers() {
    for (auto &bb : func->basicBlocks) {
        if (bb.predecessors.size() < 2) continue;

        // 对于每个有多个前驱的块
        for (auto *pred : bb.predecessors) {
            // 从 pred 沿支配树上溯，直到遇到 bb 的直接支配者
            auto *runner = pred;
            auto idomIt = idomMap.find(bb.id);
            int bbIdomId = idomIt != idomMap.end() ? idomIt->second : -1;

            while (runner && runner->id != bbIdomId) {
                domFrontiers[runner->id].insert(bb.id);
                auto it = idomMap.find(runner->id);
                if (it == idomMap.end()) break;
                runner = getBlockById(it->second);
            }
        }
    }
}

// === 识别可提升的 alloca ===

void SSAConstructor::findPromotableAllocas() {
    if (!func->entryBlock) return;

    for (auto &inst : func->entryBlock->instructions) {
        if (inst.opcode != IROpcode::Alloca) continue;

        // 检查所有使用：必须只有 load 和 store
        bool promotable = true;
        for (auto &use : inst.uses) {
            if (!use.user) continue;
            if (use.user->opcode != IROpcode::Load &&
                use.user->opcode != IROpcode::Store) {
                promotable = false;
                break;
            }
            // store 的操作数 1（目标）必须是这个 alloca
            if (use.user->opcode == IROpcode::Store && use.operandIndex != 1) {
                promotable = false;
                break;
            }
        }

        if (promotable) {
            promotableAllocas.insert(&inst);
        }
    }
}

// === 收集定义块 ===

void SSAConstructor::collectAllocaDefs() {
    for (auto *alloca : promotableAllocas) {
        for (auto &use : alloca->uses) {
            if (!use.user || use.user->opcode != IROpcode::Store) continue;
            // store 的目标是这个 alloca
            if (use.operandIndex == 1 && use.user->parentBB) {
                allocaDefs[alloca].insert(use.user->parentBB->id);
            }
        }
    }
}

// === Phi 插入 ===

void SSAConstructor::placePhiFunctions() {
    for (auto &[alloca, defs] : allocaDefs) {
        // 计算定义块的支配边界闭包
        std::unordered_set<int> phiBlocks;
        std::vector<int> worklist(defs.begin(), defs.end());

        while (!worklist.empty()) {
            int blockId = worklist.back();
            worklist.pop_back();

            auto dfIt = domFrontiers.find(blockId);
            if (dfIt == domFrontiers.end()) continue;

            for (int dfBlockId : dfIt->second) {
                if (phiBlocks.count(dfBlockId)) continue;
                phiBlocks.insert(dfBlockId);

                // 在该块开头插入 phi
                auto *bb = getBlockById(dfBlockId);
                if (!bb) continue;

                // 获取 alloca 的元素类型
                IRType elemTy = IRType::i32();
                if (alloca->type.isPointer() && alloca->type.elementType) {
                    elemTy = *alloca->type.elementType;
                }

                // 创建 phi 指令
                IRInstruction phi(IROpcode::Phi, elemTy);
                phi.name = "phi_" + alloca->name;
                // phi 的操作数将在重命名阶段填充
                bb->instructions.push_front(std::move(phi));
                auto &phiInst = bb->instructions.front();
                phiInst.parentBB = bb;

                // 记录 phi → alloca 映射
                phiToAlloca[&phiInst] = alloca;

                // 将这个 phi 作为 alloca 的一个定义
                defs.insert(dfBlockId);
                worklist.push_back(dfBlockId);
            }
        }
    }
}

// === 变量重命名 ===

void SSAConstructor::renameVariables() {
    // 初始化每个 alloca 的重命名栈（空 = 未定义）
    for (auto *alloca : promotableAllocas) {
        renameStack[alloca] = {};
    }

    std::unordered_set<int> visited;
    renameBlock(func->entryBlock, visited);
}

void SSAConstructor::renameBlock(IRBasicBlock *bb, std::unordered_set<int> &visited) {
    if (!bb || visited.count(bb->id)) return;
    visited.insert(bb->id);

    // 记录本块中 push 的定义数量（退出时 pop）
    std::unordered_map<const IRInstruction *, int> pushedCounts;
    for (auto *alloca : promotableAllocas) {
        pushedCounts[alloca] = 0;
    }

    // 遍历块中的指令
    auto it = bb->instructions.begin();
    while (it != bb->instructions.end()) {
        auto &inst = *it;
        std::cerr << "[SSA] rename: op=" << static_cast<int>(inst.opcode) << " name=" << inst.name << " ops=" << inst.getNumOperands() << std::endl;

        if (inst.opcode == IROpcode::Store) {
            auto *ptr = inst.getOperand(1);
            auto *val = inst.getOperand(0);

            // 检查是否是 store 到可提升的 alloca
            auto allocaIt = std::find_if(promotableAllocas.begin(), promotableAllocas.end(),
                [&](const IRInstruction *a) { return a == ptr; });
            if (allocaIt != promotableAllocas.end()) {
                std::cerr << "[SSA]   -> pushing val=" << val << " (store kept for use tracking)" << std::endl;
                renameStack[*allocaIt].push_back(val);
                pushedCounts[*allocaIt]++;
                // 不删除 store：保留它作为 val 的使用方，防止 DCE 误删。
                // removeDeadStores 会在 rename 完成后统一删除多余的 store。
            }
        } else if (inst.opcode == IROpcode::Load) {
            auto *ptr = inst.getOperand(0);

            auto allocaIt = std::find_if(promotableAllocas.begin(), promotableAllocas.end(),
                [&](const IRInstruction *a) { return a == ptr; });
            if (allocaIt != promotableAllocas.end()) {
                auto &stack = renameStack[*allocaIt];
                std::cerr << "[SSA]   -> erasing load " << inst.name << ", stack.size=" << stack.size()
                          << " uses=" << inst.uses.size() << " ptr=" << &inst << std::endl;
                if (!stack.empty()) {
                    IRValue *replacement = stack.back();
                    inst.replaceAllUsesWith(replacement);
                    // 更新 Load 的指针操作数，使其指向替换值而非 alloca
                    // 这样如果存在通过指针读取 alloca 存储值的 Load（如 *q），
                    // 它会从正确的地址读取
                    inst.setOperand(0, replacement);
                }
                inst.cleanupOperands();
                it = bb->instructions.erase(it);
                continue;
            }
        } else if (inst.opcode == IROpcode::Phi) {
            // 检查是否是我们插入的 phi
            auto phiIt = phiToAlloca.find(&inst);
            if (phiIt != phiToAlloca.end()) {
                // phi 作为对应 alloca 的定义：将 phi 本身推入重命名栈
                const IRInstruction *alloca = phiIt->second;
                renameStack[alloca].push_back(&inst);
                pushedCounts[alloca]++;
            }
        }

        ++it;
    }

    // 为后继块的 phi 填充 incoming 值
    for (auto *succ : bb->successors) {
        for (auto &succInst : succ->instructions) {
            if (succInst.opcode != IROpcode::Phi) continue;
            auto phiIt = phiToAlloca.find(&succInst);
            if (phiIt == phiToAlloca.end()) continue;

            const IRInstruction *alloca = phiIt->second;
            auto &stack = renameStack[alloca];
            if (!stack.empty()) {
                addPhiIncoming(const_cast<IRInstruction *>(&succInst), stack.back(), bb);
            }
        }
    }

    // 递归处理支配树中的子块
    for (auto &childBB : func->basicBlocks) {
        auto idomIt = idomMap.find(childBB.id);
        if (idomIt != idomMap.end() && idomIt->second == bb->id) {
            renameBlock(&childBB, visited);
        }
    }

    // 退出块：pop 本块中 push 的定义
    for (auto *alloca : promotableAllocas) {
        int count = pushedCounts[alloca];
        for (int i = 0; i < count; ++i) {
            renameStack[alloca].pop_back();
        }
    }
}

// === 清理 ===

// 辅助方法：安全删除指令，清理 use 列表
static void eraseInstructionWithUseCleanup(std::list<IRInstruction> &list,
                                            std::list<IRInstruction>::iterator it) {
    it->cleanupOperands();
    list.erase(it);
}

void SSAConstructor::removeTrivialPhis() {
    for (auto &bb : func->basicBlocks) {
        auto it = bb.instructions.begin();
        while (it != bb.instructions.end()) {
            if (it->opcode == IROpcode::Phi) {
                // 检查是否所有 incoming 值相同
                if (it->operands.size() == 2) {
                    IRValue *val = it->getOperand(0);
                    it->replaceAllUsesWith(val);
                    eraseInstructionWithUseCleanup(bb.instructions, it);
                    it = bb.instructions.begin(); // 重新扫描
                    continue;
                }

                // 检查是否有自引用
                bool allSame = true;
                IRValue *firstVal = nullptr;
                for (size_t i = 0; i < it->operands.size(); i += 2) {
                    auto *val = it->getOperand(i);
                    if (!val || val == &(*it)) continue; // 自引用或空
                    if (!firstVal) {
                        firstVal = val;
                    } else if (val != firstVal) {
                        allSame = false;
                        break;
                    }
                }
                if (allSame && firstVal) {
                    it->replaceAllUsesWith(firstVal);
                    eraseInstructionWithUseCleanup(bb.instructions, it);
                    it = bb.instructions.begin(); // 重新扫描
                    continue;
                }
            }
            ++it;
        }
    }
}

void SSAConstructor::removeDeadStores() {
    // 收集所有有效指令指针
    std::unordered_set<const IRInstruction *> validInsts;
    for (auto &bb : func->basicBlocks) {
        for (auto &inst : bb.instructions) {
            validInsts.insert(&inst);
        }
    }

    for (auto *alloca : promotableAllocas) {
        if (!validInsts.count(alloca)) continue;

        // 手动扫描检查 alloca 是否被任何指令引用
        bool hasUses = false;
        for (auto &bb : func->basicBlocks) {
            for (auto &inst : bb.instructions) {
                if (&inst == alloca) continue;
                for (int i = 0; i < inst.getNumOperands(); ++i) {
                    if (inst.getOperand(i) == alloca) {
                        hasUses = true;
                        break;
                    }
                }
                if (hasUses) break;
            }
            if (hasUses) break;
        }

        if (!hasUses) {
            // 删除所有指向该 alloca 的 store 指令
            for (auto &bb : func->basicBlocks) {
                auto it = bb.instructions.begin();
                while (it != bb.instructions.end()) {
                    if (it->opcode == IROpcode::Store && it->getNumOperands() >= 2) {
                        auto *op1 = it->getOperand(1);
                        if (op1 && op1 == alloca) {
                            eraseInstructionWithUseCleanup(bb.instructions, it);
                            continue;
                        }
                    }
                    ++it;
                }
            }
            // 删除 alloca 指令本身
            for (auto &bb : func->basicBlocks) {
                auto it = bb.instructions.begin();
                while (it != bb.instructions.end()) {
                    if (&(*it) == alloca) {
                        eraseInstructionWithUseCleanup(bb.instructions, it);
                        break;
                    }
                    ++it;
                }
            }
        }
    }

    // 标记 Store 到可提升 alloca 的指令（代码生成时跳过）
    // 这些 Store 由 SSA rename 保留以维持 use 链，但不应生成代码
    func->deadStores.clear();
    std::cerr << "[SSA] promotableAllocas count=" << promotableAllocas.size() << std::endl;
    for (auto *a : promotableAllocas) {
        std::cerr << "[SSA]   promotable alloca: " << a->name << " ptr=" << a << std::endl;
    }
    for (auto &bb : func->basicBlocks) {
        for (auto &inst : bb.instructions) {
            if (inst.opcode == IROpcode::Store && inst.getNumOperands() >= 2) {
                auto *ptr = inst.getOperand(1);
                if (promotableAllocas.count(static_cast<const IRInstruction *>(ptr))) {
                    func->deadStores.insert(&inst);
                    std::cerr << "[SSA]   dead store: ptr=" << ptr << " name=" << (ptr ? ptr->name : "?") << std::endl;
                }
            }
        }
    }
    std::cerr << "[SSA] deadStores count=" << func->deadStores.size() << std::endl;
}

// === 工具方法 ===

IRBasicBlock *SSAConstructor::getBlockById(int id) {
    for (auto &bb : func->basicBlocks) {
        if (bb.id == id) return &bb;
    }
    return nullptr;
}

int SSAConstructor::getBlockId(const IRBasicBlock *bb) {
    return bb ? bb->id : -1;
}

} // namespace ir
