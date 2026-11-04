#ifndef MMIO_H
#define MMIO_H
#include <stdint.h>

/* El HHDM de Limine solo mapea lo que aparece en el mapa de memoria: las BAR
 * de los dispositivos viven en el hueco PCI y no estan ahi. mmio_map() crea
 * las paginas que falten en las tablas del kernel (misma direccion virtual,
 * offset + fisico) y las marca como no cacheables. Devuelve NULL si no hay
 * memoria para las tablas. */
void *mmio_map(uint64_t physical, uint64_t length);
#endif
