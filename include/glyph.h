#ifndef GLYPH_H
#define GLYPH_H
#include <stdint.h>
uint8_t glyph_row(uint32_t codepoint,unsigned row);
int glyph_supported(uint32_t codepoint);
#endif
