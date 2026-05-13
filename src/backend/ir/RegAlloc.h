#pragma once
#include "Liveness.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ir {

struct RegAllocResult {
    std::unordered_map<int, int> vregToPhysReg;   // vreg id → 物理寄存器索引
    std::unordered_map<int, int> vregToSpillSlot;  // vreg id → 栈偏移（从 rbp-N）
    int spillSlotSize = 0;                          // 溢出区总大小（字节）
    std::unordered_set<int> usedCalleeSavedRegs;    // 使用的 callee-saved 寄存器索引
};

// 物理寄存器分配顺序
// Caller-saved: r10=0, r11=1, rax=2, rdx=3, r8=4, r9=5, rcx=6
// Callee-saved: rbx=7, r12=8, r13=9, r14=10, r15=11
constexpr int NUM_CALLER_SAVED = 7;
constexpr int NUM_PHYS_REGS = 12;
constexpr const char *PHYS_REG_NAMES[] = {
    "r10", "r11", "rax", "rdx", "r8", "r9", "rcx",
    "rbx", "r12", "r13", "r14", "r15"
};

class LinearScanAllocator {
public:
    RegAllocResult allocate(const std::vector<LiveInterval> &intervals,
                            const std::vector<int> &callPositions = {},
                            int argVregStart = -1, int numArgs = 0);
};

} // namespace ir
