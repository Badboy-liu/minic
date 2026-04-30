# PE Base Relocation Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add real PE32+ x64 base relocation support to `minic-link` so supported Windows executables remain correct after rebasing.

**Architecture:** Extend `PeLinker` at the point where it already writes final absolute addresses for `ADDR64` relocations. Record every final image RVA that receives an absolute VA, synthesize a `.reloc` section grouped into `IMAGE_BASE_RELOCATION` blocks, expose the result in `--link-trace`, and add deterministic regression checks that validate both PE structure and rebasing behavior without relying on the OS to pick a random load base.

**Tech Stack:** C++17, existing `PeLinker` PE writer, CMake/CTest, PowerShell regression scripts, Windows x64 PE32+ format.

---

## File Structure Map

### Existing files to modify

- `E:/project/cpp/minic/src/linker/PeLinker.cpp`
  - Current PE writer, section merge logic, import synthesis, relocation patching, trace output, final header/data directory emission
- `E:/project/cpp/minic/src/linker/PeLinker.h`
  - Public entry points only; likely unchanged unless a helper type needs to be exposed
- `E:/project/cpp/minic/CMakeLists.txt`
  - Register new relocation/rebasing regression cases and any test helper executable
- `E:/project/cpp/minic/CMakePresets.json`
  - Optional new test preset if the rebasing checks deserve a dedicated group
- `E:/project/cpp/minic/tests/run_regression_case.ps1`
  - Extend the test harness so one regression case can validate trace output, runtime exit code, and PE relocation metadata in one pass
- `E:/project/cpp/minic/README.md`
- `E:/project/cpp/minic/docs/pe-coff-linker-support.md`
- `E:/project/cpp/minic/docs/minic-relocation-matrix.md`
- `E:/project/cpp/minic/docs/project-status-overview.md`

### New files to create

- `E:/project/cpp/minic/tests/pe_reloc_probe.cpp`
  - Small deterministic binary checker that parses PE headers, inspects `.reloc`, applies a synthetic image-base delta, and validates selected rebased `DIR64` slots
- `E:/project/cpp/minic/input/tmp_reloc_probe_global_ptr.c`
  - Focused positive sample for `.data -> .bss` rebasing
- `E:/project/cpp/minic/input/tmp_reloc_probe_function_ptr.c`
  - Focused positive sample for `.data -> .text` rebasing
- `E:/project/cpp/minic/input/tmp_reloc_probe_string_ptr.c`
  - Focused positive sample for `.data -> .rdata` rebasing
- `E:/project/cpp/minic/input/tmp_reloc_no_absolute_data.c`
  - Negative/edge sample for the chosen “no absolute address sites” policy

## Task 1: Establish Failing `.reloc` Coverage

**Files:**
- Create: `E:/project/cpp/minic/input/tmp_reloc_probe_global_ptr.c`
- Create: `E:/project/cpp/minic/input/tmp_reloc_probe_function_ptr.c`
- Create: `E:/project/cpp/minic/input/tmp_reloc_probe_string_ptr.c`
- Create: `E:/project/cpp/minic/input/tmp_reloc_no_absolute_data.c`
- Modify: `E:/project/cpp/minic/CMakeLists.txt`
- Modify: `E:/project/cpp/minic/tests/run_regression_case.ps1`

- [ ] **Step 1: Add focused source samples that cover each current absolute-address family**

Use these exact sample shapes:

```c
/* tmp_reloc_probe_global_ptr.c */
int x;
int *p = &x;
int main() {
    *p = 42;
    return x;
}
```

```c
/* tmp_reloc_probe_function_ptr.c */
int answer() { return 42; }
int (*fn_ptr)() = answer;
int main() { return fn_ptr(); }
```

```c
/* tmp_reloc_probe_string_ptr.c */
char *p = "A";
int main() { return p[0]; }
```

```c
/* tmp_reloc_no_absolute_data.c */
int main() { return 7; }
```

- [ ] **Step 2: Register new relocation-focused regression cases**

Add CTest cases that compile these sources with `--link-trace` and preserve the built executable path. Use labels that keep them in `relocations` and also add a new `rebasing` label if you choose to expose a dedicated preset.

Expected new test names:

- `minic_reloc_probe_global_ptr`
- `minic_reloc_probe_function_ptr`
- `minic_reloc_probe_string_ptr`
- `minic_reloc_no_absolute_data`

