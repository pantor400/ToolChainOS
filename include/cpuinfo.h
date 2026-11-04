#ifndef CPUINFO_H
#define CPUINFO_H
#include <stdint.h>
#include <stddef.h>

/* Datos del procesador obtenidos por CPUID, sin libc: la ventana Sistema los
 * muestra tal cual, sin interpretar. */
void cpu_vendor(char *out, size_t capacity);
void cpu_brand(char *out, size_t capacity);
unsigned cpu_cores(void);
/* CPUID crudo (salida eax, ebx, ecx, edx): consulta hojas fuera del informe
 * de la ventana Sistema, como los rangos maximos o los rasgos extendidos. */
void cpu_cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4]);

#endif
