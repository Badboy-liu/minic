# Remove NASM From The Windows Path Design

## Goal

Remove the `nasm` dependency from the `x86_64-windows` compilation path by teaching `minic` to emit AMD64 COFF object files directly, while continuing to use the existing built-in PE/COFF linker pipeline through `minic-link`.

This design is intentionally scoped to the current teaching subset. It does not attempt to replace every use of `nasm` across all targets in one step.

## Current State

Today the Windows pipeline is:

1. frontend builds the typed AST
2. backend emits NASM assembly text
3. `nasm` assembles that text into a COFF `.obj`
4. `minic-link` and `PeLinker` link one or more objects into a PE executable

Important observations:

- the project already owns the final PE/COFF linker
- the project already understands the current COFF relocation subset on input
- `nasm` is only used as the object-file producer between backend code generation and final linking
- Linux currently still depends on NASM plus a WSL-hosted system linker path

## Why This Is The Right Slice

Removing `nasm` is best approached as "replace the object writer" rather than "rewrite the linker again".

That gives the project a clear incremental path:

- keep the existing PE/COFF linker
- keep the existing target and relocation model
- replace only the translation from backend output into Windows COFF object files

This is the narrowest change that removes the Windows NASM dependency while preserving the current teaching architecture.

## Non-Goals

This phase does not attempt to:

- remove NASM from the Linux path
- introduce a general-purpose assembler
- support arbitrary third-party COFF producer compatibility
- support every AMD64 COFF relocation kind
- implement debug info, unwind info, archives, resources, or TLS
- change the built-in PE linker's documented teaching subset

## User-Facing Outcome

After this work, the supported Windows path should be:

1. `minic` parses and analyzes C input
2. `minic` emits a COFF `.obj` directly for each translation unit
3. `minic-link` links those objects into a PE executable

The command line should continue to feel the same for ordinary Windows builds:

```powershell
.\build\Debug\minic.exe .\input\answer.c -o .\build\output\answer.exe
```

The difference is internal: `nasm` is no longer required on the Windows path.

## Recommended Rollout Strategy

The safest rollout is a dual-path migration:

1. keep the current NASM-backed path available during transition
2. add a direct COFF writer path for Windows
3. validate both paths against the same regression suite
4. switch the default Windows backend only after parity is established

This allows the project to compare behavior against the existing known-good teaching path instead of trying to replace everything in one jump.

## Design Overview

The key design move is to stop treating NASM assembly text as the only backend output. Instead, the backend should expose an explicit object-file model that can be serialized directly to COFF.

Recommended layers:

1. machine-code and data emission
2. object-file model
3. COFF object writer
4. existing linker consumption

That produces a cleaner architecture than trying to reverse-engineer object structure back out of emitted assembly strings.

## New Internal Boundary: Object File Model

Introduce a backend-owned in-memory representation of one object file.

Suggested data model:

- `ObjectFileModel`
  - vector of sections
  - vector of symbols
  - vector of relocations
- `ObjectSection`
  - section name
  - raw bytes
  - alignment
  - characteristics
  - virtual size or uninitialized size for `.bss`
- `ObjectSymbol`
  - symbol name
  - section index or external/undefined marker
  - value or section-relative offset
  - storage class
- `ObjectRelocation`
  - source section
  - source offset
  - target symbol
  - relocation kind

This model should contain only what the current teaching subset needs. It should not try to become a full assembler IR.

## Why The Object Model Matters

Today several critical facts are implicit in generated NASM text:

- which bytes belong to `.text`, `.data`, `.rdata`, and `.bss`
- which names are external or global
- where relocations exist
- what relocation kind each site needs
- which symbol a relocation refers to

Those facts must become explicit if `nasm` is removed. Making them first-class data is the core of the change.

## Windows Backend Architecture

For the Windows path, the backend should stop emitting text as the primary artifact and instead emit:

- machine-code bytes for `.text`
- initialized bytes for `.data` and `.rdata`
- virtual size for `.bss`
- externally visible symbols
- relocation entries

The final COFF object writer should only serialize this model. It should not own code-generation semantics.

## COFF Writer Scope

Add a dedicated `CoffObjectWriter` responsible only for AMD64 COFF object emission on the current project subset.

Its responsibilities:

- write the COFF file header
- write the section table
- write raw section bytes
- write relocation tables
- write the symbol table
- write the string table for long names

Its non-responsibilities:

- deciding which relocations exist
- selecting instruction encodings
- computing language-level type or ABI rules
- linking objects into an executable

## Supported Section Boundary For Phase 1

The first direct COFF writer should support only the sections the project already uses and links:

- `.text`
- `.data`
- `.rdata`
- `.bss`

That matches the existing `PeLinker` teaching boundary and keeps writer and linker aligned.

## Supported Relocation Boundary For Phase 1

The first direct COFF writer should support only the relocation shapes already documented by the project's linker support:

- AMD64 `REL32` from `.text`
- AMD64 `ADDR64` from `.text` on the documented subset
- AMD64 `ADDR64` from initialized data sections for the compiler's current pointer-initializer subset

This is enough to cover:

- ordinary direct function calls
- cross-translation-unit calls
- global pointer initializers
- string-literal pointer initializers
- function pointer globals and pointer tables

## Symbol Boundary For Phase 1

The writer only needs to emit the project's current symbol families:

- compiler-generated function symbols such as `fn_main`
- compiler-generated global symbols such as `gv_value`
- section-relative symbols when needed by the current relocation model
- undefined external symbols for imports or cross-object references

