/* Exercise actual app parser, history, UTF-8 editing with ASan/UBSan. */
#define main browser_entry
#include "../apps/navegador.c"
#undef main
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void quiet(const char *s) { (void)s; }
static int width(void) { return 940; }
static int measure(const char *s,int scale) { (void)scale;return (int)strlen(s)*7; }
static host_api_t fake={.write=quiet,.width=width,.text_width=measure};
static void put16(unsigned char *p,unsigned n) {p[0]=n;p[1]=n>>8;}
int main(void) {
    a=&fake;
    reset_page();
    add_text("Perú año áéíóú üñ ¿Qué? ¡Sí!",0,-1);
    assert(line_count==1 && strcmp(lines[0],"Perú año áéíóú üñ ¿Qué? ¡Sí!")==0);
    copy(url,"áño",URL_MAX);cursor=len(url);replace=0;
    edit_key(HOST_KEY_HOME);edit_key(HOST_KEY_RIGHT);assert(cursor==2);
    edit_key('\b');assert(strcmp(url,"ño")==0);
    edit_key(HOST_KEY_DELETE);assert(strcmp(url,"o")==0);
    history_count=0;history_pos=-1;
    for(int i=0;i<20;i++) {snprintf(url,URL_MAX,"https://example.com/%d",i);remember();}
    assert(history_count==HISTORY && history_pos==HISTORY-1);
    assert(strcmp(history[0],"https://example.com/12")==0);
    history_pos=3;copy(url,"https://example.com/branch",URL_MAX);remember();
    assert(history_count==5 && history_pos==4);
    for(int size=0;size<10;size++) assert(!parse(size));
    memset(doc,0,sizeof(doc));put16(doc,200);put16(doc+2,1);doc[10]='x';
    assert(parse(11));
    put16(doc+8,1);assert(!parse(11));
    put16(doc+8,0);put16(doc+2,65535);assert(!parse(11));
    /* Adversarial lengths and random complete buffers; no out-of-bounds. */
    unsigned seed=0x12345678;
    for(int round=0;round<20000;round++) {
        unsigned n=(unsigned)round%sizeof(doc);
        for(unsigned i=0;i<n;i++) {seed=seed*1664525u+1013904223u;doc[i]=(unsigned char)(seed>>24);}
        (void)parse((int)n);
    }
    puts("PASSED: app parser 20000 malformed buffers, wrap, UTF8 cursor/delete, history eviction/branch");
    return 0;
}
