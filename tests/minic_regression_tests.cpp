#include "regression_test_utils.h"

#include <gtest/gtest.h>

void PrintTo(const CompilerRegressionCase &testCase, std::ostream *os) {
    *os << testCase.name;
}

void PrintTo(const LinkerRegressionCase &testCase, std::ostream *os) {
    *os << testCase.name;
}

namespace {

std::vector<CompilerRegressionCase> compilerCases() {
    return {
        {"minic_answer_baseline", {"input/answer.c"}, {}, "build/output/answer.exe", 42},
        {"minic_local_string_init", {"input/tmp_string_local.c"}, {}, "build/output/tmp_string_local.exe", 65},
        {"minic_windows_stack_args", {"input/tmp_four_args.c"}, {}, "build/output/tmp_four_args.exe", 42},
        {"minic_bad_arity", {"input/tmp_bad_arity.c"}, {}, "build/output/tmp_bad_arity.exe", 0, {}, {}, {"wrong number of arguments in function call"}, "", {}, "", "", {}, true},
        {"minic_bad_argument_type", {"input/tmp_cross_no_decl_main.c"}, {}, "build/output/tmp_cross_no_decl_main.exe", 0, {}, {}, {"argument type mismatch in function call"}, "", {}, "", "", {}, true},
        {"minic_bad_return_type", {"input/tmp_bad_void_var.c"}, {}, "build/output/tmp_bad_void_var.exe", 0, {}, {}, {"return type mismatch"}, "", {}, "", "", {}, true},
        {"minic_mixed_integer_conversions", {"input/tmp_basic_types.c"}, {}, "build/output/tmp_basic_types.exe", 42},
        {"minic_function_pointer_parameter", {"input/tmp_fnptr_param.c"}, {}, "build/output/tmp_fnptr_param.exe", 42},
        {"minic_local_struct_layout", {"input/tmp_struct_local.c"}, {}, "build/output/tmp_struct_local.exe", 42},
        {"minic_struct_arrow_access", {"input/tmp_struct_arrow.c"}, {}, "build/output/tmp_struct_arrow.exe", 42},
        {"minic_struct_local_init", {"input/tmp_struct_local_init.c"}, {}, "build/output/tmp_struct_local_init.exe", 42},
        {"minic_struct_global_bss", {"input/tmp_struct_global_bss.c"}, {}, "build/output/tmp_struct_global_bss.exe", 42},
        {"minic_struct_global_init", {"input/tmp_struct_global_init.c"}, {}, "build/output/tmp_struct_global_init.exe", 42},
        {"minic_struct_global_ptr_init", {"input/tmp_struct_global_ptr_init.c"}, {"--link-trace"}, "build/output/tmp_struct_global_ptr_init.exe", 42,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations"},
            {"symbol gv_g_value", "symbol gv_g_holder", "section=.data", "type=ADDR64"}},
        {"minic_struct_by_value_small", {"input/tmp_struct_by_value_small.c"}, {}, "build/output/tmp_struct_by_value_small.exe", 42},
        {"minic_struct_by_value_large_arg", {"input/tmp_struct_by_value_large_arg.c"}, {}, "build/output/tmp_struct_by_value_large_arg.exe", 42},
        {"minic_struct_by_value_large_return_asm", {"input/tmp_struct_by_value_large_return.c"}, {"-S", "--emit-asm", "REPO:build/output/tmp_struct_by_value_large_return.asm"}, "build/output/tmp_struct_by_value_large_return.asm", 0,
            {}, {}, {}, "build/output/tmp_struct_by_value_large_return.asm",
            {"mov qword [rbp-24], rcx", "mov rcx, qword [rbp-24]"},
            "", "", {}, false, false, false, true},
        {"minic_struct_by_value_large_return", {"input/tmp_struct_by_value_large_return.c"}, {}, "build/output/tmp_struct_by_value_large_return.exe", 42},
        {"minic_struct_init_too_many", {"input/tmp_struct_init_too_many.c"}, {}, "build/output/tmp_struct_init_too_many.exe", 0, {}, {}, {"too many elements in global struct initializer"}, "", {}, "", "", {}, true},
        {"minic_struct_init_bad_type", {"input/tmp_struct_init_bad_type.c"}, {}, "build/output/tmp_struct_init_bad_type.exe", 0, {}, {}, {"struct initializer element type mismatch"}, "", {}, "", "", {}, true},
        {"minic_struct_init_nested_unsupported", {"input/tmp_struct_init_nested_unsupported.c"}, {}, "build/output/tmp_struct_init_nested_unsupported.exe", 0, {}, {}, {"struct initializers do not yet support array or struct members"}, "", {}, "", "", {}, true},
        {"minic_struct_linux_by_value_unsupported", {"input/tmp_struct_linux_by_value_unsupported.c"}, {"--target", "x86_64-linux"}, "build/output/tmp_struct_linux_by_value_unsupported", 0, {}, {}, {"by-value struct parameters are not supported yet on x86_64-linux"}, "", {}, "", "", {}, true},
        {"minic_local_int_array_init", {"input/tmp_if.c"}, {}, "build/output/tmp_if.exe", 42},
        {"minic_global_int_array_init", {"input/tmp_else.c"}, {}, "build/output/tmp_else.exe", 42},
        {"minic_local_ptr_array_init", {"input/tmp_void_ptr.c"}, {}, "build/output/tmp_void_ptr.exe", 42},
        {"minic_array_too_many_elements", {"input/tmp_bss_global_int.c"}, {}, "build/output/tmp_bss_global_int.exe", 0, {}, {}, {"too many elements in global array initializer"}, "", {}, "", "", {}, true},
        {"minic_array_bad_element_type", {"input/tmp_ptr_add.c"}, {}, "build/output/tmp_ptr_add.exe", 0, {}, {}, {"array initializer element type mismatch"}, "", {}, "", "", {}, true},
        {"minic_local_ptr_array_invalid_source", {"input/tmp_bss_global_array.c"}, {}, "build/output/tmp_bss_global_array.exe", 0, {}, {}, {"local pointer array initializers currently support only function names"}, "", {}, "", "", {}, true},
        {"minic_bss_integrity", {"input/tmp_bss_integrity.c"}, {"--link-trace", "--keep-obj"}, "build/output/tmp_bss_integrity.exe", 42,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations"},
            {"symbol gv_counter", "symbol gv_bytes", "symbol fn_main", "symbol mainCRTStartup"}},
        {"minic_multifile_trace", {"input/tmp_multi_main.c", "input/tmp_multi_math.c"}, {"--link-trace", "-o", "REPO:build/output/tmp_multi_trace.exe"}, "build/output/tmp_multi_trace.exe", 42,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations"},
            {"extern fn_add7", "symbol fn_main", "symbol mainCRTStartup", "symbol fn_add7", "summary sections=", "summary 2 objects", "summary 2 relocations"}},
        {"minic_multifile_jobs", {"input/tmp_multi_main.c", "input/tmp_multi_math.c"}, {"-j", "2", "-o", "REPO:build/output/tmp_multi_jobs.exe"}, "build/output/tmp_multi_jobs.exe", 42},
        {"minic_direct_asm_input_windows", {"input/tmp_direct_asm_main.c", "input/tmp_direct_asm_helper.asm"}, {"-o", "REPO:build/output/tmp_direct_asm_input.exe"}, "build/output/tmp_direct_asm_input.exe", 42},
        {"minic_direct_asm_input_linux", {"input/tmp_direct_asm_main.c", "input/tmp_direct_asm_helper.asm"}, {"--target", "x86_64-linux", "-o", "REPO:build/output/tmp_direct_asm_input_linux"}, "build/output/tmp_direct_asm_input_linux", 42, {}, {}, {}, "", {}, "", "", {}, false, true, true},
        {"minic_constant_fold_asm", {"input/tmp_const_fold.c"}, {"-S", "--emit-asm", "REPO:build/output/tmp_const_fold.asm"}, "build/output/tmp_const_fold.asm", 0, {}, {}, {}, "build/output/tmp_const_fold.asm", {"mov eax, 43"}, "", "", {}, false, false, false, true},
        {"minic_global_ptr_to_global", {"input/tmp_global_ptr_to_global.c"}, {"--link-trace"}, "build/output/tmp_global_ptr_to_global.exe", 42,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations"},
            {"symbol gv_x", "symbol gv_p"}},
        {"minic_global_ptr_to_string", {"input/tmp_global_ptr_to_string.c"}, {"--link-trace"}, "build/output/tmp_global_ptr_to_string.exe", 65,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations"},
            {"symbol gv_p"}},
        {"minic_import_kernel32_pid", {"input/tmp_import_kernel32_pid.c"}, {"--link-trace"}, "build/output/tmp_import_kernel32_pid.exe", 1,
            {"[link] input objects", "[link] merged sections", "[link] imports", "[link] resolved symbols", "[link] relocations"},
            {"extern fn_GetCurrentProcessId", "dll kernel32.dll: ExitProcess (source=builtin), fn_GetCurrentProcessId (source=builtin)", "fn_GetCurrentProcessId ->", "dll=kernel32.dll", "source=builtin"}},
        {"minic_import_msvcrt_puts", {"input/tmp_import_msvcrt_puts.c"}, {"--link-trace"}, "build/output/tmp_import_msvcrt_puts.exe", 1,
            {"[link] input objects", "[link] merged sections", "[link] imports", "[link] resolved symbols", "[link] relocations"},
            {"extern fn_puts", "dll msvcrt.dll: fn_puts", "fn_puts ->", "dll=msvcrt.dll"}},
        {"minic_import_mixed_showcase", {"input/tmp_import_mixed_showcase.c"}, {"--link-trace"}, "build/output/tmp_import_mixed_showcase.exe", 1,
            {"[link] input objects", "[link] merged sections", "[link] imports", "[link] resolved symbols", "[link] relocations"},
            {"extern fn_puts", "extern fn_GetCurrentProcessId", "dll kernel32.dll: ExitProcess (source=builtin), fn_GetCurrentProcessId (source=builtin)", "dll msvcrt.dll: fn_puts (source=builtin)"}},
        {"minic_import_putchar", {"input/tmp_import_putchar.c"}, {"--link-trace"}, "build/output/tmp_import_putchar.exe", 1,
            {"[link] input objects", "[link] merged sections", "[link] imports", "[link] resolved symbols", "[link] relocations"},
            {"extern fn_putchar", "dll msvcrt.dll: fn_putchar", "fn_putchar ->", "dll=msvcrt.dll"}},
        {"minic_import_printf_simple", {"input/tmp_import_printf_simple.c"}, {"--link-trace"}, "build/output/tmp_import_printf_simple.exe", 1,
            {"[link] input objects", "[link] merged sections", "[link] imports", "[link] resolved symbols", "[link] relocations"},
            {"extern fn_printf", "dll msvcrt.dll: fn_printf", "fn_printf ->", "dll=msvcrt.dll"}},
        {"minic_import_msvcrt_triple", {"input/tmp_import_msvcrt_triple.c"}, {"--link-trace"}, "build/output/tmp_import_msvcrt_triple.exe", 1,
            {"[link] input objects", "[link] merged sections", "[link] imports", "[link] resolved symbols", "[link] relocations"},
            {"extern fn_puts", "extern fn_putchar", "extern fn_printf", "dll msvcrt.dll:", "fn_puts ->", "fn_putchar ->", "fn_printf ->"}},
        {"minic_import_file_backed", {"input/tmp_import_file_backed.c"}, {"--link-trace"}, "build/output/tmp_import_file_backed.exe", 5,
            {"[link] input objects", "[link] merged sections", "[link] imports", "[link] resolved symbols", "[link] relocations"},
            {"extern fn_strlen", "dll msvcrt.dll: fn_strlen (source=file)", "fn_strlen ->", "dll=msvcrt.dll", "source=file"}},
        {"minic_import_unresolved", {"input/tmp_import_unresolved.c"}, {"--link-trace"}, "build/output/tmp_import_unresolved.exe", 0, {}, {}, {"unresolved external symbol: fn_MissingImport"}, "", {}, "", "", {}, true},
        {"minic_import_catalog_bad_row", {"input/answer.c"}, {"--link-trace"}, "build/output/tmp_import_catalog_bad_row.exe", 0, {}, {}, {"invalid import catalog row"}, "", {}, "tests/data/import_catalog_bad_row.txt", "", {}, true},
        {"minic_import_catalog_duplicate_symbol", {"input/answer.c"}, {"--link-trace"}, "build/output/tmp_import_catalog_duplicate_symbol.exe", 0, {}, {}, {"duplicate import catalog symbol", "fn_strlen"}, "", {}, "tests/data/import_catalog_duplicate_symbol.txt", "", {}, true},
        {"minic_import_catalog_duplicate_builtin", {"input/answer.c"}, {"--link-trace"}, "build/output/tmp_import_catalog_duplicate_builtin.exe", 0, {}, {}, {"may not override built-in symbol", "fn_puts"}, "", {}, "tests/data/import_catalog_duplicate_builtin.txt", "", {}, true},
        {"minic_text_addr64_relocation", {"input/tmp_link_text_addr64_main.c", "input/tmp_link_text_addr64_helper.asm"}, {"--link-trace", "-o", "REPO:build/output/tmp_link_text_addr64.exe"}, "build/output/tmp_link_text_addr64.exe", 42,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations"},
            {"symbol gv_value", "symbol fn_read_global_through_abs", "section=.text", "type=ADDR64"}},
        {"minic_duplicate_external_symbol", {"input/tmp_link_duplicate_main.c", "input/tmp_link_duplicate_one.asm", "input/tmp_link_duplicate_two.asm"}, {"--link-trace"}, "build/output/tmp_link_duplicate.exe", 0, {}, {}, {"duplicate external symbol: fn_dupe"}, "", {}, "", "", {}, true},
        {"minic_unsupported_text_relocation", {"input/tmp_link_bad_reloc_main.c", "input/tmp_link_bad_reloc_helper.asm"}, {"--link-trace"}, "build/output/tmp_link_bad_reloc.exe", 0, {}, {}, {"unsupported COFF", "section .text"}, "", {}, "", "", {}, true},
        {"minic_unsupported_target_section", {"input/tmp_link_bad_section_main.c", "input/tmp_link_bad_section_helper.asm"}, {"--link-trace"}, "build/output/tmp_link_bad_section.exe", 0, {}, {}, {"linker diagnostic:", ".pdata"}, "", {}, "", "", {}, true},
        {"minic_function_ptr_global", {"input/tmp_function_ptr_global.c"}, {"--link-trace"}, "build/output/tmp_function_ptr_global.exe", 42,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations"},
            {"symbol gv_fn_ptr", "symbol fn_answer", "section=.data", "type=ADDR64"}},
        {"minic_function_ptr_table", {"input/tmp_function_ptr_table.c"}, {"--link-trace"}, "build/output/tmp_function_ptr_table.exe", 2,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations"},
            {"symbol gv_fn_table", "symbol fn_one", "symbol fn_two", "section=.data", "type=ADDR64"}},
        {"minic_function_ptr_invalid_init", {"input/tmp_function_ptr_invalid_init.c"}, {"--link-trace"}, "build/output/tmp_function_ptr_invalid_init.exe", 0, {}, {}, {"function names"}, "", {}, "", "", {}, true},
        {"minic_reloc_probe_global_ptr", {"input/tmp_reloc_probe_global_ptr.c"}, {"--link-trace"}, "build/output/tmp_reloc_probe_global_ptr.exe", 42,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations", "[link] base relocations"},
            {"symbol gv_x", "symbol gv_p", "section=.data", "type=ADDR64"},
            {}, "", {}, "", "build/Debug/pe_reloc_probe.exe", {"--expect-reloc", "yes", "--delta", "0x100000", "--site", "0x2000=0x3000"}},
        {"minic_reloc_probe_function_ptr", {"input/tmp_reloc_probe_function_ptr.c"}, {"--link-trace"}, "build/output/tmp_reloc_probe_function_ptr.exe", 42,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations", "[link] base relocations"},
            {"symbol gv_fn_ptr", "symbol fn_answer", "section=.data", "type=ADDR64"},
            {}, "", {}, "", "build/Debug/pe_reloc_probe.exe", {"--expect-reloc", "yes", "--delta", "0x100000", "--site", "0x2000=0x1000"}},
        {"minic_reloc_probe_string_ptr", {"input/tmp_reloc_probe_string_ptr.c"}, {"--link-trace"}, "build/output/tmp_reloc_probe_string_ptr.exe", 65,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations", "[link] base relocations"},
            {"symbol gv_p", "section=.data", "type=ADDR64"},
            {}, "", {}, "", "build/Debug/pe_reloc_probe.exe", {"--expect-reloc", "yes", "--delta", "0x100000", "--site", "0x2000=0x3000"}},
        {"minic_reloc_no_absolute_data", {"input/tmp_reloc_no_absolute_data.c"}, {"--link-trace"}, "build/output/tmp_reloc_no_absolute_data.exe", 7,
            {"[link] input objects", "[link] merged sections", "[link] resolved symbols", "[link] relocations", "[link] base relocations"},
            {}, {}, "", {}, "", "build/Debug/pe_reloc_probe.exe", {"--expect-reloc", "no"}},
        {"minic_linux_stack_args_asm", {"input/tmp_linux_stack_args.c"}, {"--target", "x86_64-linux", "-S", "--emit-asm", "REPO:build/output/tmp_linux_stack_args.asm"}, "build/output/tmp_linux_stack_args.asm", 0,
            {}, {}, {}, "build/output/tmp_linux_stack_args.asm",
            {"global _start", "mov r8d, dword [rsp+48]", "mov r9d, dword [rsp+56]", "mov eax, dword [rbp+16]", "mov eax, dword [rbp+24]", "sub rsp, 64", "add rsp, 64"},
            "", "", {}, false, false, false, true},
        {"minic_linux_answer_link", {"input/answer.c"}, {"--target", "x86_64-linux", "-o", "REPO:build/output/answer_linux"}, "build/output/answer_linux", 42, {}, {}, {}, "", {}, "", "", {}, false, true, true},
        {"minic_linux_stack_args_link", {"input/tmp_linux_stack_args.c"}, {"--target", "x86_64-linux", "-o", "REPO:build/output/tmp_linux_stack_args"}, "build/output/tmp_linux_stack_args", 42, {}, {}, {}, "", {}, "", "", {}, false, true, true},
        {"minic_linux_global_array_link", {"input/tmp_else.c"}, {"--target", "x86_64-linux", "-o", "REPO:build/output/tmp_else_linux"}, "build/output/tmp_else_linux", 42, {}, {}, {}, "", {}, "", "", {}, false, true, true},
        {"minic_linux_rejects_windows_obj", {"input/answer.c", "input/tmp_link_text_addr64_helper.asm"}, {"--target", "x86_64-linux"}, "build/output/linux_rejects_windows_obj", 0, {}, {}, {"target x86_64-linux only accepts ELF .o object inputs for final linking"}, "", {}, "", "", {}, true, false, false, false, true},
        {"minic_invalid_small_object", {"input/answer.c", "input/tmp_invalid_small.obj"}, {"--link-trace"}, "build/output/tmp_invalid_small.exe", 0, {}, {}, {"linker diagnostic:", "object file is too small"}, "", {}, "", "", {}, true},
        {"minic_missing_entry_symbol", {"input/tmp_link_missing_entry.asm"}, {"--link-trace"}, "build/output/tmp_link_missing_entry.exe", 0, {}, {}, {"missing entry symbol: mainCRTStartup", "defines main"}, "", {}, "", "", {}, true},
        {"minic_missing_text_section", {"input/tmp_link_data_only.asm"}, {"--link-trace"}, "build/output/tmp_link_data_only.exe", 0, {}, {}, {"linked objects do not contain a .text section"}, "", {}, "", "", {}, true}
    };
}

std::vector<LinkerRegressionCase> linkerCases() {
    return {
        {"minic_standalone_linker_smoke", {"input/answer.c"}, {"build/output/answer_link_only.obj"}, "build/output/answer_link_only.exe", 42},
        {"minic_standalone_linker_multifile", {"input/tmp_multi_main.c", "input/tmp_multi_math.c"}, {"build/output/tmp_multi_main_link_only.obj", "build/output/tmp_multi_math_link_only.obj"}, "build/output/tmp_multi_link_only.exe", 42},
        {"minic_standalone_linker_multifile_single_job", {"input/tmp_multi_main.c", "input/tmp_multi_math.c"}, {"build/output/tmp_multi_main_link_single.obj", "build/output/tmp_multi_math_link_single.obj"}, "build/output/tmp_multi_link_single.exe", 42, "x86_64-windows", {"-j", "1"}, {"-j", "1"}},
        {"minic_standalone_linker_linux_smoke", {"input/answer.c"}, {"build/output/answer_linux_link_only.o"}, "build/output/answer_linux_link_only", 42, "x86_64-linux", {}, {}, true, true},
        {"minic_standalone_linker_linux_multifile", {"input/tmp_multi_main.c", "input/tmp_multi_math.c"}, {"build/output/tmp_multi_main_linux_link_only.o", "build/output/tmp_multi_math_linux_link_only.o"}, "build/output/tmp_multi_linux_link_only", 42, "x86_64-linux", {}, {}, true, true}
    };
}

class CompilerRegression : public ::testing::TestWithParam<CompilerRegressionCase> {};
class StandaloneLinkerRegression : public ::testing::TestWithParam<LinkerRegressionCase> {};

TEST_P(CompilerRegression, Run) {
    RegressionTestUtils::runCompilerRegression(GetParam());
}

TEST_P(StandaloneLinkerRegression, Run) {
    RegressionTestUtils::runLinkerRegression(GetParam());
}

std::string compilerName(const ::testing::TestParamInfo<CompilerRegressionCase> &info) {
    return info.param.name;
}

std::string linkerName(const ::testing::TestParamInfo<LinkerRegressionCase> &info) {
    return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(Compiler, CompilerRegression, ::testing::ValuesIn(compilerCases()), compilerName);
INSTANTIATE_TEST_SUITE_P(Linker, StandaloneLinkerRegression, ::testing::ValuesIn(linkerCases()), linkerName);

} // namespace
