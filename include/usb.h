#ifndef USB_H
#define USB_H
#include <stdint.h>

/* Subsistema USB: controladoras xHCI por PCI, enumeracion de dispositivos,
 * almacenamiento masivo (BOT/SCSI) y lectura de memorias FAT. Todo por sondeo:
 * el kernel no usa IRQ para USB. */
void usb_init(void);
void usb_command(const char *arg);
int usb_available(void);
/* Lectura de sectores del primer medio masivo USB (512 bytes). */
int usb_read(uint32_t lba, void *buffer);
int usb_write(uint32_t lba, const void *buffer);
uint32_t usb_capacity(void);
#endif
