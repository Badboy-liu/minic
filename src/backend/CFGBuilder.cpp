#include "CFGBuilder.h"

CFG CFGBuilder::build(Function &function) {
    cfg = CFG();
    currentBlock = nullptr;

    // 入口块
    cfg.entry = cfg.createBlock();
    currentBlock = cfg.entry;

    // 函数体
    if (function.body) {
        buildBlockStmt(*function.body);
    }

    // 出口块
    cfg.exit = cfg.createBlock();

    // 如果当前块没有终止，连接到出口
    if (currentBlock && currentBlock->successors.empty()) {
        addEdge(currentBlock, cfg.exit, BasicBlock::EdgeKind::Fallthrough);
    }

    // 处理没有前驱的块（如 goto 目标标签块可能还未连接）
    // 确保 exit 至少有一个前驱
    if (cfg.exit->predecessors.empty() && !cfg.blocks.empty()) {
        // 从最后一个非出口块连接
        for (auto it = cfg.blocks.rbegin(); it != cfg.blocks.rend(); ++it) {
            if (it->get() != cfg.exit && !it->get()->successors.empty()) {
                break;
            }
            if (it->get() != cfg.exit) {
                addEdge(it->get(), cfg.exit, BasicBlock::EdgeKind::Fallthrough);
                break;
            }
        }
    }

    return std::move(cfg);
}

void CFGBuilder::addStmt(Stmt *stmt) {
    if (!currentBlock) {
        currentBlock = cfg.createBlock();
    }
    currentBlock->statements.push_back(stmt);
}

void CFGBuilder::setCurrentBlock(BasicBlock *block) {
    currentBlock = block;
}

void CFGBuilder::addEdge(BasicBlock *from, BasicBlock *to, BasicBlock::EdgeKind kind) {
    if (!from || !to) return;
    from->successors.push_back({to, kind});
    to->predecessors.push_back(from);
}

void CFGBuilder::buildBlockStmt(BlockStmt &block) {
    for (auto &stmt : block.statements) {
        buildStmt(*stmt);
    }
}

