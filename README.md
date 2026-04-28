# Mini C Compiler

This project is now a small educational C compiler written in C++.

It implements the pipeline in clear stages:

- Lexing: convert source text into tokens
- Parsing: build an AST for a small C subset
- Semantic analysis: resolve locals, reject undeclared or duplicate variables, compute stack layout
- Code generation: emit Windows x64 NASM assembly
- Linking: invoke `nasm` to produce a COFF `.obj`, then use the built-in PE linker to produce a working `.exe`

## Documentation

Primary docs:

- [Documentation Guide](/E:/project/cpp/minic/docs/README.md)
- [Project Status Overview](/E:/project/cpp/minic/docs/project-status-overview.md)
- [PE/COFF Linker Support](/E:/project/cpp/minic/docs/pe-coff-linker-support.md)
- [minic Relocation Matrix](/E:/project/cpp/minic/docs/minic-relocation-matrix.md)

Document types:

- Long-lived docs describe the project as it works today.
- Process docs under [docs/superpowers](/E:/project/cpp/minic/docs/superpowers) record design, planning, and progress history for specific workstreams.

## Supported C subset

The current compiler supports:

- multiple `char` / `short` / `int` / `long` / `long long` / `void` functions
- function declarations and later definitions
- external function declarations resolved either from other input objects or the built-in import map
- global integer variables
- global `char[]` variables
- global pointer initializers for `&global_object`
- global pointer initializers for function names such as `int (*p)() = answer;`
- global pointer initializers for string literals such as `char *p = "A";`
- global function pointer arrays with initializer lists such as `int (*table[2])() = { one, two };`
- tentative global definitions emitted via `.bss`
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

Global variables are emitted into `.data` when initialized and `.bss` when they are uninitialized/tentative definitions. The built-in PE linker preserves section-relative offsets for NASM COFF `REL32` relocations, so multiple globals placed in `.bss` keep distinct addresses in the final executable. It also supports the current compiler's minimal `.data` `ADDR64` relocation path for global pointer initializers such as `int *p = &x;`, `char *p = "A";`, `int (*fn_ptr)() = answer;`, and `int (*fn_table[2])() = { one, two };`.

The linker now also supports a small teaching-oriented import map across multiple DLLs. Today that includes:

- `kernel32.dll!ExitProcess`
- `kernel32.dll!GetCurrentProcessId`
- `msvcrt.dll!puts`

That means ordinary external declarations such as `extern int puts(char *text);` can now link through the built-in PE import machinery without adding new source syntax.

## Quick Check

Fastest way to run the current phase regression suite:

```powershell
ctest --preset phase-current
```

Fastest way to run only the `.bss` teaching case:

```powershell
ctest --preset bss
```

Fastest way to run only the import teaching cases:

```powershell
ctest --preset imports
```

Fastest way to run only the relocation teaching cases:

```powershell
ctest --preset relocations
```

## Manual Verification

Sample program:

```powershell
.\build\Debug\minic.exe .\input\answer.c
.\build\output\answer.exe
$LASTEXITCODE
```

`.bss` integrity sample with link trace:

```powershell
.\build\Debug\minic.exe .\input\tmp_bss_integrity.c --link-trace --keep-obj
```

The trace prints:

- input object summaries
- object-level defined symbols and extern references
- merged section layout
- import groups by DLL
- resolved symbols
- applied `REL32` and minimal `ADDR64` relocations

To verify multi-object PE linking:

```powershell
.\build\Debug\minic.exe .\input\tmp_multi_main.c .\input\tmp_multi_math.c --link-trace -o .\build\output\tmp_multi_trace.exe
.\build\output\tmp_multi_trace.exe
$LASTEXITCODE
```

## Run Tests

Fastest commands:

```powershell
ctest --preset phase-current
ctest --preset bss
```

Explicit underlying CTest command for the current phase:

```powershell
ctest --test-dir .\build -C Debug -L phase-current --output-on-failure
```

Current regression cases are declared in [CMakeLists.txt](/E:/project/cpp/minic/CMakeLists.txt), with per-case source files, compiler arguments, expected exit codes, and trace/output checks passed into [run_regression_case.ps1](/E:/project/cpp/minic/tests/run_regression_case.ps1).

To run a single example by test name:

```powershell
ctest --test-dir .\build -C Debug -R minic_bss_integrity --output-on-failure
```

To run only the DLL-aware import regressions:

```powershell
ctest --preset imports
```

To run only the relocation-focused regressions:

```powershell
ctest --preset relocations
```

## Notes

This is intentionally a tiny compiler, not a full ISO C implementation.
Current limits:

- no general non-constant global initializers beyond function names, `&function`, `&global_object`, and string-literal pointer forms
- no general global array initializers beyond `char[] = "..."` and the minimal function-pointer-table subset
- no local string literal initialization
- no structs
- only up to 4 parameters
- no array initializers
