#pragma once

#include "CFG.h"

#include <string>

class CFGBuilder {
public:
    CFG build(Function &function);

private:
    CFG cfg;
    BasicBlock *currentBlock = nullptr;
    // break/continue 目标栈
    std::vector<BasicBlock *> breakTargets;
    std::vector<BasicBlock *> continueTargets;

    void addStmt(Stmt *stmt);
    void setCurrentBlock(BasicBlock *block);
    void addEdge(BasicBlock *from, BasicBlock *to, BasicBlock::EdgeKind kind);
    void buildStmt(Stmt &stmt);
    void buildBlockStmt(BlockStmt &block);
};
