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

If you want CTest to print failing case output, keep `--output-on-failure`:

```powershell
ctest --preset phase-current --output-on-failure
```

## Fastest Entry Points

Run the full current regression suite:

```powershell
ctest --preset phase-current
```

Run the focused preset groups:

```powershell
ctest --preset bss
ctest --preset imports
ctest --preset relocations
ctest --preset rebasing
ctest --preset linker-failures
ctest --preset linux-link
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
- `bss`
  Runs only `minic_bss_integrity`.
- `imports`
  Runs all import-related positive and negative linker cases.
- `relocations`
  Runs relocation and function-pointer related cases.
- `rebasing`
  Runs `.reloc` / base-relocation verification cases.
- `linker-failures`
  Runs explicit linker failure-path regressions.
- `linux-link`
  Runs the Linux system-linker teaching path through WSL when available.

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
- Many regression cases use helper scripts under [tests](../tests/).
- Linux-link cases require WSL with `gcc`; they are skipped automatically when unavailable.
