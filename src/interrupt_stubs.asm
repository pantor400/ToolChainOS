BITS 64
SECTION .text
EXTERN interrupt_dispatch
GLOBAL isr_ignore
isr_ignore:
    iretq
%assign i 0
%rep 48
GLOBAL isr%+i
isr%+i:
%if i != 8 && i != 10 && i != 11 && i != 12 && i != 13 && i != 14 && i != 17 && i != 21 && i != 29 && i != 30
    push qword 0
%endif
    push qword i
    jmp isr_common_entry
%assign i i+1
%endrep
isr_common_entry:
    cld
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi,rsp
    mov rbx,rsp
    and rsp,-16
    call interrupt_dispatch
    mov rsp,rbx
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp,16
    iretq
SECTION .rodata
GLOBAL isr_table
isr_table:
%assign i 0
%rep 48
    dq isr%+i
%assign i i+1
%endrep
SECTION .note.GNU-stack noalloc noexec nowrite progbits
