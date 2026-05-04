# Testing Commands

This document collects the current `minic` test commands in one place.

Document type:

- long-lived testing reference
- focused on how to run the current configured test suites and individual cases

## Prerequisites

Configure and build first:

```powershell
cmake --preset default
cmake --build --preset debug
```

On Windows, the current regression runner depends on GoogleTest being available through `vcpkg`. The CMake setup first checks `VCPKG_ROOT`, then falls back to `D:/vcpkg` when that local path exists.

If you want CTest to print failing case output, keep `--output-on-failure`:

```powershell
ctest --preset phase-current --output-on-failure
```

## Fastest Entry Points

Run the full current regression suite:

```powershell
ctest --preset phase-current
ctest --preset phase-current-parallel
```

Run only the module you are changing:

```powershell
ctest --preset frontend
ctest --preset frontend-parallel
ctest --preset backend
ctest --preset backend-parallel
ctest --preset linker
ctest --preset linker-parallel
ctest --preset optimizer
```

Run the focused preset groups:

```powershell
ctest --preset bss
ctest --preset imports
ctest --preset imports-parallel
ctest --preset relocations
ctest --preset relocations-parallel
ctest --preset rebasing
ctest --preset linker-failures
ctest --preset linker-failures-parallel
ctest --preset linux-link
ctest --preset linux-link-parallel
ctest --preset multifile
ctest --preset multifile-parallel
```

If you want to split the suite into smaller batches and run them on separate terminals, a practical layout is:

```powershell
ctest --preset imports-parallel
ctest --preset relocations-parallel
ctest --preset linker-failures-parallel
ctest --preset linux-link-parallel
```

Or use plain CTest parallelism directly:

```powershell
ctest --test-dir .\build -C Debug -L phase-current -j 8 --output-on-failure
```

List all configured Debug tests without running them:

```powershell
ctest --test-dir .\build -N -C Debug
```

Run one named case directly:

```powershell
ctest --test-dir .\build -C Debug -R "^minic_import_file_backed$" --output-on-failure
```

## Preset Reference

- `phase-current`
  Runs the main current-stage regression set.
- `phase-current-parallel`
  Runs the same phase-current suite with preset parallelism enabled.
- `frontend`
  Runs parser, semantic-analysis, type-checking, initializer, and current struct-front-end cases.
- `frontend-parallel`
  Runs the same frontend-focused suite with preset parallelism enabled.
- `backend`
  Runs code-generation, ABI, assembler-input, Linux backend, and optimizer-adjacent backend cases.
- `backend-parallel`
  Runs the same backend-focused suite with preset parallelism enabled.
- `linker`
  Runs standalone-linker, import, relocation, rebasing, Linux final-link, and explicit linker-failure cases.
- `linker-parallel`
  Runs the same linker-focused suite with preset parallelism enabled.
- `optimizer`
  Runs optimizer-specific regression coverage.
- `bss`
  Runs only `minic_bss_integrity`.
- `imports`
  Runs all import-related positive and negative linker cases.
- `imports-parallel`
  Runs the import group with preset parallelism enabled.
- `relocations`
  Runs relocation and function-pointer related cases.
- `relocations-parallel`
  Runs the relocation group with preset parallelism enabled.
- `rebasing`
  Runs `.reloc` / base-relocation verification cases.
- `linker-failures`
  Runs explicit linker failure-path regressions.
- `linker-failures-parallel`
  Runs the linker-failure group with preset parallelism enabled.
- `linux-link`
  Runs the Linux system-linker path through WSL when available.
- `linux-link-parallel`
  Runs the Linux-link group with preset parallelism enabled.
- `multifile`
  Runs the multi-file compile and standalone-linker cases.
- `multifile-parallel`
  Runs the multi-file group with preset parallelism enabled.

## Full Test Case List

Each case can be run with:

```powershell
ctest --test-dir .\build -C Debug -R "^<test-name>$" --output-on-failure
```

### General Regression

- `minic_standalone_linker_smoke`
- `minic_answer_baseline`
- `minic_local_string_init`
- `minic_windows_stack_args`

### Semantic Diagnostics

- `minic_bad_arity`
- `minic_bad_argument_type`
- `minic_bad_return_type`
- `minic_mixed_integer_conversions`

### Initializers

- `minic_local_int_array_init`
- `minic_global_int_array_init`
- `minic_local_ptr_array_init`
- `minic_array_too_many_elements`
- `minic_array_bad_element_type`
- `minic_local_ptr_array_invalid_source`

### `.bss` And Multi-File Linking

- `minic_bss_integrity`
- `minic_multifile_trace`
- `minic_global_ptr_to_global`
- `minic_global_ptr_to_string`

### Imports

- `minic_import_kernel32_pid`
- `minic_import_msvcrt_puts`
- `minic_import_mixed_showcase`
- `minic_import_putchar`
- `minic_import_printf_simple`
- `minic_import_msvcrt_triple`
- `minic_import_file_backed`
- `minic_import_unresolved`
- `minic_import_catalog_bad_row`
- `minic_import_catalog_duplicate_symbol`
- `minic_import_catalog_duplicate_builtin`

### Relocations And Function Pointers

- `minic_text_addr64_relocation`
- `minic_function_ptr_global`
- `minic_function_ptr_table`
- `minic_function_ptr_invalid_init`

### Rebasing

- `minic_reloc_probe_global_ptr`
- `minic_reloc_probe_function_ptr`
- `minic_reloc_probe_string_ptr`
- `minic_reloc_no_absolute_data`

### Linker Failure Cases

- `minic_duplicate_external_symbol`
- `minic_unsupported_text_relocation`
- `minic_unsupported_target_section`
- `minic_invalid_small_object`
- `minic_missing_entry_symbol`
- `minic_missing_text_section`

### Linux / WSL Path

- `minic_linux_stack_args_asm`
- `minic_linux_answer_link`
- `minic_linux_stack_args_link`
- `minic_linux_global_array_link`
- `minic_linux_rejects_windows_obj`

## Useful Command Patterns

If you change only one module, use the matching label directly:

```powershell
ctest --test-dir .\build -C Debug -L frontend --output-on-failure
ctest --test-dir .\build -C Debug -L backend --output-on-failure
ctest --test-dir .\build -C Debug -L linker --output-on-failure
ctest --test-dir .\build -C Debug -L optimizer --output-on-failure
```

Run all import tests with explicit Debug selection instead of presets:

```powershell
ctest --test-dir .\build -C Debug -L imports --output-on-failure
```

Run all rebasing tests without using the preset:

```powershell
ctest --test-dir .\build -C Debug -L rebasing --output-on-failure
```

Run all linker failure tests:

```powershell
ctest --test-dir .\build -C Debug -L linker-failures --output-on-failure
```

Run all Linux-link cases:

```powershell
ctest --test-dir .\build -C Debug -L linux-link --output-on-failure
```

## Notes

- Test declarations live in [CMakeLists.txt](../CMakeLists.txt).
- Preset definitions live in [CMakePresets.json](../CMakePresets.json).
- The regression suite is now GoogleTest-driven. CTest still runs named cases and presets, but the underlying assertions live in [tests/minic_regression_tests.cpp](../tests/minic_regression_tests.cpp) and [tests/regression_test_utils.cpp](../tests/regression_test_utils.cpp).
- The older PowerShell harness scripts under [tests](../tests/) are no longer the primary execution path.
- Linux-link cases require WSL with `gcc`; they are skipped automatically when unavailable.
