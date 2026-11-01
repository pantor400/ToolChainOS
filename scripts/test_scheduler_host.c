#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "tasks.h"
#include "pmm.h"
#include "heap.h"
#include "console.h"
static _Alignas(4096) unsigned char ram[8*1024*1024];
static uint64_t now;
uint64_t timer_ticks(void){return now;}
void console_write(const char *s){fputs(s,stdout);}
void console_dec(uint64_t n){printf("%lu",(unsigned long)n);}
void console_hex(uint64_t n){printf("%lx",(unsigned long)n);}
void console_putc(char c){putchar(c);}
static unsigned done,checks;
static void worker(void *arg){
    volatile uint64_t local=(uintptr_t)arg;
    for(unsigned n=0;n<5;n++){
        assert(local==(uintptr_t)arg+n);local++;checks++;thread_yield();
    }
    thread_sleep(3);assert(local==(uintptr_t)arg+5);done++;
}
int main(void){
    struct limine_memmap_entry e={.base=4096,.length=sizeof(ram)-4096,.type=LIMINE_MEMMAP_USABLE};
    struct limine_memmap_entry *es[]={&e};
    struct limine_memmap_response map={.entry_count=1,.entries=es};
    pmm_init(&map,(uintptr_t)ram);tasks_init();uint64_t baseline=pmm_info().free;
    uint64_t previous=1;
    for(unsigned repeat=0;repeat<30;repeat++){
        done=checks=0;uint64_t pid[15];
        for(unsigned n=0;n<15;n++){
            pid[n]=process_create("capacity");assert(pid[n]>previous);previous=pid[n];
        }
        assert(!process_create("overflow"));
        assert(!thread_create(1,"protected",worker,0));
        assert(!thread_create(UINT64_MAX,"bad-owner",worker,0));
        assert(!thread_create(pid[0],"null-entry",0,0));
        for(unsigned n=0;n<15;n++)assert(thread_create(pid[n],"worker",worker,(void*)(uintptr_t)(n+100)));
        assert(!thread_create(pid[0],"overflow",worker,0));
        for(unsigned n=0;n<300;n++){now++;tasks_poll();}
        assert(done==15&&checks==75);
        assert(pmm_info().free==baseline&&heap_info().pages==0);
        assert(!thread_create(pid[0],"terminated-owner",worker,0));
    }
    uint64_t held[4096];unsigned count=0;
    while(pmm_info().free>10){held[count++]=pmm_alloc();assert(held[count-1]);}
    puts("EXPECTED_FAILURE_BEGIN partial creation OOM");
    scheduler_command("TEST");
    puts("EXPECTED_FAILURE_END");
    assert(pmm_info().free==10&&heap_info().pages==0);
    while(count)assert(pmm_free(held[--count]));
    assert(pmm_info().free==baseline);
    uint64_t pending[15];
    for(unsigned n=0;n<15;n++)assert((pending[n]=process_create("post-rollback")));
    assert(!process_create("overflow"));
    for(unsigned n=0;n<15;n++)assert(thread_create(pending[n],"worker",worker,(void*)(uintptr_t)n));
    for(unsigned n=0;n<300;n++){now++;tasks_poll();}
    assert(pmm_info().free==baseline);
    process_list();thread_list();scheduler_command("");
    puts("PASSED scheduler capacity PCB/TCB 15, invalid owners, 450 real thread lifecycles, 2250 yields/local checks, reclaimed stacks");
}
