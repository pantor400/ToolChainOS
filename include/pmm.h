#ifndef PMM_H
#define PMM_H
#include <limine.h>
#include <stdint.h>
#define PMM_PAGE 4096ULL
#define PMM_LIMIT (4ULL*1024*1024*1024)
typedef struct {uint64_t total_pages,managed,used,free,reserved,metadata;} pmm_info_t;
void pmm_init(struct limine_memmap_response *map,uint64_t hhdm);
uint64_t pmm_alloc(void);
uint64_t pmm_alloc_run(uint64_t count);
int pmm_free(uint64_t physical);
int pmm_free_run(uint64_t physical,uint64_t count);
void *pmm_ptr(uint64_t physical);
/* Offset del HHDM: las BAR de PCI pueden quedar por encima de 4 GiB, fuera
 * del alcance de pmm_ptr(), pero dentro de la ventana del direct map. */
uint64_t pmm_hhdm(void);
pmm_info_t pmm_info(void);
int pmm_is_usable(uint64_t physical);
/* Comprueba si un rango fisico esta descrito por el mapa de Limine: el HHDM
 * solo mapea lo que aparece ahi, asi que es la guarda antes de tocar MMIO. */
int pmm_map_covers(uint64_t physical, uint64_t length);
void pmm_stats(void);
void pmm_map(void);
void pmm_test(void);
#endif
