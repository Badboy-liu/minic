# Remove NASM From The Windows Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the `nasm` dependency from the Windows build path by teaching `minic` to emit AMD64 COFF object files directly and then link them through the existing `minic-link` PE/COFF pipeline.

**Architecture:** Keep Linux on the current NASM plus WSL linker path. Introduce a Windows-only object-file model plus a direct `CoffObjectWriter`, first for minimal `.text` plus `REL32`, then extend to `.data`, `.rdata`, `.bss`, and the current teaching subset of `ADDR64` relocations. Preserve the existing NASM path during migration for parity testing.

**Tech Stack:** C++17, COFF AMD64 object format, existing `PeLinker`, existing regression framework in `CMakeLists.txt` plus `tests/run_regression_case.ps1`.

---

## File Structure

Planned file responsibilities for this implementation:

- Create: `src/backend/ObjectFileModel.h`
  - define the in-memory object model used by the direct COFF path.
- Create: `src/backend/CoffObjectWriter.h`
  - declare the Windows COFF serialization API.
- Create: `src/backend/CoffObjectWriter.cpp`
  - serialize `ObjectFileModel` into AMD64 COFF `.obj` bytes.
- Create: `src/backend/WindowsObjectEmitter.h`
  - declare the Windows backend that emits object-model sections, symbols, and relocations.
- Create: `src/backend/WindowsObjectEmitter.cpp`
  - encode the current Windows teaching subset directly into an `ObjectFileModel`.
- Modify: `src/backend/CodeGenerator.h`
  - narrow its responsibility toward text assembly or shared helpers as needed.
- Modify: `src/backend/CodeGenerator.cpp`
  - either reuse shared lowering helpers or reduce Windows-only NASM-specific ownership.
- Modify: `src/backend/Target.h`
  - add backend-selection metadata if needed for migration.
- Modify: `src/support/Toolchain.h`
  - add backend dispatch for direct COFF object writing versus NASM assembly.
- Modify: `src/support/Toolchain.cpp`
  - preserve NASM for Linux and legacy Windows, but skip assembly when using the direct COFF path.
- Modify: `src/app/Driver.h`
  - add CLI/backend-selection fields if the migration path is exposed to users.
- Modify: `src/app/Driver.cpp`
  - choose the Windows object backend, write direct `.obj` output, and keep existing linker invocation unchanged.
- Modify: `src/linker/PeLinker.cpp`
  - only if needed to accept direct-writer object details that are still within the documented subset.
- Modify: `CMakeLists.txt`
  - add direct-COFF regression cases and, during migration, parity-oriented cases.
- Create: `input/tmp_coff_direct_call.c`
  - minimal single-file `.text` teaching case for direct COFF output.
- Create: `input/tmp_coff_multifile_main.c`
  - multifile call case for direct COFF `.text` plus `REL32`.
- Create: `input/tmp_coff_multifile_math.c`
  - second translation unit for multifile direct COFF validation.
- Create: `input/tmp_coff_global_ptr.c`
  - direct COFF data-relocation teaching case.
- Modify: `README.md`
  - document the Windows object backend transition once it works.
- Modify: `docs/pe-coff-linker-support.md`
  - document that the linker now accepts the compiler's direct COFF objects in addition to the historical NASM-backed path.

## Task 1: Introduce The Object File Model

**Files:**
- Create: `src/backend/ObjectFileModel.h`

- [ ] **Step 1: Define explicit section, symbol, and relocation enums**

```cpp
enum class ObjectSectionKind {
    Text,
    Data,
    ReadOnlyData,
    Bss
};

enum class ObjectRelocationKind {
    Rel32,
    Addr64
};

enum class ObjectSymbolBinding {
    Local,
    Global,
    Undefined
};
```

- [ ] **Step 2: Define the section model**

```cpp
struct ObjectSection {
    std::string name;
    std::vector<std::uint8_t> bytes;
    std::uint32_t characteristics = 0;
    std::uint32_t alignment = 1;
    std::uint32_t virtualSize = 0;
    bool isBss = false;
};
```

- [ ] **Step 3: Define the symbol and relocation model**

```cpp
struct ObjectSymbol {
    std::string name;
    int sectionIndex = 0;
    std::uint32_t value = 0;
    ObjectSymbolBinding binding = ObjectSymbolBinding::Local;
};

struct ObjectRelocation {
    int sectionIndex = 0;
    std::uint32_t offset = 0;
    std::string targetSymbol;
    ObjectRelocationKind kind = ObjectRelocationKind::Rel32;
};
```

- [ ] **Step 4: Define the full object container**

```cpp
struct ObjectFileModel {
    std::vector<ObjectSection> sections;
    std::vector<ObjectSymbol> symbols;
    std::vector<ObjectRelocation> relocations;
};
```

