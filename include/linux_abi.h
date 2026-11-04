#ifndef LINUX_ABI_H
#define LINUX_ABI_H
#include <stdint.h>

/* Personalidad Linux: ejecuta binarios ELF64 static-pie de Linux (musl) en
 * ring 0. El kernel programa EFER.SCE, STAR y LSTAR para que la instruccion
 * SYSCALL entre en linux_syscall_entry, salva los registros y despacha a
 * linux_dispatch(). No hay cambio de privilegio ni paginacion por proceso: el
 * binario comparte el espacio de direcciones del kernel, como las apps de
 * host_api, y solo ve las llamadas al sistema que aqui se implementan.
 *
 * El binario no se enlaza con el kernel: se descarga aparte (scripts/linux_bin.py)
 * y viaja en la ISO como modulo Limine, asi que el codigo MIT del kernel no se
 * convierte en obra derivada de la GPL de BusyBox. */

/* Programa los MSR de SYSCALL. Devuelve 0 si el entorno no lo permite: en ese
 * caso "LINUX" sigue disponible pero informa del motivo en lugar de arrancar. */
int linux_abi_init(void);
/* Cierto mientras un binario Linux esta en ejecucion (lo consulta el manejador
 * de excepciones para abortar solo esa ejecucion). */
int linux_busy(void);
/* Deshace la pila del binario y devuelve el control al interprete de ordenes. */
void linux_abort(void) __attribute__((noreturn));

/* Orden del shell: LINUX [<applet> [argumentos...]] | LINUX LIST | LINUX TRACE */
void linux_command(const char *args);

/* Ejecuta un applet como si se hubiera escrito "LINUX <applet> <args>".
 * Devuelve 1 si lo delego y 0 si no hay binario en la ISO: entonces el shell
 * usa su propia implementacion, que es la que ven las pruebas de la entrega. */
int linux_applet(const char *applet, const char *args);

/* Punto de entrada de SYSCALL (src/linux_enter.asm). Devuelve el resultado en
 * RAX segun el convenio Linux: valor o -errno. */
uint64_t linux_dispatch(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6);

#endif
