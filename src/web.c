#include "web.h"
#include "io.h"
#include "interrupts.h"
#include "util.h"

#define UART 0x3e8
#define FRAME_MAX 1024
static uint8_t rx[FRAME_MAX+12], tx[FRAME_MAX+12], document[WEB_DOC_MAX];
static unsigned used, tx_used, tx_pos, doc_used;
static uint32_t next_id, request;
static uint64_t deadline, last_byte;
static int initialized, present, pending, acknowledged, result;
static uint16_t get16(const uint8_t *p) { return p[0] | ((uint16_t)p[1]<<8); }
static uint32_t get32(const uint8_t *p) { return get16(p) | ((uint32_t)get16(p+2)<<16); }
static uint16_t checksum(const uint8_t *p, unsigned n) {
    uint16_t s=0;
    for(unsigned i=0;i<n;i++) if(i!=10 && i!=11) s+=p[i];
    return s;
}
static void init(void) {
    if(initialized) return;
    initialized=1;
    outb(UART+7,0x5a); present=inb(UART+7)==0x5a;
    if(!present) return;
    outb(UART+1,0); outb(UART+3,0x80); outb(UART,1);
    outb(UART+1,0); outb(UART+3,3); outb(UART+2,0xc7); outb(UART+4,0x0b);
}
static void packet(unsigned kind, const char *body, unsigned n) {
    tx[0]='T'; tx[1]='W'; tx[2]=1; tx[3]=kind;
    for(unsigned i=0;i<4;i++) tx[4+i]=request>>(i*8);
    tx[8]=n; tx[9]=n>>8; tx[10]=tx[11]=0;
    for(unsigned i=0;i<n;i++) tx[12+i]=(uint8_t)body[i];
    uint16_t s=checksum(tx,n+12); tx[10]=s; tx[11]=s>>8;
    tx_used=n+12; tx_pos=0;
}
void web_cancel(void) {
    /* Best-effort cancellation only if the previous frame was fully sent.
     * Never wait for UART readiness; request IDs discard all late replies. */
    if(pending && present && tx_pos==tx_used) {
        packet(3,"",0);
        for(unsigned i=0;i<12 && (inb(UART+5)&0x20);i++) outb(UART,tx[i]);
    }
    pending=0; result=0; used=doc_used=tx_used=tx_pos=0; request=0;
}
int web_start(const char *url) {
    init(); web_cancel();
    if(!present) return -1;
    unsigned n=0;
    while(url && url[n] && n<=WEB_URL_MAX) n++;
    if(!url || !n || n>WEB_URL_MAX) return -3;
    request=++next_id; if(!request) request=++next_id;
    /* Drain old UART bytes with a fixed budget. */
    for(unsigned i=0;i<4096 && (inb(UART+5)&1);i++) (void)inb(UART);
    packet(1,url,n);
    pending=1; acknowledged=0; deadline=timer_ticks()+200;
    return 0;
}
static void discard(void) {
    for(unsigned i=1;i<used;i++) rx[i-1]=rx[i];
    if(used) used--;
}
int web_poll(void *out, uint32_t capacity) {
    if(!pending) return result;
    uint64_t now=timer_ticks();
    /* Both TX/RX budgets are bounded: the GUI continues pumping. */
    for(unsigned i=0;i<256 && tx_pos<tx_used && (inb(UART+5)&0x20);i++)
        outb(UART,tx[tx_pos++]);
    if(used && now-last_byte>200) { used=0; result=-3; }
    for(unsigned budget=0;budget<4096 && (inb(UART+5)&1);budget++) {
        if(used==sizeof(rx)) discard();
        rx[used++]=inb(UART); last_byte=now;
        for(;;) {
            if(used<2) break;
            if(rx[0]!='T'||rx[1]!='W') { discard(); continue; }
            if(used<12) break;
            unsigned n=get16(rx+8);
            if(rx[2]!=1||n>FRAME_MAX) { result=-3; discard(); continue; }
            if(used<n+12) break;
            if(get16(rx+10)!=checksum(rx,n+12)) { result=-3; discard(); continue; }
            if(get32(rx+4)==request) {
                if(rx[3]==0x80 && n==0) {
                    if(!acknowledged) deadline=now+WEB_RESPONSE_TICKS;
                    acknowledged=1;
                } else if(rx[3]==0x81 && acknowledged) {
                    if(n>WEB_DOC_MAX-doc_used) { result=-3; pending=0; }
                    else { for(unsigned i=0;i<n;i++) document[doc_used++]=rx[12+i]; }
                } else if(rx[3]==0x82 && acknowledged && n==0) {
                    if(!out || capacity<doc_used || doc_used<10) result=-3;
                    else {
                        for(unsigned i=0;i<doc_used;i++) ((uint8_t*)out)[i]=document[i];
                        result=(int)doc_used;
                    }
                    pending=0;
                } else { result=-3; pending=0; }
            }
            unsigned remain=used-n-12;
            for(unsigned i=0;i<remain;i++) rx[i]=rx[n+12+i];
            used=remain;
            if(!pending) return result;
        }
    }
    if(now>=deadline) {
        pending=0;
        if(result!=-3) result=acknowledged?-2:-1;
        return result;
    }
    return 0;
}
