# minic Types, Multi-File Compilation, and Linker Evolution Design

## Goal

Extend `minic` from a single-file `int`-centric teaching compiler into a still-small but materially more capable compiler that:

- supports the common built-in C scalar types plus existing pointer/array forms,
- compiles and links multiple `.c` translation units into one executable,
- introduces internal artifact and link abstractions that support a later self-hosted object writer and linker.

This design intentionally preserves the staged compiler architecture and keeps the first implementation phase compatible with the existing Windows x64 MASM and `link.exe` toolchain.

## Current State

The repository currently implements:

- lexing and parsing for a small C subset,
- semantic analysis for `int`, `void`, pointers, and local `int[N]`,
- MASM assembly generation for a single translation unit,
- direct `ml64.exe` plus `link.exe` invocation to produce one executable from one generated assembly file.

Important current limitations:

- only one input source file can be compiled per invocation,
- the type system only models `int`, `void`, `pointer`, and `array`,
- the toolchain layer is hard-coded around one `.asm` to one `.obj` to one `.exe`,
- there is no internal abstraction for link inputs, symbol aggregation, or future self-hosted linking.

## Non-Goals

This project expansion does not attempt to make `minic` a full ISO C compiler. The following stay out of scope for this effort:

- preprocessor support such as `#include`, `#define`, or macro expansion,
- `struct`, `union`, `enum`, and `typedef`,
- qualifiers such as `const` or `volatile`,
- function pointers,
- variadic functions,
- global variables and global initializers,
- full literal suffix parsing and full constant-folding semantics,
- source compatibility with arbitrary external C object files,
- fully replacing the platform CRT or implementing a production-grade PE linker in this phase.

## User-Facing Outcomes

After the first implementation phase, users should be able to:

- declare and use the common built-in scalar types in locals, parameters, returns, pointers, and local arrays,
- compile multiple `.c` files in one command, for example `minic a.c b.c c.c -o app.exe`,
- produce one assembly file and one object file per input translation unit,
- link all generated object files into one executable through the existing external Microsoft toolchain,
- rely on compiler diagnostics that describe type mismatches, unsupported forms, and cross-translation-unit symbol conflicts clearly.

## Supported Type Boundary

### Built-In Scalar Types

The first implementation phase should support these built-in scalar types:

- `void`
- `_Bool`
- `char`
- `signed char`
- `unsigned char`
- `short`
- `unsigned short`
- `int`
- `unsigned int`
- `long long`
- `unsigned long long`
- `float`
- `double`

### Derived Types

The first implementation phase should support:

- pointers to all supported non-function types,
- local arrays of supported non-void element types,
- function parameters using supported scalar and pointer types,
- function return values using supported scalar and pointer types.

### Explicitly Unsupported Type Forms in Phase 1

Even after this expansion, the compiler should still reject:

- arrays as function parameters unless they decay through the parser or semantic rules already established for parameter declarations,
- arrays returned from functions,
- `void` variables,
- dereferencing `void*`,
- arrays of `void`,
- arrays of unsupported aggregate types.

## Type System Design

### Representation

Replace the current binary distinction of `Int` and `Void` with a richer scalar representation while preserving `Pointer` and `Array` as derived forms.

Recommended structure:

- a top-level `TypeKind` distinguishing `Void`, `Scalar`, `Pointer`, and `Array`,
- a `ScalarKind` distinguishing `_Bool`, integer families, and floating-point families,
- scalar metadata containing:
  - size in bytes,
  - signedness for integer-like types,
  - whether the scalar is floating-point,
  - display spelling for diagnostics.

This keeps storage, layout, and conversion rules centralized instead of scattering special cases across semantic analysis and code generation.

### Core Semantic Rules

The first implementation phase should implement a disciplined, teachable subset of C conversions:

- integer promotion for narrow integer types and `_Bool`,
- usual arithmetic conversions for integer/integer, integer/floating, and floating/floating binary arithmetic,
- assignment compatibility across scalar types with explicit code generation for truncation, sign extension, zero extension, and integer/float conversion,
- pointer assignment only for identical pointed-to types and `void*` compatibility,
- pointer arithmetic only for pointer plus integer, integer plus pointer, and pointer minus integer,
- pointer comparison only for compatible pointer types,
- array-to-pointer decay in expression contexts and function call argument matching,
- scalar-only conditions for `if`, `while`, `for`, `&&`, `||`, and `!`.

### Diagnostics

Diagnostics should continue to reject unsupported or ambiguous cases early, with concrete type names in errors. Examples:

- assigning `double*` to `int*`,
- returning `float` from a `char*` function,
- dereferencing `void*`,
- declaring `void items[4]`,
- using array values where assignment requires a modifiable lvalue.

