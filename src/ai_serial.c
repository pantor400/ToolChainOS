#include "ai_serial.h"
#include "io.h"
#include "interrupts.h"
#include "console.h"
#include "util.h"
#define UART 0x2f8
#define MAX 1024
static uint8_t rx[MAX+12];
static unsigned used;
static uint32_t next_id,request;
static uint64_t deadline,last_byte;
static int pending,uart_present;
static uint16_t get16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t get32(const uint8_t *p){return get16(p)|((uint32_t)get16(p+2)<<16);}
static uint16_t sum(const uint8_t *p,unsigned n){uint16_t v=0;for(unsigned i=0;i<n;i++)if(i!=10&&i!=11)v+=p[i];return v;}
void ai_init(void){outb(UART+7,0x5a);uart_present=inb(UART+7)==0x5a;if(!uart_present)return;outb(UART+1,0);outb(UART+3,0x80);outb(UART,1);outb(UART+1,0);outb(UART+3,3);outb(UART+2,0xc7);outb(UART+4,0x0b);}
static int tx(uint8_t b){for(unsigned n=0;n<100000;n++)if(inb(UART+5)&0x20){outb(UART,b);return 1;}return 0;}
static int frame(uint8_t type,uint32_t id,const char *body){
    if(!uart_present)return 0;
    uint8_t packet[MAX+12];size_t n=slen(body);if(n>MAX)return 0;
    packet[0]='T';packet[1]='C';packet[2]=1;packet[3]=type;
    for(int i=0;i<4;i++)packet[4+i]=id>>(i*8);
    packet[8]=n;packet[9]=n>>8;packet[10]=packet[11]=0;
    for(unsigned i=0;i<n;i++)packet[12+i]=body[i];
    uint16_t s=sum(packet,n+12);packet[10]=s;packet[11]=s>>8;
    for(unsigned i=0;i<n+12;i++)if(!tx(packet[i]))return 0;
    return 1;
}
void ai_command(const char *arg){
    if(ci_same(arg,"CANCEL")){
        if(pending){frame(3,request,"");pending=0;used=0;console_write("IA CANCELADA; RESPUESTAS TARDIAS DESCARTADAS.\n");}
        else console_write("IA SIN SOLICITUD ACTIVA.\n");
        return;
    }
    if(pending){console_write("IA OCUPADA; IA CANCEL PARA CANCELAR.\n");return;}
    int status=ci_same(arg,"STATUS");
    if(!*arg){console_write("USO: IA pregunta | IA STATUS | IA CANCEL\n");return;}
    if(slen(arg)>MAX){console_write("ERROR IA: PREGUNTA EXCEDE 1024 BYTES.\n");return;}
    request=++next_id;if(!request)request=++next_id;
    if(!frame(status?2:1,request,status?"":arg)){console_write("ERROR IA: UART NO DISPONIBLE.\n");return;}
    pending=1;deadline=timer_ticks()+(status?200:12000);
    console_write("IA REQUEST ");console_dec(request);console_write(status?" STATUS: VERIFICANDO PUENTE/MODELO\n":" ENVIADA (COM2); SHELL DISPONIBLE\n");
}
static void discard(void){if(used){for(unsigned i=1;i<used;i++)rx[i-1]=rx[i];used--;}}
int ai_poll(void){
    if(!uart_present)return 0;
    int event=0;
    if(used&&timer_ticks()-last_byte>200)used=0;
    for(unsigned budget=0;budget<256&&(inb(UART+5)&1);budget++){
        uint8_t c=inb(UART);last_byte=timer_ticks();
        if(used==sizeof(rx))discard();
        rx[used++]=c;
        for(;;){
            if(used<2)break;
            if(rx[0]!='T'||rx[1]!='C'){discard();continue;}
            if(used<12)break;
            unsigned n=get16(rx+8);
            if(rx[2]!=1||n>MAX){discard();continue;}
            if(used<n+12)break;
            if(get16(rx+10)!=sum(rx,n+12)){discard();continue;}
            if(pending&&get32(rx+4)==request&&(rx[3]==0x81||rx[3]==0x82||rx[3]==0xff)){
                pending=0;console_write(rx[3]==0xff?"\nERROR IA: ":"\nIA RESPONSE: ");
                /* The complete TC/1 payload is UTF-8 text, not bytewise ASCII. */
                console_write_text((const char*)rx+12,n);
                console_write("\nIA END\n");event=1;
            }
            unsigned remain=used-(n+12);for(unsigned i=0;i<remain;i++)rx[i]=rx[n+12+i];used=remain;
        }
    }
    if(pending&&timer_ticks()>=deadline){pending=0;used=0;console_write("\nERROR IA: TIMEOUT; PUENTE O MODELO NO DISPONIBLE\nIA END\n");event=1;}
    return event;
}
