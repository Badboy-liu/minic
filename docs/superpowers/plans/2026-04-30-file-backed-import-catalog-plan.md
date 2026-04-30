# File-Backed Import Catalog Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add one repository-local additive import catalog file source to `minic-link`, merge it with the built-in catalog, and preserve the current built-in import behavior.

**Architecture:** Keep the current table-driven import resolver and `.idata` builder intact, then add a small parser and merge step for one repository-backed catalog file. Resolution priority stays: real object-defined symbols first, then built-in catalog entries, then file-backed entries, with duplicate symbol mappings rejected instead of overridden.

**Tech Stack:** C++17, existing `PeLinker` import resolver, CMake/CTest, PowerShell regression scripts, repository-local text configuration.

---

## File Structure Map

### Existing files to modify

- `E:/project/cpp/minic/src/linker/PeLinker.cpp`
  - current built-in catalog, resolver path, import grouping, trace output
- `E:/project/cpp/minic/CMakeLists.txt`
  - register new file-backed import regressions and malformed/duplicate catalog failure cases
- `E:/project/cpp/minic/tests/run_regression_case.ps1`
  - only if the new catalog tests need extra file checks or source-specific diagnostics
- `E:/project/cpp/minic/README.md`
- `E:/project/cpp/minic/docs/pe-coff-linker-support.md`
- `E:/project/cpp/minic/docs/project-status-overview.md`

### New files to create

- `E:/project/cpp/minic/config/import_catalog.txt`
  - repository-local additive import catalog file used by the linker
- `E:/project/cpp/minic/input/tmp_import_file_backed.c`
  - positive sample that resolves through the file-backed catalog only
- `E:/project/cpp/minic/input/tmp_import_file_duplicate.c`
  - negative sample or companion config scenario for duplicate conflict checks if needed
- `E:/project/cpp/minic/tests/data/import_catalog_bad_row.txt`
  - malformed catalog fixture
- `E:/project/cpp/minic/tests/data/import_catalog_duplicate_symbol.txt`
  - duplicate-symbol-in-file fixture
- `E:/project/cpp/minic/tests/data/import_catalog_duplicate_builtin.txt`
  - file-backed symbol that collides with a built-in entry

## Task 1: Establish Failing Coverage For File-Backed Imports

**Files:**
- Create: `E:/project/cpp/minic/config/import_catalog.txt`
- Create: `E:/project/cpp/minic/input/tmp_import_file_backed.c`
- Create: `E:/project/cpp/minic/tests/data/import_catalog_bad_row.txt`
- Create: `E:/project/cpp/minic/tests/data/import_catalog_duplicate_symbol.txt`
- Create: `E:/project/cpp/minic/tests/data/import_catalog_duplicate_builtin.txt`
- Modify: `E:/project/cpp/minic/CMakeLists.txt`

- [ ] **Step 1: Choose one file-backed import symbol that is absent from the built-in catalog**

Use a small, current-calling-convention-friendly C runtime symbol. A good default is:

```text
fn_strlen|msvcrt.dll|strlen
```

This stays simple:

- integer return type
- one pointer parameter
- no new varargs concerns

- [ ] **Step 2: Add the repository import catalog file**

Create:

```text
E:/project/cpp/minic/config/import_catalog.txt
```

with the single starter line:

```text
fn_strlen|msvcrt.dll|strlen
```

No comments are needed yet in the initial red step.

- [ ] **Step 3: Add a positive source sample that depends on the file-backed symbol**

Use this exact starter sample:

```c
extern int strlen(char *text);

int main() {
    return strlen("minic");
}
```

Save it as:

```text
E:/project/cpp/minic/input/tmp_import_file_backed.c
```

Expected runtime exit code after implementation: `5`

- [ ] **Step 4: Add negative catalog fixtures**

Create:

```text
tests/data/import_catalog_bad_row.txt
```

with malformed content such as:

```text
fn_broken|msvcrt.dll
```

Create:

```text
tests/data/import_catalog_duplicate_symbol.txt
```

with duplicate symbol content such as:

```text
fn_strlen|msvcrt.dll|strlen
fn_strlen|msvcrt.dll|strlen
```

Create:

```text
tests/data/import_catalog_duplicate_builtin.txt
```

with a built-in collision such as:

```text
fn_puts|msvcrt.dll|puts
```

- [ ] **Step 5: Register the new regression cases**

Add at least these tests:

- `minic_import_file_backed`
- `minic_import_catalog_bad_row`
- `minic_import_catalog_duplicate_symbol`
- `minic_import_catalog_duplicate_builtin`

