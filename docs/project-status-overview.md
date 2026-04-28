# Project Status Overview

Date: 2026-04-28

Document type:

- long-lived status document
- summarizes the current project state rather than a single implementation session

## Summary

`minic` has moved beyond a front-end-only toy compiler and now works as a small end-to-end teaching compiler toolchain.

The strongest part of the project is no longer just parsing or code generation in isolation. The biggest milestone is that the repository now demonstrates a complete path from a small C subset to NASM assembly, COFF object files, and a self-written PE linker that produces working Windows executables.

At the same time, the project is still intentionally narrow in scope. It is best understood as a teaching-oriented compiler and linker prototype, not a broadly capable systems compiler.

## Status By Pipeline Stage

### 1. Front End

Current status:

- lexer, parser, and semantic analysis are broadly stable for the currently supported C subset
- the project now supports more than trivial single-function programs
- multi-file functions, global variables, and `.bss`-backed tentative definitions are already integrated into the front-end-to-linker flow

Assessment:

- this part is no longer the main bottleneck
- the supported language subset is still intentionally small, but the current boundary is usable and teachable

Approximate maturity:

- 60% to 70%

### 2. Code Generation

Current status:

- Windows x64 code generation is the main working path
- Linux support already exists for assembly and object emission
- the target abstraction is strong enough to support multiple output targets at the code generation layer

Assessment:

- the code generator is good enough to support the teaching goals of the current project
- the main limitation here is feature breadth, not basic correctness on the supported path

Approximate maturity:

- about 65%

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
- `REL32` relocation handling works on the current supported path
- `.bss` section-symbol plus addend behavior has been debugged and fixed
- teaching-oriented `--link-trace` output now exposes:
  - input objects
  - object-level symbols and extern references
  - merged sections
  - resolved symbols
  - relocations
- automated regression coverage now exists for the current teaching phase

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
- COFF object emission through NASM
- built-in PE linking
- section-aware and relocation-aware debugging support
- regression coverage for the current teaching examples

This is the part of the project with the clearest identity.

## Weakest Area

The weakest technical area in the full toolchain is still the PE/COFF linker's support breadth.

This is not because the linker is low quality. It is because it now matters more than the rest of the pipeline. The project has already reached the point where the linker is the narrowest part of what the toolchain can safely accept and explain.

The main weak spots are:

- relocation coverage is still narrow
- import handling is still minimal
- compatibility with non-`minic` object producers is intentionally weak
- failure diagnostics are functional but not yet systematic

## Weakest Engineering Area

The weakest engineering area is the depth of regression coverage relative to future linker growth.

Recent work added useful automated tests for the current teaching phase, but the coverage is still concentrated on the current happy path:

- baseline single-file linking
- `.bss` integrity
- multi-file function resolution

The next wave of growth will need:

- more failure-case regression tests
- more section and relocation combinations
- clearer grouping of tests by development phase or linker feature

## Overall Assessment

The project is now best described as:

- a teachable, working end-to-end educational compiler toolchain
- a strong PE/COFF linker teaching prototype
- not yet a broadly capable small systems compiler

Approximate overall maturity:

- the project is in a "working, explainable, extensible" stage
- it is not yet in a "broadly complete and robust" stage

## Recommended Next Focus

The most natural next step is to keep improving the PE/COFF linker, because that is both:

- the current identity of the project
- the narrowest part of the toolchain

Recommended priority order:

1. Expand relocation coverage
2. Add more failure-case teaching and regression samples
3. Extend import handling

## Short Version

In one sentence:

The project's biggest success is that it already demonstrates a real compiler-to-linker pipeline, and its biggest weakness is that the linker still supports only a narrow, carefully controlled PE/COFF subset.
