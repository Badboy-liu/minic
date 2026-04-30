# PE Base Relocation Support Design

Date: 2026-04-30

## Goal

Extend `minic-link` so Windows x64 PE executables remain correct when the image is loaded at a base address different from the preferred image base.

This phase adds real PE base relocation support rather than keeping absolute addresses valid only when the image lands at the linker's fixed preferred base.

The immediate objective is:

- emit a valid `.reloc` section for the current `minic`-generated Windows executables
- record every image location that stores a rebasing-sensitive absolute address
- support loader-applied `DIR64` base relocations for the current PE32+ subset

## Why This Phase Next

The current linker already supports several forms of absolute addresses stored in the final image:

- `.data -> .bss` via `int *p = &x;`
- `.data -> .rdata` via `char *p = "A";`
- `.data -> .text` via `int (*p)() = f;`
- repeated `.data -> .text` via small function-pointer tables
- a small hand-written NASM helper path that can also produce `ADDR64`

Today those values are written as preferred-base virtual addresses. That works only because the produced image does not emit a PE base relocation table and therefore implicitly depends on loading at its preferred base.

For a hand-written compiler/linker, this is now the most important correctness gap in the Windows executable path.

## Scope

### In Scope

- PE32+ x64 base relocation support
- synthesized `.reloc` section emission
- `IMAGE_REL_BASED_DIR64` relocation entries
- collection of rebasing-sensitive absolute addresses produced by current `minic` code generation
- collection of rebasing-sensitive absolute addresses produced by the currently supported hand-written NASM helper subset
- `--link-trace` reporting for generated base relocation blocks and entry counts
- regression coverage proving rebasing-sensitive executables are no longer fixed-base only

### Out Of Scope

- PE32 32-bit support
- relocation kinds other than `IMAGE_REL_BASED_DIR64`
- import-by-ordinal, delay import, TLS, SEH/unwind, resources, debug info
- arbitrary third-party COFF compatibility
- a generic loader simulator inside `minic`
- Linux or ELF relocation work

## Current Limitation

The current linker writes absolute addresses into the image for supported `ADDR64` relocation sites, but it does not also describe those sites in a PE base relocation table.

As a result:

- the image is only correct at the preferred base
- absolute pointers in `.data` are not loader-fixable after rebasing
- the current linker docs must keep an explicit caveat about fixed image base behavior

## Required Outcome

After this phase:

- every supported absolute address written into the image must either:
  - be listed in `.reloc` as a `DIR64` base relocation site, or
  - be proven not to require rebasing
- the final PE image must include a valid `.reloc` data directory entry
- the generated `.reloc` section must contain one or more base relocation blocks grouped by 4 KiB page RVA, following PE rules
- current pointer-in-data examples must still run correctly even if the loader chooses a non-preferred base

## Absolute Address Site Inventory

This phase is driven by the final image, not just by input COFF relocation records.

The linker must explicitly track every write site that stores an absolute virtual address into the image.

### Current Known Required Sites

1. `.data` entries patched from compiler-generated `ADDR64`

Examples:

- `int *p = &x;`
- `char *p = "A";`
- `int (*p)() = f;`
- `int (*table[2])() = { f, g };`

These are the primary current rebasing-sensitive sites.

2. `.text` or `.data` entries patched from the currently supported hand-written NASM helper subset

The current linker already accepts a documented teaching subset of `ADDR64` usage from small helper objects. Any accepted case that stores an absolute VA into the final image must also contribute a base relocation site.

### Not Required Sites

The following do not need PE base relocation entries:

- RIP-relative `REL32` call and address references from `.text`
- pure section layout metadata not stored as runtime VA values
- file offsets
- RVAs that are not converted to absolute virtual addresses

## Design Rule

The source of truth is:

- not the source code
- not the assembly text
- not only the input COFF relocation type

The source of truth is the final image write:

- if the linker writes an absolute image virtual address into section contents, that location must be eligible for rebasing and therefore must be recorded for `.reloc`

## Proposed Linker Model

### 1. Track Base-Relocation Sites During Image Patching

