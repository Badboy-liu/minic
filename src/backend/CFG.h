#pragma once

#include "Ast.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct BasicBlock {
    int id;
    std::vector<Stmt *> statements;  // 非拥有指针

    enum class EdgeKind { Fallthrough, Branch, Jump };
    struct Edge {
        BasicBlock *target;
        EdgeKind kind;
    };

    std::vector<Edge> successors;
    std::vector<BasicBlock *> predecessors;

    // 分支条件（Branch 边使用）
    Expr *branchCondition = nullptr;
    bool branchTrueGoesToFirstSuccessor = true;
};

struct CFG {
    BasicBlock *entry = nullptr;
    BasicBlock *exit = nullptr;
    std::vector<std::unique_ptr<BasicBlock>> blocks;
    std::unordered_map<std::string, BasicBlock *> labelToBlock;

    BasicBlock *createBlock() {
        auto block = std::make_unique<BasicBlock>();
        block->id = static_cast<int>(blocks.size());
        BasicBlock *ptr = block.get();
        blocks.push_back(std::move(block));
        return ptr;
    }

    // 获取逆后序（Reverse Postorder）— 数据流分析的标准遍历顺序
    std::vector<BasicBlock *> reversePostorder() const {
        std::vector<BasicBlock *> postorder;
        std::unordered_set<int> visited;
        if (entry) {
            dfsPostorder(entry, visited, postorder);
        }
        std::reverse(postorder.begin(), postorder.end());
        return postorder;
    }

    // 检查块是否从 entry 可达
    bool isReachable(const BasicBlock *block) const {
        if (!entry || !block) return false;
        std::unordered_set<int> visited;
        std::queue<const BasicBlock *> worklist;
        worklist.push(entry);
        while (!worklist.empty()) {
            auto *current = worklist.front();
            worklist.pop();
            if (current == block) return true;
            if (!visited.insert(current->id).second) continue;
            for (auto &edge : current->successors) {
                worklist.push(edge.target);
            }
        }
        return false;
    }

    // 循环信息：由 findLoops() 计算
    struct LoopInfo {
        BasicBlock *header;                      // 循环头（后边目标）
        std::unordered_set<int> bodyBlockIds;    // 循环体块 ID 集合（含 header）
        std::vector<BasicBlock *> bodyBlocks;    // 循环体块列表
    };

    // 检测自然循环：基于后边（back edge）发现
    std::vector<LoopInfo> findLoops() const {
        auto rpo = reversePostorder();
        std::unordered_map<int, int> rpoIndex;
        for (int i = 0; i < static_cast<int>(rpo.size()); ++i) {
            rpoIndex[rpo[i]->id] = i;
        }

        std::vector<LoopInfo> loops;
        // 遍历所有边，找后边（target 在 RPO 中位于 source 之前或相同）
        for (auto &block : blocks) {
            for (auto &edge : block->successors) {
                auto tgtIt = rpoIndex.find(edge.target->id);
                auto srcIt = rpoIndex.find(block->id);
                if (tgtIt == rpoIndex.end() || srcIt == rpoIndex.end()) continue;
                // 后边：target 的 RPO 序号 <= source 的 RPO 序号
                if (tgtIt->second <= srcIt->second) {
                    // 计算自然循环：从 source 出发，沿前驱回到 target
                    LoopInfo loop;
                    loop.header = edge.target;
                    std::unordered_set<int> inLoop;
                    inLoop.insert(edge.target->id);
                    inLoop.insert(block->id);

                    std::vector<BasicBlock *> worklist = {block.get()};
                    while (!worklist.empty()) {
                        auto *bb = worklist.back();
                        worklist.pop_back();
                        for (auto *pred : bb->predecessors) {
                            if (inLoop.insert(pred->id).second) {
                                worklist.push_back(pred);
                            }
                        }
                    }

                    loop.bodyBlockIds = std::move(inLoop);
                    for (auto &b : blocks) {
                        if (loop.bodyBlockIds.count(b->id)) {
                            loop.bodyBlocks.push_back(b.get());
                        }
                    }
                    loops.push_back(std::move(loop));
                }
            }
        }
        return loops;
    }

private:
    static void dfsPostorder(BasicBlock *block, std::unordered_set<int> &visited,
                             std::vector<BasicBlock *> &postorder) {
        if (!visited.insert(block->id).second) return;
        for (auto &edge : block->successors) {
            dfsPostorder(edge.target, visited, postorder);
        }
        postorder.push_back(block);
    }
};
