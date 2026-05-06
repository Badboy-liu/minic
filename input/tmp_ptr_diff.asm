default rel
extern ExitProcess
extern fmod
global mainCRTStartup
global fn_main

section .text

fn_main:
    push rbp
    mov rbp, rsp
    sub rsp, 224
    lea rax, [rbp-48]
    push rax
    lea rax, [rbp-40]
    push rax
    mov eax, 0
    imul rax, 4
    mov rcx, rax
    pop rax
    add rax, rcx
    pop rcx
    mov qword [rcx], rax
    lea rax, [rbp-56]
    push rax
    lea rax, [rbp-40]
    push rax
    mov eax, 5
    imul rax, 4
    mov rcx, rax
    pop rax
    add rax, rcx
    pop rcx
    mov qword [rcx], rax
    lea rax, [rbp-64]
    push rax
    lea rax, [rbp-40]
    push rax
    mov eax, 9
    imul rax, 4
    mov rcx, rax
    pop rax
    add rax, rcx
    pop rcx
    mov qword [rcx], rax
    lea rax, [rbp-72]
    push rax
    lea rax, [rbp-56]
    mov rax, qword [rax]
    push rax
    lea rax, [rbp-48]
    mov rax, qword [rax]
    mov rcx, rax
    pop rax
    sub rax, rcx
    mov rcx, 4
    cqo
    idiv rcx
    pop rcx
    mov qword [rcx], rax
    lea rax, [rbp-80]
    push rax
    lea rax, [rbp-64]
    mov rax, qword [rax]
    push rax
    lea rax, [rbp-48]
    mov rax, qword [rax]
    mov rcx, rax
    pop rax
    sub rax, rcx
    mov rcx, 4
    cqo
    idiv rcx
    pop rcx
    mov qword [rcx], rax
    lea rax, [rbp-72]
    mov rax, qword [rax]
    push rax
    mov eax, 5
    mov rcx, rax
    pop rax
    cmp rax, rcx
    setne al
    movzx eax, al
    cmp rax, 0
    je if_else_1
    mov eax, 1
    jmp main_return_0
    jmp if_end_2
if_else_1:
if_end_2:
    lea rax, [rbp-80]
    mov rax, qword [rax]
    push rax
    mov eax, 9
    mov rcx, rax
    pop rax
    cmp rax, rcx
    setne al
    movzx eax, al
    cmp rax, 0
    je if_else_3
    mov eax, 2
    jmp main_return_0
    jmp if_end_4
if_else_3:
if_end_4:
    lea rax, [rbp-192]
    push rax
    lea rax, [rbp-180]
    push rax
    mov eax, 0
    imul rax, 1
    mov rcx, rax
    pop rax
    add rax, rcx
    pop rcx
    mov qword [rcx], rax
    lea rax, [rbp-200]
    push rax
    lea rax, [rbp-180]
    push rax
    mov eax, 42
    imul rax, 1
    mov rcx, rax
    pop rax
    add rax, rcx
    pop rcx
    mov qword [rcx], rax
    lea rax, [rbp-208]
    push rax
    lea rax, [rbp-200]
    mov rax, qword [rax]
    push rax
    lea rax, [rbp-192]
    mov rax, qword [rax]
    mov rcx, rax
    pop rax
    sub rax, rcx
    pop rcx
    mov qword [rcx], rax
    lea rax, [rbp-208]
    mov rax, qword [rax]
    push rax
    mov eax, 42
    mov rcx, rax
    pop rax
    cmp rax, rcx
    setne al
    movzx eax, al
    cmp rax, 0
    je if_else_5
    mov eax, 3
    jmp main_return_0
    jmp if_end_6
if_else_5:
if_end_6:
    lea rax, [rbp-216]
    push rax
    lea rax, [rbp-64]
    mov rax, qword [rax]
    push rax
    lea rax, [rbp-56]
    mov rax, qword [rax]
    mov rcx, rax
    pop rax
    sub rax, rcx
    mov rcx, 4
    cqo
    idiv rcx
    pop rcx
    mov qword [rcx], rax
    lea rax, [rbp-216]
    mov rax, qword [rax]
    push rax
    lea rax, [rbp-72]
    mov rax, qword [rax]
    mov rcx, rax
    pop rax
    add rax, rcx
    push rax
    mov eax, 9
    mov rcx, rax
    pop rax
    cmp rax, rcx
    setne al
    movzx eax, al
    cmp rax, 0
    je if_else_7
    mov eax, 4
    jmp main_return_0
    jmp if_end_8
if_else_7:
if_end_8:
    mov eax, 42
    jmp main_return_0
    xor eax, eax
main_return_0:
    add rsp, 224
    pop rbp
    ret

mainCRTStartup:
    sub rsp, 40
    call fn_main
    mov ecx, eax
    call ExitProcess