void CFGBuilder::buildStmt(Stmt &stmt) {
    switch (stmt.kind) {
    case Stmt::Kind::Return:
        addStmt(&stmt);
        addEdge(currentBlock, cfg.exit, BasicBlock::EdgeKind::Jump);
        currentBlock = nullptr;  // 当前块终止
        break;

    case Stmt::Kind::Expr:
    case Stmt::Kind::Decl:
    case Stmt::Kind::StaticAssert:
        addStmt(&stmt);
        break;

    case Stmt::Kind::Block:
        buildBlockStmt(static_cast<BlockStmt &>(stmt));
        break;

    case Stmt::Kind::If: {
        auto &ifStmt = static_cast<IfStmt &>(stmt);
        addStmt(&stmt);  // 条件求值

        BasicBlock *thenBlock = cfg.createBlock();
        BasicBlock *elseBlock = ifStmt.elseBranch ? cfg.createBlock() : nullptr;
        BasicBlock *mergeBlock = cfg.createBlock();

        // 条件块 → then/else
        currentBlock->branchCondition = ifStmt.condition.get();
        addEdge(currentBlock, thenBlock, BasicBlock::EdgeKind::Branch);
        if (elseBlock) {
            addEdge(currentBlock, elseBlock, BasicBlock::EdgeKind::Fallthrough);
        } else {
            addEdge(currentBlock, mergeBlock, BasicBlock::EdgeKind::Fallthrough);
        }

        // then 分支
        setCurrentBlock(thenBlock);
        buildStmt(*ifStmt.thenBranch);
        if (currentBlock && currentBlock->successors.empty()) {
            addEdge(currentBlock, mergeBlock, BasicBlock::EdgeKind::Fallthrough);
        }

        // else 分支
        if (elseBlock && ifStmt.elseBranch) {
            setCurrentBlock(elseBlock);
            buildStmt(*ifStmt.elseBranch);
            if (currentBlock && currentBlock->successors.empty()) {
                addEdge(currentBlock, mergeBlock, BasicBlock::EdgeKind::Fallthrough);
            }
        }

        setCurrentBlock(mergeBlock);
        break;
    }

    case Stmt::Kind::While: {
        auto &whileStmt = static_cast<WhileStmt &>(stmt);

        BasicBlock *condBlock = cfg.createBlock();
        BasicBlock *bodyBlock = cfg.createBlock();
        BasicBlock *exitBlock = cfg.createBlock();

        // 进入条件块
        addEdge(currentBlock, condBlock, BasicBlock::EdgeKind::Fallthrough);

        // 条件块
        setCurrentBlock(condBlock);
        addStmt(&stmt);  // 条件求值
        currentBlock->branchCondition = whileStmt.condition.get();
        addEdge(currentBlock, bodyBlock, BasicBlock::EdgeKind::Branch);
        addEdge(currentBlock, exitBlock, BasicBlock::EdgeKind::Fallthrough);

        breakTargets.push_back(exitBlock);
        continueTargets.push_back(condBlock);

        // 循环体
        setCurrentBlock(bodyBlock);
        buildStmt(*whileStmt.body);
        if (currentBlock && currentBlock->successors.empty()) {
            addEdge(currentBlock, condBlock, BasicBlock::EdgeKind::Jump);
        }

        breakTargets.pop_back();
        continueTargets.pop_back();

        setCurrentBlock(exitBlock);
        break;
    }

    case Stmt::Kind::For: {
        auto &forStmt = static_cast<ForStmt &>(stmt);

        // init
        if (forStmt.init) {
            buildStmt(*forStmt.init);
        }

        BasicBlock *condBlock = cfg.createBlock();
        BasicBlock *updateBlock = cfg.createBlock();
        BasicBlock *bodyBlock = cfg.createBlock();
        BasicBlock *exitBlock = cfg.createBlock();

        addEdge(currentBlock, condBlock, BasicBlock::EdgeKind::Fallthrough);

        // 条件块
        setCurrentBlock(condBlock);
        if (forStmt.condition) {
            addStmt(&stmt);  // 条件求值
            currentBlock->branchCondition = forStmt.condition.get();
        }
        addEdge(currentBlock, bodyBlock, BasicBlock::EdgeKind::Branch);
        addEdge(currentBlock, exitBlock, BasicBlock::EdgeKind::Fallthrough);

        breakTargets.push_back(exitBlock);
        continueTargets.push_back(updateBlock);

        // 循环体
        setCurrentBlock(bodyBlock);
        buildStmt(*forStmt.body);
        if (currentBlock && currentBlock->successors.empty()) {
            addEdge(currentBlock, updateBlock, BasicBlock::EdgeKind::Fallthrough);
        }

        // update 块
        setCurrentBlock(updateBlock);
        if (forStmt.update) {
            addStmt(&stmt);  // update 求值
        }
        addEdge(currentBlock, condBlock, BasicBlock::EdgeKind::Jump);

        breakTargets.pop_back();
        continueTargets.pop_back();

        setCurrentBlock(exitBlock);
        break;
    }

    case Stmt::Kind::DoWhile: {
        auto &doWhileStmt = static_cast<DoWhileStmt &>(stmt);

        BasicBlock *bodyBlock = cfg.createBlock();
        BasicBlock *exitBlock = cfg.createBlock();

        addEdge(currentBlock, bodyBlock, BasicBlock::EdgeKind::Fallthrough);

        breakTargets.push_back(exitBlock);
        continueTargets.push_back(bodyBlock);

        // 循环体
        setCurrentBlock(bodyBlock);
        buildStmt(*doWhileStmt.body);
        if (currentBlock && currentBlock->successors.empty()) {
            // 条件求值
            addStmt(&stmt);
            currentBlock->branchCondition = doWhileStmt.condition.get();
            addEdge(currentBlock, bodyBlock, BasicBlock::EdgeKind::Branch);
            addEdge(currentBlock, exitBlock, BasicBlock::EdgeKind::Fallthrough);
        }

        breakTargets.pop_back();
        continueTargets.pop_back();

        setCurrentBlock(exitBlock);
        break;
    }

    case Stmt::Kind::Switch: {
        auto &sw = static_cast<SwitchStmt &>(stmt);
        addStmt(&stmt);  // scrutinee 求值

        BasicBlock *exitBlock = cfg.createBlock();
        breakTargets.push_back(exitBlock);

        BasicBlock *defaultBlock = sw.defaultBody ? cfg.createBlock() : exitBlock;

        for (auto &c : sw.cases) {
            BasicBlock *caseBlock = cfg.createBlock();
            addEdge(currentBlock, caseBlock, BasicBlock::EdgeKind::Branch);
            setCurrentBlock(caseBlock);
            buildStmt(*c.body);
            if (currentBlock && currentBlock->successors.empty()) {
                addEdge(currentBlock, exitBlock, BasicBlock::EdgeKind::Fallthrough);
            }
        }

        if (sw.defaultBody) {
            addEdge(currentBlock, defaultBlock, BasicBlock::EdgeKind::Fallthrough);
            setCurrentBlock(defaultBlock);
            buildStmt(*sw.defaultBody);
            if (currentBlock && currentBlock->successors.empty()) {
                addEdge(currentBlock, exitBlock, BasicBlock::EdgeKind::Fallthrough);
            }
        }

        breakTargets.pop_back();
        setCurrentBlock(exitBlock);
        break;
    }

    case Stmt::Kind::Break:
        addStmt(&stmt);
        if (!breakTargets.empty()) {
            addEdge(currentBlock, breakTargets.back(), BasicBlock::EdgeKind::Jump);
        }
        currentBlock = nullptr;
        break;

    case Stmt::Kind::Continue:
        addStmt(&stmt);
        if (!continueTargets.empty()) {
            addEdge(currentBlock, continueTargets.back(), BasicBlock::EdgeKind::Jump);
        }
        currentBlock = nullptr;
        break;

    case Stmt::Kind::Goto: {
        auto &gotoStmt = static_cast<GotoStmt &>(stmt);
        addStmt(&stmt);
        // 查找或创建目标标签块
        std::string labelName = "label_" + gotoStmt.targetName;
        auto it = cfg.labelToBlock.find(labelName);
        if (it != cfg.labelToBlock.end()) {
            addEdge(currentBlock, it->second, BasicBlock::EdgeKind::Jump);
        } else {
            // 延迟连接：创建块并记录
            BasicBlock *targetBlock = cfg.createBlock();
            cfg.labelToBlock[labelName] = targetBlock;
            addEdge(currentBlock, targetBlock, BasicBlock::EdgeKind::Jump);
        }
        currentBlock = nullptr;
        break;
    }

    case Stmt::Kind::Label: {
        auto &labelStmt = static_cast<LabelStmt &>(stmt);
        std::string labelName = "label_" + labelStmt.name;

        BasicBlock *labelBlock = nullptr;
        auto it = cfg.labelToBlock.find(labelName);
        if (it != cfg.labelToBlock.end()) {
            labelBlock = it->second;
        } else {
            labelBlock = cfg.createBlock();
            cfg.labelToBlock[labelName] = labelBlock;
        }

        // 从当前块 fallthrough 到标签块
        if (currentBlock && currentBlock->successors.empty()) {
            addEdge(currentBlock, labelBlock, BasicBlock::EdgeKind::Fallthrough);
        }

        setCurrentBlock(labelBlock);
        addStmt(&stmt);
        buildStmt(*labelStmt.body);
        break;
    }
    }
}
