#include "console.h"
#include "io.h"
#include "glyph.h"
#include "keyboard.h"
#include "utf8.h"
#include <stddef.h>
#define CW 12
#define CH 18
#define COM1 0x3f8
#define MAX_COLS 200
#define MAX_ROWS 100
static struct limine_framebuffer *screen;
static uint64_t col,row,cols,rows;
static uint32_t fg,bg;
static uint32_t cells[MAX_ROWS][MAX_COLS];
static utf8_state_t output_state;
static int screen_off;
/* Captura opcional del agente: una sola sesion a la vez. */
static char *capture_buffer;
static size_t capture_capacity,capture_used,capture_dropped;
void console_capture_begin(char *buffer,size_t capacity){
    if(capture_buffer||!buffer||!capacity)return;
    capture_buffer=buffer;capture_capacity=capacity;capture_used=0;capture_dropped=0;buffer[0]=0;
}
size_t console_capture_end(void){
    size_t dropped=capture_dropped;
    if(capture_buffer)capture_buffer[capture_used]=0;
    capture_buffer=0;capture_capacity=capture_used=capture_dropped=0;
    return dropped;
}
static void capture(const char *bytes,unsigned n){
    for(unsigned i=0;i<n;i++){
        if(capture_used+1>=capture_capacity){capture_dropped++;continue;}
        capture_buffer[capture_used++]=bytes[i];
    }
}
void serial_init(void){
    outb(COM1+1,0);outb(COM1+3,0x80);outb(COM1,3);outb(COM1+1,0);
    outb(COM1+3,3);outb(COM1+2,0xc7);outb(COM1+4,0x0b);
}
static void serial_char(char c){
    for(unsigned i=0;i<100000;i++)if(inb(COM1+5)&0x20){outb(COM1,(uint8_t)c);return;}
}
static void pixel(uint64_t x,uint64_t y,uint32_t color){
    volatile uint32_t *p=(volatile uint32_t*)((uint8_t*)screen->address+y*screen->pitch+x*4);*p=color;
}
static uint32_t rgb(uint32_t v){
    return (((v>>16)&255)<<screen->red_mask_shift)|(((v>>8)&255)<<screen->green_mask_shift)|((v&255)<<screen->blue_mask_shift);
}
static void draw_char(uint64_t x,uint64_t y,uint32_t c){
    for(unsigned py=0;py<CH;py++)for(unsigned px=0;px<CW;px++){
        unsigned gx=px/2,gy=py/2;int on=gx<5&&(glyph_row(c,gy)&(1<<(4-gx)));
        pixel(x*CW+px,y*CH+py,on?fg:bg);
    }
}
static void paint_background(void){
    if(!screen||screen_off)return;
    for(uint64_t y=0;y<screen->height;y++)for(uint64_t x=0;x<screen->width;x++)pixel(x,y,bg);
}
void console_suspend_screen(int suspend){
    if(suspend==screen_off)return;
    screen_off=suspend;
    if(!screen_off&&screen){
        paint_background();
        for(uint64_t y=0;y<rows;y++)for(uint64_t x=0;x<cols;x++)if(cells[y][x])draw_char(x,y,cells[y][x]);
    }
}
int console_screen_suspended(void){return screen_off;}
int console_set_colors(uint32_t text_rgb,uint32_t background_rgb){
    if(!screen)return 0;
    fg=rgb(text_rgb);bg=rgb(background_rgb);paint_background();
    if(screen_off)return 1;
    for(uint64_t y=0;y<rows;y++)for(uint64_t x=0;x<cols;x++)if(cells[y][x])draw_char(x,y,cells[y][x]);
    return 1;
}
void console_clear(void){
    col=row=0;output_state.count=0;
    for(unsigned y=0;y<MAX_ROWS;y++)for(unsigned x=0;x<MAX_COLS;x++)cells[y][x]=0;
    if(screen)paint_background();
}
void console_init(struct limine_framebuffer *fb){
    if(fb&&fb->bpp==32&&fb->memory_model==LIMINE_FRAMEBUFFER_RGB&&fb->red_mask_size==8&&
       fb->green_mask_size==8&&fb->blue_mask_size==8&&fb->width>=CW&&fb->height>=CH){
        screen=fb;cols=fb->width/CW;rows=fb->height/CH;
        if(cols>MAX_COLS)cols=MAX_COLS;
        if(rows>MAX_ROWS)rows=MAX_ROWS;
        fg=rgb(0x64ebd2);bg=rgb(0x0c1423);console_clear();
    }
}
static void scroll(void){
    if(row<rows)return;
    for(uint64_t y=0;y+1<rows;y++)for(uint64_t x=0;x<cols;x++)cells[y][x]=cells[y+1][x];
    for(uint64_t x=0;x<cols;x++)cells[rows-1][x]=0;
    for(uint64_t y=0;y<(rows-1)*CH;y++){
        volatile uint32_t *d=(volatile uint32_t*)((uint8_t*)screen->address+y*screen->pitch);
        volatile uint32_t *s=(volatile uint32_t*)((uint8_t*)screen->address+(y+CH)*screen->pitch);
        for(uint64_t x=0;x<screen->width;x++)d[x]=s[x];
    }
    for(uint64_t y=(rows-1)*CH;y<rows*CH;y++)for(uint64_t x=0;x<screen->width;x++)pixel(x,y,bg);
    row=rows-1;
}
static void graphical(uint32_t c){
    if(!screen||screen_off)return;
    if(c=='\n'){col=0;row++;scroll();return;}
    if(c=='\r'){col=0;return;}
    if(c=='\b'){
        if(col)col--;else if(row){row--;col=cols-1;}else return;
        cells[row][col]=' ';draw_char(col,row,' ');return;
    }
    cells[row][col]=c;draw_char(col,row,c);
    col++;if(col>=cols){col=0;row++;scroll();}
}
void console_erase_last(uint32_t c){
    unsigned width=utf8_columns(c);
    /* El terminal retrocede un byte por columna; la rejilla debe retroceder
     * el mismo numero de celdas o queda un glifo fantasma en framebuffer. */
    for(unsigned i=0;i<width;i++){serial_char('\b');serial_char(' ');serial_char('\b');graphical('\b');}
}
void console_put_unicode(uint32_t c){
    if(c=='\b'){console_erase_last(' ');return;}
    if(c=='\t'){for(unsigned i=0;i<4;i++)console_put_unicode(' ');return;}
    if(c<32&&c!='\n'&&c!='\r')c=0xfffd;
    if(c>=127&&c<160)c=0xfffd;
    char bytes[4];unsigned n=utf8_encode(c,bytes);
    if(c=='\n')serial_char('\r');
    for(unsigned i=0;i<n;i++)serial_char(bytes[i]);
    if(capture_buffer)capture(bytes,n);
    graphical(c);
}
void console_putc(char c){
    uint8_t b=(uint8_t)c;uint32_t cp;int result=utf8_feed(&output_state,b,&cp);
    if(result==1)console_put_unicode(cp);
    else if(result<0){
        console_put_unicode(0xfffd);
        /* An ASCII/new leading byte following a broken prefix starts a new scalar. */
        if(b<0x80||(b>=0xc2&&b<=0xf4)){
            result=utf8_feed(&output_state,b,&cp);if(result==1)console_put_unicode(cp);
        }
    }
}
void console_write(const char *s){while(*s)console_putc(*s++);}
void console_write_text(const char *s,size_t n){
    /* Complete framed text, not a control stream. Preserve valid Unicode bytes. */
    size_t pos=0,used;uint32_t cp;
    while(pos<n){
        int status=utf8_decode((const uint8_t*)s+pos,n-pos,&cp,&used);
        if(status!=1){cp=0xfffd;used=1;}
        if(cp=='\r'){pos+=used;continue;}
        if(cp<32&&cp!='\n'&&cp!='\t')cp=' ';
        if(cp>=127&&cp<160)cp=' ';
        console_put_unicode(cp);pos+=used;
    }
}
void console_dec(uint64_t n){char b[21];unsigned i=0;do{b[i++]='0'+n%10;n/=10;}while(n);while(i)console_putc(b[--i]);}
void console_signed(int64_t n){
    if(n<0){console_putc('-');console_dec((uint64_t)(-(n+1))+1);}else console_dec((uint64_t)n);
}
void console_hex(uint64_t n){const char *h="0123456789ABCDEF";console_write("0x");for(int i=60;i>=0;i-=4)console_putc(h[(n>>i)&15]);}
int console_read(void){
    int queued=keyboard_next_byte();if(queued>=0)return queued;
    if(inb(COM1+5)&1)return inb(COM1);
    return keyboard_read();
}
