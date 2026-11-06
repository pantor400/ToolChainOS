/* Run the actual kernel TW/1 client against a bounded emulated UART. */
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define IO_H
static uint8_t input[70000], output[70000], scratch;
static unsigned input_len, input_pos, output_len;
static uint64_t clock_ticks;
static int hardware=1;
static void outb(uint16_t port,uint8_t value) {
    if(port==0x3ef)scratch=value;
    if(port==0x3e8 && output_len<sizeof(output))output[output_len++]=value;
}
static uint8_t inb(uint16_t port) {
    if(port==0x3ef)return hardware?scratch:0;
    if(port==0x3ed)return 0x20|(input_pos<input_len);
    if(port==0x3e8)return input_pos<input_len?input[input_pos++]:0;
    return 0;
}
uint64_t timer_ticks(void) {return clock_ticks;}
#include "../src/web.c"
static void feed(unsigned kind,uint32_t id,const uint8_t *body,unsigned n) {
    assert(input_len+n+12<sizeof(input));
    uint8_t *p=input+input_len;
    p[0]='T';p[1]='W';p[2]=1;p[3]=kind;
    for(unsigned i=0;i<4;i++)p[4+i]=id>>(8*i);
    p[8]=n;p[9]=n>>8;p[10]=p[11]=0;
    for(unsigned i=0;i<n;i++)p[12+i]=body[i];
    unsigned sum=0;for(unsigned i=0;i<n+12;i++)sum+=p[i];
    p[10]=sum;p[11]=sum>>8;
    input_len+=n+12;
}
static void reset_input(void) {input_len=input_pos=0;}
static void start(void) {
    reset_input();
    assert(web_start("https://example.com/")==0);
}
int main(void) {
    uint8_t out[WEB_DOC_MAX], body[1024]={0};
    hardware=0;assert(web_start("https://example.com/")==-1);
    initialized=0;hardware=1;
    start();assert(web_poll(out,sizeof(out))==0);
    clock_ticks+=201;assert(web_poll(out,sizeof(out))==-1);
    start();feed(0x80,request,0,0);assert(web_poll(out,sizeof(out))==0);
    clock_ticks+=WEB_RESPONSE_TICKS+1;assert(web_poll(out,sizeof(out))==-2);
    start();
    /* Fragmented ACK, then data/end. */
    feed(0x80,request,0,0);unsigned full=input_len;input_len=5;
    assert(web_poll(out,sizeof(out))==0);
    input_len=full;assert(web_poll(out,sizeof(out))==0);
    feed(0x81,request,body,10);feed(0x82,request,0,0);
    assert(web_poll(out,sizeof(out))==10);
    start();feed(0x80,request,0,0);
    for(int i=0;i<25;i++)feed(0x81,request,body,1024);
    int r=0;for(int i=0;i<20 && !r;i++)r=web_poll(out,sizeof(out));
    assert(r==-3);
    start();feed(0x80,request,0,0);feed(0x81,request,body,10);feed(0x82,request,0,0);
    assert(web_poll(out,9)==-3);
    start();feed(0x80,request,0,0);feed(0x81,request,body,10);input[input_len-1]^=1;
    assert(web_poll(out,sizeof(out))==0);
    clock_ticks+=WEB_RESPONSE_TICKS+1;assert(web_poll(out,sizeof(out))==-3);
    start();uint32_t old=request;web_cancel();
    start();feed(0x80,old,0,0);feed(0x81,old,body,10);feed(0x82,old,0,0);
    assert(web_poll(out,sizeof(out))==0 && !acknowledged);
    feed(0x80,request,0,0);feed(0x81,request,body,10);feed(0x82,request,0,0);
    assert(web_poll(out,sizeof(out))==10);
    start();
    const uint8_t invalid[]={'T','W',1,1,0,0,0,0,255,255,0,0};
    memcpy(input,invalid,sizeof(invalid));input_len=sizeof(invalid);
    feed(0x80,request,0,0);feed(0x81,request,body,10);feed(0x82,request,0,0);
    assert(web_poll(out,sizeof(out))==10);
    next_id=UINT32_MAX;start();assert(request==1);
    web_cancel();
    puts("PASSED: kernel UART framing partial/checksum/length, capacity, overflow, no-bridge/timeout, cancellation/late IDs and recovery");
    return 0;
}
