#ifndef ELF_LOADER_H
#define ELF_LOADER_H
#include <limine.h>
#include <stdint.h>
#include <stddef.h>

/* Las aplicaciones llegan como modulos Limine (module_path: en limine.conf).
 * Se enlazan como PIE estatico: el cargador las copia a paginas del PMM y
 * aplica las reubicaciones R_X86_64_RELATIVE de .rela.dyn. Corren en ring 0
 * con la tabla que define include/host_api.h. */

int elf_init(struct limine_module_response *modules);
unsigned elf_count(void);
const char *elf_name(unsigned index);
size_t elf_size(unsigned index);
int elf_run(unsigned index);
void elf_list(void);
void elf_command(const char *args);
int elf_busy(void);
/* Restaura el contexto del kernel cuando una app provoca una excepcion. */
void elf_abort(void) __attribute__((noreturn));
/* Trampolin NASM: entra en la app con RDI = host_api_t*. */
void elf_call_app(uint64_t entry, uint64_t stack_top, const void *api);

#endif
