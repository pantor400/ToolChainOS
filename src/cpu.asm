; NASM transforma este archivo en build/cpu.o (ELF de 64 bits).
; System V AMD64 ABI: argumentos enteros en RDI, RSI; resultado en RAX. Enteros con signo limitados en C para evitar overflow.
BITS 64
SECTION .text
GLOBAL asm_sumar
asm_sumar:
    mov rax, rdi
    add rax, rsi
    ret

GLOBAL asm_cr0
asm_cr0:
    mov rax, cr0             ; Bit 31: paginacion habilitada.
    ret
GLOBAL asm_cr3
asm_cr3:
    mov rax, cr3             ; Referencia fisica de las tablas de paginas.
    ret
GLOBAL asm_cr4
asm_cr4:
    mov rax, cr4
    ret
GLOBAL asm_detener
asm_detener:
    cli                     ; No tenemos IDT ni manejadores de IRQ propios.
.loop:
    hlt
    jmp .loop
SECTION .note.GNU-stack noalloc noexec nowrite progbits
