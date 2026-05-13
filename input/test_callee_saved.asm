default rel
extern ExitProcess
extern fmod
global add
global foo
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

foo:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 40
    mov r12, rcx
    mov r13, r8
    mov rax, r12
    mov rcx, rdx
    add eax, ecx
    mov rbx, rax
    mov rcx, rbx
    mov rdx, r13
    lea r11, [rel add]
    call r11
    mov r14, rax
    mov rax, rbx
    mov rcx, r14
    add eax, ecx
    mov r13, rax
    mov rcx, r13
    mov rdx, r12
    lea r11, [rel add]
    call r11
    mov r15, rax
    mov rax, rbx
    mov rcx, r14
    add eax, ecx
    mov r10, rax
    mov rax, r10
    mov rcx, r13
    add eax, ecx
    mov r11, rax
    mov rax, r11
    mov rcx, r15
    add eax, ecx
    mov r10, rax
    mov rax, r10
    mov rsp, rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

main:
    push rbp
    mov rbp, rsp
    push rbx
    sub rsp, 40
    mov rcx, 1
    mov rdx, 2
    mov r8, 3
    lea r11, [rel foo]
    call r11
    mov rbx, rax
    mov rax, rbx
    mov rsp, rbp
    pop rbx
    pop rbp
    ret
