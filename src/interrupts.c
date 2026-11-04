#include "interrupts.h"
#include "console.h"
#include "elf_loader.h"
#include "linux_abi.h"
#include "mouse.h"
#include "io.h"
#include "util.h"
struct gate {uint16_t lo,cs;uint8_t ist,flags;uint16_t mid;uint32_t hi,reserved;} __attribute__((packed));
static struct gate idt[256];
static volatile uint64_t ticks;
extern void *isr_table[];
extern void isr_ignore(void);
uint64_t timer_ticks(void){return ticks;}
static void io_wait(void){outb(0x80,0);}
static void pic(uint16_t port,uint8_t value){outb(port,value);io_wait();}
void interrupts_init(void){
    uint16_t cs;__asm__ volatile("mov %%cs,%0":"=r"(cs));
    for(int i=0;i<256;i++){
        uint64_t p=(uint64_t)(i<48?isr_table[i]:(void*)isr_ignore);
        idt[i]=(struct gate){p&65535,cs,0,0x8e,(p>>16)&65535,p>>32,0};
    }
    struct {uint16_t size;uint64_t base;} __attribute__((packed)) desc={sizeof(idt)-1,(uint64_t)idt};
    __asm__ volatile("lidt %0"::"m"(desc):"memory");
    pic(0x20,0x11);pic(0xa0,0x11);pic(0x21,0x20);pic(0xa1,0x28);
    pic(0x21,4);pic(0xa1,2);pic(0x21,1);pic(0xa1,1);
    /* IRQ0 (PIT) e IRQ2 (cascada al PIC esclavo); IRQ1 sigue enmascarada y el
     * teclado se sondea como antes. Sin la cascada, la IRQ12 del raton nunca
     * llega al procesador y el puntero solo avanza cuando el bucle grafico
     * sondea: se queda atras al mover rapido. */
    pic(0x21,0xfa);pic(0xa1,0xff);
    outb(0x43,0x36);outb(0x40,11932&255);outb(0x40,11932>>8);
    __asm__ volatile("sti");
}
struct frame {uint64_t r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,vector,error,rip,cs,flags;};
void interrupt_dispatch(struct frame *f){
    if(f->vector==32){ticks++;outb(0x20,0x20);return;}
    if(f->vector>=32&&f->vector<48){
        if(f->vector==33||f->vector==44)mouse_irq(); /* IRQ1/IRQ12: byte del PS/2 */
        if(f->vector>=40)outb(0xa0,0x20);
        outb(0x20,0x20);
        return;
    }
    /* Un binario Linux faulty se descarta igual que una app: la shell sigue
     * viva y el PMM recupera lo asignado al desenrollar su pila. */
    if(linux_busy()){
        console_write("\nLINUX EXCEPTION VECTOR=");console_dec(f->vector);console_write(" ERROR=");console_hex(f->error);
        console_write(" RIP=");console_hex(f->rip);
        if(f->vector==14){uint64_t cr2;__asm__ volatile("mov %%cr2,%0":"=r"(cr2));console_write(" CR2=");console_hex(cr2);}
        console_write(" PROGRAMA ABORTADO\n");
        linux_abort();
    }
    /* Una aplicacion ELF faulty no debe detener el sistema: se descarta su
     * pila y se vuelve al bucle de la interfaz con el codigo de error. */
    if(elf_busy()){
        console_write("\nAPP EXCEPTION VECTOR=");console_dec(f->vector);console_write(" ERROR=");console_hex(f->error);
        console_write(" RIP=");console_hex(f->rip);console_write(" APLICACION ABORTADA\n");
        elf_abort();
    }
    console_write("\nEXCEPTION VECTOR=");console_dec(f->vector);console_write(" ERROR=");console_hex(f->error);
    console_write(" RIP=");console_hex(f->rip);
    if(f->vector==14){uint64_t cr2;__asm__ volatile("mov %%cr2,%0":"=r"(cr2));console_write(" CR2=");console_hex(cr2);}
    console_write("\nFATAL: CPU DETENIDA\n");__asm__ volatile("cli");for(;;)__asm__ volatile("hlt");
}
void uptime(void){uint64_t t=ticks;console_write("UPTIME TICKS=");console_dec(t);console_write(" HZ=100 SECONDS=");console_dec(t/100);console_putc('\n');}
void exception_test(int page){
    if(page){volatile uint64_t *p=(uint64_t*)0x400000000000ULL;*p=1;}
    else __asm__ volatile("xor %%edx,%%edx; mov $1,%%eax; xor %%ecx,%%ecx; div %%ecx":::"rax","rcx","rdx","cc");
}
