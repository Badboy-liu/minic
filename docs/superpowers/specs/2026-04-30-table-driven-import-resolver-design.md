# Table-Driven Import Resolver Design

Date: 2026-04-30

## Goal

Replace the current hard-coded import handling in `minic-link` with a table-driven import resolver that keeps the linker core stable while making the supported import set easier to grow.

This phase also expands the built-in C runtime import set so ordinary small C programs can use a more natural output path through `msvcrt.dll`.

The immediate objective is:

- separate import resolution from `.idata` layout generation
- keep the import source internal and code-defined for now
- expand the built-in import set to cover:
  - `msvcrt.dll!puts`
  - `msvcrt.dll!putchar`
  - `msvcrt.dll!printf`
- keep `kernel32.dll!ExitProcess` as the required process-exit import

## Why This Phase Next

The current linker already supports a small curated import subset and can successfully produce PE images with working `.idata` content.

The remaining weakness is not that import support is impossible. The weakness is that the mechanism is still too tightly coupled:

- unresolved-symbol matching
- DLL ownership
- import-name aliasing
- `.idata` emission

are still too close together inside the linker implementation.

If import growth continues through more hard-coded conditionals, later support for user-defined import sources will become harder than it needs to be.

## Scope

### In Scope

- a dedicated built-in import catalog abstraction
- a dedicated import resolver abstraction
- a clear handoff from resolved import items to existing `.idata` layout generation
- built-in support for:
  - `ExitProcess`
  - `puts`
  - `putchar`
  - `printf`
- regression and trace updates for the expanded import set
- documentation updates for the new import boundary

### Out Of Scope

- user-defined DLL import syntax in source code
- external JSON or text catalog files
- arbitrary DLL loading rules
- ordinal imports
- delay import
- broad varargs semantic work
- a general ABI study of all possible `printf` usage

## Current Limitation

The current linker can already import a few symbols, but the resolution path is still too ad hoc.

Today the linker effectively owns three responsibilities at once:

- decide whether an unresolved symbol should be treated as an import
- decide which DLL it belongs to
- decide which import name should be written into `.idata`

That works for a tiny set, but it is the wrong shape for the next stage of growth.

## Required Outcome

After this phase:

- import lookup must be driven by a table rather than symbol-specific conditionals
- the linker must resolve imports through a stable resolver interface
- `.idata` generation must consume normalized import items without caring where they came from
- adding one more built-in import entry should require only catalog expansion and tests, not control-flow surgery

## Proposed Model

### 1. Built-In Import Catalog

The linker should own one explicit catalog of built-in import entries.

Each entry should describe:

- linker-visible unresolved symbol name
- target DLL name
- imported function name written into `.idata`

Conceptually:

- `ExitProcess -> kernel32.dll / ExitProcess`
- `fn_puts -> msvcrt.dll / puts`
- `fn_putchar -> msvcrt.dll / putchar`
- `fn_printf -> msvcrt.dll / printf`

This catalog remains code-defined in this phase, but it should be treated as data, not control flow.

### 2. Import Resolver

Add a dedicated resolver layer whose job is only:

- inspect unresolved extern symbols
- match them against the built-in catalog
- return normalized import items

The resolver should not:

- build `.idata`
- patch thunks
- decide PE section layout

Its output should be stable enough that future catalog sources can plug in behind the same interface.

### 3. Import Layout Builder

The `.idata` generator should continue to do what it already does well:

- group symbols by DLL
- emit lookup tables
- emit IAT entries
- emit hint/name records
- emit DLL strings
- emit import thunks in `.text`

But it should consume already-resolved import items rather than making catalog decisions itself.

## Design Rule

The import mechanism should now be split by responsibility:

- catalog answers: what imports are known
- resolver answers: which unresolved symbol maps to which import entry
- layout builder answers: how the PE import structures are emitted

Future user-defined import support should only need to replace or augment the catalog source, not redesign the linker pipeline.

## Built-In Import Set For This Phase

This phase keeps the Windows process-exit dependency and expands only the C runtime side.

### Required built-in imports

- `ExitProcess` from `kernel32.dll`
- `fn_puts` from `msvcrt.dll` as `puts`
- `fn_putchar` from `msvcrt.dll` as `putchar`
- `fn_printf` from `msvcrt.dll` as `printf`

### Reason for the chosen set

This set is intentionally biased toward small C programs:

- `puts` gives simple line output
- `putchar` gives minimal character output
- `printf` gives formatted output for the most common simple cases

This improves the usefulness of the compiler as a hand-written C toolchain without forcing early support for a large Win32 API surface.

## `printf` Support Boundary

This phase does not claim to implement general C varargs semantics.

What it does claim is narrower:

- the linker can resolve `fn_printf` correctly
- the generated executable can call imported `printf`
- the current Windows x64 calling path is expected to support the simple repository-level usage exercised by regression samples

The supported examples should stay conservative, such as:

- `printf("value=%d\n", 42);`
- `printf("%c\n", 'A');`

This phase should not document or imply full `printf` compatibility beyond the cases actively tested.

## Trace Behavior

`--link-trace` should remain compact, but it should make the new mechanism visible through behavior:

- imported symbols still appear in the resolved-symbol trace with DLL attribution
- the imports block should show grouped DLL ownership clearly
- mixed `msvcrt.dll` imports should appear as one grouped import block

No new trace flag is needed.

## Validation Strategy

### Positive Cases

At minimum, add regression samples for:

- `puts` only
- `putchar` only
- simple `printf`
- mixed `puts + putchar + printf`

Each case should validate:

- import resolution succeeds
- the trace shows the expected `msvcrt.dll` grouping
- the executable returns the expected code

### Negative Case

Keep at least one unresolved-import regression that proves:

- unresolved externs not present in the catalog still fail clearly

### Existing Behavior Preservation

Current import behavior must remain intact for:

- `ExitProcess`
- `GetCurrentProcessId` if it remains supported in the current catalog

If that symbol is intentionally retained, it should migrate into the same catalog rather than living in a separate special path.

## Documentation Updates Required

Once implemented, long-lived docs must be updated to reflect:

- the built-in import catalog model
- the currently supported runtime-facing import set
- the still-missing future user-defined import source

That includes:

- `README.md`
- `docs/pe-coff-linker-support.md`
- `docs/project-status-overview.md`

## Success Criteria

This phase is complete when all of the following are true:

1. import resolution is table-driven rather than symbol-special-cased
2. `.idata` generation consumes normalized import items from the resolver path
3. `puts`, `putchar`, and `printf` link correctly through `msvcrt.dll`
4. existing import behavior for required Windows runtime support still works
5. unresolved imports outside the catalog still fail clearly
6. the code structure now has a clean seam for a future user-defined import source

## Short Version

The next step is not “support arbitrary DLL imports.” The next step is to turn the current hard-coded import path into a table-driven resolver backed by a built-in catalog, then grow that catalog just enough to make small C runtime-oriented programs feel natural.
