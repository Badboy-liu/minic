# minic Relocation Matrix

This document tracks relocation support for the current `minic -> NASM -> COFF -> PE` pipeline.

The purpose of this matrix is not to describe generic PE/COFF relocation theory. The purpose is to answer a narrower question:

- what relocation shapes can the current `minic` code generator actually produce
- which of those are already supported by the built-in PE linker
- which future C features would require new relocation support

## Guiding Rule

The current priority is:

- first make the linker fully self-consistent for relocations that `minic` itself can generate today
- only after that expand toward broader NASM/COFF compatibility

That makes this document a compiler-driven support matrix, not a general COFF feature checklist.

## Current Linker Boundary

Today the built-in PE linker supports:

- relocations read from `.text`
- the minimal compiler-generated `ADDR64` cases read from `.data`
- AMD64 COFF `REL32`
- AMD64 COFF `ADDR64`
- targets in merged `.text`, `.data`, `.rdata`, and `.bss`
- cross-object external symbol resolution
- the hard-coded imported `ExitProcess` thunk
- section-symbol plus addend cases such as `.bss + 4`

Today the built-in PE linker does not claim support for:

- relocation kinds other than `REL32`
- relocations originating from `.rdata` or `.bss`
- broad `.data` relocation coverage beyond the current global-pointer initializer subset
- a broad range of import-related relocation shapes
- general-purpose COFF producer compatibility

## Matrix

| Codegen scenario | Typical assembly shape | COFF target shape seen by linker | Current status | Why it matters |
|---|---|---|---|---|
| Call current-file or cross-file function | `call fn_name` | `REL32` from `.text` to function symbol in `.text` | Supported | Required for all normal function calls |
| Call imported process-exit path | `call ExitProcess` | `REL32` from `.text` to imported thunk | Supported | Required for generated Windows entry path |
| Address global scalar or global array | `lea rax, [rel gv_name]` | `REL32` from `.text` to `.data` or `.bss` | Supported | Required for global loads/stores and `&global` |
| Address string literal | `lea rax, [rel str_n]` | `REL32` from `.text` to `.rdata` | Supported | Required for current string-literal codegen path |
| Access adjacent global inside a section | `lea rax, [rel gv_name]` with section-relative addend in object | `REL32` from `.text` to section symbol plus addend | Supported | Required for `.bss` and similar packed globals |
| Store global object address in initialized pointer data | `gv_p: dq gv_x` | `ADDR64` from `.data` to `.bss` or `.data` | Supported | Required for `int *p = &x;` |
| Store string literal address in initialized pointer data | `gv_p: dq str_n` | `ADDR64` from `.data` to `.rdata` | Supported | Required for `char *p = "A";` |
| Store function address in initialized pointer data | `gv_p: dq fn_name` | `ADDR64` from `.data` to `.text` | Supported | Required for `int (*p)() = f;` |
| Store a small function-pointer table in initialized data | `gv_table: dq fn_a, fn_b` | repeated `ADDR64` from `.data` to `.text` | Supported | Required for minimal dispatch-table style programs |
| Address local stack object | `lea rax, [rbp-offset]` | no object relocation | Not applicable | Pure frame-relative addressing |
| Arithmetic on a pointer already in a register | `add/sub/imul` around runtime pointer values | no object relocation | Not applicable | Runtime address math only |

## Evidence From Current Pipeline

The following current examples exercise the supported relocation set:

- `input/tmp_bss_integrity.c`
  - `.text -> .bss`
  - section symbol plus addend cases such as `.bss + 4`
- `input/tmp_globals_main.c`
  - `.text -> .bss`
- `input/tmp_string_local.c`
  - `.text -> .rdata`
- `input/tmp_multi_main.c` + `input/tmp_multi_math.c`
  - `.text -> .text` across object files
- `input/tmp_global_ptr_to_global.c`
  - `.data -> .bss` via `ADDR64`
- `input/tmp_global_ptr_to_string.c`
  - `.data -> .rdata` via `ADDR64`
- `input/tmp_function_ptr_global.c`
  - `.data -> .text` via `ADDR64`
- `input/tmp_function_ptr_table.c`
  - repeated `.data -> .text` via `ADDR64`

The current `--link-trace` output confirms that these cases reduce to:

- `REL32` for code references from `.text`
- `ADDR64` for the minimal initialized-data pointer cases
- `ADDR64` for the minimal initialized-data function-pointer cases

## What This Means

For the current compiler subset, the most important observation is:

- the code generator currently concentrates all non-local addressing into RIP-relative references from `.text`
- that means the current chain is still mostly a one-relocation-model linker: `REL32` from `.text`

This is why the linker already feels coherent despite having a narrow relocation implementation.

## Remaining Gaps That Do Not Block The Current Pipeline

The following areas are still missing, but they do not block the current compiler-generated path today:

- relocations stored in `.rdata`
- broader absolute-address relocation forms
- more specialized call/jump relocation kinds
- richer import relocation handling
- PE base relocation table emission for rebasing absolute pointers

Those gaps matter for future growth, but they are not required to keep the current language subset self-consistent.

## Future C Features That Will Force New Relocation Support

These are the most likely future features that would break the current relocation boundary:

### 1. Function Pointer Constants Or Tables

Examples:

```c
int f() { return 1; }
int (*p)() = f;
```

Why it matters:

- function addresses stop being only call targets and become data values

Likely consequence:

- more data-side relocation coverage beyond the current object and string-pointer subset

### 2. Richer Read-Only Address Tables

Examples:

- jump tables
- arrays of pointers
- more complex constant objects

Why it matters:

- references may need to live in `.rdata`
- relocation handling would need to extend beyond code references

### 3. More General Import Usage

Examples:

- more imported APIs
- imported function addresses treated as data

Why it matters:

- the current import model is hard-coded around the entry path
- broader import usage usually expands both symbol resolution and relocation demands

## Recommended Next Relocation Work

If the goal is to keep `minic` self-consistent first, the next relocation work should follow this order:

1. Confirm whether any current or near-term codegen change can produce a relocation outside the currently supported `.text` plus minimal `.data` subset
2. If yes, add support for the narrowest real case needed by `minic`
3. Add a sample, trace coverage, and regression coverage for that exact case
4. Keep the PE image-base/rebasing boundary explicit until a `.reloc` path exists
5. Only then broaden toward more general NASM/COFF support

## Short Version

The current `minic` pipeline is in a better state because its non-local references now collapse into two small supported models:

- `REL32` from `.text`
- minimal `ADDR64` from `.data`

The next real relocation boundary is now broader data/address usage such as function pointers, pointer tables, and rebasing support for absolute pointers.