The positive case should:

- compile with `--link-trace`
- expect exit code `5`
- require import trace output and file-source attribution once implemented

The negative cases should:

- expect compiler/linker failure
- match diagnostics that mention the catalog file or the conflicting symbol

- [ ] **Step 6: Run the targeted tests to confirm the red state**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --preset default
cmake --build --preset debug
ctest --test-dir .\build -C Debug -R "minic_import_file_backed|minic_import_catalog_" --output-on-failure
```

Expected result:

- positive file-backed import case fails with unresolved external symbol
- negative catalog cases likely fail because the loader/parser path does not exist yet

- [ ] **Step 7: Commit the red scaffolding**

```powershell
git add config/import_catalog.txt input/tmp_import_file_backed.c tests/data/import_catalog_bad_row.txt tests/data/import_catalog_duplicate_symbol.txt tests/data/import_catalog_duplicate_builtin.txt CMakeLists.txt
git commit -m "test: add failing file-backed import catalog coverage"
```

## Task 2: Add Catalog File Parsing

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`

- [ ] **Step 1: Define one stable repository-local catalog path in code**

Introduce a helper or constant for the file-backed catalog location, for example:

```cpp
const char *ImportCatalogPath = "config/import_catalog.txt";
```

Keep the path repository-relative in this phase.

- [ ] **Step 2: Add a small parser for the text catalog format**

Implement a helper shaped like:

```cpp
std::vector<ImportSpec> readFileBackedImportCatalog(const fs::path &repoRoot);
```

or equivalent for the current linker context.

Parsing rules:

- ignore blank lines
- ignore lines starting with `#`
- split each non-empty row on `|`
- require exactly three non-empty fields:
  - symbol name
  - DLL name
  - import name

- [ ] **Step 3: Add strong diagnostics for malformed rows**

On malformed input, fail with diagnostics that include:

- the catalog file path
- the line number when available
- the bad row or a clear reason such as:
  - malformed row
  - missing import name

- [ ] **Step 4: Reject duplicate symbols inside the file-backed catalog**

The parser should fail when the same unresolved symbol appears twice in the file.

Use a diagnostic shaped like:

```text
duplicate file-backed import symbol: fn_strlen
```

and include the file path.

- [ ] **Step 5: Rebuild and run only the malformed/duplicate catalog tests**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build --preset debug
ctest --test-dir .\build -C Debug -R "minic_import_catalog_bad_row|minic_import_catalog_duplicate_symbol" --output-on-failure
```

Expected result:

- malformed and duplicate-file tests now fail in the expected controlled way
- positive file-backed import case may still fail unresolved

- [ ] **Step 6: Commit the parser**

```powershell
git add src/linker/PeLinker.cpp
git commit -m "feat: parse file-backed import catalog"
```

## Task 3: Merge Built-In And File-Backed Catalogs

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`

- [ ] **Step 1: Add a merge helper that preserves additive-only semantics**

Implement a helper shaped like:

```cpp
std::vector<ImportSpec> buildMergedImportCatalog(const fs::path &repoRoot);
```

Rules:

- start with built-in catalog
- append file-backed entries
- reject any file-backed symbol that matches an existing built-in symbol

- [ ] **Step 2: Keep resolution priority unchanged**

Make sure the import resolution path still behaves as:

1. real object-defined symbol wins
2. built-in catalog entry
3. file-backed catalog entry

The important part is that file-backed entries do not override built-in entries.

- [ ] **Step 3: Plumb the merged catalog through the resolver**

Update the current resolver path so it does not call only `builtinImportCatalog()`.

Instead, it should resolve imports against the merged catalog returned by the new helper.

- [ ] **Step 4: Rebuild and run the positive file-backed import case**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build --preset debug
ctest --test-dir .\build -C Debug -R "minic_import_file_backed" --output-on-failure
```

Expected result:

- the file-backed `strlen` case now links and returns `5`

- [ ] **Step 5: Commit merged-catalog resolution**

```powershell
git add src/linker/PeLinker.cpp
git commit -m "feat: merge built-in and file-backed import catalogs"
```

## Task 4: Add Conflict Diagnostics Against Built-In Entries

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`
- Modify: `E:/project/cpp/minic/CMakeLists.txt`

- [ ] **Step 1: Reject file-backed symbols that duplicate built-in symbols**

If the file-backed catalog contains a symbol already present in the built-in catalog, fail clearly.

Use a diagnostic shaped like:

```text
file-backed import symbol duplicates built-in catalog entry: fn_puts
```

