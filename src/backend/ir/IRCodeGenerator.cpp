#include "IRCodeGenerator.h"
#include "IRPrinter.h"
#include "Liveness.h"
#include "RegAlloc.h"
#include <algorithm>
#include <cassert>

namespace ir {

std::string IRCodeGenerator::generate(const IRModule &module) {
    out.str("");
    out.clear();

    emitLine("default rel");
    emitLine("extern ExitProcess");
    emitLine("extern fmod");

    bool hasMain = false;
    for (auto &fn : module.functions) {
        emitLine("global " + fn->name);
        if (fn->name == "main") hasMain = true;
    }

    emitLine("");
    emitLine("section .text");

    for (auto &fn : module.functions) {
        IRPrinter printer;
        std::cerr << "[CodeGen] IR for " << fn->name << ":\n" << printer.printFunction(*fn) << std::endl;
        generateFunction(*fn);
    }

    // 入口点桩代码：mainCRTStartup 调用 main 并退出
    if (hasMain) {
        emitLine("");
        emitLine("global mainCRTStartup");
        emitLine("mainCRTStartup:");
        emitLine("    sub rsp, 40");
        emitLine("    call main");
        emitLine("    mov ecx, eax");
        emitLine("    call ExitProcess");
    }

    return out.str();
}

void IRCodeGenerator::generateFunction(const IRFunction &func) {
    std::cerr << "[CodeGen] generateFunction: " << func.name << std::endl;
    valueSlots.clear();
    allocaSlots.clear();
    blockLabels.clear();

    vregMap.clear();
    argVregMap.clear();
    calleeRegs.clear();
    slotOffset = 0;
    labelCounter = 0;
    for (int i = 0; i < NUM_TRACKED_REGS; ++i) regContent[i] = nullptr;
    skippedICmps.clear();
    lastEmittedInst.clear();

    // 构建 vregMap：IRInstruction* → vreg id
    for (auto &bb : func.basicBlocks) {
        for (auto &inst : bb.instructions) {
            if (inst.id >= 0) {
                vregMap[&inst] = inst.id;
            }
        }
    }

    // 为参数分配 vreg ID（在指令 ID 之后）
    argVregStart = func.nextValueId;
    for (size_t i = 0; i < func.arguments.size(); ++i) {
        int vregId = argVregStart + static_cast<int>(i);
        argVregMap[func.arguments[i].get()] = vregId;
    }

    // 活跃性分析 + 寄存器分配
    std::cerr << "[CodeGen] liveness analysis..." << std::endl;
    LivenessAnalysis liveness;
    liveness.setArgVregStart(argVregStart);
    liveness.compute(func);
    std::cerr << "[CodeGen] register allocation..." << std::endl;
    LinearScanAllocator allocator;
    regAlloc = allocator.allocate(liveness.getIntervals(), liveness.getCallPositions(),
                                   argVregStart, static_cast<int>(func.arguments.size()));
    std::cerr << "[CodeGen] reg alloc done, blocks=" << func.basicBlocks.size() << std::endl;

    // 为基本块分配标签
    int bbId = 0;
    for (auto &bb : func.basicBlocks) {
        std::string label = bb.name.empty() ? ("bb_" + std::to_string(bbId)) : bb.name;
        std::string uniqueLabel = label;
        int suffix = 0;
        for (auto &[_, existing] : blockLabels) {
            if (existing == uniqueLabel) {
                uniqueLabel = label + "_" + std::to_string(++suffix);
            }
        }
        blockLabels[&bb] = uniqueLabel;
        bbId++;
    }

    // 参数不预分配栈槽——由寄存器分配器管理
    // ABI 参数寄存器（rcx/rdx/r8/r9）在函数入口通过 storeResult 保存

    // 收集使用的 callee-saved 寄存器（必须在 value slot 分配之前，避免栈帧重叠）
    calleeRegs.assign(regAlloc.usedCalleeSavedRegs.begin(),
                      regAlloc.usedCalleeSavedRegs.end());
    std::sort(calleeRegs.begin(), calleeRegs.end());

    // 初始化 slotOffset 跳过 callee-saved 寄存器保存区域
    // 栈帧布局：[rbp] = saved rbp, [rbp-8] = saved callee1, [rbp-16] = saved callee2, ...
    // value slots 必须从 callee-saved 区域之后开始
    slotOffset = static_cast<int>(calleeRegs.size()) * 8;

    // 预分配所有有结果指令的栈槽（phi copies 需要从栈槽读取值）
    // 使用实际类型大小（数组需要完整空间）
    for (auto &bb : func.basicBlocks) {
        for (auto &inst : bb.instructions) {
            if (inst.type.kind != IRTypeKind::Void) {
                int sz;
                // Alloca 的 type 是 ptr(elemType)，需要用 elementType 获取实际分配大小
                if (inst.opcode == IROpcode::Alloca && inst.type.elementType) {
                    sz = inst.type.elementType->byteSize();
                    std::cerr << "[PreAlloc] Alloca name=" << inst.name << " elemKind=" << static_cast<int>(inst.type.elementType->kind)
                              << " elemBW=" << inst.type.elementType->bitWidth << " sz=" << sz << std::endl;
                } else {
                    sz = inst.type.byteSize();
                    if (inst.opcode == IROpcode::Phi) {
                        std::cerr << "[PreAlloc] Phi name=" << inst.name << " ptr=" << &inst
                                  << " typeSz=" << sz << std::endl;
                    }
                }
                if (sz < 8) sz = 8; // 至少 8 字节（对齐）
                int slot = allocSlot(&inst, sz);
                if (inst.opcode == IROpcode::Phi) {
                    std::cerr << "[PreAlloc]   → Phi " << inst.name << " slot=" << slot << std::endl;
                }
            }
        }
    }

    std::cerr << "[CodeGen] slotOffset=" << slotOffset << " spillSlotSize=" << regAlloc.spillSlotSize
              << " numValueSlots=" << valueSlots.size() << std::endl;
    // 计算栈帧大小：溢出区 + alloca 区 + 对齐
    // 入口 RSP ≡ 8 (mod 16)，push rbp 后 ≡ 0，k 个 callee-saved push 后 ≡ -8k (mod 16)
    // sub rsp 需使 CALL 处 RSP ≡ 0 (mod 16)
    int totalPush = 1 + static_cast<int>(calleeRegs.size()); // rbp + callee-saved
    int totalLocal = regAlloc.spillSlotSize + slotOffset;
    totalLocal = (totalLocal + 15) & ~15;
    if (totalLocal < 32) totalLocal = 32;

    // 对齐调整：(totalPush * 8 + sub_rsp) % 16 == 0
    int subRsp = totalLocal;
    if ((totalPush % 2) == 0) {
        subRsp += 8; // 偶数个 push 需要额外 8 字节对齐
    }
    stackFrameSize = subRsp;

    emitLine("");
    emitLine(func.name + ":");

    // 函数序言
    emitLine("    push rbp");
    emitLine("    mov rbp, rsp");
    for (int reg : calleeRegs) {
        emitLine(std::string("    push ") + PHYS_REG_NAMES[reg]);
    }
    emitLine("    sub rsp, " + std::to_string(stackFrameSize));

    // 保存参数到寄存器分配结果（物理寄存器或溢出槽）
    static const char *paramRegs[] = {"rcx", "rdx", "r8", "r9"};
    for (size_t i = 0; i < func.arguments.size() && i < 4; ++i) {
        auto *arg = func.arguments[i].get();
        auto it = argVregMap.find(arg);
        if (it != argVregMap.end()) {
            int vregId = it->second;
            auto physIt = regAlloc.vregToPhysReg.find(vregId);
            if (physIt != regAlloc.vregToPhysReg.end()) {
                const std::string &dstReg = PHYS_REG_NAMES[physIt->second];
                if (dstReg != paramRegs[i]) {
                    emitLine("    mov " + dstReg + ", " + paramRegs[i]);
                }
            } else {
                auto spillIt = regAlloc.vregToSpillSlot.find(vregId);
                if (spillIt != regAlloc.vregToSpillSlot.end()) {
                    emitLine("    mov [rbp-" + std::to_string(spillIt->second) + "], " + paramRegs[i]);
                }
            }
        }
    }

    // 生成所有基本块
    // 预扫描：识别将被 generateICmpBranch 消费的原始 ICmp
    for (auto &bb : func.basicBlocks) {
        for (auto &inst : bb.instructions) {
            if (inst.opcode != IROpcode::CondBr || inst.getNumOperands() < 1) continue;
            auto *cond = inst.getOperand(0);
            auto *condInst = dynamic_cast<const IRInstruction *>(cond);
            if (!condInst || condInst->opcode != IROpcode::ICmp) continue;
            if (condInst->getNumOperands() < 3 || !condInst->getOperand(1)->isConstant()) continue;
            auto *intConst = dynamic_cast<const IRConstantInt *>(condInst->getOperand(1));
            auto kind = static_cast<ICmpKind>(reinterpret_cast<uintptr_t>(condInst->getOperand(2)));
            if (!intConst || intConst->value != 0 || kind != ICmpKind::NE) continue;
            auto *lhsInst = dynamic_cast<const IRInstruction *>(condInst->getOperand(0));
            if (lhsInst && lhsInst->opcode == IROpcode::ICmp) {
                skippedICmps.insert(lhsInst);
            }
        }
    }

    for (auto &bb : func.basicBlocks) {
        std::cerr << "[CodeGen] generating block: " << bb.name << " id=" << bb.id << std::endl;
        generateBasicBlock(bb);
    }
    std::cerr << "[CodeGen] generateFunction done: " << func.name << std::endl;
}

void IRCodeGenerator::generateBasicBlock(const IRBasicBlock &bb) {
    auto it = blockLabels.find(&bb);
    if (it != blockLabels.end()) {
        if (it->second != "entry") {
            emitLabel(it->second);
        }
    }

    // 调试：dump 块中的指令列表
    std::cerr << "[BB] " << bb.name << " instructions:";
    for (auto &inst : bb.instructions) {
        std::cerr << " " << inst.name << "(op=" << static_cast<int>(inst.opcode) << ")";
    }
    std::cerr << std::endl;

    for (auto &inst : bb.instructions) {
        generateInstruction(inst);
    }
}

// 将 32-bit 寄存器名提升为 64-bit（eax→rax, edx→rdx, ecx→rcx, ...）
static std::string to64(const std::string &reg) {
    if (reg == "eax") return "rax";
    if (reg == "edx") return "rdx";
    if (reg == "ecx") return "rcx";
    if (reg == "ebx") return "rbx";
    if (reg == "esi") return "rsi";
    if (reg == "edi") return "rdi";
    if (reg == "r8d") return "r8";
    if (reg == "r9d") return "r9";
    if (reg == "r10d") return "r10";
    if (reg == "r11d") return "r11";
    if (reg == "r12d") return "r12";
    if (reg == "r13d") return "r13";
    if (reg == "r14d") return "r14";
    if (reg == "r15d") return "r15";
    if (reg == "al") return "rax";
    if (reg == "dl") return "rdx";
    if (reg == "cl") return "rcx";
    return reg; // 已经是 64-bit
}

// 将 64-bit 寄存器名降为 32-bit（rax→eax, rdx→edx, r13→r13d, ...）
static std::string to32(const std::string &reg) {
    if (reg == "rax") return "eax";
    if (reg == "rdx") return "edx";
    if (reg == "rcx") return "ecx";
    if (reg == "rbx") return "ebx";
    if (reg == "rsi") return "esi";
    if (reg == "rdi") return "edi";
    if (reg == "r8") return "r8d";
    if (reg == "r9") return "r9d";
    if (reg == "r10") return "r10d";
    if (reg == "r11") return "r11d";
    if (reg == "r12") return "r12d";
    if (reg == "r13") return "r13d";
    if (reg == "r14") return "r14d";
    if (reg == "r15") return "r15d";
    return reg;
}

void IRCodeGenerator::generateInstruction(const IRInstruction &inst) {
    switch (inst.opcode) {
    case IROpcode::Alloca: {
        // alloca 始终使用栈槽（需要地址）
        // 复用预分配的栈槽（避免重复分配导致栈帧过大）
        auto vit = valueSlots.find(&inst);
        bool found = (vit != valueSlots.end());
        int fallbackSz = inst.type.elementType ? inst.type.elementType->byteSize() : 8;
        if (fallbackSz < 8) fallbackSz = 8;
        int slot = found ? vit->second : allocSlot(&inst, fallbackSz);
        allocaSlots[&inst] = slot;
        std::cerr << "[Alloca] name=" << inst.name << " ptr=" << &inst << " slot=" << slot << " preAllocated=" << found << std::endl;
        break;
    }
    case IROpcode::Load: {
        auto *ptr = inst.getOperand(0);
        auto allocaIt = allocaSlots.find(ptr);
        std::cerr << "[Load] name=" << inst.name << " ptrType=" << ptr->type.toString()
                  << " ptrKind=" << static_cast<int>(ptr->type.kind)
                  << " ptrIsPtr=" << ptr->type.isPointer()
                  << " ptrIsArr=" << ptr->type.isArray()
                  << " ptrElemSz=" << (ptr->type.elementType ? ptr->type.elementType->byteSize() : -1)
                  << " resType=" << inst.type.toString()
                  << " resSz=" << inst.type.byteSize() << std::endl;

        // 确定目标寄存器：优先使用分配的物理寄存器，避免覆盖其他活跃值
        std::string dstReg = "rax";
        {
            auto vregIt = vregMap.find(&inst);
            if (vregIt != vregMap.end()) {
                auto physIt = regAlloc.vregToPhysReg.find(vregIt->second);
                if (physIt != regAlloc.vregToPhysReg.end()) {
                    dstReg = PHYS_REG_NAMES[physIt->second];
                }
            }
        }

        // 根据加载值的类型确定操作大小
        // 直接使用 Load 指令的结果类型（inst.type），它正确反映了要加载的值类型
        // 例如：ptrType=[5 x i32]* 但 resType=i32 → 应该加载 4 字节
        int ptrElemSize = inst.type.byteSize();

        // 确定内存地址表达式
        std::string memAddr;
        if (allocaIt != allocaSlots.end()) {
            memAddr = "[rbp-" + std::to_string(allocaIt->second) + "]";
        } else {
            // 用 rcx 加载指针（避免覆盖目标寄存器）
            loadValToReg(ptr, "rcx");
            memAddr = "[rcx]";
        }

        if (ptrElemSize == 1) {
            // 8-bit: 直接用 movzx dstReg, byte [addr]，避免中间 eax 覆盖活跃的 rax
            emitLine("    movzx " + dstReg + ", byte " + memAddr);
        } else if (ptrElemSize == 4) {
            // 32-bit: 直接用 movsxd dstReg, dword [addr]，避免中间 eax 覆盖活跃的 rax
            emitLine("    movsxd " + dstReg + ", dword " + memAddr);
        } else {
            // 64-bit/pointer: 直接 mov dstReg, [addr]
            if (dstReg == "rax") {
                emitLine("    mov rax, " + memAddr);
            } else {
                emitLine("    mov " + dstReg + ", " + memAddr);
            }
        }
        storeResult(&inst, dstReg);
        break;
    }
    case IROpcode::Store: {
        auto *val = inst.getOperand(0);
        auto *ptr = inst.getOperand(1);

        auto allocaIt = allocaSlots.find(ptr);
        bool ptrIsAlloca = (allocaIt != allocaSlots.end());

        auto *intConst = val->isConstant() ? dynamic_cast<const IRConstantInt *>(val) : nullptr;

        if (intConst && intConst->value >= -2147483648LL && intConst->value <= 2147483647LL) {
            // 常量值：直接 mov [addr], imm，无需临时寄存器
            std::string immStr = std::to_string(static_cast<int32_t>(intConst->value));
            if (ptrIsAlloca) {
                emitLine("    mov dword [rbp-" + std::to_string(allocaIt->second) + "], " + immStr);
            } else {
                loadValToReg(ptr, "rcx");
                emitLine("    mov dword [rcx], " + immStr);
            }
        } else {
            // 非常量值：需要临时寄存器
            // 确定指针的物理寄存器（ptr 被 Store 消费，其寄存器可复用）
            std::string ptrPhysReg;
            if (!ptrIsAlloca) {
                auto *ptrInst = dynamic_cast<const IRInstruction *>(ptr);
                if (ptrInst) {
                    ptrPhysReg = getPhysRegName(ptrInst);
                }
            }

            // 选择值寄存器：
            // 1. 如果值已经在物理寄存器中且存储到 alloca，直接用该寄存器
            // 2. 否则优先用指针的物理寄存器（Store 后不再需要）
            // 3. 存储到 alloca 时用 rcx（不需指针加载），否则用 rdx
            std::string valReg;
            // 检查值是否已在物理寄存器中
            auto *valAsArg = dynamic_cast<const IRArgument *>(val);
            if (valAsArg && ptrIsAlloca) {
                auto ait = argVregMap.find(val);
                if (ait != argVregMap.end()) {
                    auto pit = regAlloc.vregToPhysReg.find(ait->second);
                    if (pit != regAlloc.vregToPhysReg.end()) {
                        valReg = PHYS_REG_NAMES[pit->second];
                    }
                }
            }
            if (valReg.empty()) {
                auto *valAsInst = dynamic_cast<const IRInstruction *>(val);
                if (valAsInst) {
                    valReg = getPhysRegName(valAsInst);
                }
            }
            if (valReg.empty()) {
                if (!ptrPhysReg.empty() && ptrPhysReg != "rcx" && ptrPhysReg != "rdx") {
                    valReg = ptrPhysReg;
                } else {
                    valReg = ptrIsAlloca ? "rcx" : "rdx";
                }
            }

            // 确定存储大小：使用被存储值的类型大小
            // 例如 ptrType=[5 x i32]* 但 val type=i32 → 应该存储 4 字节
            int storeSize = val->type.byteSize();
            if (storeSize <= 0) storeSize = 8; // 默认 64-bit
            // 选择正确的值寄存器名和内存前缀
            std::string valRegForStore = valReg;
            std::string memPrefix;
            if (storeSize == 1) {
                // 8-bit: 映射到低字节寄存器
                if (valReg == "rdx" || valReg == "edx") valRegForStore = "dl";
                else if (valReg == "rcx" || valReg == "ecx") valRegForStore = "cl";
                else if (valReg == "rax" || valReg == "eax") valRegForStore = "al";
                else valRegForStore = valReg; // r8+ 没有低字节别名，用原始名
                memPrefix = "byte ";
            } else if (storeSize == 4) {
                valRegForStore = to32(valReg);
                memPrefix = "dword ";
            } else {
                memPrefix = "";
            }

            // 加载地址到 rcx
            if (!ptrIsAlloca) {
                loadValToReg(ptr, "rcx");
            }

            // 加载值到 valReg
            if (intConst) {
                emitLine("    mov " + valReg + ", " + std::to_string(intConst->value));
            } else {
                auto valAllocaIt = allocaSlots.find(val);
                if (valAllocaIt != allocaSlots.end()) {
                    emitLine("    lea " + valReg + ", [rbp-" + std::to_string(valAllocaIt->second) + "]");
                } else {
                    loadValToReg(val, valReg);
                }
            }

            // 存储到目标
            if (ptrIsAlloca) {
                emitLine("    mov " + memPrefix + "[rbp-" + std::to_string(allocaIt->second) + "], " + valRegForStore);
            } else {
                emitLine("    mov " + memPrefix + "[rcx], " + valRegForStore);
            }
        }
        break;
    }
    case IROpcode::GEP: {
        auto *base = inst.getOperand(0);
        auto *idx = inst.getOperand(1);

        auto allocaIt = allocaSlots.find(base);
        if (allocaIt != allocaSlots.end()) {
            emitLine("    lea rax, [rbp-" + std::to_string(allocaIt->second) + "]");
        } else {
            loadValToReg(base, "rax");
        }
        loadValToReg(idx, "rcx");
        emitLine("    lea rax, [rax + rcx]");
        storeResult(&inst, "rax");
        break;
    }
    case IROpcode::Add:
    case IROpcode::Sub:
    case IROpcode::Mul:
    case IROpcode::SDiv:
    case IROpcode::UDiv:
    case IROpcode::SRem:
    case IROpcode::URem:
    case IROpcode::And:
    case IROpcode::Or:
    case IROpcode::Xor:
    case IROpcode::Shl:
    case IROpcode::LShr:
    case IROpcode::AShr:
    case IROpcode::FAdd:
    case IROpcode::FSub:
    case IROpcode::FMul:
    case IROpcode::FDiv:
        generateBinaryOp(inst);
        break;
    case IROpcode::ICmp: {
        // 优化：跳过两种 ICmp：
        // 1. tobool 模式（icmp ne %orig, 0）且唯一使用是 CondBr
        // 2. 被 generateICmpBranch 消费的原始 ICmp（在 skippedICmps 中）
        bool skip = false;
        if (skippedICmps.count(&inst)) {
            skip = true;
        } else if (inst.uses.size() == 1 && inst.uses[0].user &&
                   inst.uses[0].user->opcode == IROpcode::CondBr &&
                   inst.getNumOperands() >= 3 && inst.getOperand(1)->isConstant()) {
            auto *intConst = dynamic_cast<const IRConstantInt *>(inst.getOperand(1));
            auto kind = static_cast<ICmpKind>(reinterpret_cast<uintptr_t>(inst.getOperand(2)));
            if (intConst && intConst->value == 0 && kind == ICmpKind::NE) {
                if (auto *lhsInst = dynamic_cast<const IRInstruction *>(inst.getOperand(0))) {
                    if (lhsInst->opcode == IROpcode::ICmp) {
                        skip = true;
                    }
                }
            }
        }
        if (!skip) {
            generateICmp(inst);
        }
        break;
    }
    case IROpcode::Br: {
        auto *target = reinterpret_cast<const IRBasicBlock *>(inst.getOperand(0));
        emitPhiCopies(target, inst.parentBB);
        auto it = blockLabels.find(target);
        if (it != blockLabels.end()) {
            emitLine("    jmp " + it->second);
        }
        break;
    }
    case IROpcode::CondBr: {
        auto *cond = inst.getOperand(0);
        auto *trueBB = reinterpret_cast<const IRBasicBlock *>(inst.getOperand(1));
        auto *falseBB = reinterpret_cast<const IRBasicBlock *>(inst.getOperand(2));

        std::cerr << "[CondBr] cond=" << cond << " trueBB=" << (trueBB ? trueBB->name : "null")
                  << " falseBB=" << (falseBB ? falseBB->name : "null");
        if (auto *ci = dynamic_cast<const IRConstantInt*>(cond)) std::cerr << " condInt=" << ci->value;
        if (auto *condI = dynamic_cast<const IRInstruction*>(cond)) {
            std::cerr << " condOp=" << static_cast<int>(condI->opcode) << " uses=" << condI->uses.size();
            if (condI->opcode == IROpcode::ICmp && condI->getNumOperands() >= 3) {
                auto *cLhs = condI->getOperand(0);
                auto *cRhs = condI->getOperand(1);
                std::cerr << " icmpLhs=" << cLhs << " icmpRhs=" << cRhs;
                if (auto *li = dynamic_cast<const IRConstantInt*>(cLhs)) std::cerr << " icmpLhsInt=" << li->value;
                if (auto *ri = dynamic_cast<const IRConstantInt*>(cRhs)) std::cerr << " icmpRhsInt=" << ri->value;
            }
        }
        std::cerr << std::endl;

        // 优化：如果条件来自 ICmp（可能经过 ZExt/SExt），直接发射 cmp+jCC
        // 跳过 set+movzx+store+load+cmp rax,0 冗余链
        const IRInstruction *cmpInst = nullptr;
        if (auto *condInst = dynamic_cast<const IRInstruction *>(cond)) {
            if (condInst->opcode == IROpcode::ICmp) {
                cmpInst = condInst;
            } else if ((condInst->opcode == IROpcode::ZExt ||
                        condInst->opcode == IROpcode::SExt) &&
                       condInst->getNumOperands() >= 1) {
                if (auto *src = dynamic_cast<const IRInstruction *>(condInst->getOperand(0))) {
                    if (src->opcode == IROpcode::ICmp) {
                        cmpInst = src;
                    }
                }
            }
        }

        if (cmpInst && cmpInst->uses.size() == 1 && cmpInst->getNumOperands() >= 3) {
            generateICmpBranch(*cmpInst, trueBB, falseBB, inst.parentBB);
        } else if (cond->isConstant()) {
            // 常量条件：直接跳转到正确的分支
            auto *intConst = dynamic_cast<const IRConstantInt *>(cond);
            if (intConst && intConst->value != 0) {
                emitPhiCopies(trueBB, inst.parentBB);
                auto trueIt = blockLabels.find(trueBB);
                if (trueIt != blockLabels.end()) {
                    emitLine("    jmp " + trueIt->second);
                }
            } else {
                emitPhiCopies(falseBB, inst.parentBB);
                auto falseIt = blockLabels.find(falseBB);
                if (falseIt != blockLabels.end()) {
                    emitLine("    jmp " + falseIt->second);
                }
            }
        } else {
            // fallback：原始逻辑
            loadValToReg(cond, "rax");
            emitLine("    cmp rax, 0");
            auto trueIt = blockLabels.find(trueBB);
            auto falseIt = blockLabels.find(falseBB);
            emitPhiCopies(trueBB, inst.parentBB);
            if (trueIt != blockLabels.end()) {
                emitLine("    jne " + trueIt->second);
            }
            emitPhiCopies(falseBB, inst.parentBB);
            if (falseIt != blockLabels.end()) {
                emitLine("    jmp " + falseIt->second);
            }
        }
        break;
    }
    case IROpcode::Ret: {
        if (!inst.operands.empty()) {
            auto *val = inst.getOperand(0);
            loadValToReg(val, "rax");
        }
        // 恢复栈帧：add rsp 跳过 sub rsp 分配的区域
        emitLine("    add rsp, " + std::to_string(stackFrameSize));
        // 恢复 callee-saved 寄存器（逆序弹出）
        for (auto it = calleeRegs.rbegin(); it != calleeRegs.rend(); ++it) {
            emitLine(std::string("    pop ") + PHYS_REG_NAMES[*it]);
        }
        emitLine("    pop rbp");
        emitLine("    ret");
        break;
    }
    case IROpcode::Phi: {
        // phi 结果已在 generateFunction 中预分配栈槽
        // 值在前驱块末尾的 phi copy 中写入
        break;
    }
    case IROpcode::Call: {
        auto *callee = inst.getOperand(0);
        int numArgs = inst.getNumOperands() - 1;

        static const char *paramRegs[] = {"rcx", "rdx", "r8", "r9"};
        for (int i = 0; i < numArgs && i < 4; ++i) {
            auto *arg = inst.getOperand(i + 1);
            loadValToReg(arg, paramRegs[i]);
        }

        std::string calleeName = callee->name;
        emitLine("    lea r11, [rel " + calleeName + "]");
        emitLine("    call r11");

        if (!inst.type.isVoid()) {
            storeResult(&inst, "rax");
        }
        break;
    }
    case IROpcode::ZExt:
    case IROpcode::SExt:
    case IROpcode::Trunc:
    case IROpcode::BitCast:
    case IROpcode::IntToPtr:
    case IROpcode::PtrToInt:
    case IROpcode::FPToUI:
    case IROpcode::FPToSI:
    case IROpcode::UIToFP:
    case IROpcode::SIToFP: {
        auto *val = inst.getOperand(0);
        loadValToReg(val, "rax");
        storeResult(&inst, "rax");
        break;
    }
    case IROpcode::Select: {
        auto *cond = inst.getOperand(0);
        auto *trueVal = inst.getOperand(1);
        auto *falseVal = inst.getOperand(2);

        std::string trueLabel = "sel_true_" + std::to_string(labelCounter++);
        std::string endLabel = "sel_end_" + std::to_string(labelCounter++);

        loadValToReg(cond, "rax");
        emitLine("    cmp rax, 0");
        emitLine("    jne " + trueLabel);
        loadValToReg(falseVal, "rax");
        emitLine("    jmp " + endLabel);
        emitLabel(trueLabel);
        loadValToReg(trueVal, "rax");
        emitLabel(endLabel);
        storeResult(&inst, "rax");
        break;
    }
    case IROpcode::ExtractValue:
    case IROpcode::InlineAsm:
    case IROpcode::Switch:
    default:
        break;
    }
}

void IRCodeGenerator::generateBinaryOp(const IRInstruction &inst) {
    auto *lhs = inst.getOperand(0);
    auto *rhs = inst.getOperand(1);

    // 处理除法/取模的特殊情况：rdx:rax / divisor
    if (inst.opcode == IROpcode::SDiv || inst.opcode == IROpcode::UDiv ||
        inst.opcode == IROpcode::SRem || inst.opcode == IROpcode::URem) {
        std::string rhsReg = getPhysRegName(rhs);
        if (rhsReg == "rcx") {
            loadValToReg(lhs, "rax");
        } else if (rhsReg == "rax") {
            emitLine("    mov rcx, rax");
            loadValToReg(lhs, "rax");
        } else {
            loadValToReg(lhs, "rax");
            loadValToReg(rhs, "rcx");
        }
        // 除零保护：触发 ud2（SIGILL）而非 Windows 异常对话框
        emitLine("    test rcx, rcx");
        std::string divOkLabel = ".div_ok_" + std::to_string(labelCounter++);
        emitLine("    jnz " + divOkLabel);
        emitLine("    ud2");
        emitLine(divOkLabel + ":");
        emitLine("    cqo");
        emitLine(inst.opcode == IROpcode::SDiv || inst.opcode == IROpcode::SRem
                     ? "    idiv rcx"
                     : "    div rcx");
        if (inst.opcode == IROpcode::SRem || inst.opcode == IROpcode::URem) {
            emitLine("    mov rax, rdx");
        }
        storeResult(&inst, "rax");
        return;
    }

    bool isFloat = inst.type.isFloat();
    bool is64 = inst.type.bitWidth == 64;

    // 检查右操作数是否为常量（立即数折叠）
    auto *intConst = dynamic_cast<const IRConstantInt *>(rhs);
    if (intConst && !isFloat) {
        int64_t imm = intConst->value;
        // 小立即数可直接编码
        if (imm >= -128 && imm <= 127) {
            loadValToReg(lhs, "rax");
            std::string immStr = std::to_string(imm);
            switch (inst.opcode) {
            case IROpcode::Add:  emitLine(is64 ? "    add rax, " + immStr : "    add eax, " + immStr); break;
            case IROpcode::Sub:  emitLine(is64 ? "    sub rax, " + immStr : "    sub eax, " + immStr); break;
            case IROpcode::Mul:  emitLine(is64 ? "    imul rax, " + immStr : "    imul eax, " + immStr); break;
            case IROpcode::And:  emitLine(is64 ? "    and rax, " + immStr : "    and eax, " + immStr); break;
            case IROpcode::Or:   emitLine(is64 ? "    or rax, " + immStr  : "    or eax, " + immStr);  break;
            case IROpcode::Xor:  emitLine(is64 ? "    xor rax, " + immStr : "    xor eax, " + immStr); break;
            case IROpcode::Shl:  emitLine(is64 ? "    shl rax, " + immStr : "    shl eax, " + immStr);  break;
            case IROpcode::LShr: emitLine(is64 ? "    shr rax, " + immStr : "    shr eax, " + immStr);  break;
            case IROpcode::AShr: emitLine(is64 ? "    sar rax, " + immStr : "    sar eax, " + immStr);  break;
            default:
                // 不支持立即数的操作码，fallback 到寄存器形式
                goto load_rhs;
            }
            storeResult(&inst, "rax");
            return;
        }
    }

load_rhs:
    {
        std::string rhsPhysReg = getPhysRegName(rhs);
        if (rhsPhysReg == "rcx") {
            // rhs 已在 rcx 中：直接加载 lhs 到 rax，无需移动 rhs
            loadValToReg(lhs, "rax");
        } else if (rhsPhysReg == "rax") {
            // rhs 在 rax 中：保存到 rcx，再加载 lhs 到 rax
            emitLine("    mov rcx, rax");
            loadValToReg(lhs, "rax");
        } else {
            // rhs 在其他位置：先加载 lhs 到 rax（不会覆盖 rhs），再加载 rhs 到 rcx
            loadValToReg(lhs, "rax");
            loadValToReg(rhs, "rcx");
        }
    }

    switch (inst.opcode) {
    case IROpcode::Add:  emitLine(is64 ? "    add rax, rcx" : "    add eax, ecx"); break;
    case IROpcode::Sub:  emitLine(is64 ? "    sub rax, rcx" : "    sub eax, ecx"); break;
    case IROpcode::Mul:  emitLine(is64 ? "    imul rax, rcx" : "    imul eax, ecx"); break;
    case IROpcode::And:  emitLine(is64 ? "    and rax, rcx" : "    and eax, ecx"); break;
    case IROpcode::Or:   emitLine(is64 ? "    or rax, rcx"  : "    or eax, ecx");  break;
    case IROpcode::Xor:  emitLine(is64 ? "    xor rax, rcx" : "    xor eax, ecx"); break;
    case IROpcode::Shl:  emitLine(is64 ? "    shl rax, cl"  : "    shl eax, cl");  break;
    case IROpcode::LShr: emitLine(is64 ? "    shr rax, cl"  : "    shr eax, cl");  break;
    case IROpcode::AShr: emitLine(is64 ? "    sar rax, cl"  : "    sar eax, cl");  break;
    default: break;
    }

    if (!isFloat) {
        storeResult(&inst, "rax");
    }
}

void IRCodeGenerator::emitPhiCopies(const IRBasicBlock *targetBB, const IRBasicBlock *currentBB) {
    if (!targetBB || !currentBB) return;
    // 重置窥孔优化状态，避免跨块指令被误判为冗余
    lastEmittedInst.clear();
    std::cerr << "[PhiCopy] target=" << targetBB->name << " from=" << currentBB->name
              << " phis=" << std::count_if(targetBB->instructions.begin(), targetBB->instructions.end(),
                   [](const IRInstruction &i) { return i.opcode == IROpcode::Phi; }) << std::endl;
    for (auto &inst : targetBB->instructions) {
        if (inst.opcode != IROpcode::Phi) break; // phi 在块开头，遇到非 phi 即停
        // 操作数布局：[val1, bb1, val2, bb2, ...]
        for (size_t i = 0; i + 1 < inst.operands.size(); i += 2) {
            auto *incomingBB = reinterpret_cast<const IRBasicBlock *>(inst.getOperand(static_cast<int>(i + 1)));
            std::cerr << "[PhiCopy]   phi=" << inst.name << " incomingBB=" << (incomingBB ? incomingBB->name : "null")
                      << " currentBB=" << currentBB->name << " match=" << (incomingBB == currentBB) << std::endl;
            if (incomingBB == currentBB) {
                auto *val = inst.getOperand(static_cast<int>(i));
                auto slotIt = valueSlots.find(&inst);
                std::cerr << "[PhiCopy]     val=" << val << " phiSlot=" << (slotIt != valueSlots.end() ? slotIt->second : -1) << std::endl;
                if (slotIt != valueSlots.end()) {
                    // 用 loadValToReg 正确读取 incoming 值（可能在物理寄存器、溢出槽或值槽中）
                    loadValToReg(val, "rax");
                    emitLine("    mov [rbp-" + std::to_string(slotIt->second) + "], rax");
                }
                break;
            }
        }
    }
}

void IRCodeGenerator::generateICmp(const IRInstruction &inst) {
    auto *lhs = inst.getOperand(0);
    auto *rhs = inst.getOperand(1);
    auto kind = static_cast<ICmpKind>(reinterpret_cast<uintptr_t>(inst.getOperand(2)));

    loadValToReg(lhs, "rax");
    if (rhs->isConstant()) {
        auto *intConst = dynamic_cast<const IRConstantInt *>(rhs);
        if (intConst) {
            emitLine("    cmp eax, " + std::to_string(intConst->value));
        }
    } else {
        loadValToReg(rhs, "rcx");
        emitLine("    cmp eax, ecx");
    }

    const char *setInstr = nullptr;
    switch (kind) {
    case ICmpKind::EQ:  setInstr = "sete";  break;
    case ICmpKind::NE:  setInstr = "setne"; break;
    case ICmpKind::SLT: setInstr = "setl";  break;
    case ICmpKind::SLE: setInstr = "setle"; break;
    case ICmpKind::SGT: setInstr = "setg";  break;
    case ICmpKind::SGE: setInstr = "setge"; break;
    case ICmpKind::ULT: setInstr = "setb";  break;
    case ICmpKind::ULE: setInstr = "setbe"; break;
    case ICmpKind::UGT: setInstr = "seta";  break;
    case ICmpKind::UGE: setInstr = "setae"; break;
    }

    emitLine(std::string("    ") + setInstr + " al");
    emitLine("    movzx eax, al");
    storeResult(&inst, "rax");
}

void IRCodeGenerator::generateICmpBranch(const IRInstruction &cmp,
                                          const IRBasicBlock *trueBB,
                                          const IRBasicBlock *falseBB,
                                          const IRBasicBlock *currentBB) {
    auto *lhs = cmp.getOperand(0);
    auto *rhs = cmp.getOperand(1);
    auto kind = static_cast<ICmpKind>(reinterpret_cast<uintptr_t>(cmp.getOperand(2)));

    std::cerr << "[ICmpBranch] lhs=" << lhs << " rhs=" << rhs
              << " kind=" << static_cast<int>(kind);
    if (auto *ci = dynamic_cast<const IRConstantInt*>(lhs)) std::cerr << " lhsInt=" << ci->value;
    if (auto *ci = dynamic_cast<const IRConstantInt*>(rhs)) std::cerr << " rhsInt=" << ci->value;
    std::cerr << " lhsName=" << (lhs ? lhs->name : "null")
              << " rhsName=" << (rhs ? rhs->name : "null") << std::endl;

    // 优化：tobool 模式（icmp ne %orig, 0）时，从 %orig 的操作数重新发射 cmp+jCC，
    // 跳过 tobool 的 set+movzx 冗余链
    if (kind == ICmpKind::NE && rhs->isConstant()) {
        auto *intConst = dynamic_cast<const IRConstantInt *>(rhs);
        if (intConst && intConst->value == 0) {
            if (auto *origInst = dynamic_cast<const IRInstruction *>(lhs)) {
                if (origInst->opcode == IROpcode::ICmp && origInst->getNumOperands() >= 3) {
                    auto *origLhs = origInst->getOperand(0);
                    auto *origRhs = origInst->getOperand(1);
                    auto origKind = static_cast<ICmpKind>(reinterpret_cast<uintptr_t>(origInst->getOperand(2)));

                    loadValToReg(origLhs, "rax");
                    if (origRhs->isConstant()) {
                        auto *origConst = dynamic_cast<const IRConstantInt *>(origRhs);
                        if (origConst) {
                            emitLine("    cmp eax, " + std::to_string(origConst->value));
                        }
                    } else {
                        loadValToReg(origRhs, "rcx");
                        emitLine("    cmp eax, ecx");
                    }

                    const char *jTrue = nullptr;
                    switch (origKind) {
                    case ICmpKind::EQ:  jTrue = "je";  break;
                    case ICmpKind::NE:  jTrue = "jne"; break;
                    case ICmpKind::SLT: jTrue = "jl";  break;
                    case ICmpKind::SLE: jTrue = "jle"; break;
                    case ICmpKind::SGT: jTrue = "jg";  break;
                    case ICmpKind::SGE: jTrue = "jge"; break;
                    case ICmpKind::ULT: jTrue = "jb";  break;
                    case ICmpKind::ULE: jTrue = "jbe"; break;
                    case ICmpKind::UGT: jTrue = "ja";  break;
                    case ICmpKind::UGE: jTrue = "jae"; break;
                    }

                    auto trueIt = blockLabels.find(trueBB);
                    auto falseIt = blockLabels.find(falseBB);
                    emitPhiCopies(trueBB, currentBB);
                    if (trueIt != blockLabels.end()) {
                        emitLine(std::string("    ") + jTrue + " " + trueIt->second);
                    }
                    emitPhiCopies(falseBB, currentBB);
                    if (falseIt != blockLabels.end()) {
                        emitLine("    jmp " + falseIt->second);
                    }
                    return;
                }
            }
        }
    }

    // 非递归情况：直接使用 cmp 的条件码
    loadValToReg(lhs, "rax");
    if (rhs->isConstant()) {
        auto *intConst = dynamic_cast<const IRConstantInt *>(rhs);
        if (intConst) {
            emitLine("    cmp eax, " + std::to_string(intConst->value));
        }
    } else {
        loadValToReg(rhs, "rcx");
        emitLine("    cmp eax, ecx");
    }

    const char *jTrue = nullptr;
    switch (kind) {
    case ICmpKind::EQ:  jTrue = "je";  break;
    case ICmpKind::NE:  jTrue = "jne"; break;
    case ICmpKind::SLT: jTrue = "jl";  break;
    case ICmpKind::SLE: jTrue = "jle"; break;
    case ICmpKind::SGT: jTrue = "jg";  break;
    case ICmpKind::SGE: jTrue = "jge"; break;
    case ICmpKind::ULT: jTrue = "jb";  break;
    case ICmpKind::ULE: jTrue = "jbe"; break;
    case ICmpKind::UGT: jTrue = "ja";  break;
    case ICmpKind::UGE: jTrue = "jae"; break;
    }

    auto trueIt = blockLabels.find(trueBB);
    auto falseIt = blockLabels.find(falseBB);
    emitPhiCopies(trueBB, currentBB);
    if (trueIt != blockLabels.end()) {
        emitLine(std::string("    ") + jTrue + " " + trueIt->second);
    }
    emitPhiCopies(falseBB, currentBB);
    if (falseIt != blockLabels.end()) {
        emitLine("    jmp " + falseIt->second);
    }
}

// === 寄存器状态追踪 ===

int IRCodeGenerator::regIndex(const std::string &name) {
    // 与 PHYS_REG_NAMES 顺序一致：r10=0, r11=1, rax=2, rdx=3, r8=4, r9=5, rcx=6
    if (name == "r10") return 0;
    if (name == "r11") return 1;
    if (name == "rax" || name == "eax") return 2;
    if (name == "rdx" || name == "edx") return 3;
    if (name == "r8")  return 4;
    if (name == "r9")  return 5;
    if (name == "rcx" || name == "ecx") return 6;
    return -1;
}

void IRCodeGenerator::updateRegContent(const std::string &reg, const IRValue *val) {
    int idx = regIndex(reg);
    if (idx >= 0 && idx < NUM_TRACKED_REGS) {
        regContent[idx] = val;
    }
}

void IRCodeGenerator::invalidateRegContent(const std::string &reg) {
    int idx = regIndex(reg);
    if (idx >= 0 && idx < NUM_TRACKED_REGS) {
        regContent[idx] = nullptr;
    }
}

std::string IRCodeGenerator::getPhysRegName(const IRValue *val) {
    if (!val) return "";
    // IRInstruction
    auto *inst = dynamic_cast<const IRInstruction *>(val);
    if (inst) {
        auto it = vregMap.find(inst);
        if (it != vregMap.end()) {
            auto pit = regAlloc.vregToPhysReg.find(it->second);
            if (pit != regAlloc.vregToPhysReg.end())
                return PHYS_REG_NAMES[pit->second];
        }
        return "";
    }
    // IRArgument
    auto ait = argVregMap.find(val);
    if (ait != argVregMap.end()) {
        auto pit = regAlloc.vregToPhysReg.find(ait->second);
        if (pit != regAlloc.vregToPhysReg.end())
            return PHYS_REG_NAMES[pit->second];
    }
    return "";
}

// === 值加载/存储辅助方法 ===

void IRCodeGenerator::loadValToReg(const IRValue *val, const std::string &reg) {
    // null 检查
    if (!val) {
        emitLine("    xor " + reg + ", " + reg);
        return;
    }

    // 常量
    if (val->isConstant()) {
        auto *intConst = dynamic_cast<const IRConstantInt *>(val);
        if (intConst) {
            emitLine("    mov " + reg + ", " + std::to_string(intConst->value));
            return;
        }
        auto *floatConst = dynamic_cast<const IRConstantFloat *>(val);
        if (floatConst) {
            emitLine("    mov " + reg + ", 0");
            return;
        }
        auto *nullConst = dynamic_cast<const IRConstantNull *>(val);
        if (nullConst) {
            emitLine("    xor " + reg + ", " + reg);
            return;
        }
    }

    // alloca 指针
    auto allocaIt = allocaSlots.find(val);
    if (allocaIt != allocaSlots.end()) {
        emitLine("    lea " + reg + ", [rbp-" + std::to_string(allocaIt->second) + "]");
        return;
    }

    // IRInstruction：检查寄存器分配
    auto *inst = dynamic_cast<const IRInstruction *>(val);
    if (inst) {
        // 查找 vreg ID：先查 vregMap，再查 inst->id（优化 pass 可能创建新指令）
        int vregId = -1;
        auto vregIt = vregMap.find(inst);
        if (vregIt != vregMap.end()) {
            vregId = vregIt->second;
        } else if (inst->id >= 0) {
            vregId = inst->id;
        }
        if (vregId >= 0) {
            // 优先检查物理寄存器（storeResult 可能刚写入）
            auto physIt = regAlloc.vregToPhysReg.find(vregId);
            if (physIt != regAlloc.vregToPhysReg.end()) {
                const std::string &srcReg = PHYS_REG_NAMES[physIt->second];
                if (srcReg != reg) {
                    emitLine("    mov " + reg + ", " + srcReg);
                }
                return;
            }
            auto spillIt = regAlloc.vregToSpillSlot.find(vregId);
            if (spillIt != regAlloc.vregToSpillSlot.end()) {
                emitLine("    mov " + reg + ", [rbp-" + std::to_string(spillIt->second) + "]");
                return;
            }
        }
        // 没有寄存器分配（phi 等），使用栈槽
        auto slotIt = valueSlots.find(val);
        if (slotIt != valueSlots.end()) {
            emitLine("    mov " + reg + ", [rbp-" + std::to_string(slotIt->second) + "]");
            return;
        }
    }

    // IRArgument：检查寄存器分配
    auto *arg = dynamic_cast<const IRArgument *>(val);
    if (arg) {
        auto argIt = argVregMap.find(val);
        if (argIt != argVregMap.end()) {
            int vregId = argIt->second;
            auto physIt = regAlloc.vregToPhysReg.find(vregId);
            if (physIt != regAlloc.vregToPhysReg.end()) {
                const std::string &srcReg = PHYS_REG_NAMES[physIt->second];
                if (srcReg != reg) {
                    emitLine("    mov " + reg + ", " + srcReg);
                }
                return;
            }
            auto spillIt = regAlloc.vregToSpillSlot.find(vregId);
            if (spillIt != regAlloc.vregToSpillSlot.end()) {
                emitLine("    mov " + reg + ", [rbp-" + std::to_string(spillIt->second) + "]");
                return;
            }
        }
        // fallback：使用栈槽（phi 预分配等）
        auto slotIt = valueSlots.find(val);
        if (slotIt != valueSlots.end()) {
            emitLine("    mov " + reg + ", [rbp-" + std::to_string(slotIt->second) + "]");
            return;
        }
    }

    // fallback：xor 清零
    emitLine("    xor " + reg + ", " + reg);
}

void IRCodeGenerator::storeResult(const IRInstruction *inst, const std::string &reg) {
    // 查找 vreg ID：先查 vregMap，再查 inst->id（优化 pass 可能创建新指令）
    int vregId = -1;
    auto vregIt = vregMap.find(inst);
    if (vregIt != vregMap.end()) {
        vregId = vregIt->second;
    } else if (inst->id >= 0) {
        vregId = inst->id;
    }
    if (vregId >= 0) {
        std::cerr << "[storeResult] inst=" << inst->name << " vreg=" << vregId << " reg=" << reg
                  << " mapSize=" << regAlloc.vregToPhysReg.size() << " id=" << inst->id << std::endl;
        auto physIt = regAlloc.vregToPhysReg.find(vregId);
        if (physIt == regAlloc.vregToPhysReg.end()) {
            std::cerr << "[storeResult]   NOT FOUND in vregToPhysReg! Trying inst->id=" << inst->id << std::endl;
            physIt = regAlloc.vregToPhysReg.find(inst->id);
        }
        if (physIt != regAlloc.vregToPhysReg.end()) {
            // 结果在物理寄存器中
            const std::string &dstReg = PHYS_REG_NAMES[physIt->second];
            std::cerr << "[storeResult]   physReg=" << dstReg << " reg=" << reg << std::endl;
            if (dstReg != reg) {
                emitLine("    mov " + dstReg + ", " + to64(reg));
            }
            // 有物理寄存器时不需要写入栈槽——物理寄存器是权威来源。
            // 写入栈槽会导致后续通过指针的 Store 覆盖该栈位置。
            // 溢出由寄存器分配器管理（vregToSpillSlot）。
            return;
        }
        auto spillIt = regAlloc.vregToSpillSlot.find(vregId);
        if (spillIt != regAlloc.vregToSpillSlot.end()) {
            // 结果溢出到栈：始终使用 64-bit 寄存器写入，保证 loadValToReg 读取一致
            emitLine("    mov [rbp-" + std::to_string(spillIt->second) + "], " + to64(reg));
            return;
        }
    }

    // 没有寄存器分配（phi 等），使用栈槽：始终使用 64-bit 寄存器写入
    auto slotIt = valueSlots.find(inst);
    if (slotIt != valueSlots.end()) {
        emitLine("    mov [rbp-" + std::to_string(slotIt->second) + "], " + to64(reg));
    }
}

// === 旧辅助方法（保留兼容性） ===

int IRCodeGenerator::getSlot(const IRValue *val) {
    auto it = valueSlots.find(val);
    if (it != valueSlots.end()) return it->second;
    return allocSlot(val, 8);
}

int IRCodeGenerator::allocSlot(const IRValue *val, int size) {
    slotOffset += size;
    slotOffset = (slotOffset + 7) & ~7;
    valueSlots[val] = slotOffset;
    return slotOffset;
}

void IRCodeGenerator::loadToRax(const IRValue *val) {
    loadValToReg(val, "rax");
}

void IRCodeGenerator::emitLine(const std::string &line) {
    // 窥孔优化：消除冗余 mov 对
    // 模式：mov A, B 后紧跟 mov B, A → 保留第一条，跳过第二条
    if (!lastEmittedInst.empty()) {
        auto parseMov = [](const std::string &s, std::string &dst, std::string &src) -> bool {
            auto p = s.find("mov ");
            if (p == std::string::npos) return false;
            auto rest = s.substr(p + 4);
            auto comma = rest.find(',');
            if (comma == std::string::npos) return false;
            dst = rest.substr(0, comma);
            src = rest.substr(comma + 1);
            // 去除首尾空格
            auto trim = [](std::string &x) {
                while (!x.empty() && x.front() == ' ') x.erase(0, 1);
                while (!x.empty() && x.back() == ' ') x.pop_back();
            };
            trim(dst);
            trim(src);
            return true;
        };
        std::string prevDst, prevSrc, curDst, curSrc;
        if (parseMov(lastEmittedInst, prevDst, prevSrc) &&
            parseMov(line, curDst, curSrc)) {
            // mov A, B 后紧跟 mov B, A → 跳过第二条
            // 但不排除 mov rax, X（返回值加载）——storeResult 写 r10 后，
            // Ret 的 loadValToReg 需要 mov rax, r10
            if (prevDst == curSrc && prevSrc == curDst && curDst != "rax") {
                lastEmittedInst.clear();
                return;
            }
        }
    }
    lastEmittedInst = line;
    out << line << "\n";
}

void IRCodeGenerator::emitLabel(const std::string &label) {
    lastEmittedInst.clear(); // 标签中断指令流
    out << label << ":\n";
}

std::string IRCodeGenerator::blockName(const IRBasicBlock *bb) {
    auto it = blockLabels.find(bb);
    return it != blockLabels.end() ? it->second : "unknown";
}

std::string IRCodeGenerator::valueStr(const IRValue *val) {
    if (!val) return "null";
    return val->toString();
}

} // namespace ir