## Parser Changes

The parser currently understands `int` and `void` with postfix `*` and array syntax. It should be extended to parse:

- `_Bool`,
- `char`,
- `short`,
- `long long`,
- `signed` and `unsigned` combinations,
- `float`,
- `double`.

Because this is a teaching compiler, the parser should accept only the normalized combinations that the implementation supports, rather than trying to accept every spelling the real C grammar permits. For example:

- accept `unsigned int`,
- accept `unsigned short`,
- accept `signed char`,
- accept `long long`,
- reject combinations outside the supported table with a parser or semantic error.

This keeps the grammar small while still covering the common built-in types the user asked for.

## Semantic Analysis Changes

Semantic analysis should be split conceptually into two layers:

1. translation-unit-local checks,
2. whole-invocation symbol aggregation for multi-file builds.

### Translation-Unit-Local Checks

Per translation unit, semantic analysis should continue to:

- resolve local variable scopes,
- compute stack layout,
- check statement and expression typing,
- validate parameter and return types,
- annotate the AST with concrete types and storage details needed by code generation.

### Whole-Invocation Checks

Across all input files, the compiler driver should aggregate function declarations and definitions and enforce:

- no conflicting declarations across translation units,
- no duplicate non-declaration definitions,
- exactly one `main` definition for an executable build,
- `main` remains `int main()` in phase 1.

This whole-invocation pass should reuse the same notion of canonical function signature used by per-unit semantic analysis.

## Code Generation Design

### ABI Boundary

The implementation target remains Windows x64. The expanded type support should follow a constrained but consistent ABI subset:

- integer and pointer parameters continue through the general-purpose calling convention path,
- `float` and `double` parameters and returns use XMM registers and corresponding stack spill behavior where needed,
- narrow integer values load and store at their real width,
- sign extension versus zero extension depends on source type signedness,
- scalar return values use the appropriate integer or floating return register path.

### Data Width Behavior

Code generation must stop assuming every scalar is an `int`. It should instead use helper routines that answer:

- storage width,
- load width,
- whether sign extension is required,
- whether floating instructions are required,
- array element stride,
- how binary operators should lower for the resulting common type.

This design is important not only for correctness but also because multi-file calls need consistent ABI treatment on both caller and callee sides.

### Phase-1 Floating-Point Constraint

`float` and `double` should be supported for:

- literals if the lexer and parser are extended that far in the same phase, or
- at minimum declarations, parameters, returns, assignments, and arithmetic on compiler-generated or passed values.

If floating literals are too large a slice for the same phase, that should be called out explicitly in the implementation plan and delivered as a follow-up task, not left ambiguous.

## Multi-File Compilation Model

### Invocation Shape

The driver should change from:

- `minic input.c -o output.exe`

to:

- `minic input1.c input2.c ... -o output.exe`

with compatibility preserved for the single-file case.

### Translation Units

Each input file becomes an independent translation unit with its own:

- source text,
- token stream,
- AST,
- per-unit semantic annotations,
- generated assembly path,
- generated object path.

The driver should compile each translation unit independently through the staged pipeline and only converge at the linking step.

### Artifact Naming

The output model should avoid collisions when multiple input files share the same stem. A safe phase-1 strategy is:

- use the output directory plus sanitized input stem for human-readable artifact names when unique,
- append a deterministic suffix when stems collide.

The artifact plan should always know the exact `.asm` and `.obj` path for each translation unit before execution begins.

## Link Abstraction Design

### Motivation

Today `Toolchain` is effectively a hard-coded "assemble this one assembly file and immediately link it into an executable" function. That shape blocks both multi-file builds and future self-hosted linking.

The new design should introduce explicit abstractions for:

- one translation unit's compile outputs,
- one object file as a link input,
- the full executable link plan,
- the backend responsible for assembling and linking artifacts.

### Recommended Internal Abstractions

Reasonable internal data objects for phase 1:

- `TranslationUnitInput`
  - source path
  - source text
- `TranslationUnitResult`
  - source path
  - assembly path
  - object path
  - declared and defined function signatures
- `LinkInput`
  - object path
  - source provenance for diagnostics
- `LinkPlan`
  - vector of `LinkInput`
  - final executable path
  - entry symbol expectations

Exact names may vary, but this information boundary should exist.

### Toolchain Backend in Phase 1

Phase 1 still uses:

- generated MASM assembly,
- `ml64.exe` to create each `.obj`,
- `link.exe` to link all `.obj` files into the final `.exe`.

But the toolchain API should change from a single-file API into separate operations, for example:

- assemble one translation unit,
- assemble many translation units,
- link many object files.

