#ifndef POWER_H
#define POWER_H
#include <stdint.h>

/* Apagado nativo del equipo. Antes de cortar la energia se cierra el
 * escritorio cuando lo hay y se sincroniza el sistema de archivos. El apagado
 * se pide escribiendo SLP_TYP=5 y SLP_EN en el registro PM1a_CNT del bloque
 * ACPI fijo. Si el firmware ignora S5, se detiene la CPU y se avisa por el
 * puerto serie. La llamada no regresa. */
void power_off(const char *reason) __attribute__((noreturn));

#endif
