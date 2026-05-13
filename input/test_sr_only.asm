default rel
extern ExitProcess
extern fmod
global compute
global main

section .text

compute:
    push rbp
    mov rbp, rsp
    sub rsp, 32
    mov rax, rcx
    shl eax, 3
    mov r10, rax
    mov rax, rcx
    xor rcx, rcx
    sar eax, cl
    mov r11, rax
    mov rax, rcx
    xor rcx, rcx
    and eax, ecx
    mov rax, r10
    mov rcx, r11
    add eax, ecx
    mov rdx, rax
    mov rcx, rax
    mov rax, rdx
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
    mov rcx, 100
    lea r11, [rel compute]
    call r11
    mov rbx, rax
    mov rax, rbx
    mov rsp, rbp
    pop rbx
    pop rbp
    ret
