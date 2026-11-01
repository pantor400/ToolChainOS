#include "editor.h"
#include "fs.h"
#include "console.h"
#include "util.h"
static int active;
static char path[FS_PATH_MAX],buffer[FS_FILE_MAX_SIZE],scratch[FS_FILE_MAX_SIZE];
int editor_active(void){return active;}
void editor_open(const char *p){
    char d[FS_PATH_MAX],n[FS_NAME_MAX];
    if(!fs_split(p,d,n)||(fs_find_node(d)<0||fs_nodes()[fs_find_node(d)].type!=FS_TYPE_DIR)){console_write("ERROR: RUTA INVALIDA.\n");return;}
    uint32_t size;const char *old=fs_read_file(p,&size);
    if(!old&&fs_find_node(p)>=0){console_write("ERROR: NO ES ARCHIVO.\n");return;}
    copystr(path,p,sizeof(path));copystr(buffer,old?old:"",sizeof(buffer));active=1;
    console_write("EDITOR 1023 BYTES UTF-8: texto agrega linea; :line N texto reemplaza, :del N, :clear, :show, :wq, :q\n");
    console_write(buffer);if(slen(buffer)&&buffer[slen(buffer)-1]!='\n')console_putc('\n');
}
void editor_line(const char *s){
    if(same(s,":q")){active=0;console_write("EDICION CANCELADA.\n");return;}
    if(same(s,":wq")){
        char e[96];
        if(fs_write_path(path,buffer,e,sizeof(e))){active=0;console_write("ARCHIVO GUARDADO CON EXITO EN RAM (DISCO: VER PERSISTENCIA/FSSTATUS).\n");}
        else {console_write("ERROR: ");console_write(e);console_putc('\n');}
        return;
    }
    if(same(s,":clear")){buffer[0]=0;return;}
    if(same(s,":show")){console_write(buffer);console_putc('\n');return;}
    if((s[0]==':'&&s[1]=='l'&&s[2]=='i'&&s[3]=='n'&&s[4]=='e'&&s[5]==' ')||
       (s[0]==':'&&s[1]=='d'&&s[2]=='e'&&s[3]=='l'&&s[4]==' ')){
        int del=s[1]=='d';s+=del?5:6;uint64_t line=0;int digits=0;
        while(*s>='0'&&*s<='9'){if(line>1024){console_write("ERROR: LINEA INVALIDA.\n");return;}line=line*10+*s++-'0';digits++;}
        if(!digits||!line||(*s&&*s!=' ')||(del&&*s)){console_write("ERROR: SINTAXIS DE LINEA.\n");return;}
        if(*s==' ')s++;
        size_t start=0;uint64_t num=1;
        while(buffer[start]&&num<line)if(buffer[start++]=='\n')num++;
        if(!buffer[start]||num!=line){console_write("ERROR: LINEA NO EXISTE.\n");return;}
        size_t end=start;while(buffer[end]&&buffer[end]!='\n')end++;if(buffer[end])end++;
        size_t insert=del?0:slen(s)+1,total=start+insert+slen(buffer+end);
        if(total>=sizeof(buffer)){console_write("ERROR: LIMITE 1023 BYTES.\n");return;}
        size_t pos=0;for(size_t i=0;i<start;i++)scratch[pos++]=buffer[i];
        if(!del){while(*s)scratch[pos++]=*s++;scratch[pos++]='\n';}
        for(size_t i=end;buffer[i];i++)scratch[pos++]=buffer[i];
        scratch[pos]=0;copystr(buffer,scratch,sizeof(buffer));return;
    }
    size_t n=slen(buffer),add=slen(s);
    if(n+add+1>=sizeof(buffer)){console_write("ERROR: LIMITE 1023 BYTES; SIN CAMBIOS.\n");return;}
    while(*s)buffer[n++]=*s++;
    buffer[n++]='\n';buffer[n]=0;
}
