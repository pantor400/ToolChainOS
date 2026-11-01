#ifndef INTERRUPTS_H
#define INTERRUPTS_H
#include <stdint.h>
void interrupts_init(void);
uint64_t timer_ticks(void);
void uptime(void);
void exception_test(int page);
#endif
