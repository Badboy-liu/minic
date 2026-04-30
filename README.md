# Mini C Compiler

This project is now a small educational C compiler written in C++.

It implements the pipeline in clear stages:

- Lexing: convert source text into tokens
- Parsing: build an AST for a small C subset
- Semantic analysis: resolve locals, reject undeclared or duplicate variables, compute stack layout
- Code generation: emit Windows x64 NASM assembly or a teaching-subset direct COFF object
- Linking: assemble with `nasm` when using the NASM backend, then hand final linking to the standalone `minic-link` driver

## Documentation

Primary docs:

- [Documentation Guide](docs/README.md)
- [Project Status Overview](docs/project-status-overview.md)
- [PE/COFF Linker Support](docs/pe-coff-linker-support.md)
- [minic Relocation Matrix](docs/minic-relocation-matrix.md)
- [Testing Commands](docs/testing-commands.md)

Document types:

- Long-lived docs describe the project as it works today.
- Process docs under [docs/superpowers](docs/superpowers) record design, planning, and progress history for specific workstreams.

## Supported C subset

The current compiler supports:

- multiple `_Bool` / `char` / `short` / `int` / `long` / `long long` / `float` / `double` / `void` functions
- `signed` / `unsigned` integer type spellings across `char` / `short` / `int` / `long` / `long long`
- function declarations and later definitions
- external function declarations resolved either from other input objects or the built-in import map
- global integer variables
- global `char[]` variables
- global integer arrays with initializer lists
- global pointer initializers for `&global_object`
- global pointer initializers for function names such as `int (*p)() = answer;`
- global pointer initializers for string literals such as `char *p = "A";`
- global function pointer arrays with initializer lists such as `int (*table[2])() = { one, two };`
- tentative global definitions emitted via `.bss`
- `void` functions
- integer and pointer parameters, including Windows x64 stack-passed arguments beyond the first four
- `float` / `double` parameters and returns through the current teaching subset of x64 XMM calling convention support
- mixed integer-type arithmetic, assignment, argument passing, and `return` conversion across `_Bool` / signed integer / unsigned integer variants of `char` / `short` / `int` / `long` / `long long`
- floating-point literals and same-family `float` / `double` arithmetic, comparison, assignment, and function calls
- pointer parameters and locals
- local arrays of non-void element types
- local integer arrays with initializer lists
- local pointer arrays with restricted initializer lists
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

The linker is also available as a standalone executable:

```powershell
.\build\Debug\minic-link.exe .\build\output\answer.obj -o .\build\output\answer_from_linker.exe
```

Internally, `minic-link` now dispatches through target-specific linker backends instead of keeping all Windows and Linux linker logic inside `Toolchain.cpp`. Today that means a built-in PE/COFF backend for `x86_64-windows` and a WSL-hosted system-linker backend for `x86_64-linux`.

The default target is `x86_64-windows`. You can request targets explicitly:

```powershell
.\build\Debug\minic.exe .\input\answer.c --target x86_64-windows
.\build\Debug\minic.exe .\input\answer.c --target x86_64-linux -S --emit-asm .\build\output\answer_linux.asm
.\build\Debug\minic.exe .\input\answer.c --target x86_64-linux -c -o .\build\output\answer_linux.o
```

Linux code generation currently supports NASM ELF64 assembly/object output, including SysV-style integer/pointer argument passing through `rdi, rsi, rdx, rcx, r8, r9` plus stack arguments beyond the first six. Linux executable linking now goes through a WSL-hosted system linker path (`wsl gcc -nostdlib -no-pie ...`), so it requires a working WSL distribution with `gcc` installed.

You can also compile multiple files into one executable:

```powershell
.\build\Debug\minic.exe .\input\file1.c .\input\file2.c -o .\build\output\app.exe
```

You can also link existing Windows COFF objects alongside C inputs on the supported teaching path:

```powershell
.\build\Debug\minic.exe .\input\main.c .\build\output\helper.obj -o .\build\output\app.exe
```

Or invoke the linker directly once objects already exist:

```powershell
.\build\Debug\minic.exe .\input\answer.c -c -o .\build\output\answer.obj
.\build\Debug\minic-link.exe .\build\output\answer.obj -o .\build\output\answer_link_only.exe
```

Each input file is compiled into its own NASM assembly file and object file. The built-in PE linker then resolves external function symbols across those objects, so functions defined in one `.c` file can be called from another `.c` file.

On Windows, the experimental direct object path is also available:

```powershell
.\build\Debug\minic.exe .\input\answer.c --windows-obj-backend coff -o .\build\output\answer_direct_coff.exe
```

That path already covers a useful teaching subset, including:

- multi-file integer code
- Windows x64 stack-passed integer arguments
- simple globals in `.data` / `.bss`
- global pointer initializers
- basic imported calls such as `puts`, `putchar`, `printf`, and `GetCurrentProcessId`
- the current teaching subset of `float` / `double` parameters, returns, literals, arithmetic, comparison, and integer/floating conversion on the Windows path

It does **not** fully replace `nasm` yet. The main remaining `nasm`-backed Windows tests are now concentrated in two categories:

