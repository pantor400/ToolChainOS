#include "mmio.h"
#include "pmm.h"
#include "util.h"

/* Paginacion de 4 niveles heredada de Limine. Se recorre desde CR3 (direccion
 * fisica) y se completan las tablas que falten; nunca se mueve lo ya mapeado,
 * solo se afina un mapeo de 1 GiB/2 MiB a 4 KiB cuando hace falta. */

#define PAGE_MASK      0xfffULL
#define ENTRY_PRESENT  1ULL
#define ENTRY_WRITE    2ULL
#define ENTRY_HUGE     0x80ULL
#define ENTRY_PWT      (1ULL << 3)
#define ENTRY_PCD      (1ULL << 4)       /* no cacheable: MMIO */
#define ENTRY_GLOBAL   (1ULL << 8)
#define ENTRY_NX       (1ULL << 63)
#define PRESERVE       (ENTRY_WRITE | 4ULL | ENTRY_PWT | ENTRY_GLOBAL | ENTRY_NX)

static uint64_t *table(uint64_t entry) {
    return (uint64_t *)pmm_ptr(entry & ~PAGE_MASK);
}

static void flush(uint64_t virtual_address) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_address) : "memory");
}

/* Crea una tabla vacia; devuelve su direccion virtual y deja en *physical la
 * fisica que hay que escribir en la entrada del nivel superior. */
static uint64_t *fresh_table(uint64_t *physical) {
    uint64_t address = pmm_alloc();
    if (!address) return 0;
    uint64_t *created = table(address);
    if (!created) { pmm_free(address); return 0; }
    zero(created, PMM_PAGE);
    *physical = address;
    return created;
}

/* Un PDPT con PS=1 cubre 1 GiB con paginas de 2 MiB: se baja a una PD que
 * conserva esas mismas paginas grandes. */
static uint64_t *split_gigabyte(uint64_t *pdpt, unsigned index) {
    uint64_t physical = 0;
    uint64_t *pd = fresh_table(&physical);
    if (!pd) return 0;
    uint64_t entry = pdpt[index];
    uint64_t base = entry & ~((1ULL << 30) - 1);
    uint64_t flags = (entry & PRESERVE) | ENTRY_PRESENT | ENTRY_PWT | ENTRY_PCD;
    for (unsigned i = 0; i < 512; i++)
        pd[i] = (base + ((uint64_t)i << 21)) | flags | ENTRY_HUGE;
    pdpt[index] = (physical & ~PAGE_MASK) | ENTRY_PRESENT | ENTRY_WRITE;
    return pd;
}

/* Un PDE con PS=1 cubre 2 MiB con 512 paginas de 4 KiB. */
static uint64_t *split_megabyte(uint64_t *pd, unsigned index) {
    uint64_t physical = 0;
    uint64_t *pt = fresh_table(&physical);
    if (!pt) return 0;
    uint64_t entry = pd[index];
    uint64_t base = entry & ~((1ULL << 21) - 1);
    uint64_t flags = (entry & PRESERVE) | ENTRY_PRESENT | ENTRY_PWT | ENTRY_PCD;
    for (unsigned i = 0; i < 512; i++)
        pt[i] = (base + ((uint64_t)i << 12)) | flags;
    pd[index] = (physical & ~PAGE_MASK) | ENTRY_PRESENT | ENTRY_WRITE;
    return pt;
}

void *mmio_map(uint64_t physical, uint64_t length) {
    if (!physical || !length) return 0;
    uint64_t first = physical & ~PAGE_MASK;
    uint64_t last = (physical + length + PAGE_MASK) & ~PAGE_MASK;
    if (last <= first) return 0;
    /* El HHDM es un mapa directo: vale para cualquier BAR, incluso por encima
     * de 4 GiB, aunque pmm_ptr() solo traduzca RAM por debajo de ese limite. */
    uint64_t hhdm = pmm_hhdm();
    if (!hhdm || hhdm > UINT64_MAX - last) return 0;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = table(cr3);
    if (!pml4) return 0;
    for (uint64_t page = first; page < last; page += PMM_PAGE) {
        if (hhdm > UINT64_MAX - page) return 0;
        uint64_t virtual_address = hhdm + page;
        unsigned i4 = (unsigned)((virtual_address >> 39) & 0x1ff);
        unsigned i3 = (unsigned)((virtual_address >> 30) & 0x1ff);
        unsigned i2 = (unsigned)((virtual_address >> 21) & 0x1ff);
        unsigned i1 = (unsigned)((virtual_address >> 12) & 0x1ff);
        if (!(pml4[i4] & ENTRY_PRESENT)) {
            uint64_t table_physical = 0;
            uint64_t *pdpt = fresh_table(&table_physical);
            if (!pdpt) return 0;
            pml4[i4] = (table_physical & ~PAGE_MASK) | ENTRY_PRESENT | ENTRY_WRITE;
        }
        uint64_t *pdpt = table(pml4[i4]);
        if (!pdpt) return 0;
        if (!(pdpt[i3] & ENTRY_PRESENT) || (pdpt[i3] & ENTRY_HUGE)) {
            if (pdpt[i3] & ENTRY_HUGE) {
                if (!split_gigabyte(pdpt, i3)) return 0;
            } else {
                uint64_t table_physical = 0;
                uint64_t *pd = fresh_table(&table_physical);
                if (!pd) return 0;
                pdpt[i3] = (table_physical & ~PAGE_MASK) | ENTRY_PRESENT | ENTRY_WRITE;
            }
        }
        uint64_t *pd = table(pdpt[i3]);
        if (!pd) return 0;
        if (!(pd[i2] & ENTRY_PRESENT) || (pd[i2] & ENTRY_HUGE)) {
            if (pd[i2] & ENTRY_HUGE) {
                if (!split_megabyte(pd, i2)) return 0;
            } else {
                uint64_t table_physical = 0;
                uint64_t *pt = fresh_table(&table_physical);
                if (!pt) return 0;
                pd[i2] = (table_physical & ~PAGE_MASK) | ENTRY_PRESENT | ENTRY_WRITE;
            }
        }
        uint64_t *pt = table(pd[i2]);
        if (!pt) return 0;
        pt[i1] = page | ENTRY_PRESENT | ENTRY_WRITE | ENTRY_PWT | ENTRY_PCD;
        flush(virtual_address);
    }
    return (void *)(hhdm + first);
}
