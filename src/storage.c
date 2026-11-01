#include "storage.h"
#include "ata.h"
#include "fs.h"
#include "console.h"
#include "util.h"
/* Explicit little endian format, no ABI padding. Two committed snapshots. */
#define START 2048U
#define PAYLOAD (FS_MAX_NODES*1064U)
#define SECTORS (PAYLOAD/512U)
#define SLOT_SIZE (SECTORS+1U)
static int authorized,active=-1,dirty;
static uint64_t generation;
static uint8_t image[PAYLOAD],previous[PAYLOAD];
static fs_node_t candidate[FS_MAX_NODES];
static uint32_t get32(const uint8_t *b){return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
static void put32(uint8_t *b,uint32_t v){for(int i=0;i<4;i++)b[i]=v>>(8*i);}
static uint64_t get64(const uint8_t *b){return get32(b)|((uint64_t)get32(b+4)<<32);}
static void put64(uint8_t *b,uint64_t v){put32(b,v);put32(b+4,v>>32);}
static int magic(const uint8_t *b,const char *s){for(size_t i=0;s[i];i++)if(b[i]!=(uint8_t)s[i])return 0;return 1;}
static uint32_t checksum(const uint8_t *b,size_t n){uint32_t h=2166136261U;for(size_t i=0;i<n;i++)h=(h^b[i])*16777619U;return h;}
static void encode(void){
    const fs_node_t *nodes=fs_nodes();zero(image,sizeof(image));
    for(int i=0;i<FS_MAX_NODES;i++){
        uint8_t *p=image+i*1064;const fs_node_t *n=&nodes[i];
        p[0]=n->type;p[1]=n->parent_idx;put32(p+4,n->size);
        for(int j=0;j<FS_NAME_MAX;j++)p[8+j]=n->name[j];
        for(int j=0;j<FS_FILE_MAX_SIZE;j++)p[40+j]=n->content[j];
    }
}
static int decode(void){
    zero(candidate,sizeof(candidate));
    for(int i=0;i<FS_MAX_NODES;i++){
        uint8_t *p=image+i*1064;fs_node_t *n=&candidate[i];n->type=p[0];n->parent_idx=p[1];n->size=get32(p+4);
        if(n->type>FS_TYPE_FILE||p[2]||p[3])return 0;
        for(int j=0;j<FS_NAME_MAX;j++)n->name[j]=p[8+j];
        for(int j=0;j<FS_FILE_MAX_SIZE;j++)n->content[j]=p[40+j];
        if(n->name[FS_NAME_MAX-1]||n->content[FS_FILE_MAX_SIZE-1])return 0;
    }
    return fs_import(candidate);
}
int storage_can_write(uint32_t lba){return authorized&&lba>=START&&lba<START+1+2*SLOT_SIZE;}
static int load_slot(int slot,uint64_t *gen){
    uint8_t h[512];uint32_t base=START+1+slot*SLOT_SIZE;
    if(!ata_read(base,h)||!magic(h,"TCOSSNAP")||get32(h+8)!=1||get32(h+12)!=PAYLOAD||get32(h+32)!=checksum(h,32))return 0;
    *gen=get64(h+16);if(!*gen)return 0;
    for(unsigned i=0;i<SECTORS;i++)if(!ata_read(base+1+i,image+i*512))return 0;
    if(checksum(image,PAYLOAD)!=get32(h+24))return 0;
    return decode();
}
void storage_init(void){
    uint8_t mbr[512],h[512];authorized=0;
    if(!ata_available()){console_write("FS MODE=RAM (SIN ATA)\n");return;}
    /* First operations are reads. Only this exact test-image layout enables writes. */
    if(!ata_read(0,mbr)||!ata_read(1,h)||mbr[510]!=0x55||mbr[511]!=0xaa||
       mbr[450]!=0x7f||get32(mbr+454)!=START||get32(mbr+458)!=14336||
       !magic(h,"TCOS-DATA-V1")||get32(h+16)!=16384||ata_capacity()!=16384||
       get32(h+20)!=START||get32(h+24)!=START+1+2*SLOT_SIZE){
        console_write("FS MODE=RAM (FORMATO NO AUTORIZADO; SIN ESCRITURAS)\n");return;
    }
    authorized=1;uint64_t g0=0,g1=0;int v0=load_slot(0,&g0),v1=load_slot(1,&g1);
    if(v0||v1){
        active=v1&&(!v0||g1>g0)?1:0;
        uint64_t g;if(!load_slot(active,&g)){authorized=0;console_write("FS MODE=RAM (ERROR RELECTURA)\n");fs_init();return;}
        generation=g;console_write("FS RECUPERADO GENERATION=");console_dec(g);console_putc('\n');
    }else{
        int nonzero=0;uint8_t hdr[512];
        for(int slot=0;slot<2;slot++){
            if(!ata_read(START+1+slot*SLOT_SIZE,hdr)){authorized=0;break;}
            for(int i=0;i<512;i++)if(hdr[i])nonzero=1;
        }
        if(nonzero||!authorized){authorized=0;fs_init();console_write("FS MODE=RAM (CORRUPT: SIN SNAPSHOTS VALIDOS; ESCRITURA DESACTIVADA)\n");return;}
        console_write("FS SIN SNAPSHOT VALIDO: RAM INICIAL; SE GUARDA EN SIGUIENTE CAMBIO\n");active=-1;
    }
    encode();for(unsigned i=0;i<PAYLOAD;i++)previous[i]=image[i];
}
void storage_sync(void){
    if(!authorized)return;
    encode();int changed=dirty;
    for(unsigned i=0;i<PAYLOAD;i++)if(previous[i]!=image[i]){changed=1;break;}
    if(!changed)return;
    if(generation==UINT64_MAX){console_write("ERROR PERSISTENCIA: GENERACION AGOTADA.\n");return;}
    int next=active==0?1:0;uint32_t base=START+1+next*SLOT_SIZE;uint8_t h[512];zero(h,sizeof(h));dirty=1;
    if(!ata_write(base,h))goto bad;
    for(unsigned i=0;i<SECTORS;i++)if(!ata_write(base+1+i,image+i*512))goto bad;
    copystr((char*)h,"TCOSSNAP",9);put32(h+8,1);put32(h+12,PAYLOAD);put64(h+16,generation+1);
    put32(h+24,checksum(image,PAYLOAD));put32(h+32,checksum(h,32));
    if(!ata_write(base,h))goto bad;
    for(unsigned i=0;i<PAYLOAD;i++)previous[i]=image[i];
    dirty=0;active=next;generation++;console_write("PERSISTENCIA OK GENERATION=");console_dec(generation);console_putc('\n');return;
bad:console_write("ERROR PERSISTENCIA: CAMBIO SOLO EN RAM; SNAPSHOT ANTERIOR CONSERVADO: ");console_write(ata_error());console_putc('\n');
}
void storage_status(void){
    console_write(authorized?"FS MODE=PERSISTENT":"FS MODE=RAM");console_write(" GENERATION=");console_dec(generation);
    console_write(dirty?" DIRTY=YES\n":" DIRTY=NO\n");
}
