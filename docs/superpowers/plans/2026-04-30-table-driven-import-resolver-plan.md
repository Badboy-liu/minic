# Table-Driven Import Resolver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor `minic-link` import handling into a table-driven resolver backed by a built-in catalog, while adding `puts`, `putchar`, and conservative `printf` support through `msvcrt.dll`.

**Architecture:** Keep PE `.idata` generation in `PeLinker.cpp`, but split import work into three responsibilities: a built-in import catalog, a resolver that turns unresolved symbols into normalized import items, and an `.idata` layout builder that only consumes those resolved items. The implementation should preserve current behavior for `ExitProcess` and existing imports while making future import-source replacement possible without redesigning the linker core.

**Tech Stack:** C++17, existing `PeLinker` PE writer, CMake/CTest, PowerShell regression scripts, Windows x64 PE32+ format, `msvcrt.dll` imports.

---

## File Structure Map

### Existing files to modify

- `E:/project/cpp/minic/src/linker/PeLinker.cpp`
  - Current import catalog, unresolved-symbol scan, `.idata` builder, trace output
- `E:/project/cpp/minic/CMakeLists.txt`
  - New regression cases and labels for expanded import coverage
- `E:/project/cpp/minic/CMakePresets.json`
  - Optional preset update if import-focused cases need a narrower execution path
- `E:/project/cpp/minic/tests/run_regression_case.ps1`
  - Reuse existing harness; likely no structural change unless output checks need extra support
- `E:/project/cpp/minic/README.md`
- `E:/project/cpp/minic/docs/pe-coff-linker-support.md`
- `E:/project/cpp/minic/docs/project-status-overview.md`

### New files to create

- `E:/project/cpp/minic/input/tmp_import_putchar.c`
  - Minimal `putchar`-only positive case
- `E:/project/cpp/minic/input/tmp_import_printf_simple.c`
  - Conservative `printf` positive case such as `printf("value=%d\n", 42);`
- `E:/project/cpp/minic/input/tmp_import_msvcrt_triple.c`
  - Mixed `puts + putchar + printf` import case

## Task 1: Add Failing Coverage For The New Import Set

**Files:**
- Create: `E:/project/cpp/minic/input/tmp_import_putchar.c`
- Create: `E:/project/cpp/minic/input/tmp_import_printf_simple.c`
- Create: `E:/project/cpp/minic/input/tmp_import_msvcrt_triple.c`
- Modify: `E:/project/cpp/minic/CMakeLists.txt`

- [ ] **Step 1: Add focused import samples**

Use these exact starter shapes:

```c
/* tmp_import_putchar.c */
extern int putchar(int ch);

int main() {
    putchar(65);
    return 1;
}
```

```c
/* tmp_import_printf_simple.c */
extern int printf(char *fmt, int value);

int main() {
    printf("value=%d\n", 42);
    return 1;
}
```

```c
/* tmp_import_msvcrt_triple.c */
extern int puts(char *text);
extern int putchar(int ch);
extern int printf(char *fmt, int value);

int main() {
    puts("triple");
    putchar(33);
    printf(" n=%d\n", 7);
    return 1;
}
```

- [ ] **Step 2: Register import regression cases that are expected to fail before implementation**

Add tests shaped like:

- `minic_import_putchar`
- `minic_import_printf_simple`
- `minic_import_msvcrt_triple`

Each should:

- compile with `--link-trace`
- expect runtime exit code `1`
- require trace markers:
  - `[link] input objects`
  - `[link] imports`
  - `[link] resolved symbols`
  - `[link] relocations`
- check output markers that name the unresolved/imported symbols once support lands

Before implementation, these should fail with unresolved external symbol diagnostics for:

- `fn_putchar`
- `fn_printf`

- [ ] **Step 3: Run the targeted tests to confirm the red state**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --preset default
cmake --build --preset debug
ctest --test-dir .\build -C Debug -R "minic_import_putchar|minic_import_printf_simple|minic_import_msvcrt_triple" --output-on-failure
```

Expected result:

- at least the new `putchar` / `printf` cases fail with unresolved external symbol errors

- [ ] **Step 4: Commit the failing import-coverage scaffolding**

```powershell
git add input/tmp_import_putchar.c input/tmp_import_printf_simple.c input/tmp_import_msvcrt_triple.c CMakeLists.txt
git commit -m "test: add failing runtime import coverage"
```

## Task 2: Separate Catalog Data From Import Resolution

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`

