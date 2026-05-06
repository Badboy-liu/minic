default rel
extern ExitProcess
extern fmod
global mainCRTStartup
global fn_main

section .text

fn_main:
    push rbp
    mov rbp, rsp
    sub rsp, 96
    lea rax, [rbp-8]
    push rax
    movsd xmm0, [rel flt_0]
    pop rcx
    movsd qword [rcx], xmm0
    lea rax, [rbp-16]
    push rax
    movsd xmm0, [rel flt_1]
    pop rcx
    movsd qword [rcx], xmm0
    lea rax, [rbp-24]
    push rax
    movsd xmm0, [rel flt_2]
    pop rcx
    movsd qword [rcx], xmm0
    lea rax, [rbp-32]
    push rax
    movsd xmm0, [rel flt_2]
    pop rcx
    movsd qword [rcx], xmm0
    lea rax, [rbp-40]
    push rax
    movsd xmm0, [rel flt_3]
    sub rsp, 8
    movsd [rsp], xmm0
    movsd xmm0, [rel flt_4]
    movsd xmm1, xmm0
    movsd xmm0, [rsp]
    add rsp, 8
    comisd xmm0, xmm1
    seta al
    movzx eax, al
    pop rcx
    mov dword [rcx], eax
    lea rax, [rbp-48]
    push rax
    movsd xmm0, [rel flt_5]
    sub rsp, 8
    movsd [rsp], xmm0
    movsd xmm0, [rel flt_6]
    movsd xmm1, xmm0
    movsd xmm0, [rsp]
    add rsp, 8
    comisd xmm0, xmm1
    setb al
    movzx eax, al
    pop rcx
    mov dword [rcx], eax
    lea rax, [rbp-56]
    push rax
    movsd xmm0, [rel flt_0]
    sub rsp, 8
    movsd [rsp], xmm0
    movsd xmm0, [rel flt_0]
    movsd xmm1, xmm0
    movsd xmm0, [rsp]
    add rsp, 8
    comisd xmm0, xmm1
    setae al
    movzx eax, al
    pop rcx
    mov dword [rcx], eax
    lea rax, [rbp-64]
    push rax
    movsd xmm0, [rel flt_7]
    pop rcx
    cvtsd2ss xmm0, xmm0
    movss dword [rcx], xmm0
    lea rax, [rbp-72]
    push rax
    lea rax, [rbp-64]
    movss xmm0, dword [rax]
    cvtss2sd xmm0, xmm0
    cvttsd2si eax, xmm0
    pop rcx
    mov dword [rcx], eax
    lea rax, [rbp-80]
    push rax
    mov eax, 1
    cmp rax, 0
    je ternary_false_1
    movsd xmm0, [rel flt_4]
    jmp ternary_end_2
ternary_false_1:
    movsd xmm0, [rel flt_0]
ternary_end_2:
    pop rcx
    movsd qword [rcx], xmm0
    lea rax, [rbp-88]
    push rax
    lea rax, [rbp-8]
    movsd xmm0, qword [rax]
    cvttsd2si eax, xmm0
    push rax
    lea rax, [rbp-16]
    movsd xmm0, qword [rax]
    cvttsd2si eax, xmm0
    mov rcx, rax
    pop rax
    add eax, ecx
    push rax
    lea rax, [rbp-24]
    movsd xmm0, qword [rax]
    cvttsd2si eax, xmm0
    mov rcx, rax
    pop rax
    add eax, ecx
    push rax
    lea rax, [rbp-32]
    movsd xmm0, qword [rax]
    cvttsd2si eax, xmm0
    mov rcx, rax
    pop rax
    add eax, ecx
    push rax
    lea rax, [rbp-40]
    mov eax, dword [rax]
    mov rcx, rax
    pop rax
    add eax, ecx
    push rax
    lea rax, [rbp-48]
    mov eax, dword [rax]
    mov rcx, rax
    pop rax
    add eax, ecx
    push rax
    lea rax, [rbp-56]
    mov eax, dword [rax]
    mov rcx, rax
    pop rax
    add eax, ecx
    pop rcx
    mov dword [rcx], eax
    lea rax, [rbp-88]
    push rax
    lea rax, [rbp-88]
    mov eax, dword [rax]
    push rax
    lea rax, [rbp-72]
    mov eax, dword [rax]
    push rax
    mov eax, 23
    mov rcx, rax
    pop rax
    sub eax, ecx
    mov rcx, rax
    pop rax
    add eax, ecx
    pop rcx
    mov dword [rcx], eax
    lea rax, [rbp-88]
    mov eax, dword [rax]
    jmp main_return_0
    xor eax, eax
main_return_0:
    add rsp, 96
    pop rbp
    ret

mainCRTStartup:
    sub rsp, 40
    call fn_main
    mov ecx, eax
    call ExitProcess

section .rdata
flt_0: dq __float64__(3.0)
flt_1: dq __float64__(7.0)
flt_2: dq __float64__(5.0)
flt_3: dq __float64__(3.1400000000000001)
flt_4: dq __float64__(2.0)
flt_5: dq __float64__(1.0)
flt_6: dq __float64__(2.5)
flt_7: dq __float64__(42.0)