- [ ] **Step 5: Rebuild to verify the new model header integrates cleanly**

Run: `cmake --build --preset debug`

Expected: build succeeds because the new header is additive and not yet wired into the pipeline.

- [ ] **Step 6: Commit the object-model foundation**

```bash
git add src/backend/ObjectFileModel.h
git commit -m "feat: add object file model for direct coff output"
```

## Task 2: Implement A Minimal COFF Writer

**Files:**
- Create: `src/backend/CoffObjectWriter.h`
- Create: `src/backend/CoffObjectWriter.cpp`

- [ ] **Step 1: Declare a single-purpose writer API**

```cpp
class CoffObjectWriter {
public:
    static std::vector<std::uint8_t> writeAmd64(const ObjectFileModel &model);
};
```

- [ ] **Step 2: Implement fixed-width little-endian append helpers**

```cpp
static void append16(std::vector<std::uint8_t> &out, std::uint16_t value);
static void append32(std::vector<std::uint8_t> &out, std::uint32_t value);
static void appendBytes(std::vector<std::uint8_t> &out, const std::vector<std::uint8_t> &bytes);
```

- [ ] **Step 3: Write the COFF file header and section table for the minimal subset**

```cpp
append16(file, 0x8664); // AMD64
append16(file, static_cast<std::uint16_t>(model.sections.size()));
append32(file, 0); // timestamp
append32(file, symbolTableOffset);
append32(file, symbolCount);
append16(file, 0); // optional header size
append16(file, 0);
```

- [ ] **Step 4: Serialize raw section contents and relocation tables**

```cpp
for (const auto &section : model.sections) {
    writeSectionHeader(...);
    appendBytes(file, section.bytes);
    writeRelocationsForSection(...);
}
```

- [ ] **Step 5: Serialize the symbol table and string table**

```cpp
for (const auto &symbol : model.symbols) {
    writeSymbol(file, symbol, stringTable);
}
writeStringTable(file, stringTable);
```

- [ ] **Step 6: Add a focused writer smoke test path through a tiny synthetic model**

Run: create a temporary in-code synthetic `ObjectFileModel` with one `.text` section and one global symbol, then call `writeAmd64`.

Expected: the function returns non-empty bytes with a valid AMD64 machine field and one section entry.

- [ ] **Step 7: Rebuild after the COFF writer lands**

Run: `cmake --build --preset debug`

Expected: build succeeds; the writer is present but not yet used by the driver.

- [ ] **Step 8: Commit the minimal COFF writer**

```bash
git add src/backend/CoffObjectWriter.h src/backend/CoffObjectWriter.cpp
git commit -m "feat: add minimal amd64 coff object writer"
```

## Task 3: Emit Minimal `.text` Plus `REL32` Directly

**Files:**
- Create: `src/backend/WindowsObjectEmitter.h`
- Create: `src/backend/WindowsObjectEmitter.cpp`
- Modify: `src/backend/CodeGenerator.h`
- Modify: `src/backend/CodeGenerator.cpp`

- [ ] **Step 1: Declare a Windows object emitter that returns an `ObjectFileModel`**

```cpp
class WindowsObjectEmitter {
public:
    ObjectFileModel emit(const Program &program, bool emitEntryPoint);
};
```

- [ ] **Step 2: Start with the minimal `.text` subset**

```cpp
ObjectSection text;
text.name = ".text";
text.characteristics = 0x60000020;
text.alignment = 16;
model.sections.push_back(std::move(text));
```

- [ ] **Step 3: Emit defined function symbols and undefined extern symbols explicitly**

```cpp
model.symbols.push_back(ObjectSymbol{"fn_main", textSectionIndex, functionOffset, ObjectSymbolBinding::Global});
model.symbols.push_back(ObjectSymbol{"fn_helper", 0, 0, ObjectSymbolBinding::Undefined});
```

- [ ] **Step 4: Emit `REL32` relocation records for direct calls**

```cpp
model.relocations.push_back(ObjectRelocation{
    textSectionIndex,
    relocationOffset,
    "fn_helper",
    ObjectRelocationKind::Rel32
});
```

- [ ] **Step 5: Keep Windows entry-point ownership explicit**

```cpp
if (emitEntryPoint) {
    model.symbols.push_back(ObjectSymbol{"mainCRTStartup", textSectionIndex, entryOffset, ObjectSymbolBinding::Global});
}
```

- [ ] **Step 6: Add a tiny single-file source that exercises direct `.text` emission**

```c
int helper() {
    return 42;
}

int main() {
    return helper();
}
```

- [ ] **Step 7: Rebuild after introducing the minimal object emitter**

Run: `cmake --build --preset debug`

Expected: build succeeds; the direct emitter compiles even if the driver still defaults to NASM.

- [ ] **Step 8: Commit the minimal `.text` emitter**

