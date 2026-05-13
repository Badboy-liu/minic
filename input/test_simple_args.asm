default rel
extern ExitProcess
extern fmod
global add
global main

section .text

add:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov rax, rcx
    mov rcx, rdx
    add eax, ecx
    mov r10, rax
    mov rax, r10
    mov rsp, rbp
    pop rbp
    ret

main:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 40
    mov rcx, 10
    mov rdx, 20
    lea r11, [rel add]
    call r11
    mov rbx, rax
    mov rax, rbx
    mov rsp, rbp
    pop rbx
    pop rbp
    ret
