#include "host_api.h"

/* Educational static-HTML browser. Page buffers live in the ELF image (PMM),
 * never in the shared 512 KiB heap. All work per GUI tick is bounded. */
#define URL_MAX 1024
#define DOC_MAX 24576
#define LINE_MAX 480
#define LINE_BYTES 144
#define LINKS_MAX 48
#define HISTORY 8
#define BG 0xf4f7fc
#define FG 0x14213a
#define DIM 0x5a6b85
#define BLUE 0x2470e8
static host_api_t *a;
static char url[URL_MAX], title[193], status[256];
static char history[HISTORY][URL_MAX], targets[LINKS_MAX][URL_MAX];
static unsigned char doc[DOC_MAX];
static char lines[LINE_MAX][LINE_BYTES];
static int line_link[LINE_MAX], line_kind[LINE_MAX];
static int line_count, link_count, scroll, selected=-1;
static int history_count, history_pos, editing, replace, cursor, loading;
static int changed=1;
static unsigned u16(const unsigned char *p) { return p[0]|((unsigned)p[1]<<8); }
static int len(const char *s) { int n=0; while(s[n]) n++; return n; }
static int same(const char *s,const char *t) { while(*s && *s==*t) {s++;t++;} return *s==*t; }
static void copy(char *d,const char *s,int cap) {
    int i=0; while(s[i] && i+1<cap) {d[i]=s[i];i++;} d[i]=0;
}
static void bytes(char *d,const unsigned char *s,unsigned n,unsigned cap) {
    unsigned i=0; while(i<n && i+1<cap) {d[i]=(char)s[i];i++;}
    /* Don't retain a partial final UTF-8 sequence when truncating. */
    if(i<n) { while(i && (s[i]&0xc0)==0x80) i--; }
    d[i]=0;
}
static int previous(const char *s,int p) { if(p) p--; while(p && ((unsigned char)s[p]&0xc0)==0x80) p--; return p; }
static int next(const char *s,int p) { if(s[p]) p++; while(s[p] && ((unsigned char)s[p]&0xc0)==0x80) p++; return p; }
static void log_page(unsigned code) {
    char number[5]; int n=0;
    do {number[n++]=(char)('0'+code%10);code/=10;} while(code);
    a->write("WEB_PAGE HTTP=");
    while(n) { char c[2]={number[--n],0}; a->write(c); }
    a->write(" URL=");a->write(url);a->write(" TITLE=");a->write(title);a->write("\n");
}
static void clipped(int x,int y,int width,const char *s,unsigned color) {
    char out[URL_MAX]; int n=0;
    while(s[n] && n+1<URL_MAX) {
        int end=next(s,n); if(end>=URL_MAX) break;
        for(int i=n;i<end;i++) out[i]=s[i];
        out[end]=0;
        if(a->text_width(out,-1)>width) break;
        n=end;
    }
    out[n]=0; a->text(x,y,out,color,-1);
}
static void add_text(const char *text,int kind,int link) {
    int used=0; char part[LINE_BYTES]; part[0]=0;
    while(*text && line_count<LINE_MAX) {
        if(*text=='\n') {
            part[used]=0; copy(lines[line_count],part,LINE_BYTES);
            line_link[line_count]=link;line_kind[line_count++]=kind;used=0;text++;continue;
        }
        int n=next(text,0);
        if(n<=0 || n>4) n=1;
        if(used+n>=LINE_BYTES) {
            part[used]=0;copy(lines[line_count],part,LINE_BYTES);
            line_link[line_count]=link;line_kind[line_count++]=kind;used=0;continue;
        }
        for(int i=0;i<n;i++) part[used+i]=text[i];
        part[used+n]=0;
        if(used && a->text_width(part,-1)>a->width()-64) {
            int cut=used-1;
            while(cut>0 && part[cut]!=' ') cut--;
            if(cut>0) {
                part[cut]=0;copy(lines[line_count],part,LINE_BYTES);
                line_link[line_count]=link;line_kind[line_count++]=kind;
                int remain=used-cut-1;
                for(int i=0;i<remain;i++) part[i]=part[cut+1+i];
                used=remain;
            } else {
                part[used]=0;copy(lines[line_count],part,LINE_BYTES);
                line_link[line_count]=link;line_kind[line_count++]=kind;used=0;
            }
            continue;
        }
        used+=n;text+=n;
    }
    if(line_count<LINE_MAX && used) {
        part[used]=0;copy(lines[line_count],part,LINE_BYTES);
        line_link[line_count]=link;line_kind[line_count++]=kind;
    }
}
static void reset_page(void) { line_count=link_count=scroll=0;selected=-1; }
static void local(void) {
    reset_page();copy(url,"about:inicio",URL_MAX);copy(title,"ToolchainOS Web",193);
    copy(status,"Página local sin Internet. --web activa HTTP/HTTPS por Ubuntu.",256);
    add_text("Bienvenido a ToolchainOS Web",1,-1);
    add_text("Navegador educativo de HTML estático. La ventana y los enlaces se dibujan dentro de ToolchainOS.",0,-1);
    add_text("Escribe una URL y pulsa Enter. Sin esquema se usa HTTPS. TLS se verifica en Ubuntu mediante COM3.",0,-1);
    add_text("Tab selecciona enlaces; Enter abre. Flechas arriba/abajo desplazan. Inicio vuelve a esta ayuda.",0,-1);
    add_text("ESC deja la barra; otro ESC cierra. Durante carga, R cambia a X para cancelar.",0,-1);
    add_text("No ejecuta JavaScript, formularios, vídeo, login ni CSS completo. Las apps siguen en ring 0.",0,-1);
    copy(targets[0],"https://info.cern.ch/",URL_MAX);link_count=1;
    add_text("CERN: primera web y enlaces reales (HTTPS; requiere puente)",2,0);
    copy(targets[1],"http://example.com/",URL_MAX);link_count=2;
    add_text("Abrir example.com por HTTP (requiere puente real)",2,1);
    a->write("WEB_LOCAL OFFLINE\n");
}
static void remember(void) {
    if(history_count && same(history[history_pos],url)) return;
    history_count=history_pos+1;
    if(history_count>=HISTORY) {
        for(int i=1;i<HISTORY;i++) copy(history[i-1],history[i],URL_MAX);
        history_count=HISTORY-1;
    }
    history_pos=history_count++;copy(history[history_pos],url,URL_MAX);
}
static void navigate(const char *target,int push) {
    char normalized[URL_MAX];
    copy(normalized,target,URL_MAX);
    if(!same(normalized,"about:inicio")) {
        int has_scheme=0;for(int i=0;normalized[i];i++) if(normalized[i]==':') has_scheme=1;
        if(!has_scheme) {
            char temp[URL_MAX];copy(temp,normalized,URL_MAX);
            copy(normalized,"https://",URL_MAX);
            int i=8,j=0;while(temp[j] && i+1<URL_MAX) normalized[i++]=temp[j++];
            normalized[i]=0;
        }
    }
    a->web_cancel();loading=0;copy(url,normalized,URL_MAX);
    cursor=len(url);editing=0;replace=0;
    if(push) remember();
    if(same(url,"about:inicio")) local();
    else {
        int result=a->web_start(url);
        loading=result==0;
        copy(status,loading?"Cargando... TLS/HTTP mediante puente Ubuntu":"Sin puente disponible (--web)",256);
        a->write("WEB_REQUEST ");a->write(url);a->write("\n");
        if(result) {a->write("WEB_STATUS ");a->write(status);a->write("\n");}
    }
    changed=1;
}
static int parse(int size) {
    if(size<10) return 0;
    unsigned code=u16(doc),ul=u16(doc+2),tl=u16(doc+4),ml=u16(doc+6),count=u16(doc+8);
    unsigned pos=10;
    if((code && (code<100 || code>599)) || ul>=URL_MAX || tl>=193 || ml>=256 || count>128 || pos+ul+tl+ml>(unsigned)size) return 0;
    /* Validate the whole document before replacing the current page. */
    unsigned scan=pos+ul+tl+ml;
    for(unsigned i=0;i<count;i++) {
        if(scan+5>(unsigned)size) return 0;
        unsigned text=u16(doc+scan+1),target=u16(doc+scan+3);
        if(doc[scan]>2 || text>2048 || target>=URL_MAX || scan+5+text+target>(unsigned)size) return 0;
        scan+=5+text+target;
    }
    if(scan!=(unsigned)size) return 0;
    reset_page();
    bytes(url,doc+pos,ul,URL_MAX);pos+=ul;cursor=len(url);
    bytes(title,doc+pos,tl,193);pos+=tl;
    bytes(status,doc+pos,ml,256);pos+=ml;
    if(!status[0]) copy(status,code?"Página cargada":"Error de navegación",256);
    if(code && code<400) {
        char note[256];copy(note,status,256);
        copy(status,"Página cargada. ",256);
        int n=len(status),j=0;while(note[j] && n+1<256) status[n++]=note[j++];status[n]=0;
    }
    if(code) {
        char note[256];copy(note,status,256);
        copy(status,"HTTP 000 | ",256);
        status[5]=(char)('0'+code/100);
        status[6]=(char)('0'+code/10%10);
        status[7]=(char)('0'+code%10);
        int n=len(status),j=0;
        while(note[j] && n+1<256) status[n++]=note[j++];
        status[n]=0;
    }
    copy(history[history_pos],url,URL_MAX);
    for(unsigned i=0;i<count;i++) {
        unsigned kind=doc[pos],text=u16(doc+pos+1),target=u16(doc+pos+3);pos+=5;
        char content[2049];bytes(content,doc+pos,text,sizeof(content));pos+=text;
        int link=-1;
        if(kind==2 && target && link_count<LINKS_MAX) {
            link=link_count++;bytes(targets[link],doc+pos,target,URL_MAX);
        }
        pos+=target;add_text(content,(int)kind,link);
        if(i<4) { a->write("WEB_TEXT ");a->write(content);a->write("\n"); }
    }
    log_page(code);
    return 1;
}
static int rows(void) { int r=(a->height()-134)/20;return r>0?r:1; }
static void clamp_scroll(void) {
    int max=line_count-rows();if(max<0) max=0;
    if(scroll<0) scroll=0;
    if(scroll>max) scroll=max;
}
static void button(int x,int w,const char *label) {
    a->rect(x,10,w,28,0xffffff);a->frame(x,10,w,28,0xc9d5e6);a->text(x+9,17,label,BLUE,-1);
}
static void draw(void) {
    int w=a->width(),h=a->height();
    clamp_scroll();a->clear(BG);
    button(12,36,"<");button(54,36,">");button(96,36,loading?"X":"R");button(138,64,"Inicio");
    a->rect(210,10,w-280,28,0xffffff);a->frame(210,10,w-280,28,editing?BLUE:0xc9d5e6);
    const char *shown=url;
    char prefix[URL_MAX];
    if(editing) {
        bytes(prefix,(const unsigned char*)url,(unsigned)cursor,URL_MAX);
        int start=0;
        while(prefix[start] && a->text_width(prefix+start,-1)>w-312) start=next(prefix,start);
        shown=url+start;
        clipped(218,17,w-300,shown,replace?BLUE:FG);
        int caret=a->text_width(prefix+start,-1);
        a->rect(218+caret,16,1,16,BLUE);
    } else clipped(218,17,w-300,shown,FG);
    button(w-62,50,"Ir");
    clipped(14,48,w-28,title,FG);clipped(14,70,w-28,status,loading?BLUE:DIM);
    a->rect(12,98,w-24,h-132,0xffffff);
    for(int row=0;row<rows() && scroll+row<line_count;row++) {
        int index=scroll+row,y=106+row*20,link=line_link[index];
        if(link>=0 && link==selected) a->rect(16,y-2,w-48,20,0xe1edff);
        clipped(22,y,w-62,lines[index],link>=0?BLUE:FG);
        if(line_kind[index]==1) a->rect(16,y+3,3,10,BLUE);
    }
    int track=h-144,thumb=line_count>rows()?track*rows()/line_count:track;
    if(thumb<12) thumb=12;
    int max=line_count-rows();
    int offset=max>0?(track-thumb)*scroll/max:0;
    a->rect(w-26,104,6,track,0xe1e8f2);a->rect(w-26,104+offset,6,thumb,BLUE);
    clipped(14,h-24,w-28,"HTML básico | Tab: enlaces | Enter: abrir | Flechas: scroll | ESC: salir de barra/cerrar",DIM);
    a->present();changed=0;
}
static void select_link(void) {
    if(!link_count) return;
    selected=(selected+1)%link_count;
    for(int i=0;i<line_count;i++) if(line_link[i]==selected) {
        if(i<scroll || i>=scroll+rows()) scroll=i;
        break;
    }
    a->write("WEB_LINK_SELECTED ");a->write(targets[selected]);a->write("\n");
}
static void focus_url(void) {
    if(loading) {
        a->web_cancel();loading=0;copy(status,"Carga cancelada para editar URL",256);
    }
    editing=replace=1;cursor=len(url);
}
static void edit_key(int key) {
    int n=len(url);
    if(key=='\n') {navigate(url,1);return;}
    if(key==27) {editing=replace=0;return;}
    if(key==HOST_KEY_LEFT) {cursor=previous(url,cursor);replace=0;return;}
    if(key==HOST_KEY_RIGHT) {cursor=next(url,cursor);replace=0;return;}
    if(key==HOST_KEY_HOME) {cursor=0;replace=0;return;}
    if(key==HOST_KEY_END) {cursor=n;replace=0;return;}
    if(key=='\b' || key==HOST_KEY_DELETE) {
        if(replace) {url[0]=0;cursor=0;replace=0;return;}
        int start=key=='\b'?previous(url,cursor):cursor;
        int end=key=='\b'?cursor:next(url,cursor);
        for(int i=end;i<=n;i++) url[start+i-end]=url[i];
        cursor=start;return;
    }
    if(key>=32 && key<=255 && n+1<URL_MAX) {
        if(replace) {url[0]=0;cursor=n=0;replace=0;}
        for(int i=n;i>=cursor;i--) url[i+1]=url[i];
        url[cursor++]=(char)key;
    }
}
int main(host_api_t *api) {
    if(!api || api->abi_version!=HOST_ABI_VERSION || api->size<sizeof(host_api_t)) return 1;
    a=api;
    if(!a->window("Navegador",940,520)) return a->finish(2);
    a->escape_events(1);
    local();history_count=1;history_pos=0;copy(history[0],url,URL_MAX);
    editing=1;replace=1;cursor=len(url);
    host_event_t e;
    while(a->pump(&e)) {
        if(loading) {
            int result=a->web_poll(doc,DOC_MAX);
            if(result) {
                loading=0;changed=1;
                if(result>0) {if(!parse(result)) copy(status,"Respuesta malformada: documento rechazado",256);}
                else copy(status,result==-1?"Sin puente disponible: inicia con --web":result==-2?"Tiempo de espera agotado":"Respuesta malformada: protocolo rechazado",256);
                a->write("WEB_STATUS ");a->write(status);a->write("\n");
            }
        }
        if(e.type==HOST_EV_KEY) {
            int k=(int)e.key;changed=1;
            if(k==HOST_KEY_TAB) {editing=replace=0;select_link();}
            else if(editing) edit_key(k);
            else if(k==27) break;
            else if(k==HOST_KEY_UP) scroll-=3;
            else if(k==HOST_KEY_DOWN) scroll+=3;
            else if(k==HOST_KEY_HOME) scroll=0;
            else if(k==HOST_KEY_END) scroll=line_count;
            else if(k=='\n' && selected>=0) navigate(targets[selected],1);
            else if(k=='l' || k=='L') focus_url();
        }
        if(e.type==HOST_EV_DOWN) {
            changed=1;
            if(e.y>=10 && e.y<38) {
                if(e.x>=210 && e.x<a->width()-70) focus_url();
                else if(e.x>=12 && e.x<48 && history_pos>0) navigate(history[--history_pos],0);
                else if(e.x>=54 && e.x<90 && history_pos+1<history_count) navigate(history[++history_pos],0);
                else if(e.x>=96 && e.x<132) {
                    if(loading) {a->web_cancel();loading=0;copy(status,"Carga cancelada",256);a->write("WEB_CANCELLED\n");}
                    else navigate(url,0);
                }
                else if(e.x>=138 && e.x<202) navigate("about:inicio",1);
                else if(e.x>=a->width()-62) navigate(url,1);
            } else if(e.y>=98 && e.y<a->height()-34) {
                editing=replace=0;
                if(e.x>=a->width()-32) {
                    scroll=(e.y-104)*(line_count-rows())/(a->height()-144);
                } else {
                    int row=(e.y-106)/20+scroll;
                    if(row>=0 && row<line_count && line_link[row]>=0) navigate(targets[line_link[row]],1);
                }
            }
        }
        if(changed) draw();
    }
    a->web_cancel();a->escape_events(0);a->close();
    a->write("WEB_CLOSED BUFFERS_RELEASED_WITH_ELF\n");
    return a->finish(0);
}
