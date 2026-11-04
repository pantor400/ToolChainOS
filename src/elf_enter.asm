; Trampolin hacia aplicaciones ELF64 cargadas en ring 0.
; elf_call_app(entry=rdi, stack_top=rsi, api=rdx)
; Guarda los registros que el ABI SysV exige preservar, cambia a la pila de
; la aplicacion y llama a su entrada con el puntero host_api_t en RDI.
; elf_abort() descarta la pila de la app desde el manejador de excepciones.
BITS 64

global elf_call_app
global elf_abort

section .bss
align 8
kernel_rsp: resq 1

section .text
elf_call_app:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rel kernel_rsp], rsp
    mov rsp, rsi
    and rsp, -16
    mov rbx, rdi
    mov rdi, rdx
    xor ebp, ebp
    call rbx
elf_return:
    mov rsp, [rel kernel_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

elf_abort:
    ; La excepcion llego con IF=0; se restaura antes de volver al kernel.
    mov rsp, [rel kernel_rsp]
    sti
    jmp elf_return
