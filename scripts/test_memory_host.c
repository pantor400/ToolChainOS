/* Same PMM/heap sources as freestanding kernel; host supplies only a Limine fixture. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "pmm.h"
#include "heap.h"
#include "console.h"
static _Alignas(4096) unsigned char ram[8*1024*1024];
void console_write(const char *s){fputs(s,stdout);}
void console_dec(uint64_t n){printf("%lu",(unsigned long)n);}
void console_hex(uint64_t n){printf("%lx",(unsigned long)n);}
void console_putc(char c){putchar(c);}
int main(void){
    struct limine_memmap_entry a={.base=4096,.length=sizeof(ram)-4096,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry b={.base=4*4096+3,.length=4096,.type=LIMINE_MEMMAP_RESERVED};
    struct limine_memmap_entry c={.base=UINT64_MAX-8,.length=99,.type=LIMINE_MEMMAP_RESERVED};
    struct limine_memmap_entry *es[]={&a,&b,&c};
    struct limine_memmap_response map={.entry_count=3,.entries=es};
    pmm_init(&map,(uintptr_t)ram);
    assert(!pmm_is_usable(0)&&!pmm_is_usable(4096)&&!pmm_is_usable(4*4096)&&!pmm_is_usable(5*4096));
    uint64_t before=pmm_info().free;
    uint64_t run=pmm_alloc_run(5);assert(run&&pmm_free_run(run,5));
    assert(!pmm_free_run(run,5)&&!pmm_free(UINT64_MAX)&&!pmm_alloc_run(UINT64_MAX));
    uint64_t p=pmm_alloc();assert(p);assert(!pmm_free_run(p,PMM_LIMIT));assert(pmm_free(p));assert(pmm_alloc()==p);assert(pmm_free(p));
    uint64_t ps[4096],n=0;
    while((p=pmm_alloc())){assert(n<4096);ps[n++]=p;assert(pmm_is_usable(p));}
    assert(n==before&&!pmm_alloc());
    while(n)assert(pmm_free(ps[--n]));
    assert(pmm_info().free==before);
    for(int round=0;round<20;round++){
        void *x=kmalloc(123),*y=kmalloc(456),*z=kmalloc(789);assert(x&&y&&z);
        kfree(y);void *r=kmalloc(200);assert(r==y);
        kfree((char*)x+1);kfree((void*)7);
        kfree(r);kfree(r);assert(heap_check());
        kfree(x);kfree(z);assert(heap_info().pages==0);
        heap_test();assert(heap_info().pages==0&&pmm_info().free==before);
    }
    /* Small map: fragmentation and complete exhaustion, not a simulated return. */
    a.length=24*4096;b.base=8*4096;b.length=4096;
    pmm_init(&map,(uintptr_t)ram);before=pmm_info().free;n=0;
    while((p=pmm_alloc()))ps[n++]=p;
    for(uint64_t i=0;i<n;i+=2)assert(pmm_free(ps[i]));
    assert(!pmm_alloc_run(3));
    for(uint64_t i=1;i<n;i+=2)assert(pmm_free(ps[i]));
    assert(pmm_info().free==before);
    puts("PASSED PMM maps reserved overlap overflow exhaustion fragmentation atomic_free; HEAP x20");
}
