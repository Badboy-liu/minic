default rel

global fn_bad_section

section .pdata
custom_entry:
    dq 1

section .text
fn_bad_section:
    lea rax, [rel custom_entry]
    ret
