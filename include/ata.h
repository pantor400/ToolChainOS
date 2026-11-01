#ifndef ATA_H
#define ATA_H
#include <stdint.h>
void ata_init(void);
int ata_read(uint32_t lba,void *buffer);
int ata_write(uint32_t lba,const void *buffer);
int ata_available(void);
uint32_t ata_capacity(void);
const char *ata_error(void);
void disk_command(const char *arg);
#endif