Whenever the linker resolves a supported absolute-address relocation and writes the final 64-bit virtual address into section contents, it must also record:

- target section name
- final section RVA
- relocation site offset within the merged section
- final image RVA of the patched slot

The recorded site should be attached to the final merged image rather than kept only in per-object relocation processing state.

### 2. Build PE Base Relocation Blocks

After final section RVAs are known and after all absolute addresses have been patched:

- group recorded relocation-site RVAs by 4 KiB page
- for each page, emit one `IMAGE_BASE_RELOCATION` block
- append one 16-bit relocation entry per site
- use type `IMAGE_REL_BASED_DIR64`

Each encoded entry should contain:

- high 4 bits: relocation type = `DIR64`
- low 12 bits: offset within the 4 KiB page

Each block size must include:

- 8-byte block header
- 2 bytes per relocation entry
- padding as needed so the block ends on a 4-byte boundary

### 3. Add `.reloc` As A Synthesized Section

The linker should synthesize `.reloc` similarly to how it already synthesizes `.idata`:

- assign final section RVA and raw file placement
- emit raw bytes for relocation blocks
- add section header with appropriate characteristics for initialized read-only data
- populate the PE data directory entry for base relocations

### 4. Preserve Existing Addressing Model

This phase should not change:

- the current `REL32` model
- symbol resolution
- import thunk generation
- current `ADDR64` support semantics

It only adds the missing loader metadata needed to rebase already-supported absolute addresses.

## Trace Behavior

`--link-trace` should grow with one compact additional block:

- `[link] base relocations`

It should show:

- total number of recorded `DIR64` sites
- number of `.reloc` blocks
- one short line per page block, for example:
  - page RVA
  - entry count

It may also show a short per-site summary when the count is small, but the default should stay compact.

## Validation Strategy

### 1. Positive Compiler-Generated Cases

At minimum, keep the following cases in regression:

- `int *p = &x;`
- `char *p = "A";`
- `int (*p)() = f;`
- `int (*table[2])() = { f, g };`

These cases should continue to pass their current runtime checks.

### 2. Rebase-Sensitive Verification

This phase needs stronger validation than "the executable runs once".

The validation path should prove:

- the final image contains a non-empty `.reloc` section when rebasing-sensitive absolute addresses are present
- the PE base relocation directory is populated
- a forced non-preferred-base load still preserves correct runtime behavior

The implementation may choose one of these verification styles:

- inspect `.reloc` structure and verify the expected relocation-site RVAs are present
- or add a small test helper that maps the image at a non-preferred base and checks that rebased pointers still point to correct runtime targets
- or combine structural validation plus controlled runtime rebasing validation

The important requirement is that rebasing is actually verified, not only inferred.

### 3. Negative Coverage

Add at least one targeted regression that proves:

- images with no absolute-address write sites may emit an empty or omitted `.reloc` path according to the chosen implementation rule
- unsupported absolute-address relocation forms still fail clearly instead of silently skipping required base relocation recording

## Documentation Updates Required

Once implemented, long-lived docs must be updated to remove the current fixed-image-base caveat for the supported subset.

That includes:

- `README.md`
- `docs/pe-coff-linker-support.md`
- `docs/minic-relocation-matrix.md`
- `docs/project-status-overview.md`

## Success Criteria

This phase is complete when all of the following are true:

1. `minic-link` emits a valid `.reloc` section and PE base relocation data directory for supported rebasing-sensitive images
2. all currently supported compiler-generated absolute pointer data sites are recorded as `DIR64` base relocation entries
3. the current supported hand-written `ADDR64` helper subset is either:
   - covered by `.reloc`, or
   - explicitly rejected if it falls outside the supported rebasing model
4. the runtime behavior of current pointer/address samples remains correct after non-preferred-base loading
5. docs no longer describe the supported Windows path as fixed-base only

## Short Version

The next step is not "teach `.reloc`". The next step is to make the current Windows executable path correct for rebasing by recording every absolute virtual address the linker writes into the image and emitting a real PE base relocation table for those sites.
