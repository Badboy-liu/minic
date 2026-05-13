# 项目进度

最后更新：2026-05-13

## 当前状态

项目处于「可工作、可解释、可扩展」阶段。端到端编译流水线完整可用，支持实用的 C 语言子集。

- 250 个测试用例（241 通过，0 失败，9 跳过 WSL/Linux 测试）
- SSA IR 流水线完整可用：完整 Cytron SSA + 线性扫描寄存器分配 + 20 个 --ir 回归测试
- PE 异常处理表（.pdata/.xdata）和 DWARF 调试信息链接已实现

## 各阶段成熟度

| 阶段 | 成熟度 | 说明 |
|------|--------|------|
| 前端（词法/语法/语义） | 85% | C11 关键字完整、类型系统完整（含 VLA/位域/柔性数组）、19 种表达式节点、14 种语句节点、Visitor 模式统一 AST 遍历、预处理器（宏展开/条件编译/字符串化/标记粘贴）、编译器性能优化（reserve/缓存迭代器/editDistance 优化） |
| 代码生成 | 75% | 双目标支持（Windows x64 + Linux x64）、浮点完整、结构体传参、Visitor 模式代码生成、按职责拆分为 6 个源文件 |
| 优化器 | 78% | 常量折叠/传播、复制传播、死代码消除、死存储消除、函数内联、尾调用优化、循环不变量外提、强度削减、CSE（支持 if-else 合并）、PassManager 框架、CFG 集成、循环展开（常数次 for 循环）、CFG 循环检测基础设施 |
| PE/COFF 链接器 | 85% | 多文件链接、重定位、导入表、基址重定位、延迟导入、导出表 |
| ELF 链接器 | 40% | 依赖 WSL gcc，非内置 |
| SSA IR 流水线 | 90% | IR 数据结构完整、AST→IR 降低（alloca-first）、IR→x64 代码生成（线性扫描寄存器分配）、--ir 双路径支持、完整 Cytron SSA（支配树/支配边界/phi 插入/重命名）、IR 优化 pass（常量折叠、死代码消除、复制传播、强度削减、LICM）、活跃性分析（RPO 线性化 + 迭代数据流）、代码质量优化（9 项）、Load/Store 宽度修复、栈帧 slot 重叠修复、20 个 --ir 回归测试 |
| 测试覆盖 | 80% | 250 个用例（241 通过），包含 20 个 --ir 流水线测试 |

## 已完成任务