```bash
git add src/backend/WindowsObjectEmitter.h src/backend/WindowsObjectEmitter.cpp src/backend/CodeGenerator.h src/backend/CodeGenerator.cpp
git commit -m "feat: emit minimal windows coff text objects"
```

## Task 4: Add A Windows Object Backend Switch In The Driver

**Files:**
- Modify: `src/backend/Target.h`
- Modify: `src/app/Driver.h`
- Modify: `src/app/Driver.cpp`
- Modify: `src/support/Toolchain.h`
- Modify: `src/support/Toolchain.cpp`

- [ ] **Step 1: Introduce an explicit Windows object backend enum**

```cpp
enum class WindowsObjectBackend {
    Nasm,
    Coff
};
```

- [ ] **Step 2: Add a driver option for backend selection during migration**

```cpp
struct Options {
    // ...
    WindowsObjectBackend windowsObjectBackend = WindowsObjectBackend::Nasm;
};
```

- [ ] **Step 3: Parse a migration-only CLI flag**

```cpp
if (args[i] == "--windows-obj-backend") {
    options.windowsObjectBackend = parseWindowsObjectBackend(args[++i]);
    continue;
}
```

- [ ] **Step 4: Branch the Windows compile path between NASM assembly and direct COFF writing**

```cpp
if (options.target == TargetKind::WindowsX64 &&
    options.windowsObjectBackend == WindowsObjectBackend::Coff) {
    writeFile(objPath, CoffObjectWriter::writeAmd64(emitter.emit(fileProgram, emitEntryPoint)));
} else {
    writeFile(asmPath, generator.generate(fileProgram, emitEntryPoint));
    Toolchain::assembleObject(toolchain, options.target, asmPath, objPath);
}
```

- [ ] **Step 5: Rebuild and verify the old NASM path still works**

Run: `cmake --build --preset debug`

Run: `ctest --test-dir .\build -C Debug -R minic_answer_baseline --output-on-failure`

Expected: baseline remains green on the existing default path.

- [ ] **Step 6: Commit backend selection and driver wiring**

```bash
git add src/backend/Target.h src/app/Driver.h src/app/Driver.cpp src/support/Toolchain.h src/support/Toolchain.cpp
git commit -m "feat: add windows object backend selection"
```

## Task 5: Validate Minimal Direct COFF On Single-File And Multifile `.text`

**Files:**
- Create: `input/tmp_coff_direct_call.c`
- Create: `input/tmp_coff_multifile_main.c`
- Create: `input/tmp_coff_multifile_math.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add a direct-COFF single-file call sample**

```c
int answer() {
    return 42;
}

int main() {
    return answer();
}
```

- [ ] **Step 2: Add a direct-COFF multifile sample**

```c
// input/tmp_coff_multifile_math.c
int add7(int x) {
    return x + 7;
}
```

```c
// input/tmp_coff_multifile_main.c
int add7(int x);

int main() {
    return add7(35);
}
```

- [ ] **Step 3: Add regression cases that force the `coff` backend**

```cmake
add_minic_regression_test(
    minic_coff_direct_call
    SOURCES input/tmp_coff_direct_call.c
    COMPILER_ARGS --windows-obj-backend;coff
    OUTPUT_EXE build/output/tmp_coff_direct_call.exe
    EXPECTED_EXIT 42
    LABELS regression coff-direct phase-current
)
```

- [ ] **Step 4: Run the new direct-COFF `.text` regressions**

Run: `ctest --test-dir .\build -C Debug -R minic_coff_ --output-on-failure`

Expected: the single-file and multifile direct COFF `.text` cases pass.

- [ ] **Step 5: Commit the direct-COFF `.text` regression layer**

```bash
git add input/tmp_coff_direct_call.c input/tmp_coff_multifile_main.c input/tmp_coff_multifile_math.c CMakeLists.txt
git commit -m "test: validate direct coff text emission"
```

## Task 6: Extend The Writer To `.data`, `.rdata`, `.bss`, And `ADDR64`

**Files:**
- Modify: `src/backend/WindowsObjectEmitter.cpp`
- Modify: `src/backend/CoffObjectWriter.cpp`

- [ ] **Step 1: Emit initialized writable data into `.data`**

```cpp
ObjectSection data;
data.name = ".data";
data.characteristics = 0xC0000040;
```

- [ ] **Step 2: Emit string literals and read-only constants into `.rdata`**

```cpp
ObjectSection rdata;
rdata.name = ".rdata";
rdata.characteristics = 0x40000040;
```

- [ ] **Step 3: Emit uninitialized globals into `.bss` without raw bytes**

```cpp
ObjectSection bss;
bss.name = ".bss";
bss.isBss = true;
bss.virtualSize = totalBssBytes;
bss.characteristics = 0xC0000080;
```

- [ ] **Step 4: Emit `ADDR64` relocations for the current teaching subset**

```cpp
model.relocations.push_back(ObjectRelocation{
    dataSectionIndex,
    pointerInitializerOffset,
    "gv_target",
    ObjectRelocationKind::Addr64
});
```

- [ ] **Step 5: Keep the writer restricted to the current documented subset**

```cpp
if (relocation.kind != ObjectRelocationKind::Rel32 &&
    relocation.kind != ObjectRelocationKind::Addr64) {
    throw std::runtime_error("unsupported direct COFF relocation kind");
}
```

- [ ] **Step 6: Rebuild after section and relocation expansion**

Run: `cmake --build --preset debug`

Expected: build succeeds with the expanded direct writer path.

- [ ] **Step 7: Commit the section and `ADDR64` expansion**

```bash
git add src/backend/WindowsObjectEmitter.cpp src/backend/CoffObjectWriter.cpp
git commit -m "feat: add direct coff data sections and addr64 relocations"
```

## Task 7: Add Direct COFF Regressions For Data And Relocations

**Files:**
- Create: `input/tmp_coff_global_ptr.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add a global pointer relocation sample for the direct COFF path**

