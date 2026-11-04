#include "mouse.h"
#include "keyboard.h"
#include "console.h"
#include "io.h"

#define PS2_DATA 0x60
#define PS2_STATUS 0x64
#define PS2_COMMAND 0x64

static volatile int pointer_x, pointer_y;
static volatile int pointer_buttons;
static int bound_w = 1024, bound_h = 768;
static int packet[3], packet_index;
static volatile int irq_packets;
static int ready;

/* El controlador tarda microsegundos; se acota para no colgar el arranque
 * si la maquina no tiene i8042 (por ejemplo, algunas placas ARM o QEMU sin
 * dispositivo de entrada). */
static int wait_input(void) {
    for (int i = 0; i < 200000; i++) if (!(inb(PS2_STATUS) & 2)) return 1;
    return 0;
}
static int wait_output(void) {
    for (int i = 0; i < 200000; i++) if (inb(PS2_STATUS) & 1) return 1;
    return 0;
}
static int write_aux(unsigned char value) {
    if (!wait_input()) return 0;
    outb(PS2_COMMAND, 0xd4);
    if (!wait_input()) return 0;
    outb(PS2_DATA, value);
    return 1;
}
static int aux_ack(void) {
    if (!wait_output()) return 0;
    return inb(PS2_DATA) == 0xfa;
}
static int aux_command(unsigned char value) { return write_aux(value) && aux_ack(); }

static void drain(void) {
    for (int i = 0; i < 64 && (inb(PS2_STATUS) & 1); i++) (void)inb(PS2_DATA);
}

static void clamp(void) {
    if (pointer_x < 0) pointer_x = 0;
    if (pointer_y < 0) pointer_y = 0;
    if (pointer_x > bound_w - 1) pointer_x = bound_w - 1;
    if (pointer_y > bound_h - 1) pointer_y = bound_h - 1;
}

/* Un paquete PS/2 son 3 bytes: botones+signos, delta X, delta Y. El primer
 * byte lleva siempre el bit 3 a 1; si se pierde un byte se resincroniza. */
static void decode(unsigned char byte) {
    if (packet_index == 0 && !(byte & 0x08)) return;
    packet[packet_index++] = byte;
    if (packet_index < 3) return;
    packet_index = 0;
    int flags = packet[0], dx = (int8_t)packet[1], dy = (int8_t)packet[2];
    if (flags & 0x40) dx = 0;
    if (flags & 0x80) dy = 0;
    pointer_x += dx;
    pointer_y -= dy; /* el eje Y de PS/2 crece hacia arriba */
    pointer_buttons = flags & 0x07;
    clamp();
    irq_packets++;
}

void mouse_irq(void) {
    unsigned char status = inb(PS2_STATUS);
    if (!(status & 1)) return;
    unsigned char byte = inb(PS2_DATA);
    if (status & 0x20) decode(byte);
    else keyboard_scancode(byte); /* byte de teclado fuera de su IRQ */
}

/* Respaldo por sondeo: si la interrupcion no llega (IRQ enmascarada o
 * controlador sin linea), el bucle grafico sigue leyendo los bytes. */
void mouse_poll(void) {
    for (int i = 0; i < 64; i++) {
        unsigned char status = inb(PS2_STATUS);
        if (!(status & 1)) return;
        unsigned char byte = inb(PS2_DATA);
        if (status & 0x20) decode(byte);
        else keyboard_scancode(byte);
    }
}

int mouse_init(void) {
    ready = 0;
    packet_index = 0;
    irq_packets = 0;
    drain();
    if (!wait_input()) return 0;
    outb(PS2_COMMAND, 0xa8); /* habilitar el puerto auxiliar */
    if (!wait_input()) return 0;
    outb(PS2_COMMAND, 0x20); /* leer configuracion */
    if (!wait_output()) return 0;
    unsigned char config = inb(PS2_DATA);
    config |= 0x02;  /* IRQ12 del raton */
    config &= (unsigned char)~0x20; /* reloj del raton activo */
    if (!wait_input()) return 0;
    outb(PS2_COMMAND, 0x60);
    if (!wait_input()) return 0;
    outb(PS2_DATA, config);
    if (!aux_command(0xf6)) return 0; /* valores por defecto */
    if (!aux_command(0xf3)) return 0; /* frecuencia de muestreo */
    if (!aux_command(0x64)) return 0; /* 100 muestras/s */
    if (!aux_command(0xe8)) return 0; /* resolucion */
    if (!aux_command(0x03)) return 0; /* 8 cuentas/mm */
    if (!aux_command(0xf4)) return 0; /* streaming */
    drain();
    pointer_x = bound_w / 2;
    pointer_y = bound_h / 2;
    pointer_buttons = 0;
    /* Desenmascarar solo IRQ12 en el PIC esclavo; el resto sigue igual. */
    outb(0xa1, (uint8_t)(inb(0xa1) & ~0x10));
    ready = 1;
    return 1;
}

mouse_state_t mouse_state(void) {
    mouse_state_t s;
    s.x = pointer_x;
    s.y = pointer_y;
    s.buttons = pointer_buttons;
    s.wheel = 0;
    s.ready = ready;
    return s;
}

void mouse_set_bounds(int width, int height) {
    if (width > 0) bound_w = width;
    if (height > 0) bound_h = height;
    clamp();
}

void mouse_center(void) {
    pointer_x = bound_w / 2;
    pointer_y = bound_h / 2;
}

void mouse_command(const char *args) {
    (void)args;
    mouse_state_t s = mouse_state();
    console_write(s.ready ? "RATON PS/2: ACTIVO" : "RATON PS/2: NO DETECTADO");
    console_write(" X="); console_dec((uint64_t)(s.x < 0 ? -s.x : s.x));
    console_write(" Y="); console_dec((uint64_t)(s.y < 0 ? -s.y : s.y));
    console_write(" BOTONES="); console_dec((uint64_t)s.buttons);
    console_write(" IRQ_PAQUETES="); console_dec((uint64_t)irq_packets);
    console_write("\nTOUCHPAD: SOLO MODO COMPATIBILIDAD PS/2 (RELATIVO).\n");
}
