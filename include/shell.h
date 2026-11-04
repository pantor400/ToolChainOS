#ifndef SHELL_H
#define SHELL_H
#include <stddef.h>

/* Ejecuta una orden del shell capturando su salida en 'out'. Lo usa la
 * terminal grafica (host_api v3). Devuelve 0 si la linea esta vacia, -1 si la
 * orden no se permite desde una aplicacion y 1 si se ejecuto. */
int shell_exec(const char *line, char *out, size_t capacity);

#endif
