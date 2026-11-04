#ifndef RTC_H
#define RTC_H
#include <stddef.h>

/* Hora del reloj CMOS del anfitrion (QEMU la entrega con -rtc). */
typedef struct {
    unsigned second, minute, hour;
    unsigned day, month, year;
    int valid;
} rtc_time_t;

/* Devuelve 1 y rellena out si la lectura es coherente; 0 si no. */
int rtc_now(rtc_time_t *out);
/* "HH:MM"; 0 si el CMOS no responde. */
int rtc_text(char *out, size_t capacity);
/* "HH:MM:SS DD/MM/AAAA". */
int rtc_text_full(char *out, size_t capacity);

#endif
