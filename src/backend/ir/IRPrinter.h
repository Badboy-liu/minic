#pragma once
#include "IRModule.h"
#include <sstream>
#include <string>

namespace ir {

class IRPrinter {
public:
    std::string printModule(const IRModule &module);
    std::string printFunction(const IRFunction &func);
    std::string printBasicBlock(const IRBasicBlock &bb, int indent = 1);
    std::string printInstruction(const IRInstruction &inst, int indent = 2);

private:
    std::ostringstream out;
    int valueCounter = 0;

    std::string valueName(const IRValue *val);
    std::string typeStr(const IRType &ty);
};

} // namespace ir
