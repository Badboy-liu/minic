default rel

global fn_bad_reloc
extern gv_value

section .text
reloc_word:
    dd gv_value

fn_bad_reloc:
    xor eax, eax
    ret
