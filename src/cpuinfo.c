#include "cpuinfo.h"

/* CPUID devuelve cuatro registros generales; -mgeneral-regs-only permite
 * usarlo sin tocar SSE ni la FPU. */
static void cpuid(uint32_t leaf, uint32_t sub, uint32_t out[4]) {
    uint32_t a = 0, b = 0, c = 0, d = 0;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                             : "a"(leaf), "c"(sub));
    out[0] = a; out[1] = b; out[2] = c; out[3] = d;
}

static void copy_registers(char *out, const uint32_t *regs, int count) {
    for (int i = 0; i < count; i++)
        for (int k = 0; k < 4; k++)
            *out++ = (char)((regs[i] >> (k * 8)) & 0xff);
}

void cpu_vendor(char *out, size_t capacity) {
    if (!out || !capacity) return;
    if (capacity < 13) { out[0] = 0; return; }
    uint32_t regs[4];
    cpuid(0, 0, regs);
    /* EBX, EDX, ECX contienen los 12 bytes del fabricante. */
    copy_registers(out, &regs[1], 3);
    out[12] = 0;
}

void cpu_brand(char *out, size_t capacity) {
    if (!out || !capacity) return;
    out[0] = 0;
    uint32_t regs[4];
    cpuid(0x80000000u, 0, regs);
    if (regs[0] < 0x80000004u) return;      /* sin cadena de marca */
    char raw[49];
    size_t used = 0;
    for (uint32_t leaf = 0x80000002u; leaf <= 0x80000004u; leaf++) {
        cpuid(leaf, 0, regs);
        for (int i = 0; i < 4; i++)
            for (int k = 0; k < 4; k++) raw[used++] = (char)((regs[i] >> (k * 8)) & 0xff);
    }
    raw[used] = 0;
    size_t start = 0;
    while (raw[start] == ' ') start++;
    size_t index = 0;
    while (raw[start] && index + 1 < capacity) out[index++] = raw[start++];
    out[index] = 0;
}

unsigned cpu_cores(void) {
    uint32_t regs[4];
    cpuid(1, 0, regs);
    unsigned count = (regs[1] >> 16) & 0xffu;   /* EBX[23:16]: procesadores logicos */
    return count ? count : 1;
}