- [ ] **Step 3: Extend the PowerShell regression harness interface for PE metadata checks**

Add parameters shaped like:

```powershell
[string]$RelocProbe = ""
[string]$RelocProbeArgs = ""
```

The harness should:

- skip probe invocation when no probe path is provided
- run the probe after compilation succeeds
- fail the CTest case if the probe exits non-zero

- [ ] **Step 4: Run the new tests and confirm they fail before linker changes**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --preset default
cmake --build --preset debug
ctest --test-dir .\build -C Debug -R "minic_reloc_probe_|minic_reloc_no_absolute_data" --output-on-failure
```

Expected failure:

- probe-related cases fail because the executable has no `.reloc` section and no base relocation data directory

- [ ] **Step 5: Commit the red test scaffolding**

```powershell
git add CMakeLists.txt tests/run_regression_case.ps1 input/tmp_reloc_probe_global_ptr.c input/tmp_reloc_probe_function_ptr.c input/tmp_reloc_probe_string_ptr.c input/tmp_reloc_no_absolute_data.c
git commit -m "test: add failing PE base relocation coverage"
```

## Task 2: Add a Deterministic PE Relocation Probe

**Files:**
- Create: `E:/project/cpp/minic/tests/pe_reloc_probe.cpp`
- Modify: `E:/project/cpp/minic/CMakeLists.txt`

- [ ] **Step 1: Add a small test helper executable target**

Add a CMake target named `pe_reloc_probe` built alongside `minic` and available at:

```text
E:/project/cpp/minic/build/Debug/pe_reloc_probe.exe
```

The target should be registered only in the normal build graph; no install rules are needed.

- [ ] **Step 2: Implement PE parsing and `.reloc` inspection in the helper**

The helper should:

- open a PE file from `argv[1]`
- parse DOS header, PE signature, COFF header, optional header, section table
- locate the base relocation data directory
- locate the `.reloc` section bytes
- parse `IMAGE_BASE_RELOCATION` blocks
- collect `DIR64` site RVAs

The helper should reject:

- missing `.reloc` directory when relocations are expected
- malformed block size
- relocation types other than `DIR64` inside the supported rebasing checks

- [ ] **Step 3: Implement synthetic rebasing validation**

Add helper-mode arguments shaped like:

```text
pe_reloc_probe.exe <exe> --expect-reloc yes --delta 0x100000 --site 0x2000=0x3000 --site 0x2008=0x1000
```

Meaning:

- each `--site <slot_rva>=<target_rva>` identifies one absolute pointer slot in the image
- after parsing the image, the helper:
  - copies the relevant section bytes
  - reads the original 64-bit VA at each slot
  - applies the parsed `DIR64` relocations with the supplied delta
  - verifies the slot becomes `ImageBase + delta + target_rva`

This gives deterministic rebasing validation without needing to execute the PE at an OS-chosen address.

- [ ] **Step 4: Add “no reloc expected” mode for the edge case**

Support:

```text
pe_reloc_probe.exe <exe> --expect-reloc no
```

The helper should then verify the chosen policy:

- either the image omits `.reloc`
- or emits an empty valid `.reloc`

Whichever behavior the linker adopts, make the helper enforce that exact policy consistently.

- [ ] **Step 5: Build the helper and run it against an existing executable to verify the current failure mode**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build --preset debug
.\build\Debug\pe_reloc_probe.exe .\build\output\tmp_function_ptr_global.exe --expect-reloc yes --delta 0x100000 --site 0x2000=0x1000
```

Expected failure:

- missing base relocation directory or missing `.reloc`

- [ ] **Step 6: Commit the helper**

```powershell
git add CMakeLists.txt tests/pe_reloc_probe.cpp
git commit -m "test: add PE base relocation probe helper"
```

