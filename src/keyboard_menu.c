#include "keyboard.h"
#include "console.h"
#include "interrupts.h"
#include "util.h"
static void selected(void){
    console_write("TECLADO: ");console_write(keyboard_layout_name());
    console_write("\nPS/2: distribucion del kernel. SERIAL: UTF-8 del terminal anfitrion.\n");
}
void keyboard_boot_setup(void){
    keyboard_set_layout(KEYBOARD_ES);
    console_write("\nCONFIGURACION DE TECLADO / KEYBOARD SETUP\n");
    console_write("1. Español latinoamericano (Perú) [predeterminado]\n");
    console_write("2. English - United States (US)\n");
    console_write("Elija 1 o 2 y Enter. Sin respuesta: ES-LATAM en 3 segundos.\n");
    uint64_t end=timer_ticks()+300;unsigned choice=1;int confirmed=0;
    while(timer_ticks()<end){
        int c=console_read();
        if(c=='1'||c=='2'){choice=(unsigned)(c-'0');console_putc((char)c);}
        else if(c=='\n'||c=='\r'){confirmed=1;break;}
        __asm__ volatile("pause");
    }
    keyboard_set_layout(confirmed?choice:KEYBOARD_ES);console_putc('\n');selected();
}
void keyboard_command(const char *arg){
    if(!*arg){selected();return;}
    unsigned choice=0;
    if(ci_same(arg,"ES")||ci_same(arg,"ES-LATAM")||same(arg,"1"))choice=KEYBOARD_ES;
    else if(ci_same(arg,"US")||ci_same(arg,"EN")||same(arg,"2"))choice=KEYBOARD_US;
    if(!choice){console_write("USO: TECLADO [ES|US|1|2]\n");return;}
    keyboard_set_layout(choice);selected();
}
