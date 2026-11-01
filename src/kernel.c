#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "console.h"
#include "fs.h"
#include "cpp_runner.h"
#include "util.h"
#include "editor.h"
#include "pmm.h"
#include "heap.h"
#include "interrupts.h"
#include "tasks.h"
#include "ata.h"
#include "storage.h"
#include "ai_serial.h"
#include "keyboard.h"
#include "utf8.h"
/* KEEP en linker.ld conserva estas solicitudes incluso con --gc-sections. */
__attribute__((used,section(".limine_requests_start")))
static volatile LIMINE_REQUESTS_START_MARKER
__attribute__((used,section(".limine_requests")))
static volatile LIMINE_BASE_REVISION(3)
#define REQUEST __attribute__((used,section(".limine_requests")))
REQUEST static volatile struct limine_framebuffer_request framebuffer={.id=LIMINE_FRAMEBUFFER_REQUEST,.revision=0};
REQUEST static volatile struct limine_hhdm_request hhdm={.id=LIMINE_HHDM_REQUEST,.revision=0};
REQUEST static volatile struct limine_memmap_request memmap={.id=LIMINE_MEMMAP_REQUEST,.revision=0};
REQUEST static volatile struct limine_kernel_address_request address={.id=LIMINE_KERNEL_ADDRESS_REQUEST,.revision=0};
REQUEST static volatile struct limine_bootloader_info_request bootinfo={.id=LIMINE_BOOTLOADER_INFO_REQUEST,.revision=0};
REQUEST static volatile struct limine_stack_size_request stack={.id=LIMINE_STACK_SIZE_REQUEST,.revision=0,.stack_size=65536};
__attribute__((used,section(".limine_requests_end")))
static volatile LIMINE_REQUESTS_END_MARKER
/* Definiciones en cpu.asm; ld resuelve estas referencias entre los objetos. */
extern int64_t asm_sumar(int64_t a,int64_t b);
extern uint64_t asm_cr0(void),asm_cr3(void),asm_cr4(void);
extern void asm_detener(void) __attribute__((noreturn));
/* Solo las comparaciones ignoran mayusculas; el texto del usuario se conserva. */
static char upper(char c){return c>='a'&&c<='z' ? c-32 : c;}
static int equal(const char *a,const char *b){
    while(*a&&upper(*a)==upper(*b)){a++;b++;}
    return *a==*b;
}
static void info(void){
    console_write("ARQUITECTURA: X86-64 / KERNEL FREESTANDING\n");
    if(bootinfo.response){console_write("BOOTLOADER: ");console_write(bootinfo.response->name);console_write(" ");console_write(bootinfo.response->version);console_putc('\n');}
    console_write("CR0: ");console_hex(asm_cr0());console_putc('\n');
    console_write("CR3: ");console_hex(asm_cr3());console_putc('\n');
    console_write("CR4: ");console_hex(asm_cr4());console_putc('\n');
    console_write("PAGINACION: ");console_write((asm_cr0()&(1ULL<<31))?"ACTIVA\n":"INACTIVA\n");
    if(address.response){
        console_write("BASE FISICA REAL: ");console_hex(address.response->physical_base);console_putc('\n');
        console_write("BASE VIRTUAL REAL: ");console_hex(address.response->virtual_base);console_putc('\n');
    }
    console_write("0x100000 = 1048576 BYTES = 1 MIB. EJEMPLO CLASICO,\nNO ES UNA DIRECCION OBLIGATORIA PARA ESTE KERNEL.\n");
}
static void memory_info(void){
    if(!memmap.response){console_write("MAPA NO DISPONIBLE\n");return;}
    uint64_t usable=0;
    for(uint64_t i=0;i<memmap.response->entry_count;i++){
        struct limine_memmap_entry *e=memmap.response->entries[i];
        if(e->type==LIMINE_MEMMAP_USABLE)usable+=e->length;
    }
    console_write("REGIONES DEL MAPA: ");console_dec(memmap.response->entry_count);
    console_write("\nRAM MARCADA USABLE: ");console_dec(usable/(1024*1024));console_write(" MIB\n");
    console_write("NO INCLUYE REGIONES RESERVADAS O RECUPERABLES.\n");
}

