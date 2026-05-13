#pragma once
#include "IRFunction.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ir {

struct LiveInterval {
    int vreg = -1;        // IRInstruction::id
    int start = 0;        // 活跃范围起始位置
    int end = 0;          // 活跃范围结束位置（不含）
    int physReg = -1;     // 分配的物理寄存器索引，-1 表示溢出
    int spillSlot = -1;   // 溢出栈槽偏移，-1 表示在寄存器中
};

class LivenessAnalysis {
public:
    void compute(const IRFunction &func);
    const std::vector<LiveInterval> &getIntervals() const { return intervals; }
    std::vector<LiveInterval> &getIntervals() { return intervals; }

    // 获取指令的位置号
    int getPosition(const IRInstruction *inst) const {
        auto it = instPositions.find(inst);
        return it != instPositions.end() ? it->second : -1;
    }

    // 获取所有 Call 指令的位置（用于寄存器分配时强制溢出跨越 call 的区间）
    const std::vector<int> &getCallPositions() const { return callPositions; }

    // 设置参数的 vreg 起始 ID（IRCodeGenerator 调用）
    void setArgVregStart(int start) { argVregStart = start; }

private:
    std::unordered_map<const IRInstruction *, int> instPositions;
    std::vector<LiveInterval> intervals;
    std::vector<int> callPositions; // Call 指令的位置
    int argVregStart = -1; // 参数 vreg 起始 ID

    // IRArgument* → vreg id 映射（在 compute 中构建）
    std::unordered_map<const IRArgument *, int> argVregMap;

    void linearizeBlocks(const IRFunction &func);
    void computeLiveness(const IRFunction &func);
    void buildIntervals(const IRFunction &func);
};

} // namespace ir
