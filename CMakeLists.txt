cmake_minimum_required(VERSION 3.21)

project(minic VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

if (WIN32)
    set(MINIC_VCPKG_ROOT "$ENV{VCPKG_ROOT}")
    if (NOT MINIC_VCPKG_ROOT AND EXISTS "D:/vcpkg/installed/x64-windows/share/gtest")
        set(MINIC_VCPKG_ROOT "D:/vcpkg")
    endif()
    if (MINIC_VCPKG_ROOT)
        list(APPEND CMAKE_PREFIX_PATH "${MINIC_VCPKG_ROOT}/installed/x64-windows")
    endif()
    find_package(GTest CONFIG REQUIRED)
endif()

set(MINIC_CORE_SOURCES
    src/support/AssemblerBackend.cpp
    src/support/AssemblerBackend.h
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
    src/backend/CodeGenerator.cpp
    src/backend/CodeGenerator.h
    src/backend/Optimizer.cpp
    src/backend/Optimizer.h
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

if (WIN32)
    add_executable(minic_gtests
        tests/minic_regression_tests.cpp
        tests/regression_test_utils.cpp
        tests/regression_test_utils.h
    )
    target_link_libraries(minic_gtests PRIVATE GTest::gtest GTest::gtest_main)
    target_compile_definitions(minic_gtests
        PRIVATE
            NOMINMAX
            MINIC_TEST_REPO_ROOT="${PROJECT_SOURCE_DIR}"
            MINIC_TEST_COMPILER_PATH="$<TARGET_FILE:${PROJECT_NAME}>"
            MINIC_TEST_LINKER_PATH="$<TARGET_FILE:minic-link>"
            MINIC_TEST_RELOC_PROBE_PATH="$<TARGET_FILE:pe_reloc_probe>"
    )
    add_custom_command(
        TARGET minic_gtests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:minic_gtests>
            $<TARGET_FILE_DIR:minic_gtests>
        COMMAND_EXPAND_LISTS
    )
endif()

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
    target_include_directories(minic_gtests PRIVATE ${PROJECT_SOURCE_DIR}/tests)
endif()

if (WIN32)
    function(add_minic_regression_test test_name)
        set(options SKIP_RUN RUN_WITH_WSL SKIP_IF_WSL_UNAVAILABLE PREASSEMBLE_ASM_INPUTS)
        set(oneValueArgs EXPECTED_EXIT OUTPUT_EXE CHECK_FILE RELOC_PROBE CATALOG_FIXTURE)
        set(multiValueArgs SOURCES COMPILER_ARGS TRACE_MARKERS OUTPUT_MARKERS FILE_MARKERS ERROR_MARKERS LABELS RELOC_PROBE_ARGS)
        cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

        add_test(
            NAME ${test_name}
            COMMAND $<TARGET_FILE:minic_gtests>
                --gtest_color=no
                --gtest_filter=Compiler/CompilerRegression.Run/${test_name}
        )

        if (TEST_LABELS)
            set_tests_properties(${test_name} PROPERTIES LABELS "${TEST_LABELS}")
        endif()
    endfunction()

    add_test(
        NAME minic_standalone_linker_smoke
        COMMAND $<TARGET_FILE:minic_gtests>
            --gtest_color=no
            --gtest_filter=Linker/StandaloneLinkerRegression.Run/minic_standalone_linker_smoke
    )
    set_tests_properties(minic_standalone_linker_smoke PROPERTIES LABELS "regression;phase-current;linker")

    add_test(
        NAME minic_standalone_linker_multifile
        COMMAND $<TARGET_FILE:minic_gtests>
            --gtest_color=no
            --gtest_filter=Linker/StandaloneLinkerRegression.Run/minic_standalone_linker_multifile
    )
    set_tests_properties(minic_standalone_linker_multifile PROPERTIES LABELS "regression;phase-current;linker;multifile")

    add_test(
        NAME minic_standalone_linker_multifile_single_job
        COMMAND $<TARGET_FILE:minic_gtests>
            --gtest_color=no
            --gtest_filter=Linker/StandaloneLinkerRegression.Run/minic_standalone_linker_multifile_single_job
    )
    set_tests_properties(minic_standalone_linker_multifile_single_job PROPERTIES LABELS "regression;phase-current;linker;multifile")

    add_test(
        NAME minic_standalone_linker_linux_smoke
        COMMAND $<TARGET_FILE:minic_gtests>
            --gtest_color=no
            --gtest_filter=Linker/StandaloneLinkerRegression.Run/minic_standalone_linker_linux_smoke
    )
    set_tests_properties(minic_standalone_linker_linux_smoke PROPERTIES LABELS "regression;phase-current;linker;linux;linux-link")

    add_test(
        NAME minic_standalone_linker_linux_multifile
        COMMAND $<TARGET_FILE:minic_gtests>
            --gtest_color=no
            --gtest_filter=Linker/StandaloneLinkerRegression.Run/minic_standalone_linker_linux_multifile
    )
    set_tests_properties(minic_standalone_linker_linux_multifile PROPERTIES LABELS "regression;phase-current;linker;linux;linux-link;multifile")

    add_minic_regression_test(
        minic_answer_baseline
        SOURCES input/answer.c
        OUTPUT_EXE build/output/answer.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current
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
        minic_function_pointer_parameter
        SOURCES input/tmp_fnptr_param.c
        OUTPUT_EXE build/output/tmp_fnptr_param.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_local_struct_layout
        SOURCES input/tmp_struct_local.c
        OUTPUT_EXE build/output/tmp_struct_local.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_struct_arrow_access
        SOURCES input/tmp_struct_arrow.c
        OUTPUT_EXE build/output/tmp_struct_arrow.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_struct_local_init
        SOURCES input/tmp_struct_local_init.c
        OUTPUT_EXE build/output/tmp_struct_local_init.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_struct_global_bss
        SOURCES input/tmp_struct_global_bss.c
        OUTPUT_EXE build/output/tmp_struct_global_bss.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics bss
    )

    add_minic_regression_test(
        minic_struct_global_init
        SOURCES input/tmp_struct_global_init.c
        OUTPUT_EXE build/output/tmp_struct_global_init.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_struct_global_ptr_init
        SOURCES input/tmp_struct_global_ptr_init.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_struct_global_ptr_init.exe
        EXPECTED_EXIT 42
        TRACE_MARKERS "[link] input objects" "[link] merged sections" "[link] resolved symbols" "[link] relocations"
        OUTPUT_MARKERS "symbol gv_g_value" "symbol gv_g_holder" "section=.data" "type=ADDR64"
        LABELS regression phase-current semantics datareloc
    )

    add_minic_regression_test(
        minic_struct_by_value_small
        SOURCES input/tmp_struct_by_value_small.c
        OUTPUT_EXE build/output/tmp_struct_by_value_small.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics abi
    )

    add_minic_regression_test(
        minic_struct_by_value_large_arg
        SOURCES input/tmp_struct_by_value_large_arg.c
        OUTPUT_EXE build/output/tmp_struct_by_value_large_arg.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics abi
    )

    add_minic_regression_test(
        minic_struct_by_value_large_return_asm
        SOURCES input/tmp_struct_by_value_large_return.c
        COMPILER_ARGS -S --emit-asm REPO:build/output/tmp_struct_by_value_large_return.asm
        OUTPUT_EXE build/output/tmp_struct_by_value_large_return.asm
        EXPECTED_EXIT 0
        CHECK_FILE build/output/tmp_struct_by_value_large_return.asm
        FILE_MARKERS "mov qword [rbp-24], rcx" "mov rcx, qword [rbp-24]"
        SKIP_RUN
        LABELS regression phase-current abi backend
    )

    add_minic_regression_test(
        minic_struct_by_value_large_return
        SOURCES input/tmp_struct_by_value_large_return.c
        OUTPUT_EXE build/output/tmp_struct_by_value_large_return.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current semantics abi
    )

    add_minic_regression_test(
        minic_struct_init_too_many
        SOURCES input/tmp_struct_init_too_many.c
        OUTPUT_EXE build/output/tmp_struct_init_too_many.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "too many elements in global struct initializer"
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_struct_init_bad_type
        SOURCES input/tmp_struct_init_bad_type.c
        OUTPUT_EXE build/output/tmp_struct_init_bad_type.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "struct initializer element type mismatch"
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_struct_init_nested_unsupported
        SOURCES input/tmp_struct_init_nested_unsupported.c
        OUTPUT_EXE build/output/tmp_struct_init_nested_unsupported.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "struct initializers do not yet support array or struct members"
        LABELS regression phase-current semantics
    )

    add_minic_regression_test(
        minic_struct_linux_by_value_unsupported
        SOURCES input/tmp_struct_linux_by_value_unsupported.c
        COMPILER_ARGS --target x86_64-linux
        OUTPUT_EXE build/output/tmp_struct_linux_by_value_unsupported
        EXPECTED_EXIT 0
        ERROR_MARKERS "by-value struct parameters are not supported yet on x86_64-linux"
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
        minic_multifile_jobs
        SOURCES input/tmp_multi_main.c input/tmp_multi_math.c
        COMPILER_ARGS -j 2 -o REPO:build/output/tmp_multi_jobs.exe
        OUTPUT_EXE build/output/tmp_multi_jobs.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current multifile
    )

    add_minic_regression_test(
        minic_direct_asm_input_windows
        SOURCES input/tmp_direct_asm_main.c input/tmp_direct_asm_helper.asm
        COMPILER_ARGS -o;REPO:build/output/tmp_direct_asm_input.exe
        OUTPUT_EXE build/output/tmp_direct_asm_input.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current linker multifile
    )

    add_minic_regression_test(
        minic_direct_asm_input_linux
        SOURCES input/tmp_direct_asm_main.c input/tmp_direct_asm_helper.asm
        COMPILER_ARGS --target;x86_64-linux;-o;REPO:build/output/tmp_direct_asm_input_linux
        OUTPUT_EXE build/output/tmp_direct_asm_input_linux
        EXPECTED_EXIT 42
        RUN_WITH_WSL
        SKIP_IF_WSL_UNAVAILABLE
        LABELS regression phase-current linux linux-link multifile
    )

    add_minic_regression_test(
        minic_constant_fold_asm
        SOURCES input/tmp_const_fold.c
        COMPILER_ARGS -S;--emit-asm;REPO:build/output/tmp_const_fold.asm
        OUTPUT_EXE build/output/tmp_const_fold.asm
        EXPECTED_EXIT 0
        CHECK_FILE build/output/tmp_const_fold.asm
        FILE_MARKERS "mov eax, 43"
        SKIP_RUN
        LABELS regression phase-current optimizer
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
        LABELS regression phase-current relocations
    )

    add_minic_regression_test(
        minic_duplicate_external_symbol
        SOURCES input/tmp_link_duplicate_main.c input/tmp_link_duplicate_one.asm input/tmp_link_duplicate_two.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_duplicate.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "duplicate external symbol: fn_dupe"
        LABELS regression phase-current linker-failures
    )

    add_minic_regression_test(
        minic_unsupported_text_relocation
        SOURCES input/tmp_link_bad_reloc_main.c input/tmp_link_bad_reloc_helper.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_bad_reloc.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "unsupported COFF" "section .text"
        LABELS regression phase-current linker-failures
    )

    add_minic_regression_test(
        minic_unsupported_target_section
        SOURCES input/tmp_link_bad_section_main.c input/tmp_link_bad_section_helper.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_bad_section.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "linker diagnostic:" ".pdata"
        LABELS regression phase-current linker-failures
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
        PREASSEMBLE_ASM_INPUTS
        ERROR_MARKERS "target x86_64-linux only accepts ELF .o object inputs for final linking"
        LABELS regression phase-current linux linux-link linker-failures
    )

    add_minic_regression_test(
        minic_invalid_small_object
        SOURCES input/answer.c input/tmp_invalid_small.obj
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_invalid_small.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "linker diagnostic:" "object file is too small"
        LABELS regression phase-current linker-failures
    )

    add_minic_regression_test(
        minic_missing_entry_symbol
        SOURCES input/tmp_link_missing_entry.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_missing_entry.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "missing entry symbol: mainCRTStartup" "defines main"
        LABELS regression phase-current linker-failures
    )

    add_minic_regression_test(
        minic_missing_text_section
        SOURCES input/tmp_link_data_only.asm
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_link_data_only.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "linked objects do not contain a .text section"
        LABELS regression phase-current linker-failures
    )

    function(append_minic_test_labels test_name)
        get_property(existing_labels TEST ${test_name} PROPERTY LABELS)
        if (existing_labels)
            set(updated_labels "${existing_labels};${ARGN}")
        else()
            set(updated_labels "${ARGN}")
        endif()
        set_tests_properties(${test_name} PROPERTIES LABELS "${updated_labels}")
    endfunction()

    set(MINIC_FRONTEND_TESTS
        minic_local_string_init
        minic_bad_arity
        minic_bad_argument_type
        minic_bad_return_type
        minic_mixed_integer_conversions
        minic_function_pointer_parameter
        minic_local_struct_layout
        minic_struct_arrow_access
        minic_struct_local_init
        minic_struct_global_bss
        minic_struct_global_init
        minic_struct_global_ptr_init
        minic_struct_by_value_small
        minic_struct_by_value_large_arg
        minic_struct_by_value_large_return_asm
        minic_struct_by_value_large_return
        minic_struct_init_too_many
        minic_struct_init_bad_type
        minic_struct_init_nested_unsupported
        minic_struct_linux_by_value_unsupported
        minic_local_int_array_init
        minic_global_int_array_init
        minic_local_ptr_array_init
        minic_array_too_many_elements
        minic_array_bad_element_type
        minic_local_ptr_array_invalid_source
    )
    foreach(test_name IN LISTS MINIC_FRONTEND_TESTS)
        append_minic_test_labels(${test_name} frontend)
    endforeach()

    set(MINIC_BACKEND_TESTS
        minic_answer_baseline
        minic_windows_stack_args
        minic_bss_integrity
        minic_multifile_trace
        minic_multifile_jobs
        minic_direct_asm_input_windows
        minic_direct_asm_input_linux
        minic_constant_fold_asm
        minic_struct_global_ptr_init
        minic_struct_by_value_small
        minic_struct_by_value_large_arg
        minic_struct_by_value_large_return_asm
        minic_struct_by_value_large_return
        minic_global_ptr_to_global
        minic_global_ptr_to_string
        minic_text_addr64_relocation
        minic_function_ptr_global
        minic_function_ptr_table
        minic_function_ptr_invalid_init
        minic_linux_stack_args_asm
        minic_linux_answer_link
        minic_linux_stack_args_link
        minic_linux_global_array_link
        minic_linux_rejects_windows_obj
    )
    foreach(test_name IN LISTS MINIC_BACKEND_TESTS)
        append_minic_test_labels(${test_name} backend)
    endforeach()

    set(MINIC_LINKER_TESTS
        minic_standalone_linker_smoke
        minic_standalone_linker_multifile
        minic_standalone_linker_multifile_single_job
        minic_standalone_linker_linux_smoke
        minic_standalone_linker_linux_multifile
        minic_bss_integrity
        minic_multifile_trace
        minic_direct_asm_input_windows
        minic_direct_asm_input_linux
        minic_import_kernel32_pid
        minic_import_msvcrt_puts
        minic_import_mixed_showcase
        minic_import_putchar
        minic_import_printf_simple
        minic_import_msvcrt_triple
        minic_import_file_backed
        minic_import_unresolved
        minic_import_catalog_bad_row
        minic_import_catalog_duplicate_symbol
        minic_import_catalog_duplicate_builtin
        minic_text_addr64_relocation
        minic_duplicate_external_symbol
        minic_unsupported_text_relocation
        minic_unsupported_target_section
        minic_function_ptr_global
        minic_function_ptr_table
        minic_function_ptr_invalid_init
        minic_reloc_probe_global_ptr
        minic_reloc_probe_function_ptr
        minic_reloc_probe_string_ptr
        minic_reloc_no_absolute_data
        minic_linux_answer_link
        minic_linux_stack_args_link
        minic_linux_global_array_link
        minic_linux_rejects_windows_obj
        minic_invalid_small_object
        minic_missing_entry_symbol
        minic_missing_text_section
    )
    foreach(test_name IN LISTS MINIC_LINKER_TESTS)
        append_minic_test_labels(${test_name} linker)
    endforeach()

    append_minic_test_labels(minic_constant_fold_asm optimizer)
endif()
