default rel
extern ExitProcess
extern fmod
global mainCRTStartup
global fn_main

section .text

fn_main:
    push rbp
    mov rbp, rsp
    sub rsp, 16
    lea rax, [rbp-8]
    push rax
    mov eax, 42
    pop rcx
    mov dword [rcx], eax
    mov eax, 42
    jmp main_return_0
    xor eax, eax
main_return_0:
    add rsp, 16
    pop rbp
    ret

mainCRTStartup:
    sub rsp, 40
    call fn_main
    mov ecx, eax
    call ExitProcess