```c
int x;
int *p = &x;

int main() {
    *p = 42;
    return x;
}
```

- [ ] **Step 2: Add direct-COFF regression coverage for `.bss` and pointer relocation cases**

```cmake
add_minic_regression_test(
    minic_coff_global_ptr
    SOURCES input/tmp_coff_global_ptr.c
    COMPILER_ARGS --windows-obj-backend;coff;--link-trace
    OUTPUT_EXE build/output/tmp_coff_global_ptr.exe
    EXPECTED_EXIT 42
    LABELS regression coff-direct relocations
)
```

- [ ] **Step 3: Re-run the relocation and multifile teaching subset on the direct COFF path**

Run: `ctest --test-dir .\build -C Debug -R "minic_coff_|minic_bss_integrity|minic_multifile_trace" --output-on-failure`

Expected: direct COFF cases pass and the historical NASM-backed checks still remain green.

- [ ] **Step 4: Commit the direct COFF data and relocation regressions**

```bash
git add input/tmp_coff_global_ptr.c CMakeLists.txt
git commit -m "test: cover direct coff data relocations"
```

## Task 8: Document The Direct COFF Backend And Flip The Windows Default

**Files:**
- Modify: `src/app/Driver.cpp`
- Modify: `README.md`
- Modify: `docs/pe-coff-linker-support.md`

- [ ] **Step 1: Decide whether parity is strong enough to flip the Windows default**

Run: inspect the results from Tasks 5 through 7 and confirm that the current teaching subset is stable on the `coff` path.

Expected: either keep the migration flag for one more phase or switch the default to direct COFF.

- [ ] **Step 2: If parity is stable, switch the Windows default backend from NASM to direct COFF**

```cpp
WindowsObjectBackend windowsObjectBackend = WindowsObjectBackend::Coff;
```

- [ ] **Step 3: Update README to describe the new Windows path**

```md
- Windows builds now emit AMD64 COFF objects directly
- `nasm` is no longer required on the default Windows path
- Linux still uses the NASM plus WSL linker path
```

- [ ] **Step 4: Update `docs/pe-coff-linker-support.md` to reflect accepted object sources**

```md
- compiler-generated direct COFF objects
- historical NASM-generated objects on the teaching subset during transition
```

- [ ] **Step 5: Run the main Windows regression suite after the documentation and default flip**

Run: `ctest --preset phase-current --output-on-failure`

Expected: the suite remains green on the new Windows default path.

- [ ] **Step 6: Commit the Windows default flip and documentation**

```bash
git add src/app/Driver.cpp README.md docs/pe-coff-linker-support.md
git commit -m "feat: switch windows builds to direct coff objects"
```

## Self-Review

Spec coverage check:

- object-model introduction is covered by Task 1,
- the dedicated AMD64 COFF writer is covered by Task 2,
- minimal `.text` plus `REL32` direct emission is covered by Tasks 3 through 5,
- `.data/.rdata/.bss` and `ADDR64` expansion is covered by Tasks 6 and 7,
- backend migration and documentation are covered by Task 8.

Placeholder scan:

- no `TBD`, `TODO`, or deferred filler steps remain,
- every task names exact files,
- every validation step has an explicit command and expected outcome.

Type and naming consistency check:

- the plan consistently uses `ObjectFileModel`, `ObjectSection`, `ObjectSymbol`, `ObjectRelocation`, `CoffObjectWriter`, and `WindowsObjectEmitter`,
- the staged migration from NASM to direct COFF is reflected consistently across driver, toolchain, and docs,
- the writer and linker boundaries match the current documented PE/COFF teaching subset.