This keeps the implementation compatible with today's environment while isolating the parts that later become self-hosted.

## Self-Hosted Linker Evolution Plan

### Phase 1: Link Abstraction and Multi-Object Flow

Deliver in this project:

- expanded type system,
- multi-file driver,
- link-plan abstraction,
- external MASM and `link.exe` backend.

This phase solves the user-visible feature request for multi-file compilation and creates the seam for future backend replacement.

### Phase 2: Internal COFF Object Writer

Future work after phase 1:

- replace MASM text emission plus `ml64.exe` with direct COFF `.obj` writing,
- emit sections, symbols, and relocations matching the compiler's generated code,
- keep using `link.exe` temporarily as the final executable linker.

This is the lowest-risk first step toward a self-hosted linker because it narrows the problem to object-file emission without yet taking on PE image layout.

### Phase 3: Minimal PE/COFF Linker

Later work:

- read the compiler's own COFF objects,
- resolve symbols and apply relocations,
- build a minimal PE executable image,
- provide the imports needed for the current runtime entry assumptions.

The first self-hosted linker should target only the object shapes produced by this compiler, not arbitrary third-party object files. That keeps the scope educational and tractable.

## Repository Impact

Expected modification areas:

- `src/Ast.h`
  - richer type representation
- `src/Parser.*`
  - expanded built-in type parsing and input list handling if driver-facing parsing remains local
- `src/Semantics.*`
  - conversions, compatibility, and multi-unit signature aggregation support
- `src/CodeGenerator.*`
  - width-aware and floating-aware code generation
- `src/Driver.*`
  - multiple input files, artifact planning, and whole-build orchestration
- `src/Toolchain.*`
  - separate assemble and link APIs for many artifacts
- possibly one or more new files such as:
  - `src/Artifacts.*`
  - `src/Compilation.*`
  - `src/Types.*`

Expected validation inputs:

- new sample programs under `input/` covering scalar widths and sign behavior,
- new sample programs spanning multiple source files with cross-unit declarations and definitions,
- negative samples for conflicting declarations and unsupported type operations.

## Testing and Validation Strategy

Validation for phase 1 should include:

- rebuilding the compiler,
- compiling at least one existing single-file sample to verify no regression,
- compiling a multi-file sample into one executable,
- running the produced executable and checking exit status,
- exercising at least one sample each for:
  - narrow signed integer behavior,
  - narrow unsigned integer behavior,
  - `long long`,
  - pointer arithmetic with non-`int` pointee sizes,
  - cross-file function declaration and definition matching,
  - duplicate or conflicting declarations rejected with diagnostics.

Floating-point validation should be included if floating arithmetic lands in the same phase. If it does not, the implementation plan must separate "type acceptance" from "full floating codegen" explicitly to avoid claiming support that is not executable.

## Risks and Mitigations

### Risk: Type Expansion Breaks Existing `int` Assumptions

Mitigation:

- centralize type size, classification, and conversion helpers,
- replace ad hoc `isInteger()` checks with richer predicates,
- keep `int` regression samples in the validation set.

### Risk: ABI Mismatch Across Translation Units

Mitigation:

- use one canonical function-signature model for both call checking and code generation,
- add cross-unit call samples early,
- keep `main` shape restricted in phase 1.

### Risk: Floating-Point Scope Balloons

Mitigation:

- make floating-point support an explicit implementation milestone with dedicated tests,
- permit the implementation plan to stage integer-family completeness before full floating execution if needed.

### Risk: Linker Abstraction Becomes Premature Complexity

Mitigation:

- keep the abstraction shallow and artifact-oriented,
- avoid designing for archives, shared libraries, or third-party objects in phase 1,
- only model the information the current pipeline and next backend phase actually need.

## Recommended Implementation Order

1. Refactor type representation and semantic helper utilities.
2. Extend parser support for the chosen built-in type spellings.
3. Update semantic analysis for scalar conversions, pointer compatibility, and diagnostics.
4. Update code generation for width-aware integer handling and ABI correctness.
5. Introduce multi-input driver flow and artifact planning.
6. Refactor toolchain API to assemble many objects and link many objects.
7. Add validation samples for types and multi-file builds.
8. If included in the same delivery, finish floating-point lowering and tests last so integer-path stability can be established first.

## Design Summary

The recommended path is a layered evolution:

- make the type system first-class and width-aware,
- treat each source file as an independent translation unit,
- introduce explicit object and link planning inside the compiler,
- keep external MASM and `link.exe` only as phase-1 backends,
- use the new seam to enable a later COFF writer and then a minimal self-hosted PE/COFF linker.

This meets the user's goals without collapsing the project's educational scope or forcing a risky all-at-once backend rewrite.