- hand-written `.asm` / external `.obj` interoperability cases that are intentionally kept to validate linker behavior with non-`minic` object producers
- Linux NASM/ELF paths, which are still separate from the Windows direct COFF work

Global variables are emitted into `.data` when initialized and `.bss` when they are uninitialized/tentative definitions. The built-in PE linker preserves section-relative offsets for NASM COFF `REL32` relocations, so multiple globals placed in `.bss` keep distinct addresses in the final executable. It also supports the current compiler's minimal `.data` `ADDR64` relocation path for global pointer initializers such as `int *p = &x;`, `char *p = "A";`, `int (*fn_ptr)() = answer;`, and `int (*fn_table[2])() = { one, two };`. It now also supports the current teaching subset of `.text` `ADDR64` relocations, which makes small hand-written NASM helper objects linkable on the Windows path. For supported absolute-address sites, the linker now also synthesizes a PE `.reloc` section with `DIR64` base relocations, so those executables remain correct after rebasing instead of relying on a fixed preferred image base.

The linker now also supports a small table-driven import catalog across multiple DLLs. Today that includes these built-in entries:

- `kernel32.dll!ExitProcess`
- `kernel32.dll!GetCurrentProcessId`
- `msvcrt.dll!puts`
- `msvcrt.dll!putchar`
- `msvcrt.dll!printf`

That means ordinary external declarations such as `extern int puts(char *text);`, `extern int putchar(int ch);`, and conservative simple `printf` calls can now link through the built-in PE import machinery without adding new source syntax. The import source is still built into the linker today; user-defined import catalogs are not implemented yet.

In addition, `minic-link` now loads an additive repository catalog from `config/import_catalog.txt` when that file exists. Each non-comment row uses:

```text
symbol|dll|import
```

For example:

```text
fn_strlen|msvcrt.dll|strlen
```

File-backed entries can extend the built-in catalog, but they cannot override built-in symbols.

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

The file-backed import case is part of that preset and uses the repository catalog under `config/import_catalog.txt`.

Fastest way to run only the Windows direct COFF teaching cases:

```powershell
ctest --test-dir .\build -C Debug -L coff --output-on-failure
```

Fastest way to run only the relocation teaching cases:

```powershell
ctest --preset relocations
```

Fastest way to run only the explicit linker failure teaching cases:

```powershell
ctest --preset linker-failures
```

Fastest way to run the Linux system-linker teaching cases:

```powershell
ctest --preset linux-link
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
- synthesized PE base relocations for rebasing-sensitive absolute addresses

To verify multi-object PE linking:

```powershell
.\build\Debug\minic.exe .\input\tmp_multi_main.c .\input\tmp_multi_math.c --link-trace -o .\build\output\tmp_multi_trace.exe
.\build\output\tmp_multi_trace.exe
$LASTEXITCODE
```

To verify the mixed C plus hand-written NASM object teaching path:

```powershell
D:\software\nasm\nasm.exe -f win64 -o .\build\test-objects\tmp_link_text_addr64_helper.obj .\input\tmp_link_text_addr64_helper.asm
.\build\Debug\minic.exe .\input\tmp_link_text_addr64_main.c .\build\test-objects\tmp_link_text_addr64_helper.obj --link-trace -o .\build\output\tmp_link_text_addr64_manual.exe
.\build\output\tmp_link_text_addr64_manual.exe
$LASTEXITCODE
```

Expected exit code:

```text
42
```

To verify the Linux system-linker path through WSL:

```powershell
.\build\Debug\minic.exe .\input\answer.c --target x86_64-linux -o .\build\output\answer_linux
wsl /mnt/d/project/cpp/demo2/build/output/answer_linux
```

Expected exit code:

```text
42
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

Current regression cases are declared in [CMakeLists.txt](CMakeLists.txt), with per-case source files, compiler arguments, expected exit codes, and trace/output checks passed into [run_regression_case.ps1](tests/run_regression_case.ps1). The standalone linker smoke test uses [run_linker_regression_case.ps1](tests/run_linker_regression_case.ps1).

To run a single example by test name:

```powershell
ctest --test-dir .\build -C Debug -R minic_bss_integrity --output-on-failure
```

To run only the DLL-aware import regressions:

```powershell
ctest --preset imports
```

To run only the Windows direct COFF regressions:

```powershell
ctest --test-dir .\build -C Debug -L coff --output-on-failure
```

To run only the relocation-focused regressions:

```powershell
ctest --preset relocations
```

To run only the explicit linker-failure regressions:

```powershell
ctest --preset linker-failures
```

To run only the Linux system-linker regressions:

```powershell
ctest --preset linux-link
```

## Notes

This is intentionally a tiny compiler, not a full ISO C implementation.
Current limits:

- no general non-constant global initializers beyond function names, `&function`, `&global_object`, and string-literal pointer forms
- no general aggregate initialization beyond the currently supported one-dimensional integer/pointer array subsets
- no structs
- no full ISO C floating-point and mixed arithmetic conversion coverage yet; the current support is the teaching subset exercised by the regression suite
- no full ISO C usual arithmetic conversions; integer compatibility follows the compiler's current teaching-oriented widening rules
- Linux executable linking depends on a working WSL distribution with `gcc`; this is a system-linker integration, not an in-process ELF linker