| # | 任务 | 说明 |
|---|------|------|
| 94 | 字符串字面量拼接 | 相邻字符串字面量自动合并 |
| 95 | _Alignas 实际效果 | 对齐说明符代码生成 |
| 96 | 循环不变量外提扩展 | 支持 for/do-while |
| 97 | 宽字符/多字节字符字面量 | L'x' 等前缀检测 |
| 98 | memcpy/memset 内联优化 | 小规模内存操作内联展开 |
| 99 | 循环展开优化 | 简单循环部分展开 |
| 100 | Lexer 使用 DiagnosticEngine | 词法分析器接入诊断系统 |
| 101 | 尾递归消除 | return f(x) 形式优化 |
| 102 | 预处理器使用 DiagnosticEngine | 预处理器接入诊断系统 |
| 103 | 警告抑制机制完善 | -Wno-xxx 选项支持 |
| 104 | 错误级联抑制 | 避免一个错误引发大量连锁错误 |
| 105 | 未声明函数调用检测 | 语义分析阶段报错 |
| 106 | CSE 优化 | 公共子表达式消除（已修复栈偏移 bug 并重新启用） |
| 109 | PE 导出表生成 | .edata 节和导出目录 |
| 111 | ELF 节头表生成 | ELF 目标文件节头 |
| 113 | Lexer 独立单元测试 | 32 个测试用例 |
| 114 | 预处理器独立单元测试 | 23 个测试用例 |
| 115 | 响应文件/依赖文件测试 | @file 和 -M 支持 |
| 116 | 语义分析器独立单元测试 | 35 个测试用例 |
| 117 | _Noreturn 实际效果 | 标记不返回函数 |
| 119 | long double 类型支持 | 80 位浮点 |
| 120 | 整数常量后缀支持 | u, LL 等后缀 |
| 121 | 字符常量类型修正 | 正确的类型推导 |
| 123 | _Static_assert 在 struct 内部 | 结构体内的静态断言 |
| 124 | sizeof 结果类型修正 | 返回 size_t |
| 107 | 拆分 CodeGenerator | 按职责拆分为 CodeGenerator.cpp、CodeGenExpr.cpp、CodeGenStmt.cpp、CodeGenCall.cpp、CodeGenInit.cpp、CodeGenDwarf.cpp |
| 110 | 引入 CFG 控制流图 | CFG.h + CFGBuilder，支持所有控制流结构，为数据流分析打基础 |
| 122 | Visitor 模式统一 AST 遍历 | ExprVisitor + StmtVisitor 接口，Semantics 和 CodeGenerator 均已适配 |
| 109 | PE 导出表生成 | .edata 节、导出目录、命令行 --export 参数，已完整实现 |
| — | PassManager 基础设施 | 优化器引入 PassManager 框架，各 pass 包装为独立类，支持 CFGPass 基类 |
| — | 复制传播优化 | CopyPropagationPass，追踪 x=y 赋值并替换后续使用 |
| — | Dead Store Elimination | 死存储消除 pass，移除写入后未读取的变量赋值 |
| — | CFG-aware CSE 改进 | CSE 支持 if-else 分支合并，递归处理分支体 |
| — | 优化模式扩展 | 自运算简化（x^x→0, x|x→x, x&x→x）、乘法强度削减（x*5→(x<<2)+x） |
| — | 诊断改进 | 未声明函数调用添加"did you mean?"拼写建议 |
| — | 错误恢复增强 | synchronize() 支持 case/else/while 同步，for/switch 添加 try-catch |
| — | 有符号/无符号比较警告 | 比较运算符（</<=/>/>=/==/!=）的有符号/无符号不匹配警告 |
| — | 循环展开优化 | LoopUnrollPass，将常数次 for 循环（2-16 次）展开为顺序语句 |
| — | CFG 循环检测 | CFG::findLoops() 基于后边检测自然循环，为 CFGPass 提供循环信息 |
| — | cloneStmt 修复 | DeclStmt 克隆正确复制 stackOffset/isStatic 等语义分析结果 |
| — | 编译器性能优化 | Lexer tokens reserve、editDistance 2D→2×1D、comparison decay 缓存、Parser/CodeGen reserve、double-lookup 消除 |
| — | SSA IR 基础设施 | IRType/IRValue/IRInstruction/IRBasicBlock/IRFunction/IRModule 数据结构，IRPrinter 文本输出 |
| — | AST→IR 降低 | IRLowering：alloca-first 模式，19 种表达式 + 14 种语句降低到 IR |
| — | IR→x64 代码生成 | IRCodeGenerator：栈式模型，支持算术/比较/控制流/函数调用/类型转换 |
| — | --ir 双路径 | Driver 支持 --ir 标志切换到 SSA IR 流水线 |
| — | SSA 构建（简化版 mem2reg） | SSAConstructor：单 store 模式的 alloca/load/store 消除 |
| — | 完整 Cytron SSA | 支配树计算、支配边界、phi 函数插入、变量重命名、trivial phi 消除 |
| — | 线性扫描寄存器分配 | LivenessAnalysis（RPO 线性化 + 迭代数据流）+ LinearScanAllocator（7 个物理寄存器 + 溢出） |
| — | PE 异常处理表 | .pdata/.xdata 节生成，UNWIND_INFO 结构，Data Directory index 3 |
| — | DWARF 调试信息链接 | .debug_str/.debug_abbrev/.debug_line/.debug_info 节，Debug Directory，PDB 路径 |
| — | IR 优化 Pass 框架 | IRPassManager + IRConstProp + IRDeadCodeElim + IRCopyProp |
| — | replaceAllUsesWith 修复 | 解决迭代 uses 时 setOperand 导致的迭代器失效问题 |
| — | Phi Copy Resolver | IRCodeGenerator 在前驱块末尾插入 phi copy，解决循环变量未初始化问题 |
| — | Load 寄存器覆盖修复 | 4 字节/1 字节 Load 直接用 movsxd/movzx，避免 eax 覆盖活跃的 rax 指针 |
| — | 栈帧 slot 重叠修复 | value slots 初始化跳过 callee-saved 寄存器保存区域 |
| — | --ir 回归测试 | 20 个 IR 流水线测试用例（循环、递归、指针算术、switch/goto/do-while 等） |
| — | IR 代码质量优化（9 项） | 死 alloca 清除、冗余 mov 消除、立即数折叠（add eax,10）、ICmp+CondBr 冗余消除（预扫描+跳过+cmp+jCC 直接发射，12→4 条指令）、二元运算寄存器冲突修复（getPhysRegName 自适应加载）、汇编窥孔优化（消除 mov A,B + mov B,A 冗余对）、函数调用地址加载简化（lea 直接到目标寄存器）、Call 指令感知的寄存器分配（跨 call 区间强制溢出）、二元运算 rhs 已在 rcx 时的快速路径 |

## 待办任务

### 低优先级（长期目标）

| # | 任务 | 说明 |
|---|------|------|
| — | 增量链接 | 只重新链接修改的目标文件 |
| — | SSA 溢出优化 | 减少跨基本块的寄存器溢出/重载 |
| — | 图着色寄存器分配 | 替代线性扫描，减少溢出 |

## 最强领域

- 端到端编译流水线（C 源码 → 可执行文件）
- 完整的 C 语言前端（类型系统、控制流、预处理器、Visitor 模式 AST 遍历）
- 双目标代码生成（Windows x64 + Linux x64），按职责拆分为 6 个源文件
- 内置 PE/COFF 链接器
- SSA IR 流水线（完整 Cytron SSA + 线性扫描寄存器分配）
- CFG 控制流图基础设施
- 彩色诊断输出
- 自动化回归测试（241 通过 / 250 总计，含 20 个 --ir 测试）

## 最弱领域

1. **寄存器分配精度** — 线性扫描跨块溢出较多，可优化为图着色
2. **标准合规** — 不完整的算术转换、有限的未定义行为检测
3. **ELF 支持** — 依赖 WSL gcc，非内置链接器

## 下一步计划

1. **优化** — SSA 溢出优化、优化级别控制、图着色寄存器分配
2. **工具链** — 增量链接、编译器性能优化
3. **链接器** — 更好的错误诊断、增量链接
