#pragma once
#include "IRFunction.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ir {

class IRModule {
public:
    std::vector<std::unique_ptr<IRFunction>> functions;
    std::vector<std::unique_ptr<IRGlobalVariable>> globals;
    std::vector<std::unique_ptr<IRValue>> constants; // 常量池
    std::string targetTriple;

    IRFunction *createFunction(const std::string &name, IRType retTy,
                               const std::vector<IRType> &paramTypes = {}, bool varArg = false) {
        auto fn = std::make_unique<IRFunction>(name, std::move(retTy), varArg);
        for (size_t i = 0; i < paramTypes.size(); ++i) {
            fn->addArgument(paramTypes[i], "");
        }
        auto *ptr = fn.get();
        functions.push_back(std::move(fn));
        return ptr;
    }

    IRGlobalVariable *createGlobal(IRType elemType, bool isConst, const std::string &name) {
        auto gv = std::make_unique<IRGlobalVariable>(std::move(elemType), isConst, name);
        auto *ptr = gv.get();
        globals.push_back(std::move(gv));
        return ptr;
    }

    IRFunction *getFunction(const std::string &name) {
        for (auto &fn : functions) {
            if (fn->name == name) return fn.get();
        }
        return nullptr;
    }

    IRConstantInt *createConstantInt(int64_t value, int bitWidth = 32) {
        auto c = std::make_unique<IRConstantInt>(value, bitWidth);
        auto *ptr = c.get();
        constants.push_back(std::move(c));
        return ptr;
    }

    IRConstantFloat *createConstantFloat(double value, bool isDouble = true) {
        auto c = std::make_unique<IRConstantFloat>(value, isDouble);
        auto *ptr = c.get();
        constants.push_back(std::move(c));
        return ptr;
    }

    IRConstantNull *createConstantNull() {
        auto c = std::make_unique<IRConstantNull>();
        auto *ptr = c.get();
        constants.push_back(std::move(c));
        return ptr;
    }

    // 获取常量池指针集合（用于 rebuildUseLists）
    std::unordered_set<const void *> getConstantPool() const {
        std::unordered_set<const void *> pool;
        for (auto &c : constants) pool.insert(c.get());
        for (auto &g : globals) pool.insert(g.get());
        for (auto &f : functions) pool.insert(f.get());
        return pool;
    }

    std::string toString() const;
};

} // namespace ir
