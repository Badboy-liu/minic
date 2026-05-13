#pragma once
#include "IRModule.h"
#include "RegAlloc.h"
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ir {

class IRCodeGenerator {
public:
    std::string generate(const IRModule &module);

private:
    std::ostringstream out;
    std::unordered_map<const IRValue *, int> valueSlots; // 溢出值 → 栈偏移
    std::unordered_map<const IRValue *, int> allocaSlots; // alloca 结果 → 栈偏移（地址）
    int slotOffset = 0;
    int stackFrameSize = 0;
    std::unordered_map<const IRBasicBlock *, std::string> blockLabels;
    int labelCounter = 0;

    // 寄存器分配结果
    RegAllocResult regAlloc;
    std::unordered_map<const IRInstruction *, int> vregMap; // IRInstruction* → vreg id
    std::unordered_map<const IRValue *, int> argVregMap;   // IRArgument* → vreg id
    int argVregStart = 0; // 参数 vreg 起始 ID

    // 寄存器状态追踪：消除冗余 mov
    static const int NUM_TRACKED_REGS = 16;
    const IRValue *regContent[NUM_TRACKED_REGS] = {}; // 物理寄存器当前持有的值
    int regIndex(const std::string &name); // 寄存器名 → 索引
    void updateRegContent(const std::string &reg, const IRValue *val);
    void invalidateRegContent(const std::string &reg);

    // ICmp 跳过集合：generateICmpBranch 消费的原始 ICmp，无需发射
    std::unordered_set<const IRInstruction *> skippedICmps;

    // callee-saved 寄存器列表（排序后）
    std::vector<int> calleeRegs;


    // 窥孔优化：缓存上一条输出的指令
    std::string lastEmittedInst;

    void generateFunction(const IRFunction &func);
    void generateBasicBlock(const IRBasicBlock &bb);
    void generateInstruction(const IRInstruction &inst);

    void emitLine(const std::string &line);
    void emitLabel(const std::string &label);

    // 将任意值加载到指定物理寄存器
    void loadValToReg(const IRValue *val, const std::string &reg);

    // 将寄存器中的结果存储到指令结果的位置（寄存器或溢出槽）
    void storeResult(const IRInstruction *inst, const std::string &reg);

    int getSlot(const IRValue *val);
    int allocSlot(const IRValue *val, int size = 8);
    void loadToRax(const IRValue *val);

    void generateICmp(const IRInstruction &inst);
    void generateICmpBranch(const IRInstruction &cmp, const IRBasicBlock *trueBB, const IRBasicBlock *falseBB, const IRBasicBlock *currentBB);
    void generateBinaryOp(const IRInstruction &inst);

    // phi copy resolver：在分支前将 incoming 值写入目标块的 phi 栈槽
    void emitPhiCopies(const IRBasicBlock *targetBB, const IRBasicBlock *currentBB);

    // 获取值的物理寄存器名（空字符串表示不在物理寄存器中）
    std::string getPhysRegName(const IRValue *val);

    std::string blockName(const IRBasicBlock *bb);
    std::string valueStr(const IRValue *val);
};

} // namespace ir
