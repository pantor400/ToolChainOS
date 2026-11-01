#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>
enum keyboard_layout {KEYBOARD_ES=1,KEYBOARD_US=2};
int keyboard_set_layout(unsigned layout);
unsigned keyboard_get_layout(void);
const char *keyboard_layout_name(void);
/* Pure set-1 decoder, also callable by host tests; returns Unicode scalars. */
void keyboard_scancode(uint8_t scan);
int keyboard_next_byte(void);
int keyboard_read(void);
void keyboard_boot_setup(void);
void keyboard_command(const char *args);
#endif
