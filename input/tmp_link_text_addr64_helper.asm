default rel

global fn_read_global_through_abs
extern gv_value

section .text
fn_read_global_through_abs:
    mov rax, gv_value
    mov eax, [rax]
    ret
