# Mini C Compiler

This project is now a small educational C compiler written in C++.

It implements the pipeline in clear stages:

- Lexing: convert source text into tokens
- Parsing: build an AST for a small C subset
- Semantic analysis: resolve locals, reject undeclared or duplicate variables, compute stack layout
- Code generation: emit Windows x64 MASM assembly
- Linking: invoke `ml64` and `link` to produce a working `.exe`

## Supported C subset

The current compiler supports:

- multiple `int` / `void` functions
- function declarations and later definitions
- `void` functions
- up to 4 `int` parameters per function
- `int*` parameters and locals
- local `int` arrays
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
.\build\Debug\minic.exe .\input\answer.c -o .\output\answer.exe
```

The compiler also writes the generated assembly next to the executable:

```powershell
.\output\answer.asm
.\output\answer.exe
```

## Verify the sample

The sample program returns `42` as its process exit code:

```powershell
.\output\answer.exe
$LASTEXITCODE
```

## Notes

This is intentionally a tiny compiler, not a full ISO C implementation.
Current limits:

- only `int`, `void`, `int*`, and local `int[N]`
- only up to 4 parameters
- no global variables
- no array initializers
