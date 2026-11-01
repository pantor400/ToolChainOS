#ifndef STORAGE_H
#define STORAGE_H
#include <stdint.h>
void storage_init(void);
int storage_can_write(uint32_t lba);
void storage_sync(void);
void storage_status(void);
#endif
