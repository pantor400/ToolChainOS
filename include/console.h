#ifndef CONSOLE_H
#define CONSOLE_H
#include <stdint.h>
#include <stddef.h>
#include <limine.h>
void serial_init(void);
void console_init(struct limine_framebuffer *fb);
void console_clear(void);
void console_putc(char c);
void console_put_unicode(uint32_t codepoint);
void console_erase_last(uint32_t codepoint);
void console_write_text(const char *text,size_t bytes);
void console_write(const char *s);
void console_dec(uint64_t n);
void console_signed(int64_t n);
int console_set_colors(uint32_t text_rgb, uint32_t background_rgb);
/* La interfaz grafica toma el framebuffer: la consola sigue escribiendo por
 * serie, pero no repinta pixeles hasta que se le devuelve la pantalla. */
void console_suspend_screen(int suspend);
int console_screen_suspended(void);
void console_hex(uint64_t n);
int console_read(void);
/* Captura de salida para el agente: lo escrito en consola (serie y
 * framebuffer) se copia ademas en un bufer acotado del llamador. */
void console_capture_begin(char *buffer,size_t capacity);
/* Cierra la captura; devuelve los bytes descartados por falta de espacio. */
size_t console_capture_end(void);
#endif
