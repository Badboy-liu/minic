#pragma once
#include "IRInstruction.h"
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace ir {

class IRFunction;

class IRBasicBlock : public IRValue {
public:
    std::list<IRInstruction> instructions;
    std::vector<IRBasicBlock *> predecessors;
    std::vector<IRBasicBlock *> successors;
    IRFunction *parentFunc = nullptr;
    int id = -1;

    explicit IRBasicBlock(std::string label = "")
        : IRValue(IRValueKind::BasicBlock, IRType::voidTy(), std::move(label)) {}

    bool empty() const { return instructions.empty(); }
    IRInstruction *getTerminator();
    const IRInstruction *getTerminator() const;

    // 在末尾插入指令（转移所有权）
    void appendInstruction(std::unique_ptr<IRInstruction> inst);

    // 在指定位置之前插入（转移所有权）
    void insertBefore(std::list<IRInstruction>::iterator pos, std::unique_ptr<IRInstruction> inst);

    // 前驱/后继管理
    void addSuccessor(IRBasicBlock *succ);
    void removeSuccessor(IRBasicBlock *succ);
    void addPredecessor(IRBasicBlock *pred);
    void removePredecessor(IRBasicBlock *pred);

    std::string toString() const;
};

} // namespace ir
