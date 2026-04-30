# File-Backed Import Catalog Design

Date: 2026-04-30

## Goal

Extend the current table-driven import resolver so `minic-link` can load additional import entries from a repository configuration file while keeping the existing built-in catalog intact.

This phase should make import growth more practical without yet exposing user-defined DLL syntax in source code.

The immediate objective is:

- keep the existing built-in import catalog
- add one repository-local import catalog file source
- merge built-in and file-backed entries into one resolver view
- use file-backed entries only as additive entries in this phase

## Why This Phase Next

The linker now has the right internal shape for this step:

- import catalog
- import resolver
- `.idata` layout builder

That means the next best improvement is no longer inside `.idata` generation. The next best improvement is to make the import source pluggable without destabilizing the core linker pipeline.

This is the smallest next step that:

- preserves current behavior
- improves maintainability
- directly prepares for future user-defined import sources

## Scope

### In Scope

- a repository-local import catalog file
- file parsing inside the linker driver path
- additive merging of built-in and file-backed entries
- trace/diagnostic visibility for import source
- regression coverage for file-backed imports

### Out Of Scope

- source-language syntax for naming DLLs
- user-supplied CLI flags for arbitrary import files
- external JSON schema tooling
- override semantics where file-backed entries replace built-in entries
- multiple catalog files
- delay import, ordinal import, or arbitrary DLL policy

## Current Limitation

The current resolver is now table-driven, but the catalog still comes only from code.

That means:

- adding one more import entry still requires recompiling the linker
- repository-local policy cannot evolve independently of code changes
- future user-defined import sources have no exercised path yet

## Required Outcome

After this phase:

- the resolver must see one merged catalog made of:
  - built-in entries
  - file-backed entries
- object-defined symbols must still win before import lookup
- file-backed entries must only add new mappings in this phase
- attempts to duplicate built-in symbol mappings in the file-backed catalog must fail clearly

## Proposed Model

### 1. Keep The Built-In Catalog

The current built-in catalog remains part of the linker.

It still carries the baseline imports needed by the project, such as:

- `ExitProcess`
- the currently supported `msvcrt.dll` entries

This guarantees the repository still works even if the external catalog file is missing or empty, depending on the chosen policy.

### 2. Add One File-Backed Catalog Source

The repository should define one import catalog file stored in version control.

A simple text-based format is preferred for this phase.

Each entry should encode:

- unresolved symbol name
- DLL name
- import name

For example, conceptually:

```text
fn_symbol|msvcrt.dll|puts
```

The exact delimiter is up to implementation, but the format should be:

- easy to parse in C++17 without extra dependencies
- stable enough to keep under source control
- easy to diff in reviews

### 3. Merge Rule

The merged resolver view must follow this priority:

1. real object-defined symbols
2. built-in catalog entries
3. file-backed catalog entries

In this phase, file-backed entries are additive only.

That means:

- if a file-backed entry introduces a new unresolved symbol mapping, it is accepted
- if it duplicates a built-in symbol name, linking should fail with a clear diagnostic
- if it duplicates another file-backed symbol name, linking should fail with a clear diagnostic

This deliberately avoids override semantics for now.

## Catalog File Behavior

### File Location

The repository should use one stable catalog path documented in long-lived docs.

The path should be simple and discoverable, for example under `config/` or `docs/` only if the repo already treats that folder as configuration-bearing.

The exact path should be chosen for clarity, not convenience.

### Parsing Rules

The parser should:

- ignore blank lines
- ignore comment lines
- reject malformed rows
- reject rows with missing symbol name, DLL name, or import name
- reject duplicate symbol mappings within the file

Diagnostics should identify:

- the catalog file path
- the line number when possible
- the conflicting symbol

## Trace Behavior

`--link-trace` should continue to show grouped imports by DLL, but this phase should also expose import source in a compact way.

It is enough if the resolved-symbol or imports trace shows one of:

- source=builtin
- source=file

The important part is that a reader can tell whether a resolved import came from the built-in catalog or the repository file.

No new trace flag is needed.

## Validation Strategy

### Positive Cases

At minimum, add one file-backed import case that proves:

- the linker reads the repository catalog
- a symbol absent from the built-in catalog resolves successfully through the file-backed catalog
- the trace indicates the file-backed source

The chosen symbol should stay small and compatible with the current calling model.

### Negative Cases

Add regression coverage for:

- malformed catalog row
- duplicate symbol inside the file-backed catalog
- file-backed symbol that duplicates a built-in symbol

Each should fail clearly and mention the configuration source.

### Existing Behavior Preservation

All current built-in import regressions must remain green.

That includes:

- `puts`
- `putchar`
- `printf`
- `ExitProcess`
- unresolved import failure outside both catalogs

## Documentation Updates Required

Once implemented, long-lived docs must be updated to explain:

- the built-in + file-backed catalog model
- the catalog file path and row format
- additive-only semantics in this phase
- the fact that source-level user-defined imports still do not exist

That includes:

- `README.md`
- `docs/pe-coff-linker-support.md`
- `docs/project-status-overview.md`

## Success Criteria

This phase is complete when all of the following are true:

1. the linker loads and parses one repository-local import catalog file
2. the resolver merges built-in and file-backed entries into one import lookup view
3. file-backed entries can add new imports without code changes
4. duplicate symbol mappings across built-in and file-backed catalogs fail clearly
5. trace or diagnostics can identify whether an import came from the built-in or file-backed catalog
6. current built-in import behavior remains unchanged

## Short Version

The next step is not “let users type DLL names in source code.” The next step is to keep the current built-in import catalog, add one repository-backed catalog file as an additive source, and make the resolver treat both as one merged import view with clear conflict rules.
