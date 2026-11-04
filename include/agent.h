#ifndef AGENT_H
#define AGENT_H
#include <stddef.h>
/* Nivel 3 de la integracion IA: la IA no solo responde texto, tambien
 * ejecuta ordenes del shell. El kernel valida cada orden con una lista
 * blanca y devuelve la salida real como observacion. */
#define AGENT_CMD_MAX 160
#define AGENT_OBS_MAX 704
#define AGENT_LINES_MAX 4
/* Enlaza el ejecutor del shell (kernel.c); sin el, el agente no arranca. */
void agent_bind(void (*runner)(char *line));
/* Escritura deshabilitada por defecto: solo lectura del sistema. */
void agent_allow_writes(int enabled);
int agent_writes(void);
/* 1 = permitida, 2 = requiere escritura habilitada, 0 = rechazada. */
int agent_allows(const char *command);
/* Ejecuta 1..AGENT_LINES_MAX ordenes y deja la salida en observation.
 * Devuelve 0 si el lote fue rechazado (observation explica el motivo). */
int agent_execute(const char *command,char *observation,size_t capacity);
void agent_catalog(void);
#endif
