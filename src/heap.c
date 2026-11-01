#include "heap.h"
#include "pmm.h"
#include "console.h"
#include "util.h"
#define HEAP_PAGES 128ULL
#define MAX_ARENA_PAGES 64ULL
#define MAGIC 0x48454150424c4f43ULL
typedef struct block {uint64_t magic,size,requested,free;struct block *next,*prev;} block_t;
typedef struct arena {struct arena *next;uint64_t physical,pages;block_t *first;} arena_t;
_Static_assert(sizeof(block_t)%16==0,"aligned block");
_Static_assert(sizeof(arena_t)%16==0,"aligned arena");
static arena_t *arenas;
static uint64_t page_count,invalid_frees,failures;
int heap_check(void){
    uint64_t seen=0;
    for(arena_t *a=arenas;a;a=a->next){
        if(++seen>HEAP_PAGES||!a->pages||a->pages>MAX_ARENA_PAGES)return 0;
        uintptr_t end=(uintptr_t)a+a->pages*PMM_PAGE;
        block_t *prev=0;
        for(block_t *b=a->first;b;prev=b,b=b->next){
            uintptr_t x=(uintptr_t)b;
            if(x<(uintptr_t)a+sizeof(*a)||x>end-sizeof(*b)||x%16||b->magic!=MAGIC||b->prev!=prev||b->size>end-x-sizeof(*b)||b->requested>b->size)return 0;
            if(b->next&&(uintptr_t)b->next!=x+sizeof(*b)+b->size)return 0;
            if(!b->next&&x+sizeof(*b)+b->size!=end)return 0;
        }
    }
    return 1;
}
static void *take(block_t *b,size_t need,size_t requested){
    if(b->size>=need+sizeof(*b)+16){
        block_t *next=(block_t*)((uint8_t*)(b+1)+need);
        *next=(block_t){MAGIC,b->size-need-sizeof(*b),0,1,b->next,b};
        if(next->next)next->next->prev=next;
        b->next=next;b->size=need;
    }
    b->free=0;b->requested=requested;return b+1;
}
void *kmalloc(size_t size){
    if(!size)return 0;
    if(size>MAX_ARENA_PAGES*PMM_PAGE-sizeof(arena_t)-sizeof(block_t)||!heap_check()){failures++;return 0;}
    size_t need=(size+15)&~(size_t)15;
    for(arena_t *a=arenas;a;a=a->next)
        for(block_t *b=a->first;b;b=b->next)if(b->free&&b->size>=need)return take(b,need,size);
    uint64_t n=(need+sizeof(arena_t)+sizeof(block_t)+PMM_PAGE-1)/PMM_PAGE;
    if(n<4)n=4;
    if(n>MAX_ARENA_PAGES||n>HEAP_PAGES-page_count){failures++;return 0;}
    uint64_t p=pmm_alloc_run(n);if(!p){failures++;return 0;}
    arena_t *a=pmm_ptr(p);a->next=arenas;a->physical=p;a->pages=n;
    a->first=(block_t*)(a+1);*a->first=(block_t){MAGIC,n*PMM_PAGE-sizeof(*a)-sizeof(block_t),0,1,0,0};
    arenas=a;page_count+=n;return take(a->first,need,size);
}
void *kcalloc(size_t count,size_t size){
    if(size&&count>SIZE_MAX/size){failures++;return 0;}
    size_t n=count*size;void *p=kmalloc(n);if(p)zero(p,n);return p;
}
void kfree(void *ptr){
    if(!ptr)return;
    if(!heap_check()){invalid_frees++;return;}
    arena_t **link=&arenas;
    for(arena_t *a=*link;a;link=&a->next,a=a->next){
        for(block_t *b=a->first;b;b=b->next){
            /* No dereference of untrusted ptr; only exact live payloads accepted. */
            if((void*)(b+1)!=ptr)continue;
            if(b->free){invalid_frees++;return;}
            b->free=1;b->requested=0;
            if(b->next&&b->next->free){
                block_t *n=b->next;b->size+=sizeof(*n)+n->size;b->next=n->next;
                if(b->next)b->next->prev=b;
            }
            if(b->prev&&b->prev->free){
                block_t *prev=b->prev;prev->size+=sizeof(*b)+b->size;prev->next=b->next;
                if(prev->next)prev->next->prev=prev;
                b=prev;
            }
            if(b==a->first&&!b->next){
                uint64_t p=a->physical,n=a->pages;*link=a->next;
                page_count-=n;pmm_free_run(p,n);
            }
            return;
        }
    }
    invalid_frees++;
}
heap_info_t heap_info(void){
    heap_info_t s={0};s.pages=page_count;s.invalid_frees=invalid_frees;s.failures=failures;
    for(arena_t *a=arenas;a;a=a->next){
        s.arenas++;
        for(block_t *b=a->first;b;b=b->next){s.capacity+=b->size;if(!b->free){s.blocks++;s.used+=b->requested;}}
    }
    return s;
}
void heap_stats(void){
    heap_info_t s=heap_info();
    console_write("HEAP PAGES=");console_dec(s.pages);console_write(" BLOCKS=");console_dec(s.blocks);
    console_write(" USED_BYTES=");console_dec(s.used);console_write(" ARENA_CAPACITY=");console_dec(s.capacity);
    console_write(" INVALID_FREE=");console_dec(s.invalid_frees);console_write(" FAILURES=");console_dec(s.failures);
    console_write(" LIMIT_PAGES=128 ALIGN=16 CHECK=");console_dec(heap_check());console_putc('\n');
}
void heap_test(void){
    pmm_info_t before=pmm_info();heap_info_t h=heap_info();
    void *p[32]={0};size_t sizes[]={1,17,63,128,1024,8193};
    int ok=!kmalloc(0)&&!kcalloc(SIZE_MAX,2)&&!kmalloc(SIZE_MAX);
    for(unsigned i=0;i<6;i++){
        p[i]=kmalloc(sizes[i]);
        if(!p[i]||(uintptr_t)p[i]%16){ok=0;break;}
        for(size_t n=0;n<sizes[i];n++)((uint8_t*)p[i])[n]=(uint8_t)(i+n);
    }
    for(unsigned i=0;i<6;i++)if(p[i])for(size_t n=0;n<sizes[i];n++)if(((uint8_t*)p[i])[n]!=(uint8_t)(i+n))ok=0;
    if(p[1])kfree((uint8_t*)p[1]+1);
    for(unsigned i=0;i<6;i++){kfree(p[i]);p[i]=0;}
    uint8_t *z=kcalloc(37,13);if(!z)ok=0;
    else {for(unsigned i=0;i<481;i++)if(z[i])ok=0;kfree(z);}
    unsigned n=0;
    while(n<32&&(p[n]=kmalloc(65536)))n++;
    if(n==32||!heap_check())ok=0;
    while(n)kfree(p[--n]);
    if(!heap_check()||heap_info().blocks!=h.blocks||heap_info().pages!=h.pages||pmm_info().used!=before.used)ok=0;
    console_write(ok?"HEAPTEST PASSED":"HEAPTEST FAILED");
    console_write(" DATA ALIGN CALLOC SPLIT COALESCE BOUNDED_OOM RECOVERY\n");
}
