#ifndef USB_FAT_H
#define USB_FAT_H
#include <stdint.h>

/* Capa de medio sobre el almacenamiento USB: lee la tabla de particiones,
 * avisa si el volumen esta cifrado o usa un sistema que no se interpreta y
 * monta FAT12/16/32 en modo lectura para listar, mostrar e importar archivos
 * al sistema de archivos en RAM. Nunca intenta descifrar ni saltar
 * protecciones: solo informa. */
void usb_fat_probe(void);
void usb_fat_report(void);
void usb_fat_list(const char *path);
int usb_fat_cat(const char *name);
int usb_fat_import(const char *name);
int usb_fat_mounted(void);
#endif
