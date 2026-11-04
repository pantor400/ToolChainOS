#include "ai_serial.h"
#include "io.h"
#include "interrupts.h"
#include "console.h"
#include "util.h"
#include "agent.h"
#define UART 0x2f8
#define MAX 1024
/* Sesion de agente: pasos acotados y espera maxima por paso. */
#define AGENT_MAX_STEPS 8
#define AGENT_STEP_TICKS 6000
static uint8_t rx[MAX+12];
static unsigned used;
static uint32_t next_id,request;
static uint64_t deadline,last_byte;
static int pending,uart_present;
static int agent_open;
static uint32_t agent_id;
static uint64_t agent_deadline;
static unsigned agent_steps;
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
int ai_agent_active(void){return agent_open;}
int ai_busy(void){return pending||agent_open;}
static void agent_close(const char *reason){
    if(!agent_open)return;
    agent_open=0;
    console_write("\nAGENTE FIN: ");console_write(reason);console_putc('\n');
}
static void agent_observe(const char *text){
    if(!frame(5,agent_id,text)){agent_open=0;console_write("\nAGENTE FIN: UART NO DISPONIBLE\n");return;}
    console_write("AGENTE: OBSERVACION ");console_dec(slen(text));console_write(" BYTES ENVIADA.\n");
}
void ai_agent_start(const char *goal){
    if(!uart_present){console_write("ERROR AGENTE: COM2 NO DISPONIBLE.\n");return;}
    if(pending||agent_open){console_write("IA OCUPADA; IA CANCEL PARA CANCELAR.\n");return;}
    const char *text=goal;while(*text==' ')text++;
    if(!*text){agent_catalog();console_write("USO: AGENTE <OBJETIVO> | AGENTE ESCRITURA ON|OFF\n");return;}
    if(slen(text)>AGENT_OBS_MAX){console_write("ERROR AGENTE: OBJETIVO EXCEDE 704 BYTES.\n");return;}
    agent_id=++next_id;if(!agent_id)agent_id=++next_id;
    if(!frame(4,agent_id,text)){console_write("ERROR AGENTE: UART NO DISPONIBLE.\n");return;}
    agent_open=1;agent_steps=AGENT_MAX_STEPS;agent_deadline=timer_ticks()+AGENT_STEP_TICKS;
    console_write("AGENTE INICIADO ID=");console_dec(agent_id);console_write(" PASOS=");console_dec(agent_steps);
    console_write("\nOBJETIVO: ");console_write(text);console_putc('\n');
    console_write("ESPERANDO ORDENES POR COM2; IA CANCEL PARA ABORTAR.\n");
}
static void agent_step(const char *command,unsigned n){
    if(!agent_steps){agent_close("LIMITE DE PASOS ALCANZADO");return;}
    agent_steps--;
    if(n>AGENT_CMD_MAX){agent_observe("RECHAZADO: ORDEN DEMASIADO LARGA (>160 BYTES).");agent_deadline=timer_ticks()+AGENT_STEP_TICKS;return;}
    char text[AGENT_CMD_MAX+1];
    for(unsigned i=0;i<n;i++)text[i]=(char)command[i];
    text[n]=0;
    char observation[AGENT_OBS_MAX];
    (void)agent_execute(text,observation,sizeof(observation));
    agent_observe(observation);
    agent_deadline=timer_ticks()+AGENT_STEP_TICKS;
}
static void agent_done(const char *text,unsigned n){
    if(!agent_open)return;
    agent_open=0;
    console_write("\nAGENTE FIN");
    if(n){console_write(": ");console_write_text(text,n);}
    console_putc('\n');
}
void ai_command(const char *arg){
    if(ci_same(arg,"CANCEL")){
        if(agent_open){agent_open=0;console_write("AGENTE CANCELADO; RESPUESTAS TARDIAS DESCARTADAS.\n");}
        else if(pending){frame(3,request,"");pending=0;used=0;console_write("IA CANCELADA; RESPUESTAS TARDIAS DESCARTADAS.\n");}
        else console_write("IA SIN SOLICITUD ACTIVA.\n");
        return;
    }
    if(agent_open){console_write("AGENTE ACTIVO; IA CANCEL PARA CANCELAR.\n");return;}
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
            if(agent_open&&get32(rx+4)==agent_id&&(rx[3]==0x84||rx[3]==0x85)){
                /* 0x84 = orden a ejecutar; 0x85 = la IA cierra la sesion. */
                if(rx[3]==0x84)agent_step((const char*)rx+12,n);
                else agent_done((const char*)rx+12,n);
                event=1;
            }else if(pending&&get32(rx+4)==request&&(rx[3]==0x81||rx[3]==0x82||rx[3]==0xff)){
                pending=0;console_write(rx[3]==0xff?"\nERROR IA: ":"\nIA RESPONSE: ");
                /* The complete TC/1 payload is UTF-8 text, not bytewise ASCII. */
                console_write_text((const char*)rx+12,n);
                console_write("\nIA END\n");event=1;
            }
            unsigned remain=used-(n+12);for(unsigned i=0;i<remain;i++)rx[i]=rx[n+12+i];used=remain;
        }
    }
    if(pending&&timer_ticks()>=deadline){pending=0;used=0;console_write("\nERROR IA: TIMEOUT; PUENTE O MODELO NO DISPONIBLE\nIA END\n");event=1;}
    if(agent_open&&timer_ticks()>=agent_deadline){agent_close("TIMEOUT SIN ORDEN DEL PUENTE");event=1;}
    return event;
}
