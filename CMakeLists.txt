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
        set(multiValueArgs SOURCES COMPILER_ARGS TRACE_MARKERS OUTPUT_MARKERS LABELS)
        cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

        string(JOIN ";" source_files ${TEST_SOURCES})
        string(JOIN ";" compiler_args ${TEST_COMPILER_ARGS})
        string(JOIN ";" trace_markers ${TEST_TRACE_MARKERS})
        string(JOIN ";" output_markers ${TEST_OUTPUT_MARKERS})

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
endif()
