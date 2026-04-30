# Project Status Overview

Date: 2026-04-30

Document type:

- long-lived status document
- summarizes the current project state rather than a single implementation session

## Summary

`minic` has moved beyond a front-end-only toy compiler and now works as a small end-to-end teaching compiler toolchain.

The strongest part of the project is no longer just parsing or code generation in isolation. The biggest milestone is that the repository now demonstrates a complete path from a small C subset to NASM assembly, COFF object files, and a self-written PE linker that produces working Windows executables.

At the same time, the project is still intentionally narrow in scope. It is best understood as a teaching-oriented compiler and linker prototype, not a broadly capable systems compiler.

Since the previous status snapshot, the biggest practical shift is that the Windows direct COFF backend is no longer limited to a small integer-only baseline. It now covers a much larger pure-C teaching path, including multi-file linking, globals, pointer-oriented relocation cases, import-driven programs, selected `long long` samples, and the current floating-point teaching subset.

## Status By Pipeline Stage

### 1. Front End

Current status:

- lexer, parser, and semantic analysis are broadly stable for the currently supported C subset
- the project now supports more than trivial single-function programs
- multi-file functions, global variables, and `.bss`-backed tentative definitions are already integrated into the front-end-to-linker flow
- current front-end support also includes `_Bool`, signed/unsigned integer families, and the current teaching subset of `float` / `double`

Assessment:

- this part is no longer the main bottleneck
- the supported language subset is still intentionally small, but the current boundary is usable and teachable

Approximate maturity:

- 70% to 75%

### 2. Code Generation

Current status:

- Windows x64 code generation is the main working path
- Linux support already exists for assembly and object emission
- the target abstraction is strong enough to support multiple output targets at the code generation layer
- Windows now has two meaningful object-generation paths:
  - the older NASM assembly path
  - the newer direct COFF object emitter path
- the direct COFF path now covers a broad teaching subset of pure-C Windows programs, including floating-point cases in the current regression suite

Assessment:

- the code generator is good enough to support the teaching goals of the current project
- the main limitation here is feature breadth, not basic correctness on the supported path

Approximate maturity:

- Windows NASM backend: about 75%
- Windows direct COFF backend: about 65%
- Linux code generation path: about 35% to 40%

### 3. Object Files And Linking

Current status:

- built-in PE linker exists and works on the current compiler-generated subset
- section handling covers:
  - `.text`
  - `.data`
  - `.rdata`
  - `.bss`
- cross-object external symbol resolution works
- minimal import handling exists for `ExitProcess`
- DLL-aware import grouping now exists for a small table-driven catalog across `kernel32.dll` and `msvcrt.dll`
- repository-level file-backed import catalog extension now exists through `config/import_catalog.txt`
- `REL32` relocation handling works on the current supported path
- function-address `ADDR64` relocations from `.data` into `.text` now work on the current compiler-generated path
- supported absolute-address image slots now emit PE `.reloc` metadata for rebasing
- `.bss` section-symbol plus addend behavior has been debugged and fixed
- teaching-oriented `--link-trace` output now exposes:
  - input objects
  - object-level symbols and extern references
  - merged sections
  - resolved symbols
  - relocations
- automated regression coverage now exists for the current teaching phase
- test labels now make a clearer distinction between:
  - Windows `coff` regressions that exercise the direct object path
  - `nasm-only` / `external-object` regressions that intentionally validate linker interoperability with hand-written `.asm` or prebuilt `.obj`

Assessment:

- this is the most distinctive and most valuable part of the project
- this is where `minic` starts to feel like a real educational toolchain rather than only a compiler front end

Approximate maturity:

- teaching-oriented PE/COFF linker: about 75%
- broader PE/COFF linker capability coverage: about 35% to 45%

## Strongest Area

The strongest area in the project today is the end-to-end PE/COFF teaching chain.

That includes:

- compiler front end
- Windows code generation
- COFF object emission through both NASM and the direct COFF backend
- built-in PE linking
- section-aware and relocation-aware debugging support
- regression coverage for the current teaching examples

This is the part of the project with the clearest identity.

## Weakest Area

The weakest technical area in the full toolchain is now less about "can the compiler emit objects at all" and more about feature breadth and backend convergence.

This is not because the linker is low quality. It is because it now matters more than the rest of the pipeline. The project has already reached the point where the linker is the narrowest part of what the toolchain can safely accept and explain.

The main weak spots are:

- relocation coverage is still narrow
- import handling is still intentionally small even though it now covers multiple DLLs, a few common C runtime calls, and a narrow file-backed extension path
- compatibility with non-`minic` object producers is intentionally weak
- failure diagnostics are functional but not yet systematic
- the direct COFF backend still does not cover every corner that the older NASM backend can reach
- Linux still depends on the separate NASM/ELF plus system-linker path

## Weakest Engineering Area

The weakest engineering area is no longer simple lack of tests; it is keeping the growing test matrix clearly partitioned by purpose.

Recent work added a large amount of automated direct COFF coverage, including failure cases, but future growth still needs discipline:

- keeping pure-C backend-coverage tests separate from linker interoperability tests
- continuing to classify `nasm-only` and `external-object` regressions explicitly
- adding more floating-point, relocation, and failure-shape combinations as backend breadth grows

The next wave of growth will need:

- more failure-case regression tests
- more section and relocation combinations
- clearer grouping of tests by development phase or linker feature

## Overall Assessment

The project is now best described as:

- a teachable, working end-to-end educational compiler toolchain
- a strong PE/COFF linker teaching prototype
- a Windows direct COFF object-emission prototype that is becoming viable for the main pure-C teaching path
- not yet a broadly capable small systems compiler

Approximate overall maturity:

- the project is in a "working, explainable, extensible" stage
- it is not yet in a "broadly complete and robust" stage

## Recommended Next Focus

The most natural next step is to keep improving the PE/COFF linker, because that is both:

- the current identity of the project
- the narrowest part of the toolchain

Recommended priority order:

1. Keep expanding the Windows direct COFF backend until the pure-C Windows teaching path no longer depends on NASM
2. Expand relocation coverage and linker interoperability clarity
3. Add more failure-case and floating-point coverage
4. Extend import handling and file-backed catalog flexibility

## Short Version

In one sentence:

The project's biggest success is that it now demonstrates both a real compiler-to-linker pipeline and a viable direct COFF Windows teaching path, and its biggest weakness is that backend breadth and interoperability breadth are still carefully controlled.
