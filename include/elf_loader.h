#ifndef ELF_LOADER_H
#define ELF_LOADER_H
#include <limine.h>
#include <stdint.h>
#include <stddef.h>

/* Las aplicaciones llegan como modulos Limine (module_path: en limine.conf).
 * Se enlazan como PIE estatico: el cargador las copia a paginas del PMM y
 * aplica las reubicaciones R_X86_64_RELATIVE de .rela.dyn. Corren en ring 0
 * con la tabla que define include/host_api.h. */

/* Imagen ya copiada al PMM y reubicada. bias es la direccion virtual del
 * segmento mas bajo, de modo que un simbolo vale bias + su direccion de
 * enlace; phoff apunta a la tabla de segmentos en memoria. */
typedef struct {
    uint64_t physical, pages, bias, entry, phoff, relocs;
    uint16_t phnum, phentsize;
} elf_image_t;

int elf_init(struct limine_module_response *modules);
/* Carga cualquier ELF64 PIE estatico. Lo usan las apps de host_api y la
 * personalidad Linux (src/linux_abi.c), que no comparten convenio de arranque
 * pero si formato de archivo. */
int elf_load_image(const uint8_t *image, size_t size, elf_image_t *out);
void elf_free_image(elf_image_t *image);
unsigned elf_count(void);
const char *elf_name(unsigned index);
size_t elf_size(unsigned index);
/* Bytes del modulo tal como los entrega Limine, para quien necesite cargarlo
 * con su propio convenio (personalidad Linux). */
const uint8_t *elf_module_data(unsigned index, size_t *size);
int elf_run(unsigned index);
/* Indice del modulo con ese nombre, -1 si no esta en la ISO. */
int elf_find(const char *name);
void elf_list(void);
void elf_command(const char *args);
int elf_busy(void);
/* Restaura el contexto del kernel cuando una app provoca una excepcion. */
void elf_abort(void) __attribute__((noreturn));
/* Trampolin NASM: entra en la app con RDI = host_api_t*. */
void elf_call_app(uint64_t entry, uint64_t stack_top, const void *api);

#endif
