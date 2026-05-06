# Mini C Compiler

手写的 C 编译器 + PE/COFF 链接器，使用 C++17 实现。

## 编译流水线

```
源代码 → 预处理 → 词法分析 → 语法分析 → 语义分析 → 优化 → 代码生成 → 链接 → 可执行文件
```

## 支持的 C 语言特性

### 类型系统
- 基本类型：`char`, `short`, `int`, `long`, `long long`, `void`, `_Bool`, `float`, `double`
- `unsigned` 修饰符
- `const` 限定符
- 指针类型（多级指针、`void *`）
- 数组类型（多维数组、VLA 变长数组）
- 结构体（`struct`）：本地/全局、嵌套初始化、按值传参/返回、位域、匿名成员
- 联合体（`union`）
- 枚举（`enum`）
- `typedef` 类型别名
- 函数指针类型
- 复合字面量（compound literals）
- 指定初始化器（designated initializers）
- 柔性数组成员（flexible array members）

### 控制流
- `if` / `else`
- `while` / `for` / `do-while`
- `switch` / `case` / `default`（含 fall-through 支持）
- `break` / `continue`
- `goto` / 标签
- 三元运算符 `?:`
- 逗号运算符

### 运算符
- 算术：`+ - * / %`
- 比较：`== != < <= > >=`
- 逻辑：`! && ||`
- 位运算：`& | ^ ~ << >>`
- 赋值：`= += -= *= /= %= &= |= ^= <<= >>=`
- 自增/自减：`++ --`（前缀和后缀）
- 取地址/解引用：`& *`
- 下标访问：`[]`
- 成员访问：`. ->`
- 类型转换：`(type)`
- `sizeof` / `_Alignof`

### 函数特性
- 函数声明和定义
- 可变参数函数（`va_list`, `va_start`, `va_arg`, `va_end`）
- 静态局部变量
- 函数内联优化（对简单函数）

### 预处理器
- `#include`（尖括号和双引号）
- `#define` / `#undef` 宏定义
- `#if` / `#ifdef` / `#ifndef` / `#else` / `#elif` / `#endif` 条件编译
- `#line` 行号指令
- `#pragma` 指令
- 预定义宏：`__FILE__`, `__LINE__`, `__DATE__`, `__TIME__`, `__minic__`
- 字符串化运算符 `#` 和标记粘贴运算符 `##`
- `#` / `##` 预处理运算符

### 其他特性
- `_Static_assert` 静态断言
- `_Generic` 泛型选择
- 字符字面量（含转义序列）
- 字符串字面量
- 全局变量（初始化和未初始化/BSS）

## 优化器

编译器在语义分析和代码生成之间运行优化遍（pass）：

- **常量折叠** — 编译时计算常量表达式
- **常量传播** — 跟踪已知常量变量并替换
- **死代码消除** — 移除不可达代码和恒真/恒假分支
- **强度削减** — `x * 2^n` → `x << n`，`x % 2^n` → `x & (2^n-1)`
- **循环不变量外提** — 将循环内不变的赋值移到循环前
- **函数内联** — 将简单函数调用替换为函数体
- **尾调用优化** — `return f(x)` 内联 epilogue 避免额外跳转

## 代码生成

### Windows x86_64
- NASM 汇编输出
- Windows x64 调用约定（rcx, rdx, r8, r9 + 栈参数）
- 浮点参数通过 xmm0-xmm3 传递
- 大结构体通过隐藏返回指针传递
- PE/COFF 目标文件生成

### Linux x86_64
- NASM 汇编输出（ELF64 格式）
- System V ABI 调用约定（rdi, rsi, rdx, rcx, r8, r9 + 栈参数）
- 浮点参数通过 xmm0-xmm7 传递
- ELF 目标文件生成

## 链接器

内置 PE/COFF 链接器（`minic-link`）支持：

- 多目标文件链接
- 跨文件符号解析
- COFF 重定位处理（REL32, ADDR64, SECTION, SECREL）
- PE 基址重定位（`.reloc` 节）
- 导入表生成（kernel32.dll, msvcrt.dll 等）
- 延迟加载导入
- TLS（线程本地存储）
- 文件导入目录（`config/import_catalog.txt`）
- Linux ELF 链接（通过 WSL gcc）

## 构建

```powershell
cmake --build build --config Debug
```

## 运行

```powershell
# 编译单个文件
.\build\Debug\minic.exe .\input\answer.c

# 编译多个文件
.\build\Debug\minic.exe .\input\file1.c .\input\file2.c -o .\build\output\app.exe

# 只生成汇编
.\build\Debug\minic.exe .\input\answer.c -S

# 只生成目标文件
.\build\Debug\minic.exe .\input\answer.c -c

# 指定目标平台
.\build\Debug\minic.exe .\input\answer.c --target x86_64-linux

# 链接追踪
.\build\Debug\minic.exe .\input\answer.c --link-trace

# 并行编译
.\build\Debug\minic.exe .\input\*.c -j 4

# 独立链接器
.\build\Debug\minic-link.exe .\build\output\answer.obj -o .\build\output\answer.exe
```

## 测试

```powershell
# 运行所有测试
.\build\Debug\minic_gtests.exe

# 运行指定测试
.\build\Debug\minic_gtests.exe --gtest_filter="*minic_answer*"

# 运行编译器测试
.\build\Debug\minic_gtests.exe --gtest_filter="Compiler/*"

# 运行链接器测试
.\build\Debug\minic_gtests.exe --gtest_filter="Linker/*"
```

## 源码结构

```
src/
├── app/           # Driver 入口和顶层编排
├── frontend/      # 词法分析、语法分析、语义分析、AST
│   ├── Lexer      # 词法分析器
│   ├── Parser     # 语法分析器
│   ├── Semantics  # 语义分析器
│   ├── Token      # Token 定义
│   ├── Ast        # AST 节点定义
│   └── Diagnostics # 诊断引擎
├── backend/       # 代码生成和优化
│   ├── CodeGenerator  # NASM 代码生成
│   └── Optimizer      # 优化器
├── linker/        # PE/COFF 链接器
│   ├── PeLinker           # PE 链接器实现
│   ├── LinkerBackend      # 链接器后端接口
│   ├── BuiltinPeCoffLinkerBackend  # 内置 PE 后端
│   ├── WslGccElfLinkerBackend     # WSL ELF 后端
│   └── LinkerDriver       # 链接器驱动
└── support/       # 工具链和进程辅助
```

## 诊断输出

编译器提供彩色诊断输出，包含源文件位置和上下文：

```
input/test.c:5:15: error: use of undeclared variable: y
    return x + y;
               ^
```

## 测试覆盖

128 个测试用例（119 通过，9 跳过 WSL/Linux 测试），覆盖：
- 基础类型和运算
- 控制流语句
- 结构体/联合体/枚举
- 指针和数组
- 函数特性和调用约定
- 预处理器
- 优化器
- 链接器（PE/COFF + ELF）
- 错误诊断
- 边界条件