The error should also mention the catalog file path.

- [ ] **Step 2: Wire the duplicate-built-in fixture into a regression**

Make `minic_import_catalog_duplicate_builtin` use the duplicate-built-in fixture and assert the error marker contains:

- the conflicting symbol
- the configuration source

- [ ] **Step 3: Re-run the three negative catalog tests together**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
ctest --test-dir .\build -C Debug -R "minic_import_catalog_(bad_row|duplicate_symbol|duplicate_builtin)" --output-on-failure
```

Expected result:

- all three fail in the expected controlled way and the CTest expectations pass

- [ ] **Step 4: Commit conflict handling**

```powershell
git add src/linker/PeLinker.cpp CMakeLists.txt
git commit -m "fix: reject conflicting file-backed import symbols"
```

## Task 5: Expose Import Source In Trace

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`
- Modify: `E:/project/cpp/minic/CMakeLists.txt`

- [ ] **Step 1: Extend normalized import data with source attribution**

Add a small field such as:

```cpp
std::string sourceName; // "builtin" or "file"
```

to the normalized import item type or equivalent structure.

- [ ] **Step 2: Surface source attribution in trace**

Update either the imports block or resolved-symbol block so imported entries show:

- `source=builtin`
- `source=file`

Keep the output compact.

- [ ] **Step 3: Tighten the positive file-backed import regression**

Require output markers such as:

- `dll msvcrt.dll: fn_strlen`
- `source=file`

if the symbol chosen is `fn_strlen`.

- [ ] **Step 4: Re-run the positive import tests**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
ctest --preset imports --output-on-failure
ctest --test-dir .\build -C Debug -R "minic_import_file_backed" --output-on-failure
```

Expected result:

- all built-in import tests still pass
- file-backed import case now proves source attribution in trace

- [ ] **Step 5: Commit trace attribution**

```powershell
git add src/linker/PeLinker.cpp CMakeLists.txt
git commit -m "feat: trace file-backed import source"
```

## Task 6: Update Long-Lived Documentation

**Files:**
- Modify: `E:/project/cpp/minic/README.md`
- Modify: `E:/project/cpp/minic/docs/pe-coff-linker-support.md`
- Modify: `E:/project/cpp/minic/docs/project-status-overview.md`

- [ ] **Step 1: Update the README import description**

Document that:

- the linker now resolves imports from both:
  - built-in catalog
  - repository-local file-backed catalog
- file-backed entries are additive only
- source-language user-defined imports still do not exist

- [ ] **Step 2: Document the catalog file path and row format**

In the long-lived docs, describe:

- the repository path
- row format: `symbol|dll|import`
- blank-line and comment behavior
- duplicate handling

- [ ] **Step 3: Update project status**

Revise the import notes so they describe the mechanism as:

- table-driven
- dual-source
- still intentionally narrow

- [ ] **Step 4: Commit docs**

```powershell
git add README.md docs/pe-coff-linker-support.md docs/project-status-overview.md
git commit -m "docs: describe file-backed import catalog"
```

## Task 7: Full Verification Pass

**Files:**
- Modify only if verification exposes issues

- [ ] **Step 1: Run the full build and focused suites**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --preset default
cmake --build --preset debug
ctest --preset imports --output-on-failure
ctest --preset phase-current --output-on-failure
ctest --test-dir .\build -C Debug -R "minic_import_file_backed|minic_import_catalog_" --output-on-failure
```

Expected result:

- all commands succeed
- built-in import cases stay green
- file-backed positive and negative cases behave as expected
- relocation and rebasing suites remain green as part of `phase-current`

- [ ] **Step 2: Run one manual file-backed trace spot-check**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
.\build\Debug\minic.exe .\input\tmp_import_file_backed.c --link-trace
```

Expected trace includes:

- `[link] imports`
- the file-backed symbol grouped under its DLL
- `source=file`

- [ ] **Step 3: Commit any verification-driven fixes**

```powershell
git add <only files actually changed during verification>
git commit -m "fix: finalize file-backed import catalog support"
```

## Self-Review Checklist

- Spec coverage:
  - file-backed catalog source is covered in Tasks 1 and 2
  - additive merge semantics are covered in Tasks 3 and 4
  - source attribution is covered in Task 5
  - doc updates are covered in Task 6
- Placeholder scan:
  - no `TODO` / `TBD`
  - each task contains exact files, commands, and expected outcomes
- Type consistency:
  - `ImportSpec` remains the unified catalog entry type across the plan
  - `source=builtin` / `source=file` wording is consistent across tests and docs

