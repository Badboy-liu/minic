# Mini C Compiler

This project is now a small educational C compiler written in C++.

It implements the pipeline in clear stages:

- Lexing: convert source text into tokens
- Parsing: build an AST for a small C subset
- Semantic analysis: resolve locals, reject undeclared or duplicate variables, compute stack layout
- Code generation: emit Windows x64 NASM assembly
- Linking: invoke `nasm` to produce a COFF `.obj`, then use the built-in PE linker to produce a working `.exe`

## Supported C subset

The current compiler supports:

- multiple `char` / `short` / `int` / `long` / `long long` / `void` functions
- function declarations and later definitions
- `void` functions
- up to 4 `int` parameters per function
- pointer parameters and locals
- local arrays of non-void element types
- function calls
- address-of `&`, dereference `*`, and `[]` indexing
- local `int` declarations
- assignment
- integer literals
- `+ - * /`
- `! && ||`
- `== != < <= > >=`
- `if`, `if/else`
- `while`
- `for`
- `break`, `continue`
- nested `{ ... }` blocks
- `return`

## Build the compiler

```powershell
cmake --preset default --fresh
cmake --build --preset debug
```

## Compile a C file

```powershell
.\build\Debug\minic.exe .\input\answer.c
```

The compiler writes generated files under `build/output/` by default:

```powershell
.\build\output\answer.asm
.\build\output\answer.exe
```

The default target is `x86_64-windows`. You can request targets explicitly:

```powershell
.\build\Debug\minic.exe .\input\answer.c --target x86_64-windows
.\build\Debug\minic.exe .\input\answer.c --target x86_64-linux -S --emit-asm .\build\output\answer_linux.asm
.\build\Debug\minic.exe .\input\answer.c --target x86_64-linux -c -o .\build\output\answer_linux.o
```

Linux code generation currently supports NASM ELF64 assembly/object output. Linux executable linking is not implemented yet.

You can also compile multiple files into one executable:

```powershell
.\build\Debug\minic.exe .\input\file1.c .\input\file2.c -o .\build\output\app.exe
```

Each input file is compiled into its own NASM assembly file and object file. The built-in PE linker then resolves external function symbols across those objects, so functions defined in one `.c` file can be called from another `.c` file.

## Verify the sample

The sample program returns `42` as its process exit code:

```powershell
.\build\output\answer.exe
$LASTEXITCODE
```

## Notes

This is intentionally a tiny compiler, not a full ISO C implementation.
Current limits:

- no global variables
- no string literals
- no structs
- only up to 4 parameters
- no array initializers
