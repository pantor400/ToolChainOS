#include "pmm.h"
#include "console.h"
#include "util.h"
/* BSP only. IRQ handlers never allocate; no AP scheduler or asynchronous allocator. */
static uint8_t *usable,*busy;
static uint64_t pages,offset,managed,allocated,metadata,hint;
static struct limine_memmap_response *memory_map;
static int bit(uint8_t *m,uint64_t i){return (m[i/8]>>(i%8))&1;}
static void set(uint8_t *m,uint64_t i,int v){if(v)m[i/8]|=1U<<(i%8);else m[i/8]&=~(1U<<(i%8));}
static uint64_t end_of(struct limine_memmap_entry *e){
    if(e->base>=PMM_LIMIT)return PMM_LIMIT;
    return e->length>PMM_LIMIT-e->base?PMM_LIMIT:e->base+e->length;
}
void *pmm_ptr(uint64_t p){return p<PMM_LIMIT&&offset<=UINT64_MAX-p?(void*)(offset+p):0;}
int pmm_is_usable(uint64_t p){return usable&&p%PMM_PAGE==0&&p/PMM_PAGE<pages&&bit(usable,p/PMM_PAGE);}
void pmm_init(struct limine_memmap_response *map,uint64_t hhdm){
    usable=busy=0;pages=managed=allocated=metadata=0;hint=1;offset=hhdm;memory_map=map;
    if(!map||!map->entries||!hhdm||hhdm>UINT64_MAX-PMM_LIMIT)return;
    uint64_t high=0;
    for(uint64_t i=0;i<map->entry_count;i++){
        struct limine_memmap_entry *e=map->entries[i];
        if(e&&e->type==LIMINE_MEMMAP_USABLE&&end_of(e)>high)high=end_of(e);
    }
    pages=high/PMM_PAGE;
    uint64_t bytes=(pages+7)/8,need=(2*bytes+PMM_PAGE-1)&~(PMM_PAGE-1),base=0;
    if(!bytes)return;
    for(uint64_t i=0;i<map->entry_count&&!base;i++){
        struct limine_memmap_entry *e=map->entries[i];
        if(!e||e->type!=LIMINE_MEMMAP_USABLE||e->base>=PMM_LIMIT)continue;
        uint64_t b=(e->base+PMM_PAGE-1)&~(PMM_PAGE-1);
        if(!b)b=PMM_PAGE;
        /* Bound metadata search; conflicting/non-USABLE entries always win. */
        for(;b<=end_of(e)&&need<=end_of(e)-b;b+=PMM_PAGE){
            int safe=1;
            for(uint64_t j=0;j<map->entry_count;j++){
                struct limine_memmap_entry *r=map->entries[j];
                if(r&&r->type!=LIMINE_MEMMAP_USABLE&&r->length&&r->base<b+need&&end_of(r)>b){safe=0;break;}
            }
            if(safe){base=b;break;}
        }
    }
    if(!base){pages=0;return;}
    usable=pmm_ptr(base);busy=usable+bytes;zero(usable,need);metadata=need/PMM_PAGE;
    for(uint64_t i=0;i<map->entry_count;i++){
        struct limine_memmap_entry *e=map->entries[i];
        if(!e||e->type!=LIMINE_MEMMAP_USABLE||e->base>=PMM_LIMIT)continue;
        uint64_t start=(e->base+PMM_PAGE-1)/PMM_PAGE,end=end_of(e)/PMM_PAGE;
        for(uint64_t p=start;p<end;p++)set(usable,p,1);
    }
    for(uint64_t i=0;i<map->entry_count;i++){
        struct limine_memmap_entry *e=map->entries[i];
        if(!e||e->type==LIMINE_MEMMAP_USABLE||!e->length||e->base>=PMM_LIMIT)continue;
        uint64_t start=e->base/PMM_PAGE,end=(end_of(e)+PMM_PAGE-1)/PMM_PAGE;
        if(end>pages)end=pages;
        for(uint64_t p=start;p<end;p++)set(usable,p,0);
    }
    set(usable,0,0);
    for(uint64_t p=base/PMM_PAGE;p<(base+need)/PMM_PAGE;p++)set(usable,p,0);
    for(uint64_t p=0;p<pages;p++)if(bit(usable,p))managed++;
}
uint64_t pmm_alloc_run(uint64_t count){
    if(!count||count>managed-allocated||!pages)return 0;
    /* First-fit run scan never wraps a run across the end of the bitmap. */
    uint64_t run=0;
    for(uint64_t i=1;i<pages;i++){
        if(bit(usable,i)&&!bit(busy,i))run++;else run=0;
        if(run==count){
            uint64_t first=i+1-count;
            for(uint64_t p=first;p<=i;p++)set(busy,p,1);
            allocated+=count;return first*PMM_PAGE;
        }
    }
    return 0;
}
uint64_t pmm_alloc(void){
    if(allocated>=managed||!pages)return 0;
    for(uint64_t n=0;n<pages;n++){
        uint64_t i=(hint+n)%pages;
        if(i&&bit(usable,i)&&!bit(busy,i)){set(busy,i,1);allocated++;hint=(i+1)%pages;return i*PMM_PAGE;}
    }
    return 0;
}
int pmm_free_run(uint64_t p,uint64_t count){
    if(!p||p%PMM_PAGE||!count||p/PMM_PAGE>=pages||count>pages-p/PMM_PAGE)return 0;
    uint64_t first=p/PMM_PAGE;
    /* Validate ALL pages before changing any bit (atomic failed release). */
    for(uint64_t i=first;i<first+count;i++)if(!bit(usable,i)||!bit(busy,i))return 0;
    for(uint64_t i=first;i<first+count;i++)set(busy,i,0);
    allocated-=count;if(first<hint)hint=first;return 1;
}
int pmm_free(uint64_t p){return pmm_free_run(p,1);}
pmm_info_t pmm_info(void){return (pmm_info_t){pages,managed,allocated,managed-allocated,pages-managed,metadata};}
void pmm_stats(void){
    console_write("PMM PAGE=4096 MANAGED=");console_dec(managed);console_write(" FREE=");console_dec(managed-allocated);
    console_write(" USED=");console_dec(allocated);console_write(" RESERVED=");console_dec(pages-managed);
    console_write(" METADATA_PAGES=");console_dec(metadata);console_write(" CAP_PHYSICAL=4GiB\n");
}
void pmm_map(void){
    if(!memory_map)return;
    for(uint64_t i=0;i<memory_map->entry_count;i++){
        struct limine_memmap_entry *e=memory_map->entries[i];if(!e)continue;
        console_write("REGION BASE=");console_hex(e->base);console_write(" BYTES=");console_dec(e->length);
        console_write(" LIMINE_TYPE=");console_dec(e->type);console_putc('\n');
    }
}
void pmm_test(void){
    uint64_t before=allocated,head=0,count=0,p;
    /* At most 4 MiB, reclaim synchronously; full exhaustion tested on small host maps. */
    while(count<1024&&(p=pmm_alloc())){
        uint64_t *v=pmm_ptr(p);*v=head;v[1]=0x1234abcd;head=p;count++;
    }
    int ok=count>2;
    while(head){uint64_t *v=pmm_ptr(head);p=*v;if(v[1]!=0x1234abcd)ok=0;if(!pmm_free(head))ok=0;head=p;}
    p=pmm_alloc();if(!p||pmm_free(p+1)||!pmm_free(p)||pmm_free(p)||pmm_free(0)||pmm_free(PMM_LIMIT))ok=0;
    p=pmm_alloc_run(4);if(!p||!pmm_free_run(p,4)||pmm_free_run(p,4))ok=0;
    if(allocated!=before)ok=0;
    console_write(ok?"PMMTEST PASSED PAGES=":"PMMTEST FAILED PAGES=");console_dec(count);
    console_write(" BOUNDED=1024 COUNTERS_RESTORED=");console_dec(allocated==before);console_putc('\n');
}
