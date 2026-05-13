default rel
extern ExitProcess
extern fmod
global main

section .text

main:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    xor rax, rax
    mov rsp, rbp
    pop rbp
    ret
