#include "ata.h"
#include "io.h"
#include "interrupts.h"
#include "console.h"
#include "util.h"
#include "storage.h"
static int present;
static uint32_t capacity;
static char model[41];
static const char *error="NO DISPONIBLE";
static uint32_t pci(uint8_t d,uint8_t f,uint8_t reg){
    outl(0xcf8,0x80000000U|((uint32_t)d<<11)|((uint32_t)f<<8)|(reg&0xfc));return inl(0xcfc);
}
static int legacy_ide(void){
    for(int d=0;d<32;d++)for(int f=0;f<8;f++){
        uint32_t id=pci(d,f,0);if((id&65535)==65535)continue;
        uint32_t cls=pci(d,f,8);
        if((cls>>16)==0x0101&&!(cls&(1U<<8)))return 1;
    }
    return 0;
}
static void delay(void){for(int i=0;i<4;i++)(void)inb(0x3f6);}
static int wait_status(int drq){
    uint64_t end=timer_ticks()+100;
    for(unsigned n=0;n<1000000;n++){
        uint8_t s=inb(0x1f7);
        if(s==0||s==255){error="DISPOSITIVO AUSENTE";return 0;}
        if(!(s&0x80)){
            if(drq!=2&&(s&(0x01|0x20))){error="ATA ERR/DF";return 0;}
            if(drq!=1||(s&8))return 1;
        }
        if(timer_ticks()>=end)break;
        __asm__ volatile("pause");
    }
    error="ATA TIMEOUT";return 0;
}
void ata_init(void){
    present=0;capacity=0;if(!legacy_ide()){error="NO HAY IDE PCI LEGACY";return;}
    outb(0x3f6,2);outb(0x1f6,0xa0);delay();
    outb(0x1f2,0);outb(0x1f3,0);outb(0x1f4,0);outb(0x1f5,0);outb(0x1f7,0xec);delay();
    if(!wait_status(1))return;
    if(inb(0x1f4)||inb(0x1f5)){error="NO ES ATA";return;}
    uint16_t words[256];for(int i=0;i<256;i++)words[i]=inw(0x1f0);
    if(!(words[49]&(1<<9))){error="SIN LBA28";return;}
    capacity=words[60]|((uint32_t)words[61]<<16);if(capacity>0x10000000)capacity=0x10000000;
    if(!capacity){error="CAPACIDAD INVALIDA";return;}
    for(int i=0;i<20;i++){model[2*i]=words[27+i]>>8;model[2*i+1]=words[27+i]&255;}
    model[40]=0;present=1;error="OK";
}
int ata_available(void){return present;}
uint32_t ata_capacity(void){return capacity;}
const char *ata_error(void){return error;}
static int prepare(uint32_t lba,int write){
    if(!present){error="NO DISPONIBLE";return 0;}
    if(lba>=capacity||lba>=0x10000000){error="LBA FUERA DE RANGO";return 0;}
    if(write&&!storage_can_write(lba)){error="ESCRITURA FUERA DE ZONA AUTORIZADA";return 0;}
    if(!wait_status(2))return 0;
    outb(0x1f6,0xe0|((lba>>24)&15));delay();
    outb(0x1f1,0);outb(0x1f2,1);outb(0x1f3,lba);outb(0x1f4,lba>>8);outb(0x1f5,lba>>16);
    outb(0x1f7,write?0x30:0x20);delay();return wait_status(1);
}
int ata_read(uint32_t lba,void *buffer){
    if(!buffer||!prepare(lba,0))return 0;
    uint8_t *b=buffer;for(int i=0;i<256;i++){uint16_t w=inw(0x1f0);b[i*2]=w;b[i*2+1]=w>>8;}
    delay();if(!wait_status(0))return 0;error="OK";return 1;
}
int ata_write(uint32_t lba,const void *buffer){
    if(!buffer||!prepare(lba,1))return 0;
    const uint8_t *b=buffer;for(int i=0;i<256;i++)outw(0x1f0,b[i*2]|((uint16_t)b[i*2+1]<<8));
    delay();if(!wait_status(0))return 0;
    outb(0x1f7,0xe7);delay();if(!wait_status(0))return 0;error="OK";return 1;
}
static uint32_t le32(const uint8_t *b){return b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}
void disk_command(const char *arg){
    uint8_t b[512];
    if(ci_same(arg,"INFO")){
        console_write(present?"ATA PRIMARY MASTER 0x1F0 LBA28 MODEL=":"DISK NO DISPONIBLE: ");
        console_write(present?model:error);console_write(" SECTORS=");console_dec(capacity);
        console_write(storage_can_write(2048)?" WRITE=AUTHORIZED\n":" WRITE=DISABLED\n");return;
    }
    if(ci_same(arg,"MBR")){
        if(!ata_read(0,b))goto bad;
        if(b[510]!=0x55||b[511]!=0xaa){console_write("MBR INVALIDO: SIN FIRMA 0x55AA\n");return;}
        console_write("MBR FIRMA 0x55AA (GPT NO INTERPRETADO)\n");
        for(int i=0;i<4;i++){uint8_t *p=b+446+16*i;console_write("PART ");console_dec(i);console_write(" TYPE=");console_hex(p[4]);console_write(" START=");console_dec(le32(p+8));console_write(" SIZE=");console_dec(le32(p+12));console_putc('\n');}
        return;
    }
    if(ci_same(arg,"TEST")){
        if(!storage_can_write(2048)){console_write("ERROR: DISK TEST SIN ZONA AUTORIZADA.\n");return;}
        uint8_t original[512],readback[512];if(!ata_read(2048,original))goto bad;
        for(int i=0;i<512;i++)b[i]=(uint8_t)(i^0xa5);
        int ok=ata_write(2048,b)&&ata_read(2048,readback);
        if(ok)for(int i=0;i<512;i++)if(b[i]!=readback[i])ok=0;
        int restored=ata_write(2048,original);
        console_write(ok&&restored?"DISK TEST PASSED READ WRITE RESTORE\n":"DISK TEST FAILED (RESTAURACION INTENTADA)\n");return;
    }
    if((arg[0]=='R'||arg[0]=='r')&&(arg[1]=='E'||arg[1]=='e')&&(arg[2]=='A'||arg[2]=='a')&&(arg[3]=='D'||arg[3]=='d')&&arg[4]==' '){
        uint64_t lba;if(!udecimal(arg+5,&lba)||lba>UINT32_MAX){console_write("ERROR: LBA INVALIDO\n");return;}
        if(!ata_read(lba,b))goto bad;
        console_write("SECTOR LBA=");console_dec(lba);console_write(" FIRST_32_BYTES:");
        const char *hex="0123456789ABCDEF";for(int i=0;i<32;i++){console_putc(' ');console_putc(hex[b[i]>>4]);console_putc(hex[b[i]&15]);}console_putc('\n');return;
    }
    console_write("USO: DISK INFO | READ LBA | MBR | TEST\n");return;
bad:console_write("ERROR DISK: ");console_write(error);console_putc('\n');
}
