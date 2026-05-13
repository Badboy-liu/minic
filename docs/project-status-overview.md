# 项目状态概览

日期：2026-05-13

文档类型：长期状态文档

## 摘要

minic 是一个功能完整的手写 C 编译器 + PE/COFF 链接器工具链，使用 C++17 实现。项目已从早期的前端玩具编译器发展为一个可工作的端到端编译器工具链。

当前最强的部分是完整的编译流水线：从 C 源代码到 NASM 汇编、COFF/ELF 目标文件、再到自写的 PE 链接器生成可执行文件。架构层面已完成 Visitor 模式统一 AST 遍历、CodeGenerator 按职责拆分、CFG 控制流图基础设施、以及完整 SSA IR 流水线（Cytron SSA + 线性扫描寄存器分配）。PE 异常处理表和 DWARF 调试信息链接也已实现。优化器、诊断系统和测试覆盖也已达到可用水平。

## 各阶段状态

### 1. 前端（词法/语法/语义）

**当前状态：**
- 词法分析器、语法分析器、语义分析器对当前支持的 C 子集已基本稳定
- 支持完整的类型系统：基本类型、指针、数组、结构体、联合体、枚举、typedef、函数指针
- 支持所有主要控制流：if/else、while、for、do-while、switch/case/default、goto/label
- 预处理器支持：#include、#define、#if/#ifdef/#else/#endif、#line、#pragma
- 多文件并行解析
- 彩色诊断输出，包含源文件位置和上下文

**成熟度：** 85%

### 2. 代码生成

**当前状态：**
- Windows x64 和 Linux x64 双目标支持
- Windows x64 调用约定（rcx, rdx, r8, r9 + 栈参数）
- System V ABI 调用约定（rdi, rsi, rdx, rcx, r8, r9 + 栈参数）
- 浮点运算完整支持（SSE xmm 寄存器）
- 结构体按值传参/返回（小结构体通过寄存器，大结构体通过隐藏指针）
- VLA（变长数组）运行时栈分配
- 多文件并行代码生成
- Visitor 模式代码生成（ExprVisitor + StmtVisitor）
- 按职责拆分为 6 个源文件（CodeGenerator.cpp、CodeGenExpr.cpp、CodeGenStmt.cpp、CodeGenCall.cpp、CodeGenInit.cpp、CodeGenDwarf.cpp）

**成熟度：** 75%

### 3. 优化器

**当前状态：**
- 常量折叠和常量传播
- 死代码消除（不可达代码、恒真/恒假分支）
- 强度削减（乘法→移位、取模→位与）
- 循环不变量外提
- 函数内联（简单单返回语句函数）
- 尾调用优化
- 公共子表达式消除（CSE）
- CFG 控制流图基础设施（CFG.h + CFGBuilder）

**成熟度：** 70%

### 4. 链接器

**当前状态：**
- 内置 PE/COFF 链接器完整工作
- ELF 链接通过 WSL gcc 支持
- 多目标文件链接和跨文件符号解析
- COFF 重定位处理（REL32, ADDR64, SECTION, SECREL）
- PE 基址重定位（.reloc 节）
- 导入表生成（kernel32.dll, msvcrt.dll 等）
- 延迟加载导入和 TLS 支持
- 文件导入目录扩展（config/import_catalog.txt）
- 链接追踪输出

**成熟度：** PE/COFF 80%，ELF 40%

### 5. SSA IR 流水线

**当前状态：**
- IR 数据结构完整（IRType/IRValue/IRInstruction/IRBasicBlock/IRFunction/IRModule）
- AST→IR 降低（alloca-first 模式，19 种表达式 + 14 种语句）
- 完整 Cytron SSA（支配树计算、支配边界、phi 函数插入、变量重命名）
- IR 优化 pass（常量折叠、死代码消除、复制传播、强度削减、LICM）
- 活跃性分析（RPO 线性化 + 迭代数据流 liveIn/liveOut）
- 线性扫描寄存器分配（7 个物理寄存器 + 自动溢出 + Call 感知）
- IR→x64 代码生成（使用寄存器分配结果，支持 phi 节点、正确的 Load/Store 宽度）
- --ir 双路径支持（Driver 中 --ir 标志切换到 SSA IR 流水线）
- 20 个 `--ir` 回归测试全部通过（覆盖循环、递归、指针算术、switch/goto/do-while 等）

**成熟度：** 90%

### 6. PE 异常处理和调试信息

**当前状态：**
- .pdata/.xdata 节生成（UNWIND_INFO 结构，Data Directory index 3）
- DWARF 调试信息（.debug_str/.debug_abbrev/.debug_line/.debug_info）
- Debug Directory 和 PDB 路径生成
- 源文件名传递（Driver → CodeGenerator → DWARF）

**成熟度：** 70%

### 7. 测试覆盖

**当前状态：**
- 250 个测试用例（241 通过，0 失败，9 跳过 WSL/Linux 测试）
- 覆盖所有主要功能：类型、控制流、结构体、指针、数组、函数、预处理器、优化器、链接器
- 边界条件测试：深层嵌套、递归、隐式返回、字符串操作、嵌套调用
- 错误诊断测试：类型不匹配、未声明变量、重复定义
- 20 个 `--ir` 流水线回归测试：循环、递归、指针算术、switch/goto/do-while、嵌套调用等

**成熟度：** 80%

## 最强领域

最强的领域是端到端的编译流水线：

- 完整的 C 语言前端（类型系统、控制流、预处理器）
- 双目标代码生成（Windows + Linux）
- 内置 PE/COFF 链接器
- 优化器（常量传播、死代码消除、函数内联等）
- 彩色诊断输出
- 自动化回归测试

## 最弱领域

最弱的技术领域是：

1. **寄存器分配精度** — 线性扫描跨块溢出较多，可优化为图着色
2. **标准合规** — 不完整的算术转换、有限的未定义行为检测
3. **ELF 支持** — 依赖 WSL gcc，非内置链接器

## 整体评估

项目处于"可工作、可解释、可扩展"阶段：

- ✅ 完整的端到端编译工具链
- ✅ 支持实用的 C 语言子集
- ✅ 双平台代码生成
- ✅ 内置 PE/COFF 链接器
- ✅ SSA IR 流水线（完整 Cytron SSA + 线性扫描寄存器分配）
- ✅ PE 异常处理表和 DWARF 调试信息
- ✅ 优化器和诊断系统
- ⚠️ 尚未达到"广泛完整和健壮"阶段

## 推荐下一步

1. **优化** — SSA 溢出优化、图着色寄存器分配、优化级别控制
2. **工具链** — 增量链接、编译器性能优化
3. **链接器** — 更好的错误诊断、增量链接
