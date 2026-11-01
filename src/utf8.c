#include "utf8.h"
int utf8_decode(const uint8_t *s,size_t n,uint32_t *cp,size_t *consumed){
    *consumed=0;if(!n)return 0;
    uint8_t b=s[0];unsigned need;uint32_t value;
    if(b<0x80){*cp=b;*consumed=1;return 1;}
    if(b>=0xc2&&b<=0xdf){need=2;value=b&31;}
    else if(b>=0xe0&&b<=0xef){need=3;value=b&15;}
    else if(b>=0xf0&&b<=0xf4){need=4;value=b&7;}
    else {*consumed=1;return -1;}
    for(unsigned i=1;i<need&&i<n;i++){
        if((s[i]&0xc0)!=0x80){*consumed=1;return -1;}
        if(i==1&&((b==0xe0&&s[i]<0xa0)||(b==0xed&&s[i]>=0xa0)||
                    (b==0xf0&&s[i]<0x90)||(b==0xf4&&s[i]>0x8f))){*consumed=1;return -1;}
        value=(value<<6)|(s[i]&63);
    }
    if(n<need)return 0;
    *cp=value;*consumed=need;return 1;
}
int utf8_feed(utf8_state_t *s,uint8_t b,uint32_t *cp){
    if(s->count>=4)s->count=0;
    s->bytes[s->count++]=b;size_t used;
    int result=utf8_decode(s->bytes,s->count,cp,&used);
    if(result)s->count=0;
    return result;
}
unsigned utf8_encode(uint32_t cp,char out[4]){
    if(cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))cp=0xfffd;
    if(cp<0x80){out[0]=cp;return 1;}
    if(cp<0x800){out[0]=0xc0|(cp>>6);out[1]=0x80|(cp&63);return 2;}
    if(cp<0x10000){out[0]=0xe0|(cp>>12);out[1]=0x80|((cp>>6)&63);out[2]=0x80|(cp&63);return 3;}
    out[0]=0xf0|(cp>>18);out[1]=0x80|((cp>>12)&63);out[2]=0x80|((cp>>6)&63);out[3]=0x80|(cp&63);return 4;
}
int utf8_valid(const char *s,size_t n){
    size_t pos=0,used;uint32_t cp;
    while(pos<n){if(utf8_decode((const uint8_t*)s+pos,n-pos,&cp,&used)!=1)return 0;pos+=used;}
    return 1;
}
size_t utf8_previous(const char *s,size_t n){
    if(!n)return 0;
    size_t i=n-1;while(i&&((uint8_t)s[i]&0xc0)==0x80)i--;
    return i;
}
unsigned utf8_columns(uint32_t cp){
    /* Code-point erase, not a full grapheme/terminal wcwidth implementation. */
    if((cp>=0x300&&cp<=0x36f)||cp==0x200d||(cp>=0xfe00&&cp<=0xfe0f))return 0;
    if((cp>=0x1100&&cp<=0x115f)||(cp>=0x2e80&&cp<=0xa4cf)||
       (cp>=0xac00&&cp<=0xd7a3)||(cp>=0xf900&&cp<=0xfaff)||
       (cp>=0xfe10&&cp<=0xfe6f)||(cp>=0xff01&&cp<=0xff60)||
       (cp>=0x1f300&&cp<=0x1faff)||(cp>=0x20000&&cp<=0x3fffd))return 2;
    return 1;
}
