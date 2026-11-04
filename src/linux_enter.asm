; Personalidad Linux: entrada de la instruccion SYSCALL y trampolin de arranque.
;
; Los binarios Linux (musl, static-pie) no conocen host_api_t: llaman a la
; instruccion SYSCALL con el numero de llamada en RAX y los argumentos en
; RDI, RSI, RDX, R10, R8 y R9, igual que en un kernel Linux real. El kernel
; programa LSTAR con linux_syscall_entry y STAR con su propio selector, de modo
; que la instruccion vuelve a ring 0 en este mismo codigo: no hay cambio de
; privilegio, solo un punto de entrada conocido.
;
; SYSCALL guarda en RCX la direccion de retorno y en R11 los RFLAGS. Todo lo
; demas lo conserva, asi que el manejador salva los registros, llama al
; despachador en C y los restaura: el programa solo ve cambiar RAX (resultado),
; RCX y R11, exactamente lo que permite el ABI de llamadas Linux.
BITS 64

global linux_syscall_entry
global linux_call_entry
global linux_abort
global cpu_rdmsr
global cpu_wrmsr
global cpu_read_cs
global cpu_read_ss
global cpu_write_cr0
global cpu_write_cr4
extern linux_dispatch

section .bss
align 8
; Pila del kernel antes de entrar en el binario; linux_abort() la recupera.
linux_rsp: resq 1
; Resultado de la llamada en curso: la pila del programa ya esta ocupada por los
; registros salvados y RAX debe llegar despues de restaurarlos todos.
linux_result: resq 1

section .text

; --- Entrada de SYSCALL -----------------------------------------------------
; RSP sigue siendo la pila del programa: los 14 registros salvados ocupan
; 112 bytes y el sexto argumento se pasa en pila. El kernel se compila sin SSE
; (-mgeneral-regs-only), asi que no exige alineacion de 16.
linux_syscall_entry:
    push rcx                    ; direccion de retorno del programa
    push r11                    ; RFLAGS del programa
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    ; linux_dispatch(nr, a1, a2, a3, a4, a5, a6)
    mov rdi, rax                ; numero de llamada
    mov rsi, [rsp + 88]         ; a1 = RDI del programa
    mov rdx, [rsp + 80]         ; a2 = RSI
    mov rcx, [rsp + 72]         ; a3 = RDX
    mov r8,  [rsp + 64]         ; a4 = R10
    mov r9,  [rsp + 56]         ; a5 = R8
    mov rax, [rsp + 48]         ; a6 = R9
    push rax
    call linux_dispatch
    add rsp, 8                  ; a6
    mov [rel linux_result], rax
    ; Se restauran TODOS los registros salvados, no solo los que usa el codigo
    ; del kernel: la instruccion SYSCALL de Linux conserva RDI, RSI, RDX, R10,
    ; R8 y R9, y el asm de musl solo declara RCX, R11 y RAX como alterados. Si
    ; se omitiera alguno, su valor vivo en el programa se perderia.
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    pop r9
    pop r8
    pop r10
    pop rdx
    pop rsi
    pop rdi
    pop r11
    pop rcx
    push r11
    popfq
    mov rax, [rel linux_result]
    jmp rcx

; --- Trampolin de arranque --------------------------------------------------
; linux_call_entry(entry=rdi, stack_top=rsi): cambia a la pila del programa, que
; ya contiene argc/argv/envp/auxv, y SALTA a su punto de entrada. Debe ser un
; salto, no una llamada: _start lee argc en [rsp] y una direccion de retorno
; apilada lo desplazaria todo. La vuelta la da linux_abort con la pila del
; kernel que se guarda aqui.
linux_call_entry:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rel linux_rsp], rsp
    mov rsp, rsi
    xor ebp, ebp
    xor edx, edx
    jmp rdi
linux_return:
    mov rsp, [rel linux_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret

; Salida del programa: exit_group, un fallo de syscall irrecuperable o una
; excepcion de CPU llegan aqui y devuelven el control al interprete de ordenes.
linux_abort:
    mov rsp, [rel linux_rsp]
    sti
    jmp linux_return

; --- MSR --------------------------------------------------------------------
; cpu_rdmsr(msr=rdi) -> rax
cpu_rdmsr:
    mov rcx, rdi
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret

; cpu_wrmsr(msr=rdi, valor=rsi)
cpu_wrmsr:
    mov rcx, rdi
    mov rax, rsi
    mov rdx, rsi
    shr rdx, 32
    wrmsr
    ret

cpu_read_cs:
    xor eax, eax
    mov ax, cs
    ret

cpu_read_ss:
    xor eax, eax
    mov ax, ss
    ret

; --- Registros de control ---------------------------------------------------
; Hacen falta para habilitar SSE: musl lo usa en memcpy y cada instruccion
; SSE daria #UD con CR4.OSFXSR a cero.
cpu_write_cr0:
    mov cr0, rdi
    ret

cpu_write_cr4:
    mov cr4, rdi
    ret
