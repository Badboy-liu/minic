#pragma once
#include "IRBasicBlock.h"
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace ir {

class IRFunction : public IRValue {
public:
    std::string name;
    IRType returnType;
    std::vector<std::unique_ptr<IRArgument>> arguments;
    std::list<IRBasicBlock> basicBlocks;
    IRBasicBlock *entryBlock = nullptr;
    bool isVarArg = false;

    // 指令 ID 计数器
    int nextValueId = 0;

    // SSA rename 保留但不应生成代码的 Store 指令（目标是可提升 alloca）
    std::unordered_set<const IRInstruction *> deadStores;

    IRFunction(std::string name, IRType retTy, bool varArg = false)
        : IRValue(IRValueKind::GlobalVariable, retTy, name),
          name(std::move(name)), returnType(std::move(retTy)), isVarArg(varArg) {}

    IRArgument *addArgument(IRType ty, const std::string &name = "") {
        auto arg = std::make_unique<IRArgument>(std::move(ty),
            static_cast<int>(arguments.size()), name);
        auto *ptr = arg.get();
        arguments.push_back(std::move(arg));
        return ptr;
    }

    IRBasicBlock *createBasicBlock(const std::string &name = "") {
        basicBlocks.emplace_back(name);
        auto &bb = basicBlocks.back();
        bb.parentFunc = this;
        bb.id = static_cast<int>(basicBlocks.size()) - 1;
        if (!entryBlock) entryBlock = &bb;
        return &bb;
    }

    int allocValueId() { return nextValueId++; }

    // 重建所有指令的 use 列表
    // constantPool: 外部持有的常量指针集合（来自 IRModule::constants）
    void rebuildUseLists(const std::unordered_set<const void *> &constantPool = {}) {
        // 先清空所有 use 列表
        for (auto &arg : arguments) arg->uses.clear();
        for (auto &bb : basicBlocks) {
            for (auto &inst : bb.instructions) {
                inst.uses.clear();
            }
        }
        // 收集所有有效指针（指令 + 参数 + BasicBlock + 常量池）
        std::unordered_set<const void *> validPtrs;
        for (auto &arg : arguments) validPtrs.insert(arg.get());
        for (auto &bb : basicBlocks) {
            validPtrs.insert(&bb);
            for (auto &inst : bb.instructions) {
                validPtrs.insert(&inst);
            }
        }
        // 合并常量池
        for (auto *c : constantPool) validPtrs.insert(c);

        // 重新构建，跳过无效操作数
        for (auto &bb : basicBlocks) {
            for (auto &inst : bb.instructions) {
                for (int i = 0; i < inst.getNumOperands(); ++i) {
                    auto *op = inst.getOperand(i);
                    if (!op) continue;
                    // 先做指针有效性检查（不解引用）
                    // 小整数值（如 ICmpKind 枚举）被存储为指针，跳过
                    if (reinterpret_cast<uintptr_t>(op) < 100) {
                        continue;
                    }
                    if (validPtrs.count(op)) {
                        // 有效指针，跟踪 use（仅指令和参数）
                        auto kind = op->valueKind;
                        if (kind == IRValueKind::Instruction || kind == IRValueKind::Argument) {
                            op->uses.push_back({&inst, i});
                        }
                    } else {
                        // 悬空指针，设为 null
                        const_cast<IRInstruction &>(inst).setOperand(i, nullptr);
                    }
                }
            }
        }
    }

    std::string toString() const;
};

} // namespace ir
