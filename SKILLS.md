# SKILLS.md

## build-output

Use this skill when compiling this project or compiling sample C programs with `minic`.

The purpose of this skill is to keep all compiled results inside the `build/` directory.

## Rules

- Put CMake build files in `build/`.
- Put the `minic` compiler executable under `build/Debug/` when using the Debug preset.
- Put generated sample executables and assembly files under `build/output/`.
- Generate NASM assembly and Windows x64 object files through `nasm`.
- Link executables with the built-in minimal PE linker, not MSVC `link.exe`.
- For multiple C inputs, compile each file to its own object and let the built-in linker resolve cross-file function calls.
- Use `--target x86_64-windows` for Windows PE executables.
- Use `--target x86_64-linux -S` for Linux NASM assembly or `--target x86_64-linux -c` for Linux ELF64 objects.
- Do not write generated executables or assembly files into the repository root.
- Do not write generated executables or assembly files into `src/` or `input/`.
- Prefer `build/output/` instead of the old `output/` directory for new generated files.

## Build The Compiler

Run from the repository root:

```powershell
cmake --preset default --fresh
cmake --build --preset debug
```

The compiler should be available at:

```powershell
.\build\Debug\minic.exe
```

## Compile A Sample

Create the output directory if needed:

```powershell
New-Item -ItemType Directory -Force .\build\output
```

Compile a sample C file into `build/output/`:

```powershell
.\build\Debug\minic.exe .\input\answer.c -o .\build\output\answer.exe
```

Expected generated files:

```powershell
.\build\output\answer.exe
.\build\output\answer.asm
```

## Validate

Run the generated executable:

```powershell
.\build\output\answer.exe
$LASTEXITCODE
```

For new examples, use the same pattern:

```powershell
.\build\Debug\minic.exe .\input\<name>.c -o .\build\output\<name>.exe
```
