cmake_minimum_required(VERSION 3.21)

project(minic VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

set(MINIC_CORE_SOURCES
    src/support/ProcessUtils.cpp
    src/support/ProcessUtils.h
    src/linker/LinkerBackend.cpp
    src/linker/LinkerBackend.h
    src/linker/BuiltinPeCoffLinkerBackend.cpp
    src/linker/BuiltinPeCoffLinkerBackend.h
    src/linker/WslGccElfLinkerBackend.cpp
    src/linker/WslGccElfLinkerBackend.h
    src/support/Toolchain.cpp
    src/support/Toolchain.h
    src/linker/PeLinker.cpp
    src/linker/PeLinker.h
    src/backend/CoffObjectWriter.cpp
    src/backend/CoffObjectWriter.h
    src/backend/CodeGenerator.cpp
    src/backend/CodeGenerator.h
    src/backend/ObjectFileModel.h
    src/backend/WindowsObjectEmitter.cpp
    src/backend/WindowsObjectEmitter.h
    src/frontend/Semantics.cpp
    src/frontend/Semantics.h
    src/frontend/Parser.cpp
    src/frontend/Parser.h
    src/frontend/Lexer.cpp
    src/frontend/Lexer.h
    src/frontend/Token.cpp
    src/frontend/Token.h
    src/backend/Target.h
    src/frontend/Ast.h
)

add_executable(${PROJECT_NAME}
    src/app/main.cpp
    src/app/Driver.cpp
    src/app/Driver.h
    ${MINIC_CORE_SOURCES}
)

add_executable(minic-link
    src/linker/link_main.cpp
    src/linker/LinkerDriver.cpp
    src/linker/LinkerDriver.h
    ${MINIC_CORE_SOURCES}
)

add_executable(pe_reloc_probe
    tests/pe_reloc_probe.cpp
)

function(configure_minic_target target_name)
    target_include_directories(${target_name}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src
            ${PROJECT_SOURCE_DIR}/src/app
            ${PROJECT_SOURCE_DIR}/src/backend
            ${PROJECT_SOURCE_DIR}/src/frontend
            ${PROJECT_SOURCE_DIR}/src/linker
            ${PROJECT_SOURCE_DIR}/src/support
    )

    target_compile_definitions(${target_name}
        PRIVATE
            NOMINMAX
    )
endfunction()

configure_minic_target(${PROJECT_NAME})
configure_minic_target(minic-link)
target_compile_definitions(pe_reloc_probe PRIVATE NOMINMAX)

if (WIN32)
    function(add_minic_regression_test test_name)
        set(options SKIP_RUN RUN_WITH_WSL SKIP_IF_WSL_UNAVAILABLE)
        set(oneValueArgs EXPECTED_EXIT OUTPUT_EXE CHECK_FILE RELOC_PROBE CATALOG_FIXTURE)
        set(multiValueArgs SOURCES COMPILER_ARGS TRACE_MARKERS OUTPUT_MARKERS FILE_MARKERS ERROR_MARKERS LABELS RELOC_PROBE_ARGS)
        cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

        string(JOIN ";" source_files ${TEST_SOURCES})
        string(JOIN ";" compiler_args ${TEST_COMPILER_ARGS})
        string(JOIN ";" trace_markers ${TEST_TRACE_MARKERS})
        string(JOIN ";" output_markers ${TEST_OUTPUT_MARKERS})
        string(JOIN ";" file_markers ${TEST_FILE_MARKERS})
        string(JOIN ";" error_markers ${TEST_ERROR_MARKERS})
        string(JOIN ";" reloc_probe_args ${TEST_RELOC_PROBE_ARGS})
        set(expect_compiler_failure)
        if (TEST_ERROR_MARKERS)
            set(expect_compiler_failure -ExpectCompilerFailure)
        endif()
        set(skip_run)
        if (TEST_SKIP_RUN)
            set(skip_run -SkipRun)
        endif()
        set(run_with_wsl)
        if (TEST_RUN_WITH_WSL)
            set(run_with_wsl -RunWithWsl)
        endif()
        set(skip_if_wsl_unavailable)
        if (TEST_SKIP_IF_WSL_UNAVAILABLE)
            set(skip_if_wsl_unavailable -SkipIfWslUnavailable)
        endif()

        add_test(
            NAME ${test_name}
            COMMAND powershell
                -ExecutionPolicy Bypass
                -File ${PROJECT_SOURCE_DIR}/tests/run_regression_case.ps1
                -CompilerPath $<TARGET_FILE:${PROJECT_NAME}>
                -RepoRoot ${PROJECT_SOURCE_DIR}
                -SourceFiles "${source_files}"
                -CompilerArgs "${compiler_args}"
                -OutputExe "${TEST_OUTPUT_EXE}"
                -ExpectedExit ${TEST_EXPECTED_EXIT}
                -RequiredTraceMarkers "${trace_markers}"
                -RequiredOutputMarkers "${output_markers}"
                -CheckFile "${TEST_CHECK_FILE}"
                -RequiredFileMarkers "${file_markers}"
                -CatalogFixture "${TEST_CATALOG_FIXTURE}"
                -RelocProbe "${TEST_RELOC_PROBE}"
                -RelocProbeArgs "${reloc_probe_args}"
                ${expect_compiler_failure}
                ${run_with_wsl}
                ${skip_if_wsl_unavailable}
                ${skip_run}
                -RequiredErrorMarkers "${error_markers}"
        )

        if (TEST_LABELS)
            set_tests_properties(${test_name} PROPERTIES LABELS "${TEST_LABELS}")
        endif()
    endfunction()

    add_test(
        NAME minic_standalone_linker_smoke
        COMMAND powershell
            -ExecutionPolicy Bypass
            -File ${PROJECT_SOURCE_DIR}/tests/run_linker_regression_case.ps1
            -CompilerPath $<TARGET_FILE:${PROJECT_NAME}>
            -LinkerPath $<TARGET_FILE:minic-link>
            -RepoRoot ${PROJECT_SOURCE_DIR}
            -SourceFile input/answer.c
            -OutputObject build/output/answer_link_only.obj
            -OutputExe build/output/answer_link_only.exe
            -ExpectedExit 42
    )
    set_tests_properties(minic_standalone_linker_smoke PROPERTIES LABELS "regression;phase-current;linker")

    add_minic_regression_test(
        minic_answer_baseline
        SOURCES input/answer.c
        OUTPUT_EXE build/output/answer.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current
    )

    add_minic_regression_test(
        minic_answer_direct_coff
        SOURCES input/answer.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/answer_direct_coff.exe
        OUTPUT_EXE build/output/answer_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows
    )

    add_minic_regression_test(
        minic_logic_loop_direct_coff
        SOURCES input/tmp_logic_loop.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_logic_loop_direct_coff.exe
        OUTPUT_EXE build/output/tmp_logic_loop_direct_coff.exe
        EXPECTED_EXIT 9
        LABELS regression phase-current coff windows
    )

    add_minic_regression_test(
        minic_globals_main_direct_coff
        SOURCES input/tmp_globals_main.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_globals_main_direct_coff.exe
        OUTPUT_EXE build/output/tmp_globals_main_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows
    )

    add_minic_regression_test(
        minic_global_ptr_to_global_direct_coff
        SOURCES input/tmp_global_ptr_to_global.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_global_ptr_to_global_direct_coff.exe
        OUTPUT_EXE build/output/tmp_global_ptr_to_global_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows
    )

    add_minic_regression_test(
        minic_global_ptr_to_string_direct_coff
        SOURCES input/tmp_global_ptr_to_string.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_global_ptr_to_string_direct_coff.exe
        OUTPUT_EXE build/output/tmp_global_ptr_to_string_direct_coff.exe
        EXPECTED_EXIT 65
        LABELS regression phase-current coff windows
    )

    add_minic_regression_test(
        minic_bss_integrity_direct_coff
        SOURCES input/tmp_bss_integrity.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_bss_integrity_direct_coff.exe
        OUTPUT_EXE build/output/tmp_bss_integrity_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows
    )

    add_minic_regression_test(
        minic_windows_stack_args_direct_coff
        SOURCES input/tmp_four_args.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_four_args_direct_coff.exe
        OUTPUT_EXE build/output/tmp_four_args_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows abi
    )

    add_minic_regression_test(
        minic_multifile_direct_coff
        SOURCES input/tmp_multi_main.c input/tmp_multi_math.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_multi_direct_coff.exe
        OUTPUT_EXE build/output/tmp_multi_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows multifile
    )

    add_minic_regression_test(
        minic_import_puts_direct_coff
        SOURCES input/tmp_import_msvcrt_puts.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_import_msvcrt_puts_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_msvcrt_puts_direct_coff.exe
        EXPECTED_EXIT 1
        LABELS regression phase-current coff windows imports
    )

    add_minic_regression_test(
        minic_local_int_array_direct_coff
        SOURCES input/tmp_if.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_if_direct_coff.exe
        OUTPUT_EXE build/output/tmp_if_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows initializers
    )

    add_minic_regression_test(
        minic_local_string_direct_coff
        SOURCES input/tmp_string_local.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_string_local_direct_coff.exe
        OUTPUT_EXE build/output/tmp_string_local_direct_coff.exe
        EXPECTED_EXIT 65
        LABELS regression phase-current coff windows initializers
    )

    add_minic_regression_test(
        minic_basic_types_direct_coff
        SOURCES input/tmp_basic_types.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_basic_types_direct_coff.exe
        OUTPUT_EXE build/output/tmp_basic_types_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows semantics
    )

    add_minic_regression_test(
        minic_local_ptr_array_direct_coff
        SOURCES input/tmp_void_ptr.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_void_ptr_direct_coff.exe
        OUTPUT_EXE build/output/tmp_void_ptr_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows initializers
    )

    add_minic_regression_test(
        minic_function_ptr_global_direct_coff
        SOURCES input/tmp_function_ptr_global.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_function_ptr_global_direct_coff.exe
        OUTPUT_EXE build/output/tmp_function_ptr_global_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows relocations
    )

    add_minic_regression_test(
        minic_function_ptr_table_direct_coff
        SOURCES input/tmp_function_ptr_table.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_function_ptr_table_direct_coff.exe
        OUTPUT_EXE build/output/tmp_function_ptr_table_direct_coff.exe
        EXPECTED_EXIT 2
        LABELS regression phase-current coff windows relocations
    )

    add_minic_regression_test(
        minic_import_printf_direct_coff
        SOURCES input/tmp_import_printf_simple.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_import_printf_simple_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_printf_simple_direct_coff.exe
        EXPECTED_EXIT 1
        LABELS regression phase-current coff windows imports
    )

    add_minic_regression_test(
        minic_import_triple_direct_coff
        SOURCES input/tmp_import_msvcrt_triple.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_import_msvcrt_triple_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_msvcrt_triple_direct_coff.exe
        EXPECTED_EXIT 1
        LABELS regression phase-current coff windows imports
    )

    add_minic_regression_test(
        minic_import_kernel32_pid_direct_coff
        SOURCES input/tmp_import_kernel32_pid.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_import_kernel32_pid_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_kernel32_pid_direct_coff.exe
        EXPECTED_EXIT 1
        LABELS regression phase-current coff windows imports
    )

    add_minic_regression_test(
        minic_import_file_backed_direct_coff
        SOURCES input/tmp_import_file_backed.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_import_file_backed_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_file_backed_direct_coff.exe
        EXPECTED_EXIT 5
        LABELS regression phase-current coff windows imports
    )

    add_minic_regression_test(
        minic_reloc_probe_function_ptr_direct_coff
        SOURCES input/tmp_reloc_probe_function_ptr.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_reloc_probe_function_ptr_direct_coff.exe
        OUTPUT_EXE build/output/tmp_reloc_probe_function_ptr_direct_coff.exe
        EXPECTED_EXIT 42
        RELOC_PROBE build/Debug/pe_reloc_probe.exe
        RELOC_PROBE_ARGS --expect-reloc yes --delta 0x100000 --site 0x2000=0x1000
        LABELS regression phase-current coff windows relocations rebasing
    )

    add_minic_regression_test(
        minic_reloc_probe_string_ptr_direct_coff
        SOURCES input/tmp_reloc_probe_string_ptr.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_reloc_probe_string_ptr_direct_coff.exe
        OUTPUT_EXE build/output/tmp_reloc_probe_string_ptr_direct_coff.exe
        EXPECTED_EXIT 65
        RELOC_PROBE build/Debug/pe_reloc_probe.exe
        RELOC_PROBE_ARGS --expect-reloc yes --delta 0x100000 --site 0x2000=0x3000
        LABELS regression phase-current coff windows relocations rebasing
    )

    add_minic_regression_test(
        minic_reloc_probe_global_ptr_direct_coff
        SOURCES input/tmp_reloc_probe_global_ptr.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_reloc_probe_global_ptr_direct_coff.exe
        OUTPUT_EXE build/output/tmp_reloc_probe_global_ptr_direct_coff.exe
        EXPECTED_EXIT 42
        RELOC_PROBE build/Debug/pe_reloc_probe.exe
        RELOC_PROBE_ARGS --expect-reloc yes --delta 0x100000 --site 0x2000=0x3000
        LABELS regression phase-current coff windows relocations rebasing
    )

    add_minic_regression_test(
        minic_reloc_no_absolute_data_direct_coff
        SOURCES input/tmp_reloc_no_absolute_data.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_reloc_no_absolute_data_direct_coff.exe
        OUTPUT_EXE build/output/tmp_reloc_no_absolute_data_direct_coff.exe
        EXPECTED_EXIT 7
        RELOC_PROBE build/Debug/pe_reloc_probe.exe
        RELOC_PROBE_ARGS --expect-reloc no
        LABELS regression phase-current coff windows relocations rebasing
    )

    add_minic_regression_test(
        minic_import_unresolved_direct_coff
        SOURCES input/tmp_import_unresolved.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_import_unresolved_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_unresolved_direct_coff.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "unresolved external symbol:" "invoking standalone minic-link failed"
        LABELS regression phase-current coff windows imports linker-failures
    )

    add_minic_regression_test(
        minic_import_catalog_bad_row_direct_coff
        SOURCES input/answer.c
        COMPILER_ARGS --windows-obj-backend;coff;--link-trace;-o;REPO:build/output/tmp_import_catalog_bad_row_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_catalog_bad_row_direct_coff.exe
        EXPECTED_EXIT 0
        CATALOG_FIXTURE tests/data/import_catalog_bad_row.txt
        ERROR_MARKERS "invalid import catalog row"
        LABELS regression phase-current coff windows imports linker-failures
    )

    add_minic_regression_test(
        minic_import_catalog_duplicate_symbol_direct_coff
        SOURCES input/answer.c
        COMPILER_ARGS --windows-obj-backend;coff;--link-trace;-o;REPO:build/output/tmp_import_catalog_duplicate_symbol_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_catalog_duplicate_symbol_direct_coff.exe
        EXPECTED_EXIT 0
        CATALOG_FIXTURE tests/data/import_catalog_duplicate_symbol.txt
        ERROR_MARKERS "duplicate import catalog symbol" "fn_strlen"
        LABELS regression phase-current coff windows imports linker-failures
    )

    add_minic_regression_test(
        minic_import_catalog_duplicate_builtin_direct_coff
        SOURCES input/answer.c
        COMPILER_ARGS --windows-obj-backend;coff;--link-trace;-o;REPO:build/output/tmp_import_catalog_duplicate_builtin_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_catalog_duplicate_builtin_direct_coff.exe
        EXPECTED_EXIT 0
        CATALOG_FIXTURE tests/data/import_catalog_duplicate_builtin.txt
        ERROR_MARKERS "may not override built-in symbol" "fn_puts"
        LABELS regression phase-current coff windows imports linker-failures
    )

    add_minic_regression_test(
        minic_longlong_compare_direct_coff
        SOURCES input/tmp_longlong_compare.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_longlong_compare_direct_coff.exe
        OUTPUT_EXE build/output/tmp_longlong_compare_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows semantics
    )

    add_minic_regression_test(
        minic_longlong_divide_direct_coff
        SOURCES input/tmp_longlong_divide.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_longlong_divide_direct_coff.exe
        OUTPUT_EXE build/output/tmp_longlong_divide_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows semantics
    )

    add_minic_regression_test(
        minic_unsigned_compare_direct_coff
        SOURCES input/tmp_unsigned_compare.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_unsigned_compare_direct_coff.exe
        OUTPUT_EXE build/output/tmp_unsigned_compare_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows semantics
    )

    add_minic_regression_test(
        minic_bool_normalize_direct_coff
        SOURCES input/tmp_bool_normalize.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_bool_normalize_direct_coff.exe
        OUTPUT_EXE build/output/tmp_bool_normalize_direct_coff.exe
        EXPECTED_EXIT 1
        LABELS regression phase-current coff windows semantics
    )

    add_minic_regression_test(
        minic_double_math_direct_coff
        SOURCES input/tmp_double_math.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_double_math_direct_coff.exe
        OUTPUT_EXE build/output/tmp_double_math_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows semantics floating
    )

    add_minic_regression_test(
        minic_float_math_direct_coff
        SOURCES input/tmp_float_math.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_float_math_direct_coff.exe
        OUTPUT_EXE build/output/tmp_float_math_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows semantics floating
    )

    add_minic_regression_test(
        minic_mixed_int_double_direct_coff
        SOURCES input/tmp_mixed_int_double.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_mixed_int_double_direct_coff.exe
        OUTPUT_EXE build/output/tmp_mixed_int_double_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows semantics floating
    )

    add_minic_regression_test(
        minic_double_to_int_return_direct_coff
        SOURCES input/tmp_double_to_int_return.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_double_to_int_return_direct_coff.exe
        OUTPUT_EXE build/output/tmp_double_to_int_return_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows semantics floating
    )

    add_minic_regression_test(
        minic_global_int_array_direct_coff
        SOURCES input/tmp_else.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_else_direct_coff.exe
        OUTPUT_EXE build/output/tmp_else_direct_coff.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current coff windows initializers
    )

    add_minic_regression_test(
        minic_import_mixed_showcase_direct_coff
        SOURCES input/tmp_import_mixed_showcase.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_import_mixed_showcase_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_mixed_showcase_direct_coff.exe
        EXPECTED_EXIT 1
        LABELS regression phase-current coff windows imports
    )

    add_minic_regression_test(
        minic_import_putchar_direct_coff
        SOURCES input/tmp_import_putchar.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_import_putchar_direct_coff.exe
        OUTPUT_EXE build/output/tmp_import_putchar_direct_coff.exe
        EXPECTED_EXIT 1
        LABELS regression phase-current coff windows imports
    )

    add_minic_regression_test(
        minic_bad_arity_direct_coff
        SOURCES input/tmp_bad_arity.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_bad_arity_direct_coff.exe
        OUTPUT_EXE build/output/tmp_bad_arity_direct_coff.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "wrong number of arguments in function call"
        LABELS regression phase-current coff windows semantics
    )

    add_minic_regression_test(
        minic_bad_argument_type_direct_coff
        SOURCES input/tmp_cross_no_decl_main.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_cross_no_decl_main_direct_coff.exe
        OUTPUT_EXE build/output/tmp_cross_no_decl_main_direct_coff.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "argument type mismatch in function call"
        LABELS regression phase-current coff windows semantics
    )

    add_minic_regression_test(
        minic_bad_return_type_direct_coff
        SOURCES input/tmp_bad_void_var.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_bad_void_var_direct_coff.exe
        OUTPUT_EXE build/output/tmp_bad_void_var_direct_coff.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "return type mismatch"
        LABELS regression phase-current coff windows semantics
    )

    add_minic_regression_test(
        minic_array_too_many_elements_direct_coff
        SOURCES input/tmp_bss_global_int.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_bss_global_int_direct_coff.exe
        OUTPUT_EXE build/output/tmp_bss_global_int_direct_coff.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "too many elements in global array initializer"
        LABELS regression phase-current coff windows initializers
    )

    add_minic_regression_test(
        minic_array_bad_element_type_direct_coff
        SOURCES input/tmp_ptr_add.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_ptr_add_direct_coff.exe
        OUTPUT_EXE build/output/tmp_ptr_add_direct_coff.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "array initializer element type mismatch"
        LABELS regression phase-current coff windows initializers
    )

    add_minic_regression_test(
        minic_local_ptr_array_invalid_source_direct_coff
        SOURCES input/tmp_bss_global_array.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_bss_global_array_direct_coff.exe
        OUTPUT_EXE build/output/tmp_bss_global_array_direct_coff.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "local pointer array initializers currently support only function names"
        LABELS regression phase-current coff windows initializers
    )

    add_minic_regression_test(
        minic_function_ptr_invalid_init_direct_coff
        SOURCES input/tmp_function_ptr_invalid_init.c
        COMPILER_ARGS --windows-obj-backend;coff;-o;REPO:build/output/tmp_function_ptr_invalid_init_direct_coff.exe
        OUTPUT_EXE build/output/tmp_function_ptr_invalid_init_direct_coff.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "function names"
        LABELS regression phase-current coff windows relocations
    )

    add_minic_regression_test(
        minic_local_string_init
        SOURCES input/tmp_string_local.c
        OUTPUT_EXE build/output/tmp_string_local.exe
        EXPECTED_EXIT 65
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_windows_stack_args
        SOURCES input/tmp_four_args.c
        OUTPUT_EXE build/output/tmp_four_args.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current abi
    )

    add_minic_regression_test(
        minic_bad_arity
        SOURCES input/tmp_bad_arity.c
        OUTPUT_EXE build/output/tmp_bad_arity.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "wrong number of arguments in function call"
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_bad_argument_type
        SOURCES input/tmp_cross_no_decl_main.c
        OUTPUT_EXE build/output/tmp_cross_no_decl_main.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "argument type mismatch in function call"
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_bad_return_type
        SOURCES input/tmp_bad_void_var.c
        OUTPUT_EXE build/output/tmp_bad_void_var.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "return type mismatch"
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_mixed_integer_conversions
        SOURCES input/tmp_basic_types.c
        OUTPUT_EXE build/output/tmp_basic_types.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_unsigned_compare
        SOURCES input/tmp_unsigned_compare.c
        OUTPUT_EXE build/output/tmp_unsigned_compare.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_bool_normalize
        SOURCES input/tmp_bool_normalize.c
        OUTPUT_EXE build/output/tmp_bool_normalize.exe
        EXPECTED_EXIT 1
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_double_math
        SOURCES input/tmp_double_math.c
        OUTPUT_EXE build/output/tmp_double_math.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_float_math
        SOURCES input/tmp_float_math.c
        OUTPUT_EXE build/output/tmp_float_math.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_mixed_int_double
        SOURCES input/tmp_mixed_int_double.c
        OUTPUT_EXE build/output/tmp_mixed_int_double.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_double_to_int_return
        SOURCES input/tmp_double_to_int_return.c
        OUTPUT_EXE build/output/tmp_double_to_int_return.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_local_int_array_init
        SOURCES input/tmp_if.c
        OUTPUT_EXE build/output/tmp_if.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current initializers
    )

    add_minic_regression_test(
        minic_global_int_array_init
        SOURCES input/tmp_else.c
        OUTPUT_EXE build/output/tmp_else.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current initializers
    )

    add_minic_regression_test(
        minic_local_ptr_array_init
        SOURCES input/tmp_void_ptr.c
        OUTPUT_EXE build/output/tmp_void_ptr.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current initializers
    )

    add_minic_regression_test(
        minic_array_too_many_elements
        SOURCES input/tmp_bss_global_int.c
        OUTPUT_EXE build/output/tmp_bss_global_int.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "too many elements in global array initializer"
        LABELS regression phase-current initializers
    )

    add_minic_regression_test(
        minic_array_bad_element_type
        SOURCES input/tmp_ptr_add.c
        OUTPUT_EXE build/output/tmp_ptr_add.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "array initializer element type mismatch"
        LABELS regression phase-current initializers
    )

    add_minic_regression_test(
        minic_local_ptr_array_invalid_source
        SOURCES input/tmp_bss_global_array.c
        OUTPUT_EXE build/output/tmp_bss_global_array.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "local pointer array initializers currently support only function names"
        LABELS regression phase-current initializers
    )

    add_minic_regression_test(
        minic_bss_integrity
        SOURCES input/tmp_bss_integrity.c
        COMPILER_ARGS --link-trace --keep-obj
        OUTPUT_EXE build/output/tmp_bss_integrity.exe
        EXPECTED_EXIT 42
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "symbol gv_counter" "symbol gv_bytes" "symbol fn_main" "symbol mainCRTStartup"
        LABELS regression phase-current bss
    )

    add_minic_regression_test(
        minic_multifile_trace
        SOURCES input/tmp_multi_main.c input/tmp_multi_math.c
        COMPILER_ARGS --link-trace -o REPO:build/output/tmp_multi_trace.exe
        OUTPUT_EXE build/output/tmp_multi_trace.exe
        EXPECTED_EXIT 42
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "extern fn_add7" "symbol fn_main" "symbol mainCRTStartup" "symbol fn_add7" "summary sections=" "summary 2 objects" "summary 2 relocations"
        LABELS regression phase-current multifile
    )

    add_minic_regression_test(
        minic_global_ptr_to_global
        SOURCES input/tmp_global_ptr_to_global.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_global_ptr_to_global.exe
        EXPECTED_EXIT 42
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "symbol gv_x" "symbol gv_p"
        LABELS regression phase-current datareloc
    )

    add_minic_regression_test(
        minic_global_ptr_to_string
        SOURCES input/tmp_global_ptr_to_string.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_global_ptr_to_string.exe
        EXPECTED_EXIT 65
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "symbol gv_p"
        LABELS regression phase-current datareloc
    )

    add_minic_regression_test(
        minic_import_kernel32_pid
        SOURCES input/tmp_import_kernel32_pid.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_kernel32_pid.exe
        EXPECTED_EXIT 1
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] imports" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "extern fn_GetCurrentProcessId" "dll kernel32.dll: ExitProcess (source=builtin), fn_GetCurrentProcessId (source=builtin)" "fn_GetCurrentProcessId ->" "dll=kernel32.dll" "source=builtin"
        LABELS regression phase-current imports
    )

    add_minic_regression_test(
        minic_import_msvcrt_puts
        SOURCES input/tmp_import_msvcrt_puts.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_msvcrt_puts.exe
        EXPECTED_EXIT 1
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] imports" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "extern fn_puts" "dll msvcrt.dll: fn_puts" "fn_puts ->" "dll=msvcrt.dll"
        LABELS regression phase-current imports
    )

    add_minic_regression_test(
        minic_import_mixed_showcase
        SOURCES input/tmp_import_mixed_showcase.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_mixed_showcase.exe
        EXPECTED_EXIT 1
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] imports" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "extern fn_puts" "extern fn_GetCurrentProcessId" "dll kernel32.dll: ExitProcess (source=builtin), fn_GetCurrentProcessId (source=builtin)" "dll msvcrt.dll: fn_puts (source=builtin)"
        LABELS regression phase-current imports
    )

    add_minic_regression_test(
        minic_import_putchar
        SOURCES input/tmp_import_putchar.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_putchar.exe
        EXPECTED_EXIT 1
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] imports" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "extern fn_putchar" "dll msvcrt.dll: fn_putchar" "fn_putchar ->" "dll=msvcrt.dll"
        LABELS regression phase-current imports
    )

    add_minic_regression_test(
        minic_import_printf_simple
        SOURCES input/tmp_import_printf_simple.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_printf_simple.exe
        EXPECTED_EXIT 1
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] imports" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "extern fn_printf" "dll msvcrt.dll: fn_printf" "fn_printf ->" "dll=msvcrt.dll"
        LABELS regression phase-current imports
    )

    add_minic_regression_test(
        minic_import_msvcrt_triple
        SOURCES input/tmp_import_msvcrt_triple.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_msvcrt_triple.exe
        EXPECTED_EXIT 1
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] imports" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "extern fn_puts" "extern fn_putchar" "extern fn_printf" "dll msvcrt.dll:" "fn_puts ->" "fn_putchar ->" "fn_printf ->"
        LABELS regression phase-current imports
    )

    add_minic_regression_test(
        minic_import_file_backed
        SOURCES input/tmp_import_file_backed.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_file_backed.exe
        EXPECTED_EXIT 5
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] imports" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "extern fn_strlen" "dll msvcrt.dll: fn_strlen (source=file)" "fn_strlen ->" "dll=msvcrt.dll" "source=file"
        LABELS regression phase-current imports
    )

    add_minic_regression_test(
        minic_import_unresolved
        SOURCES input/tmp_import_unresolved.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_unresolved.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "unresolved external symbol: fn_MissingImport"
        LABELS regression phase-current imports linker-failures
    )

    add_minic_regression_test(
        minic_import_catalog_bad_row
        SOURCES input/answer.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_catalog_bad_row.exe
        EXPECTED_EXIT 0
        CATALOG_FIXTURE tests/data/import_catalog_bad_row.txt
        ERROR_MARKERS "invalid import catalog row"
        LABELS regression phase-current imports linker-failures
    )

    add_minic_regression_test(
        minic_import_catalog_duplicate_symbol
        SOURCES input/answer.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_catalog_duplicate_symbol.exe
        EXPECTED_EXIT 0
        CATALOG_FIXTURE tests/data/import_catalog_duplicate_symbol.txt
        ERROR_MARKERS "duplicate import catalog symbol" "fn_strlen"
        LABELS regression phase-current imports linker-failures
    )

    add_minic_regression_test(
        minic_import_catalog_duplicate_builtin
        SOURCES input/answer.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_catalog_duplicate_builtin.exe
        EXPECTED_EXIT 0
        CATALOG_FIXTURE tests/data/import_catalog_duplicate_builtin.txt
        ERROR_MARKERS "may not override built-in symbol" "fn_puts"
        LABELS regression phase-current imports linker-failures
    )

    add_minic_regression_test(
        minic_text_addr64_relocation
        SOURCES input/tmp_link_text_addr64_main.c input/tmp_link_text_addr64_helper.asm
        COMPILER_ARGS --link-trace -o REPO:build/output/tmp_link_text_addr64.exe
        OUTPUT_EXE build/output/tmp_link_text_addr64.exe
        EXPECTED_EXIT 42
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "symbol gv_value" "symbol fn_read_global_through_abs" "section=.text" "type=ADDR64"
        LABELS regression phase-current relocations nasm-only external-object
    )

    add_minic_regression_test(
        minic_duplicate_external_symbol
        SOURCES input/tmp_link_duplicate_main.c input/tmp_link_duplicate_one.asm input/tmp_link_duplicate_two.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_duplicate.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "duplicate external symbol: fn_dupe"
        LABELS regression phase-current linker-failures nasm-only external-object
    )

    add_minic_regression_test(
        minic_unsupported_text_relocation
        SOURCES input/tmp_link_bad_reloc_main.c input/tmp_link_bad_reloc_helper.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_bad_reloc.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "unsupported COFF" "section .text"
        LABELS regression phase-current linker-failures nasm-only external-object
    )

    add_minic_regression_test(
        minic_unsupported_target_section
        SOURCES input/tmp_link_bad_section_main.c input/tmp_link_bad_section_helper.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_bad_section.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "linker diagnostic:" ".pdata"
        LABELS regression phase-current linker-failures nasm-only external-object
    )

    add_minic_regression_test(
        minic_function_ptr_global
        SOURCES input/tmp_function_ptr_global.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_function_ptr_global.exe
        EXPECTED_EXIT 42
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "symbol gv_fn_ptr" "symbol fn_answer" "section=.data" "type=ADDR64"
        LABELS regression phase-current relocations
    )

    add_minic_regression_test(
        minic_function_ptr_table
        SOURCES input/tmp_function_ptr_table.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_function_ptr_table.exe
        EXPECTED_EXIT 2
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "symbol gv_fn_table" "symbol fn_one" "symbol fn_two" "section=.data" "type=ADDR64"
        LABELS regression phase-current relocations
    )

    add_minic_regression_test(
        minic_function_ptr_invalid_init
        SOURCES input/tmp_function_ptr_invalid_init.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_function_ptr_invalid_init.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "function names"
        LABELS regression phase-current relocations
    )

    add_minic_regression_test(
        minic_reloc_probe_global_ptr
        SOURCES input/tmp_reloc_probe_global_ptr.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_reloc_probe_global_ptr.exe
        EXPECTED_EXIT 42
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations" "[link] base relocations"
        OUTPUT_MARKERS "symbol gv_x" "symbol gv_p" "section=.data" "type=ADDR64"
        RELOC_PROBE build/Debug/pe_reloc_probe.exe
        RELOC_PROBE_ARGS --expect-reloc yes --delta 0x100000 --site 0x2000=0x3000
        LABELS regression phase-current relocations rebasing
    )

    add_minic_regression_test(
        minic_reloc_probe_function_ptr
        SOURCES input/tmp_reloc_probe_function_ptr.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_reloc_probe_function_ptr.exe
        EXPECTED_EXIT 42
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations" "[link] base relocations"
        OUTPUT_MARKERS "symbol gv_fn_ptr" "symbol fn_answer" "section=.data" "type=ADDR64"
        RELOC_PROBE build/Debug/pe_reloc_probe.exe
        RELOC_PROBE_ARGS --expect-reloc yes --delta 0x100000 --site 0x2000=0x1000
        LABELS regression phase-current relocations rebasing
    )

    add_minic_regression_test(
        minic_reloc_probe_string_ptr
        SOURCES input/tmp_reloc_probe_string_ptr.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_reloc_probe_string_ptr.exe
        EXPECTED_EXIT 65
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations" "[link] base relocations"
        OUTPUT_MARKERS "symbol gv_p" "section=.data" "type=ADDR64"
        RELOC_PROBE build/Debug/pe_reloc_probe.exe
        RELOC_PROBE_ARGS --expect-reloc yes --delta 0x100000 --site 0x2000=0x3000
        LABELS regression phase-current relocations rebasing
    )

    add_minic_regression_test(
        minic_reloc_no_absolute_data
        SOURCES input/tmp_reloc_no_absolute_data.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_reloc_no_absolute_data.exe
        EXPECTED_EXIT 7
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations" "[link] base relocations"
        RELOC_PROBE build/Debug/pe_reloc_probe.exe
        RELOC_PROBE_ARGS --expect-reloc no
        LABELS regression phase-current relocations rebasing
    )

    add_minic_regression_test(
        minic_linux_stack_args_asm
        SOURCES input/tmp_linux_stack_args.c
        COMPILER_ARGS --target;x86_64-linux;-S;--emit-asm;REPO:build/output/tmp_linux_stack_args.asm
        OUTPUT_EXE build/output/tmp_linux_stack_args.asm
        EXPECTED_EXIT 0
        CHECK_FILE build/output/tmp_linux_stack_args.asm
        FILE_MARKERS
            "global _start"
            "mov r8d, dword [rsp+48]"
            "mov r9d, dword [rsp+56]"
            "mov eax, dword [rbp+16]"
            "mov eax, dword [rbp+24]"
            "sub rsp, 64"
            "add rsp, 64"
        SKIP_RUN
        LABELS regression phase-current linux abi
    )

    add_minic_regression_test(
        minic_linux_answer_link
        SOURCES input/answer.c
        COMPILER_ARGS --target;x86_64-linux;-o;REPO:build/output/answer_linux
        OUTPUT_EXE build/output/answer_linux
        EXPECTED_EXIT 42
        RUN_WITH_WSL
        SKIP_IF_WSL_UNAVAILABLE
        LABELS regression phase-current linux linux-link
    )

    add_minic_regression_test(
        minic_linux_stack_args_link
        SOURCES input/tmp_linux_stack_args.c
        COMPILER_ARGS --target;x86_64-linux;-o;REPO:build/output/tmp_linux_stack_args
        OUTPUT_EXE build/output/tmp_linux_stack_args
        EXPECTED_EXIT 42
        RUN_WITH_WSL
        SKIP_IF_WSL_UNAVAILABLE
        LABELS regression phase-current linux linux-link abi
    )

    add_minic_regression_test(
        minic_linux_global_array_link
        SOURCES input/tmp_else.c
        COMPILER_ARGS --target;x86_64-linux;-o;REPO:build/output/tmp_else_linux
        OUTPUT_EXE build/output/tmp_else_linux
        EXPECTED_EXIT 42
        RUN_WITH_WSL
        SKIP_IF_WSL_UNAVAILABLE
        LABELS regression phase-current linux linux-link initializers
    )

    add_minic_regression_test(
        minic_linux_rejects_windows_obj
        SOURCES input/answer.c input/tmp_link_text_addr64_helper.asm
        COMPILER_ARGS --target;x86_64-linux
        OUTPUT_EXE build/output/linux_rejects_windows_obj
        EXPECTED_EXIT 0
        ERROR_MARKERS "target x86_64-linux only accepts ELF .o object inputs for final linking"
        LABELS regression phase-current linux linux-link linker-failures nasm-only external-object
    )

    add_minic_regression_test(
        minic_invalid_small_object
        SOURCES input/answer.c input/tmp_invalid_small.obj
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_invalid_small.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "linker diagnostic:" "object file is too small"
        LABELS regression phase-current linker-failures external-object
    )

    add_minic_regression_test(
        minic_missing_entry_symbol
        SOURCES input/tmp_link_missing_entry.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_missing_entry.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "missing entry symbol: mainCRTStartup" "defines main"
        LABELS regression phase-current linker-failures nasm-only external-object
    )

    add_minic_regression_test(
        minic_missing_text_section
        SOURCES input/tmp_link_data_only.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_data_only.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "linked objects do not contain a .text section"
        LABELS regression phase-current linker-failures nasm-only external-object
    )
endif()