It does not need to support broad third-party symbol conventions.

## Code Generation Direction

There are two plausible ways to reach direct COFF output:

1. teach the current code generator to emit object-model records directly
2. insert a small machine-code emission layer below the current high-level backend

The better long-term direction is the second option, but the first option may be the faster first milestone if kept disciplined.

Recommendation:

- keep the high-level backend decisions where they are now
- add a small Windows object-emission layer that records bytes, symbols, and relocations instead of formatting NASM text
- avoid a giant one-shot rewrite into a full new backend abstraction

## Migration Strategy

### Stage 1: Introduce The Object File Model

Create the object-model types and route the Windows path toward them without changing the Linux path.

At this stage:

- NASM may still exist
- the goal is to make symbol, section, and relocation information explicit

### Stage 2: Implement Direct COFF Writing For Minimal `.text`

Start with the smallest executable subset:

- one `.text` section
- one or more defined functions
- undefined externs
- `REL32` call relocations

This should be enough to compile and link the simplest function-call teaching cases.

### Stage 3: Add Data Sections And Pointer Relocations

Extend the writer to support:

- `.data`
- `.rdata`
- `.bss`
- `.data` and `.text` `ADDR64` cases already accepted by `PeLinker`

At that point, the current project's Windows teaching subset should be largely covered.

### Stage 4: Switch Windows Default Away From NASM

Only after parity is demonstrated:

- make direct COFF writing the default Windows path
- keep the NASM path behind a compatibility or debug option for one transition phase if desired

## Backend Selection

During migration, add an explicit backend choice for Windows builds.

For example:

- default during transition: `--windows-obj-backend nasm`
- optional new path: `--windows-obj-backend coff`

After stabilization, flip the default to `coff`.

This is better than deleting the NASM path immediately because it creates a built-in A/B verification tool.

## Repository Impact

Likely files to create:

- `src/backend/ObjectFileModel.h`
- `src/backend/CoffObjectWriter.h`
- `src/backend/CoffObjectWriter.cpp`
- possibly `src/backend/WindowsObjectEmitter.h`
- possibly `src/backend/WindowsObjectEmitter.cpp`

Likely files to modify:

- `src/backend/CodeGenerator.h`
- `src/backend/CodeGenerator.cpp`
- `src/support/Toolchain.h`
- `src/support/Toolchain.cpp`
- `src/app/Driver.cpp`
- `src/backend/Target.h`
- `README.md`
- `docs/pe-coff-linker-support.md`

## Interaction With Existing Linker

The current `PeLinker` already documents the expected object subset clearly. That is a strength, not a limitation.

The first direct COFF writer should target exactly the subset `PeLinker` already understands. The project does not need full COFF generality to remove `nasm` from its own Windows path.

In effect, `PeLinker` becomes the consumer contract for the first `CoffObjectWriter`.

## Validation Strategy

Validation should happen in three layers.

### 1. Structural Writer Validation

Check that generated objects:

- are accepted by `minic-link`
- contain the expected section names
- contain the expected symbol names
- contain relocation counts consistent with the generated source

### 2. Behavioral Regression Validation

Run the current Windows regression set against the direct COFF path:

- baseline single-file execution
- multifile calls
- `.bss` integrity
- global pointer initializers
- function pointer globals and tables
- relocation-focused cases
- import-related cases

### 3. Cross-Path Parity Validation

For a transition period, compare:

- NASM-backed Windows output
- direct COFF Windows output

The same source programs should behave identically and produce the same externally observable results.

## Risks And Mitigations

### Risk: Hidden NASM Semantics Leak Into The Current Pipeline

Mitigation:

- force all section, symbol, and relocation facts into explicit backend data structures
- avoid scraping generated assembly text as an intermediate representation

### Risk: COFF Writer And PE Linker Drift Apart

Mitigation:

- target only the subset already documented by `PeLinker`
- add regression cases that stress every currently supported relocation class

### Risk: Windows Rewrite Accidentally Destabilizes Linux

Mitigation:

- keep Linux on the existing NASM plus WSL linker path for this phase
- isolate new COFF code behind the Windows target

### Risk: Migration Is Too Large To Debug

Mitigation:

- stage the work through `.text` plus `REL32` first
- add `.data/.rdata/.bss` and `ADDR64` later
- keep the NASM path available during transition

## Recommended Implementation Order

1. Add object-model data structures.
2. Teach the Windows backend to record bytes, symbols, and relocations explicitly for minimal `.text`.
3. Implement `CoffObjectWriter` for the minimal subset.
4. Validate simple single-file and multifile call cases through `minic-link`.
5. Extend the object writer to `.data`, `.rdata`, `.bss`, and documented `ADDR64` relocations.
6. Run the relocation, import, `.bss`, and multifile regression sets on the direct COFF path.
7. Add a backend-selection switch during migration.
8. Flip the Windows default from NASM to direct COFF only after parity is stable.

## Design Summary

Yes, `nasm` can be removed from the Windows path, and the cleanest way is to replace it with a direct AMD64 COFF object writer rather than trying to rework the linker again.

The practical plan is:

- keep Linux unchanged for now
- introduce an explicit object-file model
- write a Windows-only `CoffObjectWriter`
- validate against the existing PE/COFF linker and regression suite
- migrate Windows off NASM in stages rather than all at once

This keeps scope disciplined, matches the current architecture, and gives the project a realistic path to owning the full Windows build pipeline end to end.
