#include "fs.h"
#include "console.h"
#include "util.h"

static fs_node_t nodes[FS_MAX_NODES];
static int cwd_idx;
static int fail(char *e,size_t n,const char *s){copystr(e,s,n);return 0;}
static int valid_name(const char *s){
    size_t n=slen(s);if(!n||n>=FS_NAME_MAX||same(s,".")||same(s,".."))return 0;
    for(size_t i=0;i<n;i++)if((unsigned char)s[i]<=32||s[i]=='/'||s[i]==127)return 0;
    return 1;
}
static int child(int p,const char *name){
    if(same(name,"."))return p;
    if(same(name,".."))return nodes[p].parent_idx;
    for(int i=1;i<FS_MAX_NODES;i++)if(nodes[i].type&&nodes[i].parent_idx==p&&ci_same(nodes[i].name,name))return i;
    return -1;
}
int fs_set_cwd(int i){if(i<0||i>=FS_MAX_NODES||nodes[i].type!=FS_TYPE_DIR)return 0;cwd_idx=i;return 1;}
int fs_cwd(void){return cwd_idx;}
static size_t path_length(int i){
    size_t n=0;int depth=0;
    while(i>0&&i<FS_MAX_NODES&&depth++<FS_MAX_NODES){n+=1+slen(nodes[i].name);i=nodes[i].parent_idx;}
    return n?n:1;
}
void fs_get_cwd_path(char *buf,size_t cap){
    int chain[FS_MAX_NODES],count=0,i=cwd_idx;size_t pos=0;
    if(!cap)return;
    if(!i){copystr(buf,"/",cap);return;}
    while(i>0&&i<FS_MAX_NODES&&count<FS_MAX_NODES){chain[count++]=i;i=nodes[i].parent_idx;}
    while(count){i=chain[--count];if(pos+1<cap)buf[pos++]='/';for(size_t j=0;nodes[i].name[j]&&pos+1<cap;j++)buf[pos++]=nodes[i].name[j];}
    buf[pos]=0;
}
int fs_find_node(const char *path){
    if(!path||slen(path)>=FS_PATH_MAX)return -1;
    int cur=*path=='/'?0:cwd_idx;
    while(*path){
        while(*path=='/')path++;
        if(!*path)break;
        if(nodes[cur].type!=FS_TYPE_DIR)return -1;
        char part[FS_NAME_MAX];size_t n=0;
        while(*path&&*path!='/'){if(n+1>=sizeof(part))return -1;part[n++]=*path++;}
        part[n]=0;cur=child(cur,part);if(cur<0)return -1;
        if(*path=='/'&&nodes[cur].type!=FS_TYPE_DIR)return -1;
    }
    return cur;
}
int fs_split(const char *path,char *dir,char *name){
    size_t n=slen(path),last=0;
    dir[0]=name[0]=0;
    if(!n||n>=FS_PATH_MAX)return 0;
    for(size_t i=0;i<n;i++)if(path[i]=='/')last=i+1;
    if(!valid_name(path+last))return 0;
    copystr(name,path+last,FS_NAME_MAX);
    if(last){size_t d=last==1?1:last-1;for(size_t i=0;i<d;i++)dir[i]=path[i];dir[d]=0;}
    return 1;
}
static int parent(const char *dir,const char *name,char *e,size_t n){
    if(!valid_name(name)){fail(e,n,"NOMBRE INVALIDO O DEMASIADO LARGO.");return -1;}
    int p=fs_find_node(dir?dir:"");
    if(p<0||nodes[p].type!=FS_TYPE_DIR){fail(e,n,"CARPETA DESTINO INVALIDA.");return -1;}
    if((p?path_length(p):0)+1+slen(name)>=FS_PATH_MAX){fail(e,n,"RUTA DEMASIADO LARGA.");return -1;}
    return p;
}
static int alloc_node(void){for(int i=1;i<FS_MAX_NODES;i++)if(!nodes[i].type)return i;return -1;}
int fs_mkdir(const char *dir,const char *name,char *e,size_t n){
    int p=parent(dir,name,e,n);if(p<0)return 0;
    if(child(p,name)>=0)return fail(e,n,"YA EXISTE UN ELEMENTO CON ESE NOMBRE.");
    int i=alloc_node();if(i<0)return fail(e,n,"SISTEMA DE ARCHIVOS LLENO.");
    zero(&nodes[i],sizeof(nodes[i]));nodes[i].type=FS_TYPE_DIR;nodes[i].parent_idx=p;copystr(nodes[i].name,name,FS_NAME_MAX);return 1;
}
int fs_create_file(const char *dir,const char *name,const char *text,char *e,size_t n){
    if(slen(text)>=FS_FILE_MAX_SIZE)return fail(e,n,"CONTENIDO EXCEDE 1023 BYTES.");
    int p=parent(dir,name,e,n);if(p<0)return 0;
    int i=child(p,name);
    if(i>=0&&nodes[i].type!=FS_TYPE_FILE)return fail(e,n,"YA EXISTE UNA CARPETA CON ESE NOMBRE.");
    if(i<0)i=alloc_node();
    if(i<0)return fail(e,n,"SISTEMA DE ARCHIVOS LLENO.");
    zero(&nodes[i],sizeof(nodes[i]));nodes[i].type=FS_TYPE_FILE;nodes[i].parent_idx=p;
    copystr(nodes[i].name,name,FS_NAME_MAX);copystr(nodes[i].content,text,FS_FILE_MAX_SIZE);nodes[i].size=slen(text);return 1;
}
int fs_write_path(const char *path,const char *text,char *e,size_t n){
    char d[FS_PATH_MAX],name[FS_NAME_MAX];if(!fs_split(path,d,name))return fail(e,n,"RUTA O NOMBRE INVALIDO.");
    return fs_create_file(d,name,text,e,n);
}
const char *fs_read_file(const char *path,uint32_t *size){
    int i=fs_find_node(path);if(i<0||nodes[i].type!=FS_TYPE_FILE)return NULL;
    if(size)*size=nodes[i].size;
    return nodes[i].content;
}
int fs_remove(const char *path,int directory,char *e,size_t n){
    int i=fs_find_node(path);if(i<=0)return fail(e,n,"NO EXISTE O ES RAIZ.");
    if(nodes[i].type!=(directory?FS_TYPE_DIR:FS_TYPE_FILE))return fail(e,n,"TIPO INCORRECTO.");
    if(i==cwd_idx)return fail(e,n,"NO SE PUEDE BORRAR DIRECTORIO ACTUAL.");
    for(int j=1;j<FS_MAX_NODES;j++)if(nodes[j].type&&nodes[j].parent_idx==i)return fail(e,n,"CARPETA NO VACIA.");
    zero(&nodes[i],sizeof(nodes[i]));return 1;
}
int fs_rename(const char *path,const char *name,char *e,size_t n){
    int i=fs_find_node(path);if(i<=0)return fail(e,n,"NO EXISTE O ES RAIZ.");
    if(!valid_name(name))return fail(e,n,"NOMBRE INVALIDO.");
    int duplicate=child(nodes[i].parent_idx,name);
    if(duplicate>=0&&duplicate!=i)return fail(e,n,"YA EXISTE UN ELEMENTO CON ESE NOMBRE.");
    int delta=(int)slen(name)-(int)slen(nodes[i].name);
    for(int j=1;j<FS_MAX_NODES;j++)if(nodes[j].type){
        int k=j;for(int depth=0;k>0&&depth<FS_MAX_NODES;depth++,k=nodes[k].parent_idx)
            if(k==i&&(int)path_length(j)+delta>=FS_PATH_MAX)return fail(e,n,"RUTA DEMASIADO LARGA.");
    }
    copystr(nodes[i].name,name,FS_NAME_MAX);return 1;
}
int fs_copy(const char *src,const char *dst,char *e,size_t n){
    const char *text=fs_read_file(src,NULL);if(!text)return fail(e,n,"ORIGEN NO ES ARCHIVO.");
    if(fs_find_node(dst)>=0)return fail(e,n,"DESTINO YA EXISTE; COPY NO SOBRESCRIBE.");
    return fs_write_path(dst,text,e,n);
}
void fs_list_dir(const char *path){
    int p=fs_find_node(path?path:"");if(p<0||nodes[p].type!=FS_TYPE_DIR){console_write("CARPETA NO ENCONTRADA.\n");return;}
    console_write("CONTENIDO DE: ");console_write(nodes[p].name);console_putc('\n');int count=0;
    for(int i=1;i<FS_MAX_NODES;i++)if(nodes[i].type&&nodes[i].parent_idx==p){
        count++;console_write(nodes[i].type==FS_TYPE_DIR?"  [DIR] ":"  [TXT] ");console_write(nodes[i].name);
        if(nodes[i].type==FS_TYPE_FILE){console_write(" (");console_dec(nodes[i].size);console_write(" B)");}console_putc('\n');
    }
    if(!count)console_write("  (CARPETA VACIA)\n");
}
void fs_tree(const char *path){
    int root=fs_find_node(path?path:"");if(root<0){console_write("ERROR: RUTA NO EXISTE.\n");return;}
    int stack[FS_MAX_NODES],depths[FS_MAX_NODES],top=0;uint64_t seen=0;
    stack[top]=root;depths[top++]=0;
    while(top){
        --top;int i=stack[top],depth=depths[top];
        if(i<0||i>=FS_MAX_NODES||(seen&(1ULL<<i))){console_write("ERROR: ARBOL INVALIDO.\n");return;}
        seen|=1ULL<<i;
        for(int j=0;j<depth;j++)console_write("  ");
        console_write(nodes[i].name);if(nodes[i].type==FS_TYPE_DIR)console_putc('/');console_putc('\n');
        for(int j=FS_MAX_NODES-1;j>0;j--)if(nodes[j].type&&nodes[j].parent_idx==i){if(top>=FS_MAX_NODES)return;stack[top]=j;depths[top++]=depth+1;}
    }
}
const fs_node_t *fs_nodes(void){return nodes;}
/* Validate the entire imported graph before committing any node. */
int fs_import(const fs_node_t *in){
    for(int i=0;i<FS_MAX_NODES;i++)if(in[i].name[FS_NAME_MAX-1]||in[i].content[FS_FILE_MAX_SIZE-1])return 0;
    if(in[0].type!=FS_TYPE_DIR||!same(in[0].name,"/")||in[0].parent_idx!=0)return 0;
    for(int i=0;i<FS_MAX_NODES;i++){
        if(in[i].type<FS_TYPE_FREE||in[i].type>FS_TYPE_FILE)return 0;
        if(!in[i].type)continue;
        if(i&&(!valid_name(in[i].name)||in[i].parent_idx<0||in[i].parent_idx>=FS_MAX_NODES||in[in[i].parent_idx].type!=FS_TYPE_DIR))return 0;
        if(in[i].size>=FS_FILE_MAX_SIZE||slen(in[i].content)!=in[i].size)return 0;
        if(in[i].type==FS_TYPE_DIR&&in[i].size)return 0;
        int k=i,depth=0;size_t len=0;
        while(k&&depth++<FS_MAX_NODES){len+=1+slen(in[k].name);k=in[k].parent_idx;if(k<0||k>=FS_MAX_NODES)return 0;}
        if(k||len>=FS_PATH_MAX)return 0;
        for(int j=1;j<i;j++)if(in[j].type&&in[i].parent_idx==in[j].parent_idx&&ci_same(in[i].name,in[j].name))return 0;
    }
    for(int i=0;i<FS_MAX_NODES;i++)nodes[i]=in[i];
    cwd_idx=0;return 1;
}
void fs_init(void){
    zero(nodes,sizeof(nodes));nodes[0].type=FS_TYPE_DIR;copystr(nodes[0].name,"/",FS_NAME_MAX);cwd_idx=0;
    fs_create_file("/","script","int main() {\n    int sum = 0;\n    for (int i = 1; i <= 10; i++) {\n        sum += i;\n    }\n    return sum;\n}\n",NULL,0);
    fs_create_file("/","nota","Bienvenido a ToolchainOS.\nGestion de archivos y ejecutor de scripts C++ activo.\n",NULL,0);
}
