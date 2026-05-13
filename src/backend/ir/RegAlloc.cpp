#include "RegAlloc.h"
#include <algorithm>
#include <iostream>

namespace ir {

// 检查区间是否跨越 Call 指令
static bool crossesCall(const LiveInterval &interval, const std::vector<int> &callPositions) {
    for (int callPos : callPositions) {
        if (interval.start <= callPos && callPos < interval.end)
            return true;
    }
    return false;
}

RegAllocResult LinearScanAllocator::allocate(const std::vector<LiveInterval> &intervals,
                                              const std::vector<int> &callPositions,
                                              int argVregStart, int numArgs) {
    RegAllocResult result;
    if (intervals.empty()) return result;

    // 按 start 排序的副本
    std::vector<LiveInterval> sorted = intervals;
    std::sort(sorted.begin(), sorted.end(),
              [](const LiveInterval &a, const LiveInterval &b) { return a.start < b.start; });

    // caller-saved 活跃区间（仅不跨越 call 的值使用）
    struct ActiveInterval {
        int vreg;
        int end;
        int physReg;
    };
    std::vector<ActiveInterval> active;

    auto removeExpired = [&](int currentStart) {
        active.erase(
            std::remove_if(active.begin(), active.end(),
                           [currentStart](const ActiveInterval &a) { return a.end <= currentStart; }),
            active.end());
    };

    // 查找空闲 caller-saved 寄存器
    auto findFreeCallerReg = [&]() -> int {
        bool used[NUM_CALLER_SAVED] = {};
        for (auto &a : active) {
            if (a.physReg < NUM_CALLER_SAVED) used[a.physReg] = true;
        }
        for (int i = 0; i < NUM_CALLER_SAVED; ++i)
            if (!used[i]) return i;
        return -1;
    };

    // 活跃 callee-saved 区间（跨越 call 的值使用）
    std::vector<ActiveInterval> activeCallee;

    auto removeExpiredCallee = [&](int currentStart) {
        activeCallee.erase(
            std::remove_if(activeCallee.begin(), activeCallee.end(),
                           [currentStart](const ActiveInterval &a) { return a.end <= currentStart; }),
            activeCallee.end());
    };

    // 查找空闲 callee-saved 寄存器
    auto findFreeCalleeReg = [&]() -> int {
        bool used[NUM_PHYS_REGS - NUM_CALLER_SAVED] = {};
        for (auto &a : activeCallee) {
            int idx = a.physReg - NUM_CALLER_SAVED;
            if (idx >= 0 && idx < NUM_PHYS_REGS - NUM_CALLER_SAVED) used[idx] = true;
        }
        for (int i = 0; i < NUM_PHYS_REGS - NUM_CALLER_SAVED; ++i)
            if (!used[i]) return i + NUM_CALLER_SAVED;
        return -1;
    };

    int nextSpillSlot = 0;

    // ABI 参数寄存器索引（rcx, rdx, r8, r9）
    static const int paramPhysRegs[] = {6, 3, 4, 5}; // PHYS_REG_NAMES 索引

    for (auto &interval : sorted) {
        std::cerr << "[RegAlloc] vreg=" << interval.vreg << " [" << interval.start << "," << interval.end << ")" << std::endl;
        removeExpired(interval.start);
        removeExpiredCallee(interval.start);

        bool callCross = crossesCall(interval, callPositions);

        // 参数寄存器对齐：参数 vreg 优先分配到对应的 ABI 寄存器
        if (argVregStart >= 0 && interval.vreg >= argVregStart &&
            interval.vreg < argVregStart + numArgs && !callCross) {
            int argIdx = interval.vreg - argVregStart;
            int preferredReg = paramPhysRegs[argIdx];
            // 检查首选寄存器是否空闲
            bool used = false;
            for (auto &a : active) {
                if (a.physReg == preferredReg) { used = true; break; }
            }
            if (!used) {
                result.vregToPhysReg[interval.vreg] = preferredReg;
                active.push_back({interval.vreg, interval.end, preferredReg});
                std::sort(active.begin(), active.end(),
                          [](const ActiveInterval &a, const ActiveInterval &b) { return a.end < b.end; });
                continue;
            }
        }

        if (callCross) {
            // 优先分配 callee-saved 寄存器
            int calleeReg = findFreeCalleeReg();
            if (calleeReg >= 0) {
                result.vregToPhysReg[interval.vreg] = calleeReg;
                result.usedCalleeSavedRegs.insert(calleeReg);
                activeCallee.push_back({interval.vreg, interval.end, calleeReg});
                std::sort(activeCallee.begin(), activeCallee.end(),
                          [](const ActiveInterval &a, const ActiveInterval &b) { return a.end < b.end; });
            } else {
                // callee-saved 耗尽，溢出到栈
                nextSpillSlot += 8;
                nextSpillSlot = (nextSpillSlot + 7) & ~7;
                result.vregToSpillSlot[interval.vreg] = nextSpillSlot;
            }
        } else {
            // 不跨越 call：使用 caller-saved 寄存器
            int freeReg = findFreeCallerReg();
            if (freeReg >= 0) {
                result.vregToPhysReg[interval.vreg] = freeReg;
                active.push_back({interval.vreg, interval.end, freeReg});
                std::sort(active.begin(), active.end(),
                          [](const ActiveInterval &a, const ActiveInterval &b) { return a.end < b.end; });
            } else {
                // caller-saved 耗尽，尝试 spill 最远结束的区间
                int maxEnd = -1, maxIdx = -1;
                for (size_t i = 0; i < active.size(); ++i) {
                    if (active[i].end > maxEnd) { maxEnd = active[i].end; maxIdx = static_cast<int>(i); }
                }
                if (maxIdx >= 0 && active[maxIdx].end > interval.end) {
                    // spill 最远的，复用其寄存器
                    int regToReuse = active[maxIdx].physReg;
                    nextSpillSlot += 8;
                    nextSpillSlot = (nextSpillSlot + 7) & ~7;
                    result.vregToSpillSlot[active[maxIdx].vreg] = nextSpillSlot;
                    result.vregToPhysReg.erase(active[maxIdx].vreg);
                    result.vregToPhysReg[interval.vreg] = regToReuse;
                    active[maxIdx] = {interval.vreg, interval.end, regToReuse};
                } else {
                    // 当前区间更长或相等，溢出当前
                    nextSpillSlot += 8;
                    nextSpillSlot = (nextSpillSlot + 7) & ~7;
                    result.vregToSpillSlot[interval.vreg] = nextSpillSlot;
                }
            }
        }
    }

    result.spillSlotSize = nextSpillSlot;
    for (auto &[vreg, phys] : result.vregToPhysReg) {
        std::cerr << "[RegAlloc] vreg=" << vreg << " -> " << PHYS_REG_NAMES[phys] << std::endl;
    }
    for (auto &[vreg, slot] : result.vregToSpillSlot) {
        std::cerr << "[RegAlloc] vreg=" << vreg << " -> spill [rbp-" << slot << "]" << std::endl;
    }
    return result;
}

} // namespace ir
