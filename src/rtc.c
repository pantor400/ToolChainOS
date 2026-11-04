#include "rtc.h"
#include "io.h"
#include "util.h"

/* CMOS: indice en 0x70 y datos en 0x71. El bit 7 de 0x0A marca la
 * actualizacion en curso; 0x0B describe el formato (BCD o binario, 12 o 24 h).
 * QEMU arranca con -rtc base=localtime,clock=host, asi que la hora es la del
 * anfitrion. */
#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

static int updating(void) { return (cmos_read(0x0a) & 0x80) != 0; }

static unsigned from_bcd(uint8_t value) {
    return (unsigned)(value >> 4) * 10u + (unsigned)(value & 0x0f);
}

int rtc_now(rtc_time_t *out) {
    if (!out) return 0;
    zero(out, sizeof(*out));
    unsigned guard = 0;
    while (updating() && guard++ < 100000) __asm__ volatile("pause");
    if (updating()) return 0;
    uint8_t second = cmos_read(0x00), minute = cmos_read(0x02), hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07), month = cmos_read(0x08), year = cmos_read(0x09);
    uint8_t status = cmos_read(0x0b);
    if (updating()) return 0;              /* se leyo a mitad del refresco */
    int binary = (status & 0x04) != 0;
    int hour24 = (status & 0x02) != 0;
    int pm = (hour & 0x80) != 0;
    hour &= 0x7f;
    if (!binary) {
        second = (uint8_t)from_bcd(second);
        minute = (uint8_t)from_bcd(minute);
        hour = (uint8_t)from_bcd(hour);
        day = (uint8_t)from_bcd(day);
        month = (uint8_t)from_bcd(month);
        year = (uint8_t)from_bcd(year);
    }
    if (!hour24) {
        if (pm && hour < 12) hour = (uint8_t)(hour + 12);
        if (!pm && hour == 12) hour = 0;
    }
    if (hour > 23 || minute > 59 || second > 59) return 0;
    out->second = second; out->minute = minute; out->hour = hour;
    out->day = day; out->month = month; out->year = 2000u + year;
    out->valid = 1;
    return 1;
}

/* Deja sitio al terminador: no escribe el par si no cabe. */
static size_t two(char *out, size_t capacity, size_t used, unsigned value) {
    if (used + 2 >= capacity) return used;
    out[used++] = (char)('0' + (value / 10) % 10);
    out[used++] = (char)('0' + value % 10);
    return used;
}

static size_t four(char *out, size_t capacity, size_t used, unsigned value) {
    static const unsigned divisor[4] = { 1000u, 100u, 10u, 1u };
    for (unsigned i = 0; i < 4; i++) {
        if (used + 1 >= capacity) return used;
        out[used++] = (char)('0' + (value / divisor[i]) % 10);
    }
    return used;
}

static int format(char *out, size_t capacity, int seconds, int date) {
    if (!out || capacity < 6) return 0;
    out[0] = 0;
    rtc_time_t now;
    if (!rtc_now(&now)) return 0;
    size_t used = 0;
    used = two(out, capacity, used, now.hour);
    if (used + 1 < capacity) out[used++] = ':';
    used = two(out, capacity, used, now.minute);
    if (seconds) {
        if (used + 1 < capacity) out[used++] = ':';
        used = two(out, capacity, used, now.second);
    }
    if (date) {
        if (used + 1 < capacity) out[used++] = ' ';
        used = two(out, capacity, used, now.day);
        if (used + 1 < capacity) out[used++] = '/';
        used = two(out, capacity, used, now.month);
        if (used + 1 < capacity) out[used++] = '/';
        used = four(out, capacity, used, now.year);
    }
    if (capacity) out[used < capacity ? used : capacity - 1] = 0;
    return 1;
}

int rtc_text(char *out, size_t capacity) { return format(out, capacity, 0, 0); }

int rtc_text_full(char *out, size_t capacity) { return format(out, capacity, 1, 1); }
