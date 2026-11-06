#include "fs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void console_write(const char *s){(void)s;}
void console_putc(char c){(void)c;}
void console_dec(uint64_t n){(void)n;}
int main(void){
    char err[128],name[32],text[1025];fs_init();
    int available=0; for(int i=1;i<FS_MAX_NODES;i++) if(!fs_nodes()[i].type) available++;
    assert(available>30);
    for(int i=0;i<available;i++){snprintf(name,sizeof(name),"n%d",i);assert(fs_mkdir("/",name,err,sizeof(err)));}
    assert(!fs_mkdir("/","full",err,sizeof(err)));
    assert(fs_remove("/n30",1,err,sizeof(err)));
    assert(fs_mkdir("/","reuse",err,sizeof(err)));
    assert(fs_remove("/reuse",1,err,sizeof(err)));
    memset(text,'x',1023);text[1023]=0;
    assert(fs_create_file("/","exact",text,err,sizeof(err)));
    text[1023]='x';text[1024]=0;
    assert(!fs_create_file("/","exact",text,err,sizeof(err)));
    uint32_t sz;assert(fs_read_file("/exact",&sz)&&sz==1023);
    assert(fs_find_node("/exact/no")==-1);
    fs_init();char path[128]="/";int depth=available<50?available:50;for(int i=0;i<depth;i++){assert(fs_mkdir(path,"a",err,sizeof(err)));strcat(path,"a/");}
    assert(fs_find_node(path)>0);assert(fs_set_cwd(fs_find_node(path)));assert(!fs_remove(".",1,err,sizeof(err)));
    assert(fs_find_node("../../")>0);
    puts("PASSED: FS capacidad, reutilizacion, contenido exacto/rechazado y profundidad acotada a nodos disponibles");
    return 0;
}