## Task 3: Record Rebasing-Sensitive Absolute Address Sites In `PeLinker`

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`

- [ ] **Step 1: Introduce a dedicated trace/data structure for base relocation sites**

Add a focused internal type near the existing relocation/import trace structures, for example:

```cpp
struct BaseRelocationSite {
    std::string sectionName;
    std::uint32_t slotRva = 0;
};
```

Keep it separate from `RelocationTraceEntry` because this is about final image VA slots, not input COFF relocation records.

- [ ] **Step 2: Thread base-relocation recording through the absolute-address patch paths**

Extend:

- `applyRelocations(...)`
- `applyInitializedDataRelocations(...)`

so they accept:

```cpp
std::vector<BaseRelocationSite> *baseRelocationSites
```

Only record a site when the linker writes a final absolute VA into image contents, which today means the supported `ADDR64` paths.

Do not record:

- `REL32`
- RVAs
- import thunk metadata unless it is actually stored as a rebasing-sensitive absolute VA in image contents

- [ ] **Step 3: Record the final image RVA, not the object-relative offset**

For each supported `ADDR64` write:

- compute the merged section RVA for the slot
- append `BaseRelocationSite{section.name, slotRva}`

This should happen immediately next to the existing `write64(...)` call so the bookkeeping cannot drift from the final patched write.

- [ ] **Step 4: Rebuild and inspect trace/debug output during local probing**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build --preset debug
.\build\Debug\minic.exe .\input\tmp_reloc_probe_global_ptr.c --link-trace
```

Expected intermediate result:

- executable still links
- no `.reloc` yet
- internal data structure is populated in the debugger / temporary diagnostics if you instrumented them

- [ ] **Step 5: Commit site tracking**

```powershell
git add src/linker/PeLinker.cpp
git commit -m "feat: track PE base relocation sites"
```

## Task 4: Synthesize `.reloc` And Populate The PE Data Directory

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`

- [ ] **Step 1: Add a `.reloc` layout structure parallel to `ImportLayout`**

Define a small internal structure, for example:

```cpp
struct BaseRelocationLayout {
    std::uint32_t rva = 0;
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint32_t> pageRvas;
    std::vector<std::uint16_t> entryCounts;
};
```

It only needs to represent the already-built `.reloc` bytes and trace summaries.

- [ ] **Step 2: Implement block construction grouped by 4 KiB page**

Add a helper shaped like:

```cpp
BaseRelocationLayout buildBaseRelocationLayout(
    std::uint32_t relocRva,
    std::vector<BaseRelocationSite> sites);
```

Implementation rules:

- sort by `slotRva`
- deduplicate identical sites defensively
- group by `slotRva & ~0xFFFu`
- emit block header:
  - page RVA
  - block size
- emit 16-bit entries:
  - `(10u << 12) | (slotRva & 0x0FFFu)` because `DIR64` type value is `10`
- pad block bytes to 4-byte alignment

- [ ] **Step 3: Add `.reloc` into final image layout**

In `PeLinker::linkObjects(...)`, after section RVAs are known and after absolute addresses have been patched:

- build `BaseRelocationLayout`
- assign `.reloc` final RVA and raw pointer after `.idata`
- include `.reloc` in:
  - section count
  - `SizeOfInitializedData`
  - `SizeOfImage`
  - final file size

- [ ] **Step 4: Emit the PE base relocation data directory and `.reloc` section header**

Update optional-header data directory emission so the base relocation directory entry points at:

- `.reloc` RVA
- `.reloc` virtual size

Then write a `.reloc` section header with read-only initialized-data characteristics and copy its bytes into the final file buffer.

- [ ] **Step 5: Remove the fixed-base-only caveat from code comments around PE header emission**

The current comment near the optional header explicitly states that `.reloc` is not emitted and therefore data-side absolute addresses must keep the preferred image base. Replace it with a comment that reflects the new supported behavior.

- [ ] **Step 6: Rebuild and verify structural presence**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --build --preset debug
.\build\Debug\minic.exe .\input\tmp_reloc_probe_function_ptr.c --link-trace
.\build\Debug\pe_reloc_probe.exe .\build\output\tmp_reloc_probe_function_ptr.exe --expect-reloc yes --delta 0x100000 --site 0x2000=0x1000
```

Expected result:

- `.reloc` now exists
- probe succeeds for the function-pointer sample

- [ ] **Step 7: Commit `.reloc` generation**

```powershell
git add src/linker/PeLinker.cpp
git commit -m "feat: emit PE base relocation table"
```

## Task 5: Extend Trace Output And CTest Integration

**Files:**
- Modify: `E:/project/cpp/minic/src/linker/PeLinker.cpp`
- Modify: `E:/project/cpp/minic/CMakeLists.txt`
- Modify: `E:/project/cpp/minic/CMakePresets.json`
- Modify: `E:/project/cpp/minic/tests/run_regression_case.ps1`

- [ ] **Step 1: Add `[link] base relocations` trace output**

Add a compact trace helper, for example:

```cpp
void traceBaseRelocations(std::ostream &out, const BaseRelocationLayout &layout);
```

It should print:

