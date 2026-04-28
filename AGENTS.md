# AGENTS.md

## Project Overview

This repository contains `minic`, a small educational C compiler written in C++17. It compiles a limited C subset into Windows x64 NASM assembly, assembles it into a COFF object, and links it into a PE executable with the built-in linker.

The compiler pipeline is organized into clear stages:

- Lexing
- Parsing
- Semantic analysis
- Target-aware code generation for `x86_64-windows` and `x86_64-linux`
- Toolchain invocation for `nasm`
- Built-in multi-object PE linking

## Repository Layout

- `src/` contains the compiler implementation and headers.
- `input/` contains sample C source files.
- `output/` contains generated assembly and executables.
- `build/` is the CMake preset output directory.
- `cmake-build-debug/`, `.idea/`, and `vcpkg_installed/` are local tooling/build directories.

## Build

Run commands from the repository root.

```powershell
cmake --preset default --fresh
cmake --build --preset debug
```

The Debug compiler executable is:

```powershell
.\build\Debug\minic.exe
```

## Run

Compile a sample C file:

```powershell
.\build\Debug\minic.exe .\input\answer.c
```

Generate Linux x86_64 assembly or an ELF64 object:

```powershell
.\build\Debug\minic.exe .\input\answer.c --target x86_64-linux -S --emit-asm .\build\output\answer_linux.asm
.\build\Debug\minic.exe .\input\answer.c --target x86_64-linux -c -o .\build\output\answer_linux.o
```

Run the generated executable and inspect its exit code:

```powershell
.\build\output\answer.exe
$LASTEXITCODE
```

The compiler also writes generated assembly next to the output executable, for example:

```powershell
.\build\output\answer.asm
```

## Coding Guidelines

- Keep changes focused on the requested compiler behavior.
- Preserve the staged architecture: lexer, parser, semantic analysis, code generation, and toolchain logic should remain separated.
- Prefer C++17-compatible code.
- Keep headers and implementation files in `src/`.
- Avoid committing generated files from `build/`, `cmake-build-debug/`, `output/`, `.idea/`, or `vcpkg_installed/`.
- When adding language features, update the relevant stage and keep diagnostics clear.
- Do not introduce dependencies unless the task explicitly needs them.

## Validation

After compiler changes, build the project and run at least one sample compile:

```powershell
cmake --build --preset debug
.\build\Debug\minic.exe .\input\answer.c
.\build\output\answer.exe
$LASTEXITCODE
```

For parser, semantic, or code generation changes, add or update small sample programs under `input/` when useful, then compile them into `build/output/` for manual verification.

## Notes For Agents

- This is intentionally not a full ISO C compiler. Match the current tiny-compiler scope unless the user asks to expand it.
- Read `README.md` before making feature changes; it lists the currently supported C subset and known limits.
- Generated NASM assembly is useful for debugging code generation issues.
- `PeLinker` is intentionally minimal: it supports NASM-generated AMD64 COFF objects, `.text`, `REL32` relocations, and `kernel32.dll!ExitProcess`.
- `PeLinker` resolves external function symbols across multiple NASM-generated AMD64 COFF objects.
- Linux currently supports assembly/object emission only; do not expect Linux executable linking until an ELF linker or system linker integration is added.
- Be careful around Windows x64 calling convention and stack layout changes.
