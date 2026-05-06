# 项目规范

## 输出语言
- 所有对话输出必须使用中文（简体）
- 技术术语首次出现用「中文（English）」格式
- 代码注释使用中文
- Git commit message 使用中文

## 项目概述
minic 是一个手写的 C 编译器 + PE/COFF 链接器，使用 C++17 实现。

## 构建和测试
```bash
cmake --build build --config Debug
./build/Debug/minic_gtests.exe
```

## 新增语言特性的修改清单
添加新的语句类型需要修改以下文件：
1. `src/frontend/Token.h` — 添加 token 枚举值
2. `src/frontend/Token.cpp` — 添加 tokenKindName case
3. `src/frontend/Lexer.cpp` — 字符分派和关键字识别
4. `src/frontend/Ast.h` — AST 节点结构体
5. `src/frontend/Parser.h` — 声明 parse 方法
6. `src/frontend/Parser.cpp` — 实现 parse 方法
7. `src/frontend/Semantics.cpp` — 语义分析
8. `src/backend/CodeGenerator.cpp` — 代码生成（NASM）
9. `src/backend/Optimizer.cpp` — 优化遍历
