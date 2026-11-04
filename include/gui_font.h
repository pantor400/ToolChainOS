#ifndef GUI_FONT_H
#define GUI_FONT_H
#include <stdint.h>

/* Glifo horneado: cobertura 4bpp y metrica relativa a la linea base.
 * ox/oy son el desplazamiento desde el origen de escritura (x, base)
 * hasta la esquina superior izquierda de la caja del glifo. */
typedef struct {
    uint16_t cp;
    uint8_t w, h;
    int8_t ox, oy;
    uint8_t advance, row_bytes;
    uint32_t offset;
} gui_glyph_t;

typedef struct {
    const gui_glyph_t *glyphs;
    uint16_t count;
    const uint8_t *bits;
    int16_t ascent, descent, height, line_gap;
} gui_font_t;

#endif