/* Estado de una conversacion: SUMA sin argumentos pide dos numeros. */
enum input_state {
    COMMAND,
    FIRST_NUMBER,
    SECOND_NUMBER,
    TEXT_INPUT,
    COLOR_INPUT,
    BACKGROUND_INPUT,
    MKDIR_PATH_INPUT,
    MKDIR_NAME_INPUT,
    FILE_PATH_INPUT,
    FILE_NAME_INPUT,
    FILE_CONTENT_INPUT,
    OPEN_FILE_INPUT,
    RUN_CPP_INPUT
};
static enum input_state state=COMMAND;
static int64_t first_number;
static int previous_was_cr;
static char pending_dir[FS_PATH_MAX];
static char pending_name[FS_NAME_MAX];

static const char *skip_spaces(const char *s){while(*s==' ')s++;return s;}
/* Lee un entero decimal limitado a +/- 1000000000. No usa atoi ni libc.
 * El limite mantiene la suma muy lejos del desbordamiento de int64_t. */
static int number(const char **input,int64_t *result){
    const char *s=skip_spaces(*input);int sign=1;uint64_t n=0;
    if(*s=='-'||*s=='+'){if(*s=='-')sign=-1;s++;}
    if(*s<'0'||*s>'9')return 0;
    while(*s>='0'&&*s<='9'){
        unsigned digit=(unsigned)(*s-'0');
        if(n>(1000000000ULL-digit)/10)return 0;
        n=n*10+digit;s++;
    }
    if(*s && *s!=' ')return 0;
    *input=s;*result=sign*(int64_t)n;return 1;
}
static int single_number(const char *s,int64_t *n){
    return number(&s,n)&&!*skip_spaces(s);
}
static void sum_result(int64_t a,int64_t b){
    /* Aqui C llama a cpu.asm; el enlazador conecta ambos archivos .o. */
    int64_t result=asm_sumar(a,b);
    console_signed(a);console_write(" + ");
    if(b<0)console_putc('(');
    console_signed(b);if(b<0)console_putc(')');
    console_write(" = ");console_signed(result);console_putc('\n');
    console_write("CALCULADO POR NASM, LLAMADO DESDE C.\n");
}
static void number_error(void){console_write("USA UN ENTERO ENTRE -1000000000 Y 1000000000.\n");}
static void colors(void){console_write("VERDE AZUL ROJO AMARILLO BLANCO MORADO CIAN\n");}
static int color(const char *name){
    uint32_t rgb;
    if(equal(name,"VERDE"))rgb=0x76ed92;
    else if(equal(name,"AZUL"))rgb=0x79b8ff;
    else if(equal(name,"ROJO"))rgb=0xff8a8a;
    else if(equal(name,"AMARILLO"))rgb=0xffdd77;
    else if(equal(name,"BLANCO"))rgb=0xf0f4fa;
    else if(equal(name,"MORADO"))rgb=0xc7a0ff;
    else if(equal(name,"CIAN"))rgb=0x64ebd2;
    else return 0;
    int ok=console_set_colors(rgb,0x0c1423);
    console_write(ok?"COLOR DE TEXTO ACTUALIZADO.\n":"COLOR: NO HAY FRAMEBUFFER COMPATIBLE; SOLO SERIE.\n");
    return 1;
}
static int background(const char *name){
    uint32_t fg,bg;
    if(equal(name,"CLARO")){fg=0x172238;bg=0xf0f4fa;}
    else if(equal(name,"OSCURO")){fg=0x64ebd2;bg=0x0c1423;}
    else if(equal(name,"AZUL")){fg=0xf0f4fa;bg=0x16345b;}
    else return 0;
    int ok=console_set_colors(fg,bg);
    console_write(ok?"FONDO ACTUALIZADO CON TEXTO DE CONTRASTE.\n":"FONDO: NO HAY FRAMEBUFFER COMPATIBLE; SOLO SERIE.\n");
    return 1;
}
/* RUN CPP, RUN_CPP y la entrada guiada comparten la ejecucion de un script. */
static void run_cpp_path(const char *path){
    uint32_t size=0;
    const char *code=fs_read_file(path,&size);
    (void)size;
    if(!code){
        console_write("ARCHIVO NO ENCONTRADO: ");console_write(path);console_putc('\n');
        return;
    }
    console_write("EJECUTANDO SCRIPT C++: ");console_write(path);console_putc('\n');
    int64_t ret=0;
    if(cpp_run_script(code,&ret)){
        console_write("PROGRAMA FINALIZADO CON EXITO.\n");
        console_write("VALOR DE RETORNO (main): ");
        console_signed(ret);
        console_putc('\n');
    }
}
static void help(void){
    console_write(
        "============================================================\n"
        "HELP - COMANDOS DE TOOLCHAIN OS V3.1 E56\n"
        "[SISTEMA Y UTILIDADES]\n"
        "TECLADO [ES|US|1|2] - MAPA PS/2; SERIAL RECIBE UTF-8 DEL ANFITRION\n"
        "HELP | INFO | MEM | UPTIME - AYUDA, SISTEMA, MEMORIA Y RELOJ\n"
        "SUMA [a b] | TEXTO [texto] | ECO texto - ENTRADA DIRECTA/GUIADA\n"
        "COLOR [nombre] | FONDO [nombre] - COLORES DE CONSOLA\n"
        "LIMPIAR - BORRAR PANTALLA | HALT - DETENER CPU HUESPED\n"
        "[SISTEMA DE ARCHIVOS]\n"
        "LS / DIR [ruta] | PWD | CD [ruta] - LISTAR Y NAVEGAR\n"
        "MKDIR [ruta] | CREAR [ruta texto] - CREAR (TAMBIEN GUIADO)\n"
        "OPEN [archivo] | EDIT archivo - LEER O EDITAR TEXTO\n"
        "RM archivo | RMDIR carpeta_vacia - ELIMINAR\n"
        "REN ruta nuevo_nombre | COPY origen destino - COPIAR SIN SOBRESCRIBIR\n"
        "TREE [ruta] - MOSTRAR ARBOL DE ARCHIVOS\n"
        "ALIAS: CREAR_CARPETA, CREAR_ARCHIVO, TOUCH\n"
        "CANCELAR - SALIR DE ENTRADAS GUIADAS (FUERA DEL EDITOR)\n"
        "[EDITOR: SOLO DENTRO DE EDIT>]\n"
        ":show | :clear | :line N texto | :del N\n"
        ":wq - GUARDAR Y SALIR | :q - CANCELAR SIN GUARDAR\n"
        "LAS DEMAS LINEAS SON TEXTO; BACKSPACE CORRIGE LA ENTRADA\n"
        "[MEMORIA, INTERRUPCIONES Y TAREAS]\n"
        "MEMSTATS | PMMTEST - ESTADISTICAS Y PRUEBA DE PAGINAS\n"
        "HEAPSTATS | HEAPTEST | MEMMAP - HEAP Y MAPA LIMINE\n"
        "PS | THREADS | SCHED | SCHED TEST | PROC TEST - PROCESOS KERNEL Y RR\n"
        "TASKS | TASKS START - ESTADOS E INICIO DE TAREAS COOPERATIVAS\n"
        "UPTIME - TICKS PIT | EXTEST DE/PF - EXCEPCION FATAL DE PRUEBA\n"
        "[DISCO Y PERSISTENCIA]\n"
        "DISK INFO | DISK READ <LBA> | DISK MBR | DISK TEST\n"
        "FSSTATUS - ESTADO RAM/DISCO; TEST SOLO EN IMAGEN AUTORIZADA\n"
        "[INTERPRETE DE SCRIPTS]\n"
        "RUN CPP [archivo] | RUN_CPP [archivo] - C++ LIMITADO, SIN LIBRERIAS\n"
        "[INTELIGENCIA ARTIFICIAL - OLLAMA POR COM2]\n"
        "PARA USAR LA INTELIGENCIA ARTIFICIAL, ESCRIBE IA SEGUIDO DE TU PREGUNTA.\n"
        "IA <PREGUNTA> | IA STATUS | IA CANCEL\n"
        "EJEMPLO: IA Que es un sistema operativo?\n"
        "REQUIERE SERVIDOR OLLAMA Y PUENTE COM2 ACTIVOS EN LINUX.\n"
        "============================================================\n"
    );
}
static void prompt(void){
    if(editor_active()){console_write("EDIT> ");return;}
    switch(state){
        case FIRST_NUMBER:console_write("PRIMER NUMERO> ");break;
        case SECOND_NUMBER:console_write("SEGUNDO NUMERO> ");break;
        case TEXT_INPUT:console_write("TU TEXTO> ");break;
        case COLOR_INPUT:console_write("COLOR> ");break;
        case BACKGROUND_INPUT:console_write("FONDO> ");break;
        case MKDIR_PATH_INPUT:console_write("DONDE GUARDAR> ");break;
        case MKDIR_NAME_INPUT:console_write("NOMBRE CARPETA> ");break;
        case FILE_PATH_INPUT:console_write("DONDE GUARDAR> ");break;
        case FILE_NAME_INPUT:console_write("NOMBRE ARCHIVO> ");break;
        case FILE_CONTENT_INPUT:console_write("CONTENIDO> ");break;
        case OPEN_FILE_INPUT:console_write("ARCHIVO> ");break;
        case RUN_CPP_INPUT:console_write("SCRIPT CPP> ");break;
        default:console_write("OS> ");break;
    }
}
static void execute(char *line){
    if(editor_active()){editor_line(line);return;}
    /* El estado se consulta antes de separar comando y argumentos:
     * asi TEXTO conserva todos sus espacios y mayusculas. */
    if(state!=COMMAND){
        if(equal(line,"CANCELAR")){state=COMMAND;console_write("OPERACION CANCELADA.\n");return;}
        int64_t value;
        if(state==FIRST_NUMBER||state==SECOND_NUMBER){
            if(!single_number(line,&value)){number_error();return;}
            if(state==FIRST_NUMBER){first_number=value;state=SECOND_NUMBER;}
            else {sum_result(first_number,value);state=COMMAND;}
        }else if(state==TEXT_INPUT){
            console_write("ESCRIBISTE: ");console_write(line);console_putc('\n');state=COMMAND;
        }else if(state==COLOR_INPUT){
            if(color(skip_spaces(line)))state=COMMAND;
            else {console_write("COLOR NO RECONOCIDO.\n");colors();}
        }else if(state==BACKGROUND_INPUT){
            if(background(skip_spaces(line)))state=COMMAND;
            else console_write("ELIGE CLARO, OSCURO O AZUL.\n");
        }else if(state==MKDIR_PATH_INPUT){
            line=(char*)skip_spaces(line);
            if(slen(line)>=sizeof(pending_dir)){console_write("ERROR: RUTA DEMASIADO LARGA.\n");state=COMMAND;return;}
            copystr(pending_dir,line,sizeof(pending_dir));
            state=MKDIR_NAME_INPUT;
        }else if(state==MKDIR_NAME_INPUT){
            line=(char*)skip_spaces(line);
            char err[64];err[0]=0;
            if(fs_mkdir(pending_dir,line,err,sizeof(err))){
                console_write("CARPETA CREADA CON EXITO.\n");
            }else{
                console_write("ERROR: ");console_write(err[0]?err:"NO SE PUDO CREAR CARPETA");console_putc('\n');
            }
            state=COMMAND;
        }else if(state==FILE_PATH_INPUT){
            line=(char*)skip_spaces(line);
            if(slen(line)>=sizeof(pending_dir)){console_write("ERROR: RUTA DEMASIADO LARGA.\n");state=COMMAND;return;}
            copystr(pending_dir,line,sizeof(pending_dir));
            state=FILE_NAME_INPUT;
        }else if(state==FILE_NAME_INPUT){
            line=(char*)skip_spaces(line);
            if(slen(line)>=sizeof(pending_name)){console_write("ERROR: NOMBRE DEMASIADO LARGO.\n");state=COMMAND;return;}
            copystr(pending_name,line,sizeof(pending_name));
            state=FILE_CONTENT_INPUT;
        }else if(state==FILE_CONTENT_INPUT){
            char err[64];err[0]=0;
            if(fs_create_file(pending_dir,pending_name,line,err,sizeof(err))){
                console_write("ARCHIVO GUARDADO CON EXITO EN RAM (DISCO: VER PERSISTENCIA/FSSTATUS).\n");
            }else{
                console_write("ERROR: ");console_write(err[0]?err:"NO SE PUDO CREAR ARCHIVO");console_putc('\n');
            }
            state=COMMAND;
        }else if(state==OPEN_FILE_INPUT){
            line=(char*)skip_spaces(line);
            uint32_t sz=0;
            const char *c=fs_read_file(line,&sz);
            if(!c){
                console_write("ARCHIVO NO ENCONTRADO O NO ES UN ARCHIVO DE TEXTO.\n");
            }else{
                console_write(c);
                if(sz==0||c[sz-1]!='\n')console_putc('\n');
            }
            state=COMMAND;
        }else if(state==RUN_CPP_INPUT){
            run_cpp_path(skip_spaces(line));
            state=COMMAND;
        }
        return;
    }
    char *cmd=line;while(*cmd==' ')cmd++;
    char *args=cmd;while(*args&&*args!=' ')args++;
    if(*args)*args++=0;
    args=(char*)skip_spaces(args);
    if(equal(cmd,"TECLADO"))keyboard_command(args);
    else if(equal(cmd,"IA"))ai_command(args);
    else if(equal(cmd,"DISK"))disk_command(args);
    else if(equal(cmd,"FSSTATUS"))storage_status();
    else if(equal(cmd,"TASKS"))tasks_command(args);
    else if(equal(cmd,"PS"))process_list();
    else if(equal(cmd,"THREADS"))thread_list();
    else if(equal(cmd,"SCHED"))scheduler_command(args);
    else if(equal(cmd,"PROC")){
        if(equal(args,"TEST"))scheduler_command("TEST");
        else console_write("USO: PROC TEST\n");
    }
    else if(equal(cmd,"HEAPSTATS"))heap_stats();
    else if(equal(cmd,"HEAPTEST"))heap_test();
    else if(equal(cmd,"MEMMAP"))pmm_map();
    else if(equal(cmd,"MEMSTATS"))pmm_stats();
    else if(equal(cmd,"PMMTEST"))pmm_test();
    else if(equal(cmd,"UPTIME"))uptime();
    else if(equal(cmd,"EXTEST"))exception_test(equal(args,"PF"));
    else if(equal(cmd,"EDIT"))editor_open(args);
    else if(equal(cmd,"TREE"))fs_tree(args);
    else if(equal(cmd,"RM")||equal(cmd,"RMDIR")||equal(cmd,"REN")||equal(cmd,"COPY")){
        char err[96];int ok=0;
        if(equal(cmd,"RM")||equal(cmd,"RMDIR"))ok=fs_remove(args,equal(cmd,"RMDIR"),err,sizeof(err));
        else{
            char *other=args;while(*other&&*other!=' ')other++;
            if(!*other){console_write("USO: REN ruta nombre | COPY origen destino\n");return;}
            *other++=0;other=(char*)skip_spaces(other);
            if(equal(cmd,"REN"))ok=fs_rename(args,other,err,sizeof(err));
            else ok=fs_copy(args,other,err,sizeof(err));
        }
        console_write(ok?"OPERACION OK.\n":"ERROR: ");if(!ok){console_write(err);console_putc('\n');}
    }
    else if(equal(cmd,"HELP"))help();
    else if(equal(cmd,"INFO"))info();
    else if(equal(cmd,"MEM"))memory_info();
    else if(equal(cmd,"SUMA")){
        if(!*args){state=FIRST_NUMBER;return;}
        int64_t a,b;const char *cursor=args;
        if(!number(&cursor,&a)||!number(&cursor,&b)||*skip_spaces(cursor)){
            console_write("USO: SUMA NUMERO1 NUMERO2, O SOLO SUMA.\n");number_error();
        }else sum_result(a,b);
    }else if(equal(cmd,"TEXTO")){
        if(*args){console_write("ESCRIBISTE: ");console_write(args);console_putc('\n');}
        else state=TEXT_INPUT;
    }else if(equal(cmd,"ECO")){
        console_write("ESCRIBISTE: ");console_write(args);console_putc('\n');
    }else if(equal(cmd,"COLOR")){
        if(!*args){colors();state=COLOR_INPUT;}
        else if(!color(args)){console_write("COLOR NO RECONOCIDO.\n");colors();}
    }else if(equal(cmd,"FONDO")){
        if(!*args){console_write("CLARO OSCURO AZUL\n");state=BACKGROUND_INPUT;}
        else if(!background(args))console_write("ELIGE CLARO, OSCURO O AZUL.\n");
    }else if(equal(cmd,"OPEN")){
        if(!*args){state=OPEN_FILE_INPUT;return;}
        uint32_t sz=0;
        const char *c=fs_read_file(args,&sz);
        if(!c){
            console_write("ARCHIVO NO ENCONTRADO O NO ES UN ARCHIVO DE TEXTO.\n");
        }else{
            console_write(c);
            if(sz==0||c[sz-1]!='\n')console_putc('\n');
        }
    }else if(equal(cmd,"RUN")){
        char *sub=args;while(*sub==' ')sub++;
        char *sub_arg=sub;while(*sub_arg&&*sub_arg!=' ')sub_arg++;
        if(*sub_arg)*sub_arg++=0;
        sub_arg=(char*)skip_spaces(sub_arg);
        if(!equal(sub,"CPP"))console_write("USO: RUN CPP <SCRIPT>\n");
        else if(!*sub_arg)state=RUN_CPP_INPUT;
        else run_cpp_path(sub_arg);
    }else if(equal(cmd,"RUN_CPP")){
        if(!*args)state=RUN_CPP_INPUT;
        else run_cpp_path(args);
    }else if(equal(cmd,"MKDIR")||equal(cmd,"CREAR_CARPETA")){
        if(!*args){state=MKDIR_PATH_INPUT;pending_dir[0]=0;return;}
        char d[FS_PATH_MAX],n[FS_NAME_MAX],err[64];
        err[0]=0;
        char *p1=args;
        char *p2=p1;while(*p2&&*p2!=' ')p2++;
        if(*p2){
            *p2++=0;p2=(char*)skip_spaces(p2);
            if(slen(p1)>=sizeof(d)||slen(p2)>=sizeof(n)){console_write("ERROR: RUTA O NOMBRE DEMASIADO LARGO.\n");return;}
            copystr(d,p1,sizeof(d));
            copystr(n,p2,sizeof(n));
        }else{
            if(!fs_split(p1,d,n)){console_write("ERROR: RUTA O NOMBRE INVALIDO.\n");return;}
        }
        if(fs_mkdir(d,n,err,sizeof(err))){
            console_write("CARPETA CREADA CON EXITO.\n");
        }else{
            console_write("ERROR: ");console_write(err[0]?err:"NO SE PUDO CREAR CARPETA");console_putc('\n');
        }
    }else if(equal(cmd,"CREAR")||equal(cmd,"CREAR_ARCHIVO")||equal(cmd,"TOUCH")){
        if(!*args){state=FILE_PATH_INPUT;pending_dir[0]=0;return;}
        char *fpath=args;
        char *content=fpath;while(*content&&*content!=' ')content++;
        if(*content){*content++=0;content=(char*)skip_spaces(content);}
        char d[FS_PATH_MAX],n[FS_NAME_MAX],err[64];
        err[0]=0;
        if(!fs_split(fpath,d,n)){console_write("ERROR: RUTA O NOMBRE INVALIDO.\n");return;}
        if(fs_create_file(d,n,content,err,sizeof(err))){
            console_write("ARCHIVO GUARDADO CON EXITO EN RAM (DISCO: VER PERSISTENCIA/FSSTATUS).\n");
        }else{
            console_write("ERROR: ");console_write(err[0]?err:"NO SE PUDO CREAR ARCHIVO");console_putc('\n');
        }
    }else if(equal(cmd,"LS")||equal(cmd,"DIR")){
        fs_list_dir(args);
    }else if(equal(cmd,"CD")){
        if(!*args){
            fs_set_cwd(0);
            console_write("DIRECTORIO ACTUAL: /\n");
        }else{
            int target=fs_find_node(args);
            if(target<0)console_write("CARPETA NO ENCONTRADA.\n");
            else if(!fs_set_cwd(target))console_write("LA RUTA NO ES UNA CARPETA.\n");
            else{
                char cwd_path[FS_PATH_MAX];
                fs_get_cwd_path(cwd_path,sizeof(cwd_path));
                console_write("DIRECTORIO ACTUAL: ");
                console_write(cwd_path);
                console_putc('\n');
            }
        }
    }else if(equal(cmd,"PWD")){
        char cwd_path[FS_PATH_MAX];
        fs_get_cwd_path(cwd_path,sizeof(cwd_path));
        console_write("DIRECTORIO ACTUAL: ");
        console_write(cwd_path);
        console_putc('\n');
    }else if(equal(cmd,"LIMPIAR"))console_clear();
    else if(equal(cmd,"HALT")){console_write("CPU DETENIDA. CIERRA QEMU PARA SALIR.\n");asm_detener();}
    else if(*cmd)console_write("COMANDO DESCONOCIDO. ESCRIBE HELP.\n");
}
void kmain(void){
    __asm__ volatile("cli");
    serial_init();
    ai_init();
    if(!LIMINE_BASE_REVISION_SUPPORTED){console_write("ERROR: REVISION LIMINE NO SOPORTADA\n");asm_detener();}
    if(framebuffer.response&&framebuffer.response->framebuffer_count)
        console_init(framebuffer.response->framebuffers[0]);
    pmm_init(memmap.response,hhdm.response?hhdm.response->offset:0);
    tasks_init();
    interrupts_init();
    keyboard_boot_setup();
    fs_init();
    ata_init();
    storage_init();
    console_write("\n============================================================\n");
    console_write("TOOLCHAIN OS V3.1 E56 - SISTEMA OPERATIVO X86-64\n");
    console_write("BIENVENIDO AL LABORATORIO DE SISTEMAS OPERATIVOS\n");
    console_write("C + NASM + LD + MAKE + LIMINE + QEMU\n");
    console_write("KERNEL EDUCATIVO | ARCHIVOS | MEMORIA | TAREAS | PERSISTENCIA\n");
    console_write("INTEGRACION IA: OLLAMA MEDIANTE PUENTE LINUX Y COM2\n");
    console_write("PARA CONSULTAR A LA IA: IA <PREGUNTA>\n");
    console_write("REQUIERE SERVIDOR OLLAMA Y PUENTE COM2 ACTIVOS.\n");
    console_write("ESCRIBE HELP PARA VER TODOS LOS COMANDOS Y EL EDITOR.\n");
    console_write("============================================================\n");
    console_write("\nKERNEL_READY\n");prompt();
    /* Linea acotada: ruta + contenido de archivo; overflow descarta toda la linea. */
    char line[1153];size_t used=0;int overflow=0,invalid_utf8=0;
    utf8_state_t input={0};
    for(;;){
        tasks_poll();
        if(ai_poll())prompt();
        int c=console_read();if(c<0){__asm__ volatile("pause");continue;}
        if(c=='\n'&&previous_was_cr){previous_was_cr=0;continue;}
        previous_was_cr=(c=='\r');
        if(c=='\r'||c=='\n'){
            line[used]=0;console_putc('\n');
            if(input.count)invalid_utf8=1;
            if(overflow)console_write("ERROR: LINEA DEMASIADO LARGA; DESCARTADA.\n");
            else if(invalid_utf8)console_write("ERROR: UTF-8 INVALIDO; LINEA DESCARTADA.\n");
            else {execute(line);storage_sync();}
            used=0;overflow=invalid_utf8=0;input.count=0;prompt();
        }else if(c=='\b'||c==127){
            if(input.count){input.count=0;continue;}
            if(!overflow&&used){
                size_t previous=utf8_previous(line,used),bytes;uint32_t cp;
                if(utf8_decode((const uint8_t*)line+previous,used-previous,&cp,&bytes)==1)console_erase_last(cp);
                used=previous;
            }
        }else if(c>=32&&c<=255){
            uint32_t cp;int result=utf8_feed(&input,(uint8_t)c,&cp);
            if(result<0){invalid_utf8=1;continue;}
            if(!result)continue;
            if(cp<32||(cp>=127&&cp<160))continue;
            char bytes[4];unsigned n=utf8_encode(cp,bytes);
            if(overflow||used+n>=sizeof(line)){overflow=1;continue;}
            for(unsigned i=0;i<n;i++)line[used++]=bytes[i];
            console_put_unicode(cp);
        }
    }
}
