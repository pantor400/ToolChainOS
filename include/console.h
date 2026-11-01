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
void console_hex(uint64_t n);
int console_read(void);
#endif
