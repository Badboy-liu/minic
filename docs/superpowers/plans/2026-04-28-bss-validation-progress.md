# `.bss` Validation Progress

Date: 2026-04-28

## Scope

Validate that uninitialized global storage is emitted into `.bss`, linked correctly, zero-initialized at runtime, and addressable without aliasing adjacent globals.

## What Was Verified

- Semantic analysis marks tentative global definitions as `.bss`.
- Code generation emits uninitialized globals into `section .bss`.
- PE linking includes a `.bss` section and accounts for uninitialized data in the PE headers.
- Runtime zero-initialization works for:
  - a single global `int`
  - a single global `char[4]`
  - mixed adjacent globals in the same `.bss` section

## Bug Found And Fixed

- Symptom:
  Mixed `.bss` globals were aliasing each other in the final executable.
- Root cause:
  NASM COFF `REL32` relocations against `.bss` used the section symbol plus an inline addend, but the built-in PE linker discarded that addend during relocation.
- Fix status:
  Fixed in [src/PeLinker.cpp](/E:/project/cpp/minic/src/PeLinker.cpp) by preserving the original signed relocation addend when computing the final relative displacement.

## Validation Artifacts

Samples added under `input/`:

- `tmp_bss_integrity.c`
- `tmp_bss_global_int.c`
- `tmp_bss_global_array.c`

Observed results after the fix:

- `tmp_bss_integrity.exe` returns `42`
- `tmp_bss_global_int.exe` returns `7`
- `tmp_bss_global_array.exe` returns `35`
- baseline `answer.exe` still returns `42`

## Current Status

Completed:

- Reproduced the `.bss` corruption bug
- Isolated it to PE `REL32` relocation handling
- Implemented and verified the linker fix
- Updated README verification guidance
- Added `--link-trace` linker output for section layout, symbol resolution, and relocation summaries
- Wrote detailed PE/COFF linker support documentation

Next reasonable step:

- Add an automated regression test path for these `.bss` samples so future linker changes catch the same class of bug earlier
