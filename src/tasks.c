#include "tasks.h"
#include "pmm.h"
#include "heap.h"
#include "interrupts.h"
#include "console.h"
#include "util.h"
#define SLOTS 16
#define STACK_PAGES 4
#define CANARY 0x535441434b475541ULL
/* All records owned by BSP shell/scheduler, never mutated in IRQ context. */
typedef struct {
    uint64_t pid;char name[24];thread_state_t state;
    unsigned live,created;uint64_t stack_pages,dispatches;
} pcb_t;
typedef struct {
    uint64_t tid,*sp,physical,wake,switches,ticks;
    pcb_t *owner;thread_state_t state;
    void (*entry)(void *);void *arg;char name[24];int queued;
} tcb_t;
static pcb_t pcbs[SLOTS];
static tcb_t tcbs[SLOTS];
static unsigned queue[SLOTS],head,length;
static int current=-1;
static uint64_t next_pid=2,next_tid=2,*shell_sp,dispatches,idle_polls,integrity_errors;
static unsigned legacy_steps[2];
static uint64_t legacy_tids[2];
static int legacy_slots[2]={-1,-1};
extern void context_switch(uint64_t **old,uint64_t *next);
extern int context_probe(void (*yield_fn)(void));
static const char *state_name(thread_state_t s){
    static const char *names[]={"UNUSED","NEW","READY","RUNNING","WAITING","TERMINATED"};
    return names[s];
}
static void enqueue(unsigned slot){
    if(tcbs[slot].queued||tcbs[slot].state!=TS_READY||length>=SLOTS){integrity_errors++;return;}
    queue[(head+length)%SLOTS]=slot;length++;tcbs[slot].queued=1;
}
void tasks_init(void){
    zero(pcbs,sizeof(pcbs));zero(tcbs,sizeof(tcbs));
    pcbs[0].pid=1;copystr(pcbs[0].name,"kernel-shell",24);pcbs[0].state=TS_RUNNING;pcbs[0].live=1;pcbs[0].created=1;
    tcbs[0].tid=1;tcbs[0].owner=&pcbs[0];tcbs[0].state=TS_RUNNING;copystr(tcbs[0].name,"shell",24);
}
uint64_t process_create(const char *name){
    if(!next_pid)return 0;
    for(unsigned i=1;i<SLOTS;i++)if(!pcbs[i].pid||(pcbs[i].state==TS_TERMINATED&&!pcbs[i].live)){
        pcb_t *p=&pcbs[i];zero(p,sizeof(*p));p->pid=next_pid++;copystr(p->name,name,24);p->state=TS_NEW;return p->pid;
    }
    return 0;
}
uint64_t thread_create(uint64_t pid,const char *name,void (*entry)(void *),void *arg);
static void thread_boot(void){
    tcb_t *t=&tcbs[current];t->entry(t->arg);thread_exit();
}
uint64_t thread_create(uint64_t pid,const char *name,void (*entry)(void *),void *arg){
    if(!entry||!next_tid||pid==1)return 0;
    pcb_t *owner=0;
    for(unsigned i=1;i<SLOTS;i++)if(pcbs[i].pid==pid&&pcbs[i].state!=TS_TERMINATED)owner=&pcbs[i];
    if(!owner)return 0;
    for(unsigned i=1;i<SLOTS;i++)if(!tcbs[i].tid||(!tcbs[i].physical&&tcbs[i].state==TS_TERMINATED)){
        uint64_t p=pmm_alloc_run(STACK_PAGES);if(!p)return 0;
        tcb_t *t=&tcbs[i];zero(t,sizeof(*t));t->tid=next_tid++;t->physical=p;t->owner=owner;
        t->entry=entry;t->arg=arg;copystr(t->name,name,24);
        uint8_t *base=pmm_ptr(p);zero(base,STACK_PAGES*PMM_PAGE);*(uint64_t*)base=CANARY;
        /* Six saved callee registers + RET; bootstrap RSP == 8 mod 16 (SysV). */
        uint64_t *sp=(uint64_t*)(base+STACK_PAGES*PMM_PAGE-64);sp[6]=(uint64_t)thread_boot;
        t->sp=sp;t->state=TS_READY;owner->live++;owner->created++;owner->stack_pages+=STACK_PAGES;
        owner->state=TS_READY;enqueue(i);return t->tid;
    }
    return 0;
}
static void leave(thread_state_t state){
    if(current<1)return;
    tcbs[current].state=state;
    context_switch(&tcbs[current].sp,shell_sp);
}
void thread_yield(void){leave(TS_READY);}
void thread_sleep(uint64_t delay){
    if(current<1)return;
    if(delay>1000000)delay=1000000;
    tcbs[current].wake=timer_ticks()+delay;
    leave(delay?TS_WAITING:TS_READY);
}
void thread_exit(void){
    leave(TS_TERMINATED);
    for(;;)__asm__ volatile("pause");
}
static int stack_ok(tcb_t *t){
    uintptr_t base=(uintptr_t)pmm_ptr(t->physical),sp=(uintptr_t)t->sp;
    return *(uint64_t*)base==CANARY&&sp>=base+64&&sp<base+STACK_PAGES*PMM_PAGE;
}
static void finish_test(void);
void tasks_poll(void){
    finish_test();
    for(unsigned i=1;i<SLOTS;i++)if(tcbs[i].state==TS_WAITING&&timer_ticks()>=tcbs[i].wake){
        tcbs[i].state=TS_READY;enqueue(i);
    }
    if(!length){idle_polls++;return;} /* Safe idle: shell remains available, no spin in a dead thread. */
    unsigned slot=queue[head];head=(head+1)%SLOTS;length--;tcb_t *t=&tcbs[slot];t->queued=0;
    if(t->state!=TS_READY||!t->physical){integrity_errors++;return;}
    if(!stack_ok(t)){integrity_errors++;console_write("SCHED STACK CORRUPTION; HALTED\n");__asm__ volatile("cli");for(;;)__asm__ volatile("hlt");}
    current=(int)slot;t->state=TS_RUNNING;t->owner->state=TS_RUNNING;tcbs[0].state=TS_READY;
    uint64_t start=timer_ticks();dispatches++;t->switches++;t->owner->dispatches++;
    context_switch(&shell_sp,t->sp);
    t->ticks+=timer_ticks()-start;current=-1;tcbs[0].state=TS_RUNNING;
    if(!stack_ok(t)){integrity_errors++;console_write("SCHED STACK CORRUPTION; HALTED\n");__asm__ volatile("cli");for(;;)__asm__ volatile("hlt");}
    if(t->state==TS_READY)enqueue(slot);
    if(t->state==TS_TERMINATED){
        /* Reap on SHELL stack, never free the currently executing stack. */
        pmm_free_run(t->physical,STACK_PAGES);t->physical=0;t->sp=0;
        t->owner->live--;t->owner->stack_pages-=STACK_PAGES;
    }
    for(unsigned i=1;i<SLOTS;i++)if(pcbs[i].pid){
        pcb_t *p=&pcbs[i];int ready=0,waiting=0;
        for(unsigned j=1;j<SLOTS;j++)if(tcbs[j].owner==p){
            ready|=tcbs[j].state==TS_READY;waiting|=tcbs[j].state==TS_WAITING;
        }
        if(!p->live&&p->created)p->state=TS_TERMINATED;
        else if(p->live)p->state=ready?TS_READY:(waiting?TS_WAITING:TS_RUNNING);
    }
}
void process_list(void){
    console_write("PROCESSES KERNEL SHARED_ADDRESS_SPACE NO_USER_ISOLATION\n");
    for(unsigned i=0;i<SLOTS;i++)if(pcbs[i].pid){
        pcb_t *p=&pcbs[i];console_write("PID=");console_dec(p->pid);console_write(" NAME=");console_write(p->name);
        console_write(" STATE=");console_write(state_name(p->state));console_write(" LIVE=");console_dec(p->live);
        console_write(" STACK_PAGES=");console_dec(p->stack_pages);console_write(" DISPATCHES=");console_dec(p->dispatches);console_putc('\n');
    }
}
void thread_list(void){
    for(unsigned i=0;i<SLOTS;i++)if(tcbs[i].tid){
        tcb_t *t=&tcbs[i];console_write("TID=");console_dec(t->tid);console_write(" PID=");console_dec(t->owner->pid);
        console_write(" NAME=");console_write(t->name);console_write(" STATE=");console_write(state_name(t->state));
        console_write(" STACK=");console_hex(t->physical);console_write(" SWITCHES=");console_dec(t->switches);
        console_write(" TICKS=");console_dec(t->ticks);console_putc('\n');
    }
}
static void rollback_new_process(uint64_t pid,int free_args){
    if(!pid)return;
    for(unsigned i=1;i<SLOTS;i++)if(tcbs[i].physical&&tcbs[i].owner->pid==pid&&tcbs[i].state==TS_READY){
        if(free_args)kfree(tcbs[i].arg);
        pmm_free_run(tcbs[i].physical,STACK_PAGES);
        tcbs[i].owner->live--;tcbs[i].owner->stack_pages-=STACK_PAGES;
        tcbs[i].physical=0;tcbs[i].sp=0;tcbs[i].state=TS_TERMINATED;tcbs[i].queued=0;
    }
    unsigned kept[SLOTS],n=0;
    for(unsigned i=0;i<length;i++){
        unsigned slot=queue[(head+i)%SLOTS];
        if(tcbs[slot].state==TS_READY)kept[n++]=slot;
    }
    head=0;length=n;for(unsigned i=0;i<n;i++)queue[i]=kept[i];
    for(unsigned i=1;i<SLOTS;i++)if(pcbs[i].pid==pid&&!pcbs[i].live)pcbs[i].state=TS_TERMINATED;
}
/* Existing TASKS command now uses the SAME TCB queue and stack allocator. */
static void legacy_entry(void *arg){
    unsigned i=(unsigned)(uintptr_t)arg;
    for(unsigned k=0;k<3;k++){
        legacy_steps[i]++;console_write("TASK ");console_dec(i+1);console_write(" STEP ");console_dec(legacy_steps[i]);console_putc('\n');
        thread_sleep(5);
    }
}
void tasks_command(const char *arg){
    if(ci_same(arg,"START")){
        for(unsigned i=0;i<2;i++)if(legacy_slots[i]>=0&&tcbs[legacy_slots[i]].tid==legacy_tids[i]&&tcbs[legacy_slots[i]].state!=TS_TERMINATED){
            console_write("ERROR: TAREAS ACTIVAS.\n");return;
        }
        uint64_t pid=process_create("legacy-tasks");
        if(!pid){console_write("ERROR: SIN PCB.\n");return;}
        for(unsigned i=0;i<2;i++){
            legacy_steps[i]=0;uint64_t tid=thread_create(pid,"legacy",legacy_entry,(void*)(uintptr_t)i);
            if(!tid){rollback_new_process(pid,0);console_write("ERROR: SIN RECURSOS; ROLLBACK.\n");return;}
            legacy_tids[i]=tid;
            for(unsigned j=1;j<SLOTS;j++)if(tcbs[j].tid==tid)legacy_slots[i]=(int)j;
        }
        console_write("TASKS START OK: COOPERATIVAS KERNEL, PILAS 16384.\n");return;
    }
    if(*arg){console_write("USO: TASKS [START]\n");return;}
    for(unsigned i=0;i<2;i++){
        int slot=legacy_slots[i];
        console_write("TASK ");console_dec(i+1);console_putc(' ');
        console_write(slot<0?"UNUSED":(tcbs[slot].tid!=legacy_tids[i]||tcbs[slot].state==TS_TERMINATED?"DEAD":state_name(tcbs[slot].state)));
        console_write(" STEPS=");console_dec(legacy_steps[i]);console_putc('\n');
    }
}
typedef struct {unsigned letter;uint64_t expected;} job_t;
static struct {
    int active,failed;unsigned done,count;char trace[64];uint64_t before_pages,before_heap,probes,locals;
} test;
static void demo_entry(void *arg){
    job_t *job=arg;unsigned who=job->letter;uint64_t expected=job->expected;
    volatile uint64_t local=expected;uintptr_t stack_addr=(uintptr_t)&local;
    console_write("THREAD_ENTRY TID=");console_dec(tcbs[current].tid);console_write(" LOCAL=");console_hex(stack_addr);console_putc('\n');
    for(unsigned step=0;step<4;step++){
        if(local!=expected+step||stack_addr!=(uintptr_t)&local)test.failed=1;
        test.locals++;
        if(test.count<sizeof(test.trace)-1)test.trace[test.count++]=(char)('A'+who);else test.failed=1;
        console_write("RR ");console_putc((char)('A'+who));console_write(" STEP=");console_dec(step);
        console_write(" TID=");console_dec(tcbs[current].tid);console_putc('\n');
        local++;
        /* NASM sets RBX,RBP,R12..R15 to sentinels across an actual yield. */
        if(!context_probe(thread_yield))test.failed=1;
        test.probes++;
    }
    thread_sleep(100+who);
    if(local!=expected+4)test.failed=1;
    kfree(job);test.done++; /* normal return => trampoline => thread_exit */
}
static void finish_test(void){
    if(!test.active||test.done!=3)return;
    for(unsigned i=1;i<SLOTS;i++)if(tcbs[i].physical&&tcbs[i].owner->pid!=1&&ci_same(tcbs[i].name,"rr-demo"))return;
    test.trace[test.count]=0;
    for(unsigned i=0;i<test.count;i++)if(test.trace[i]!=(char)('A'+i%3))test.failed=1;
    int ok=!test.failed&&!integrity_errors&&test.count==12&&test.probes==12&&test.locals==12&&
        pmm_info().used==test.before_pages&&heap_info().blocks==test.before_heap&&heap_check();
    console_write(ok?"SCHEDTEST PASSED":"SCHEDTEST FAILED");console_write(" TRACE=");console_write(test.trace);
    console_write(" REG_PROBES=");console_dec(test.probes);console_write(" LOCALS=");console_dec(test.locals);
    console_write(" PMM_RESTORED=");console_dec(pmm_info().used==test.before_pages);console_write(" HEAP_RESTORED=");console_dec(heap_info().blocks==test.before_heap);
    console_write(" TERMINATED=3\n");test.active=0;
}
void scheduler_command(const char *arg){
    finish_test();
    if(!*arg){
        console_write("SCHED ROUND_ROBIN COOPERATIVE PREEMPTIVE=NO READY=");console_dec(length);
        console_write(" DISPATCHES=");console_dec(dispatches);console_write(" IDLE_POLLS=");console_dec(idle_polls);
        console_write(" ERRORS=");console_dec(integrity_errors);console_write(" TEST_ACTIVE=");console_dec(test.active);
        console_write(" PIT_HZ=100\n");return;
    }
    if(!ci_same(arg,"TEST")){console_write("USO: SCHED [TEST]\n");return;}
    for(unsigned i=1;i<SLOTS;i++)if(tcbs[i].physical){console_write("ERROR: HILOS ACTIVOS; ESPERA.\n");return;}
    zero(&test,sizeof(test));test.active=1;test.before_pages=pmm_info().used;test.before_heap=heap_info().blocks;
    uint64_t p1=process_create("demo-memory"),p2=process_create("demo-workers");
    if(!p1||!p2){rollback_new_process(p1,1);rollback_new_process(p2,1);test.active=0;console_write("SCHEDTEST FAILED PCB\n");return;}
    for(unsigned i=0;i<3;i++){
        job_t *job=kcalloc(1,sizeof(*job));
        if(!job){test.failed=1;break;}
        job->letter=i;job->expected=0x123400+i*100;
        if(!thread_create(i<2?p1:p2,"rr-demo",demo_entry,job)){kfree(job);test.failed=1;break;}
    }
    if(test.failed){
        rollback_new_process(p1,1);rollback_new_process(p2,1);
        test.active=0;console_write("SCHEDTEST FAILED ALLOCATION_ROLLBACK\n");return;
    }
    console_write("SCHEDTEST STARTED PROCESSES=2 THREADS=3 COOPERATIVE\n");
    process_list();thread_list();
}
void process_command(const char *arg){
    if(ci_same(arg,"TEST"))scheduler_command("TEST");
    else console_write("USO: PROC TEST\n");
}
/* Called from shell each poll to publish completion only after the final stack is reaped. */
void tasks_test_poll(void){finish_test();}
