#ifndef MOUSE_H
#define MOUSE_H
#include <stdint.h>

/* Raton PS/2 estandar y touchpads en modo de compatibilidad PS/2 (relativos).
 * Los touchpads absolutos por I2C/SMBus no se soportan: no hay controlador
 * I2C en este kernel y el firmware del portatil no los expone por 0x60/0x64. */
typedef struct {
    int x, y, buttons, wheel, ready;
    /* Diagnostico: paquetes completos atendidos y bytes descartados al
     * resincronizar. Un valor creciente delata perdida de paquetes. */
    int packets, discards;
} mouse_state_t;

int mouse_init(void);
void mouse_poll(void);
mouse_state_t mouse_state(void);
void mouse_set_bounds(int width, int height);
void mouse_center(void);
/* Llamada desde el despachador de interrupciones (vector 44). */
void mouse_irq(void);
void mouse_command(const char *args);

#endif
