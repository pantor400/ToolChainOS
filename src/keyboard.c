#include "keyboard.h"
#include "utf8.h"
#include "io.h"
static unsigned layout=KEYBOARD_ES,shift_l,shift_r,caps,caps_down,extended,pause_bytes,altgr;
static uint32_t dead;
/* Cola de 16 bytes: un escalar UTF-8 de 4 bytes siempre cabe completo. */
static uint8_t queue[16];
static unsigned queued,read_pos;
/* Cola de teclas extendidas: no viaja como byte, asi que no rompe el UTF-8
 * que consume la shell. */
static unsigned specials[8];
static unsigned specials_in,specials_out;
const char *keyboard_layout_name(void){return layout==KEYBOARD_ES?"ES-LATAM (PERU)":"US";}
int keyboard_set_layout(unsigned value){
    if(value!=KEYBOARD_ES&&value!=KEYBOARD_US)return 0;
    layout=value;shift_l=shift_r=caps=caps_down=extended=pause_bytes=altgr=0;dead=0;queued=read_pos=0;
    specials_in=specials_out=0;return 1;
}
static void emit(uint32_t cp){
    char bytes[4];unsigned n=utf8_encode(cp,bytes);
    if(read_pos==queued){read_pos=queued=0;}
    /* Se descarta el escalar entero, nunca una parte: media secuencia UTF-8
     * es peor que una tecla perdida. */
    if(queued+n>sizeof(queue))return;
    for(unsigned i=0;i<n;i++)queue[queued++]=(uint8_t)bytes[i];
}
int keyboard_next_byte(void){if(read_pos>=queued)return -1;return queue[read_pos++];}
static void special(uint32_t code){
    if(specials_in-specials_out>=sizeof(specials)/sizeof(specials[0]))return;
    specials[specials_in++%8]=code;
}
int keyboard_special(void){
    if(specials_in==specials_out)return 0;
    return (int)specials[specials_out++%8];
}
static uint32_t compose(uint32_t mark,uint32_t c){
    const char *vowels="aeiouAEIOU";
    static const uint32_t acute[]={0xe1,0xe9,0xed,0xf3,0xfa,0xc1,0xc9,0xcd,0xd3,0xda};
    static const uint32_t dia[]={0xe4,0xeb,0xef,0xf6,0xfc,0xc4,0xcb,0xcf,0xd6,0xdc};
    static const uint32_t grave[]={0xe0,0xe8,0xec,0xf2,0xf9,0xc0,0xc8,0xcc,0xd2,0xd9};
    static const uint32_t circum[]={0xe2,0xea,0xee,0xf4,0xfb,0xc2,0xca,0xce,0xd4,0xdb};
    for(unsigned i=0;vowels[i];i++)if(c==(uint32_t)vowels[i]){
        if(mark==0xb4)return acute[i];
        if(mark==0xa8)return dia[i];
        if(mark=='`')return grave[i];
        if(mark=='^')return circum[i];
    }
    return 0;
}
void keyboard_scancode(uint8_t scan){
    if(pause_bytes){pause_bytes--;return;}
    if(scan==0xe1){pause_bytes=5;return;}
    if(scan==0xe0){extended=1;return;}
    if(extended){extended=0;
        if((scan&0x7f)==56){altgr=!(scan&0x80);return;}
        if(scan&0x80)return;               /* solo interesa la pulsacion */
        switch(scan){
            case 0x48:special(KB_KEY_UP);break;case 0x50:special(KB_KEY_DOWN);break;
            case 0x4b:special(KB_KEY_LEFT);break;case 0x4d:special(KB_KEY_RIGHT);break;
            case 0x47:special(KB_KEY_HOME);break;case 0x4f:special(KB_KEY_END);break;
            case 0x53:special(KB_KEY_DELETE);break;
        }
        return;}
    if(scan==42||scan==170){shift_l=scan==42;return;}
    if(scan==54||scan==182){shift_r=scan==54;return;}
    if(scan==186){caps_down=0;return;}
    if(scan==58){if(!caps_down)caps^=1;caps_down=1;return;}
    if(scan&0x80)return;
    unsigned shift=shift_l||shift_r;uint32_t c=0,mark=0;
    static const char letters[128]={
        [16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',[23]='i',[24]='o',[25]='p',
        [30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',
        [44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',[50]='m'};
    if(scan==14){if(dead){dead=0;return;}emit('\b');return;}
    /* TAB viaja como 9: la shell lo ignora y las apps lo usan para cambiar
     * de campo o abrir su selector de archivos. */
    if(scan==15){emit('\t');return;}
    /* ESC viaja como 27: la shell lo ignora y la GUI lo usa para salir. */
    if(scan==1){emit(27);return;}
    if(scan==28)c='\n';
    else if(scan==57)c=' ';
    else if(letters[scan]){c=letters[scan];if(shift^caps)c-=32;}
    else if(scan>=2&&scan<=11){
        static const char digits[]="1234567890";
        static const char us[]="!@#$%^&*()";
        static const char es[]="!\"#$%&/()=";
        c=shift?(layout==KEYBOARD_ES?es[scan-2]:us[scan-2]):digits[scan-2];
    }else if(layout==KEYBOARD_US){
        static const char normal[128]={[12]='-',[13]='=',[26]='[',[27]=']',[39]=';',[40]=39,[41]=96,[43]=92,[51]=',',[52]='.',[53]='/',[86]=92};
        static const char shifted[128]={[12]='_',[13]='+',[26]='{',[27]='}',[39]=':',[40]=34,[41]='~',[43]='|',[51]='<',[52]='>',[53]='?',[86]='|'};
        c=shift?shifted[scan]:normal[scan];
    }else{
        switch(scan){
            case 12:c=shift?'?':39;break;case 13:c=shift?0xa1:0xbf;break;
            case 26:mark=shift?0xa8:0xb4;break;case 27:c=shift?'*':'+';break;
            case 39:c=(shift^caps)?0xd1:0xf1;break;
            case 40:c=shift?'[':'{';break;case 41:c=shift?0xb0:'|';break;
            case 43:c=shift?']':'}';break;case 51:c=shift?';':',';break;
            case 52:c=shift?':':'.';break;case 53:c=shift?'_':'-';break;
            case 86:c=shift?'>':'<';break;
        }
    }
    if(layout==KEYBOARD_ES&&altgr){
        mark=0;c=0;
        switch(scan){case 16:c='@';break;case 18:c=0x20ac;break;case 12:c=92;break;
            case 27:c='~';break;case 40:mark='^';break;case 43:mark=96;break;case 41:c=0xac;break;}
    }
    if(mark){if(dead==mark){emit(mark);dead=0;}else{if(dead)emit(dead);dead=mark;}return;}
    if(!c)return;
    if(dead){uint32_t combined=compose(dead,c);if(c==' '){emit(dead);dead=0;return;}
        if(combined)c=combined;else emit(dead);dead=0;}
    emit(c);
}
int keyboard_read(void){
    int byte=keyboard_next_byte();if(byte>=0)return byte;
    uint8_t status=inb(0x64);if(!(status&1))return -1;
    uint8_t scan=inb(0x60);if(status&0x20)return -1;
    keyboard_scancode(scan);return keyboard_next_byte();
}