- total `DIR64` site count
- number of page blocks
- one line per block with page RVA and entry count

- [ ] **Step 2: Wire the relocation probe into the new regression cases**

For each relocation-focused case, pass probe arguments that match the expected slots:

- `tmp_reloc_probe_global_ptr.exe`
  - `--site 0x2000=0x3000`
- `tmp_reloc_probe_string_ptr.exe`
  - `--site 0x2000=0x3000`
- `tmp_reloc_probe_function_ptr.exe`
  - `--site 0x2000=0x1000`

Adjust RVAs if the final section ordering changes, but keep the checks exact.

- [ ] **Step 3: Add one dedicated preset if the new checks deserve isolated execution**

If useful, add:

```json
{
  "name": "rebasing",
  "filter": { "include": { "label": "rebasing" } }
}
```

Otherwise keep the tests inside `relocations` only and skip this step.

- [ ] **Step 4: Run the relocation and phase-current suites**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
ctest --preset relocations --output-on-failure
ctest --preset phase-current --output-on-failure
```

Expected result:

- all existing relocation/import/`.bss`/multi-file cases remain green
- new rebasing checks pass structurally

- [ ] **Step 5: Commit trace and test integration**

```powershell
git add src/linker/PeLinker.cpp CMakeLists.txt CMakePresets.json tests/run_regression_case.ps1
git commit -m "test: validate PE base relocations in regressions"
```

## Task 6: Update Long-Lived Documentation

**Files:**
- Modify: `E:/project/cpp/minic/README.md`
- Modify: `E:/project/cpp/minic/docs/pe-coff-linker-support.md`
- Modify: `E:/project/cpp/minic/docs/minic-relocation-matrix.md`
- Modify: `E:/project/cpp/minic/docs/project-status-overview.md`

- [ ] **Step 1: Update the README capability summary**

Document that:

- supported `ADDR64` absolute pointer data now rebases correctly on the Windows path
- `.reloc` is now synthesized for rebasing-sensitive images
- there is a structural relocation regression path in CTest

- [ ] **Step 2: Update the PE linker support document**

Replace the old fixed-image-base caveat with:

- current supported `DIR64` base relocation coverage
- what counts as a rebasing-sensitive final image slot
- current limitations that still remain outside support

- [ ] **Step 3: Update the relocation matrix**

Move PE rebasing for supported absolute pointer data from “remaining gap” to “supported”.

Keep explicit non-goals for:

- unsupported relocation classes
- third-party COFF breadth
- non-`DIR64` base relocations

- [ ] **Step 4: Update project status**

Revise the “weakest area” and “next focus” notes so they no longer describe fixed-base PE behavior as an active limitation for the supported subset.

- [ ] **Step 5: Commit docs**

```powershell
git add README.md docs/pe-coff-linker-support.md docs/minic-relocation-matrix.md docs/project-status-overview.md
git commit -m "docs: document PE base relocation support"
```

## Task 7: Full Verification Pass

**Files:**
- Modify only if verification exposes issues

- [ ] **Step 1: Run the full build and targeted presets**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
cmake --preset default
cmake --build --preset debug
ctest --preset relocations --output-on-failure
ctest --preset imports --output-on-failure
ctest --preset phase-current --output-on-failure
```

Expected result:

- all three commands succeed
- no existing import or linker failure case regresses

- [ ] **Step 2: Run one manual trace spot-check**

Run:

```powershell
$OutputEncoding = [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
.\build\Debug\minic.exe .\input\tmp_reloc_probe_function_ptr.c --link-trace
```

Expected trace includes:

- `[link] relocations`
- `[link] base relocations`
- a non-zero `DIR64` site summary

- [ ] **Step 3: Commit any verification-driven fixes**

```powershell
git add <only the files actually changed during verification>
git commit -m "fix: finalize PE base relocation support"
```

## Self-Review Checklist

- Spec coverage:
  - `.reloc` emission is covered in Task 4
  - recording rebasing-sensitive sites is covered in Task 3
  - deterministic rebasing verification is covered in Task 2 and Task 5
  - trace output is covered in Task 5
  - doc updates are covered in Task 6
- Placeholder scan:
  - no `TODO`/`TBD`
  - each task has concrete files, commands, and expected outcomes
- Type consistency:
  - `BaseRelocationSite` and `BaseRelocationLayout` names are used consistently across linker tasks
  - probe CLI argument names are reused consistently across test tasks

