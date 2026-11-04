#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>
enum keyboard_layout {KEYBOARD_ES=1,KEYBOARD_US=2};
/* Teclas extendidas (flechas, inicio/fin, suprimir). Se entregan fuera del
 * flujo de bytes para no ensuciar el texto UTF-8 de la shell. */
enum {KB_KEY_UP=0x101,KB_KEY_DOWN,KB_KEY_LEFT,KB_KEY_RIGHT,KB_KEY_HOME,KB_KEY_END,KB_KEY_DELETE};
int keyboard_set_layout(unsigned layout);
const char *keyboard_layout_name(void);
/* Pure set-1 decoder, also callable by host tests; returns Unicode scalars. */
void keyboard_scancode(uint8_t scan);
int keyboard_next_byte(void);
int keyboard_read(void);
/* Cola independiente para teclas extendidas; 0 si no hay ninguna. */
int keyboard_special(void);
void keyboard_boot_setup(void);
void keyboard_command(const char *args);
#endif