- [ ] **Step 1: Replace the ad hoc import list naming with explicit catalog terminology**

Introduce focused internal types near the existing import structs, for example:

```cpp
struct ImportCatalogEntry {
    std::string symbolName;
    std::string importName;
    std::string dllName;
};

struct ResolvedImportEntry {
    std::string symbolName;
    std::string importName;
    std::string dllName;
};
```

Do not make `.idata` layout structs do double duty as catalog or resolver types.

- [ ] **Step 2: Convert the built-in import table into explicit catalog data**

Replace the current `ImportSpec` usage with a function that returns the built-in catalog data.

The built-in entries for this phase should be:

```cpp
ExitProcess -> kernel32.dll / ExitProcess
fn_GetCurrentProcessId -> kernel32.dll / GetCurrentProcessId
fn_puts -> msvcrt.dll / puts
fn_putchar -> msvcrt.dll / putchar
fn_printf -> msvcrt.dll / printf
```

- [ ] **Step 3: Add a dedicated resolver helper**

Implement a resolver helper with a narrow contract, for example:

```cpp
std::optional<ResolvedImportEntry> resolveImportSymbol(
    const std::string &symbolName,
    const std::vector<ImportCatalogEntry> &catalog);
```

If you prefer not to add `<optional>`, an equivalent pointer or boolean/result-object style is fine, but keep the responsibilities the same.

- [ ] **Step 4: Make all import matching flow through the resolver**

Update unresolved-symbol scanning so:

- real object-defined symbols still win first
- only unresolved externs are passed to the resolver
- the resolver decides whether a symbol becomes an import

Expected behavior stays unchanged for already-supported imports.

- [ ] **Step 5: Rebuild and re-run the existing import cases**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build --preset debug
ctest --preset imports --output-on-failure
```

Expected result:

- all pre-existing import tests still pass
- new `putchar` / `printf` tests may still fail until the `.idata` consumer path is fully updated

- [ ] **Step 6: Commit the catalog/resolver split**

```powershell
git add src/linker/PeLinker.cpp
git commit -m "refactor: add table-driven import catalog and resolver"
```

## Task 3: Normalize The Handoff Into `.idata` Generation

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`

- [ ] **Step 1: Make requested-import collection return normalized import items**

Refactor the current import-collection path so it returns resolved import entries instead of directly depending on raw symbol names and hard-coded catalog lookups.

The return value should carry:

- unresolved symbol name
- DLL name
- import name

and nothing PE-layout-specific beyond that.

- [ ] **Step 2: Keep `.idata` builder focused only on layout**

Update the existing import layout builder so it consumes normalized import items and builds:

- grouped DLL descriptors
- ILT entries
- IAT entries
- hint/name records
- DLL name strings
- import thunks

without doing any symbol-to-DLL decision-making itself.

- [ ] **Step 3: Keep trace output behavior stable but sourced from normalized imports**

The trace should still show:

- grouped DLL imports
- resolved symbols with DLL attribution

but it should now flow from the new normalized import data rather than raw symbol-special cases.

- [ ] **Step 4: Re-run the targeted import tests**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
ctest --test-dir .\build -C Debug -R "minic_import_(kernel32_pid|msvcrt_puts|mixed_showcase|putchar|printf_simple|msvcrt_triple)" --output-on-failure
```

Expected result:

- `puts`, `putchar`, and simple `printf` cases now link
- mixed runtime import case now links
- existing import cases still pass

- [ ] **Step 5: Commit the normalized `.idata` handoff**

```powershell
git add src/linker/PeLinker.cpp
git commit -m "refactor: normalize import resolution into idata builder"
```

## Task 4: Tighten `printf` Coverage To Conservative Supported Use

**Files:**
- Modify: `E:/project/cpp/minic/input/tmp_import_printf_simple.c`
- Modify: `E:/project/cpp/minic/input/tmp_import_msvcrt_triple.c`
- Modify: `E:/project/cpp/minic/CMakeLists.txt`

- [ ] **Step 1: Keep the `printf` samples conservative**

Ensure the only repository-level `printf` samples in this phase are simple fixed-arity cases such as:

```c
printf("value=%d\n", 42);
printf("%c\n", 'A');
```

Do not add broader varargs stress tests in this phase.

- [ ] **Step 2: Add exact trace markers for the new imports**

For the new tests, require markers such as:

- `extern fn_putchar`
- `extern fn_printf`
- `dll msvcrt.dll: fn_putchar, fn_printf` or equivalent grouped output
- resolved-symbol entries carrying `dll=msvcrt.dll`

The goal is to prove the resolver and grouping paths are being exercised, not just that the executable links.

- [ ] **Step 3: Re-run only the import-focused suite**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
ctest --preset imports --output-on-failure
```

