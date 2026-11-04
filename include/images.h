#ifndef IMAGES_H
#define IMAGES_H
#include <stdint.h>

/* Imagenes empaquetadas en el kernel por scripts/build_images.py: el anfitrion
 * convierte los JPEG/PNG de images/ a RGB565 y el blob se enlaza como datos de
 * solo lectura. El kernel no decodifica imagenes en caliente. */

#define IMAGES_MAX 8

void images_init(void);
int images_count(void);
const char *images_name(int index);
/* Extension de origen de la imagen (".jpg", ".jpeg", ".png"). El sistema de
 * archivos la usa para publicarla en /wallpapers con el mismo nombre que el
 * archivo del proyecto; un blob antiguo sin el campo se trata como JPEG. */
const char *images_extension(int index);
int images_width(int index);
int images_height(int index);
/* Pixeles RGB565 fila a fila de una imagen valida; NULL si el indice no lo es. */
const uint16_t *images_data(int index);
/* Convierte un pixel RGB565 a la superficie RGB 0x00RRGGBB. Se replica el bit
 * alto de cada canal en lugar de calcular con coma flotante. */
static inline uint32_t images_rgb(uint16_t value) {
    uint32_t red = (value >> 11) & 0x1fu;
    uint32_t green = (value >> 5) & 0x3fu;
    uint32_t blue = value & 0x1fu;
    red = (red << 3) | (red >> 2);
    green = (green << 2) | (green >> 4);
    blue = (blue << 3) | (blue >> 2);
    return (red << 16) | (green << 8) | blue;
}
/* Pixel concreto de una imagen, util para el visor. */
uint32_t images_pixel(int index, int x, int y);
/* Indice de la imagen que actua de fondo: 0 si hay alguna, -1 si no hay blob. */
int images_wallpaper(void);

#endif
