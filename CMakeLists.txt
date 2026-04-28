cmake_minimum_required(VERSION 3.21)

project(minic VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/Driver.cpp
    src/Driver.h
    src/Toolchain.cpp
    src/Toolchain.h
    src/PeLinker.cpp
    src/PeLinker.h
    src/CodeGenerator.cpp
    src/CodeGenerator.h
    src/Semantics.cpp
    src/Semantics.h
    src/Parser.cpp
    src/Parser.h
    src/Lexer.cpp
    src/Lexer.h
    src/Token.cpp
    src/Token.h
    src/Target.h
    src/Ast.h
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        NOMINMAX
)

if (WIN32)
    function(add_minic_regression_test test_name)
        set(options)
        set(oneValueArgs EXPECTED_EXIT OUTPUT_EXE)
        set(multiValueArgs SOURCES COMPILER_ARGS TRACE_MARKERS OUTPUT_MARKERS ERROR_MARKERS LABELS)
        cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

        string(JOIN ";" source_files ${TEST_SOURCES})
        string(JOIN ";" compiler_args ${TEST_COMPILER_ARGS})
        string(JOIN ";" trace_markers ${TEST_TRACE_MARKERS})
        string(JOIN ";" output_markers ${TEST_OUTPUT_MARKERS})
        string(JOIN ";" error_markers ${TEST_ERROR_MARKERS})
        set(expect_compiler_failure)
        if (TEST_ERROR_MARKERS)
            set(expect_compiler_failure -ExpectCompilerFailure)
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
                ${expect_compiler_failure}
                -RequiredErrorMarkers "${error_markers}"
        )

        if (TEST_LABELS)
            set_tests_properties(${test_name} PROPERTIES LABELS "${TEST_LABELS}")
        endif()
    endfunction()

    add_minic_regression_test(
        minic_answer_baseline
        SOURCES input/answer.c
        OUTPUT_EXE build/output/answer.exe
        EXPECTED_EXIT 42
        LABELS regression phase-current
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
        OUTPUT_MARKERS "extern fn_add7" "symbol fn_main" "symbol mainCRTStartup" "symbol fn_add7"
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
        OUTPUT_MARKERS "extern fn_GetCurrentProcessId" "dll kernel32.dll: ExitProcess, fn_GetCurrentProcessId" "fn_GetCurrentProcessId ->" "dll=kernel32.dll"
        LABELS regression phase-current imports
    )

    add_minic_regression_test(
        minic_import_msvcrt_puts
        SOURCES input/tmp_import_msvcrt_puts.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_msvcrt_puts.exe
        EXPECTED_EXIT 0
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
        OUTPUT_MARKERS "extern fn_puts" "extern fn_GetCurrentProcessId" "dll kernel32.dll: ExitProcess, fn_GetCurrentProcessId" "dll msvcrt.dll: fn_puts"
        LABELS regression phase-current imports
    )

    add_minic_regression_test(
        minic_import_unresolved
        SOURCES input/tmp_import_unresolved.c
        COMPILER_ARGS --link-trace
        OUTPUT_EXE build/output/tmp_import_unresolved.exe
        EXPECTED_EXIT 0
        ERROR_MARKERS "unresolved external symbol: fn_MissingImport"
        LABELS regression phase-current imports
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
endif()