Expected result:

- all import tests pass

- [ ] **Step 4: Commit the import regressions**

```powershell
git add input/tmp_import_printf_simple.c input/tmp_import_msvcrt_triple.c CMakeLists.txt
git commit -m "test: cover puts putchar and printf imports"
```

## Task 5: Preserve Negative Behavior And Existing Windows Support

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`
- Modify: `E:/project/cpp/minic/CMakeLists.txt`

- [ ] **Step 1: Confirm unresolved externs outside the catalog still fail clearly**

Keep or tighten a regression around unresolved imports so the diagnostic still points at the missing symbol, for example:

```text
unresolved external symbol: fn_MissingImport
```

- [ ] **Step 2: Keep `ExitProcess` and any retained `GetCurrentProcessId` support inside the same catalog path**

Do not leave a separate special-case branch for a retained import if it can flow through the same catalog/resolver path.

This task is complete only when existing Windows runtime support is preserved through the same general mechanism.

- [ ] **Step 3: Run the linker-failure and import suites together**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
ctest --preset imports --output-on-failure
ctest --preset linker-failures --output-on-failure
```

Expected result:

- positive imports pass
- unresolved-import failures still fail in the expected controlled way

- [ ] **Step 4: Commit the preservation cleanup**

```powershell
git add src/linker/PeLinker.cpp CMakeLists.txt
git commit -m "fix: preserve negative import behavior under resolver"
```

## Task 6: Update Long-Lived Documentation

**Files:**
- Modify: `E:/project/cpp/minic/README.md`
- Modify: `E:/project/cpp/minic/docs/pe-coff-linker-support.md`
- Modify: `E:/project/cpp/minic/docs/project-status-overview.md`

- [ ] **Step 1: Update the README import summary**

Document that the linker now uses a built-in table-driven import catalog and currently supports:

- `ExitProcess`
- `GetCurrentProcessId` if still retained
- `puts`
- `putchar`
- `printf`

Keep the wording explicit that user-defined import sources are not implemented yet.

- [ ] **Step 2: Update the PE linker support document**

Add or revise sections that explain:

- built-in import catalog
- import resolver vs `.idata` generation responsibilities
- the conservative `printf` support boundary

- [ ] **Step 3: Update project status**

Revise the import-handling notes so they describe the new mechanism as table-driven and slightly broader on the `msvcrt` side.

- [ ] **Step 4: Commit docs**

```powershell
git add README.md docs/pe-coff-linker-support.md docs/project-status-overview.md
git commit -m "docs: describe table-driven import resolver"
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
```

Expected result:

- all commands succeed
- expanded import cases pass
- previously green linker, relocation, and rebasing suites stay green

- [ ] **Step 2: Run one manual import trace spot-check**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
.\build\Debug\minic.exe .\input\tmp_import_msvcrt_triple.c --link-trace
```

Expected trace includes:

- `[link] imports`
- one grouped `msvcrt.dll` block containing `fn_puts`, `fn_putchar`, and `fn_printf`
- resolved-symbol entries showing `dll=msvcrt.dll`

- [ ] **Step 3: Commit any verification-driven fixes**

```powershell
git add <only files actually changed during verification>
git commit -m "fix: finalize table-driven import resolver"
```

## Self-Review Checklist

- Spec coverage:
  - catalog/resolver split is covered in Tasks 2 and 3
  - runtime import expansion is covered in Tasks 1 and 4
  - unresolved-import preservation is covered in Task 5
  - doc updates are covered in Task 6
- Placeholder scan:
  - no `TODO` / `TBD`
  - each task contains exact files, commands, and expected outcomes
- Type consistency:
  - `ImportCatalogEntry` and `ResolvedImportEntry` names are used consistently across the plan
  - the `printf` boundary stays conservative in both tests and docs

