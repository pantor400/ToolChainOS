#include "agent.h"
#include "console.h"
#include "util.h"

/* Lista blanca por prefijo. needs_argument exige algo despues del prefijo:
 * asi "OPEN" sin ruta (que abriria la entrada guiada) no se acepta. */
typedef struct { const char *prefix; unsigned needs_argument; } agent_rule_t;

/* Solo lectura: observar el sistema no puede romper nada. */
static const agent_rule_t read_rules[]={
    {"LS",0},{"DIR",0},{"TREE",0},{"PWD",0},{"CD",1},{"OPEN",1},
    {"INFO",0},{"MEM",0},{"MEMSTATS",0},{"MEMMAP",0},{"HEAPSTATS",0},
    {"UPTIME",0},{"PS",0},{"THREADS",0},{"SCHED",0},{"TASKS",0},{"FSSTATUS",0},
    {"DISK INFO",0},{"DISK READ",1},{"DISK MBR",0},
    {"HELP",0},{"TECLADO",0},{"COLOR",1},{"FONDO",1},
    {"SUMA",1},{"ECO",1},{"TEXTO",1},
    {"RUN CPP",1},{"RUN_CPP",1},
    {0,0}
};
/* Escritura: solo con AGENTE ESCRITURA ON explicito del operador. */
static const agent_rule_t write_rules[]={
    {"CREAR",1},{"TOUCH",1},{"MKDIR",1},{"CREAR_CARPETA",1},
    {"RM",1},{"RMDIR",1},{"REN",1},{"COPY",1},
    {0,0}
};
static void (*runner_hook)(char *line);
static int writes_enabled;

static const char *skip_spaces(const char *s){while(*s==' ')s++;return s;}
void agent_bind(void (*runner)(char *line)){runner_hook=runner;}
void agent_allow_writes(int enabled){writes_enabled=enabled?1:0;}
int agent_writes(void){return writes_enabled;}
static int ci_prefix(const char *text,const char *prefix){
    while(*prefix){
        char a=*text++,b=*prefix++;
        if(a>='a'&&a<='z')a-=32;
        if(b>='a'&&b<='z')b-=32;
        if(a!=b)return 0;
    }
    return *text==0||*text==' ';
}
static const agent_rule_t *match_rule(const agent_rule_t *rules,const char *command){
    for(unsigned i=0;rules[i].prefix;i++)
        if(ci_prefix(command,rules[i].prefix))
            return rules[i].needs_argument&&!*skip_spaces(command+slen(rules[i].prefix))?0:&rules[i];
    return 0;
}
/* Cadena simple sin libc: cabe en el bufer de observacion o se corta. */
static void say(char *out,size_t capacity,const char *text){
    size_t n=slen(out);
    while(text&&*text&&n+1<capacity)out[n++]=*text++;
    out[n]=0;
}
/* Todo rechazo se ve en pantalla y viaja igual en la observacion. */
static int reject(char *observation,size_t capacity,const char *reason,const char *command){
    observation[0]=0;say(observation,capacity,reason);say(observation,capacity,command);
    console_write(reason);console_write(command);console_putc('\n');
    return 0;
}
int agent_allows(const char *command){
    const char *s=skip_spaces(command?command:"");
    size_t len=slen(s);
    if(!len||len>AGENT_CMD_MAX)return 0;
    /* Fuera separadores de shell y control: una orden, una entrada. */
    for(size_t i=0;i<len;i++){
        char c=s[i];
        if(c<32||c==127)return 0;
        if(c==';'||c=='|'||c=='&'||c=='`'||c=='$'||c=='>'||c=='<')return 0;
    }
    if(match_rule(read_rules,s))return 1;
    if(match_rule(write_rules,s))return writes_enabled?1:2;
    return 0;
}
int agent_execute(const char *command,char *observation,size_t capacity){
    if(!observation||!capacity)return 0;
    observation[0]=0;
    if(!runner_hook)return reject(observation,capacity,"RECHAZADO: SIN EJECUTOR VINCULADO.","");
    const char *input=skip_spaces(command?command:"");
    if(!*input)return reject(observation,capacity,"RECHAZADO: ORDEN VACIA.","");
    if(slen(input)>AGENT_CMD_MAX)return reject(observation,capacity,"RECHAZADO: ORDEN DEMASIADO LARGA (>160 BYTES).","");
    char work[AGENT_CMD_MAX+1];
    copystr(work,input,sizeof(work));
    /* Un lote son varias ordenes separadas por salto de linea. */
    char *lines[AGENT_LINES_MAX];
    unsigned count=0;
    char *cursor=work;
    while(*cursor){
        char *start=cursor;
        while(*cursor&&*cursor!='\n')cursor++;
        if(*cursor)*cursor++=0;
        start=(char*)skip_spaces(start);
        if(!*start)continue;
        if(count==AGENT_LINES_MAX){return reject(observation,capacity,"RECHAZADO: MAS DE 4 ORDENES POR LOTE.","");}
        char *end=start+slen(start);
        while(end>start&&(end[-1]==' '||end[-1]=='\r'))*--end=0;
        lines[count++]=start;
    }
    if(!count)return reject(observation,capacity,"RECHAZADO: ORDEN VACIA.","");
    for(unsigned i=0;i<count;i++){
        int verdict=agent_allows(lines[i]);
        if(verdict==1)continue;
        return reject(observation,capacity,verdict==2?
            "RECHAZADO: REQUIERE 'AGENTE ESCRITURA ON': ":"RECHAZADO: FUERA DE LISTA BLANCA: ",lines[i]);
    }
    /* El eco queda fuera de la captura: la observacion es solo la salida. */
    console_write("AGENTE> ");console_write(input);console_putc('\n');
    console_capture_begin(observation,capacity);
    for(unsigned i=0;i<count;i++)runner_hook(lines[i]);
    if(console_capture_end())say(observation,capacity,"\n[SALIDA TRUNCADA]\n");
    return 1;
}
void agent_catalog(void){
    console_write("AGENTE: LISTA BLANCA (");
    console_write(writes_enabled?"LECTURA Y ESCRITURA":"SOLO LECTURA");
    console_write(")\n");
    console_write("NAVEGAR: LS DIR TREE PWD CD OPEN\n");
    console_write("SISTEMA: INFO MEM MEMSTATS MEMMAP HEAPSTATS UPTIME PS THREADS SCHED TASKS FSSTATUS\n");
    console_write("DISCO: DISK INFO | DISK READ <LBA> | DISK MBR\n");
    console_write("UTILES: HELP TECLADO COLOR FONDO SUMA ECO TEXTO RUN CPP RUN_CPP\n");
    console_write(writes_enabled?"ESCRITURA: CREAR TOUCH MKDIR CREAR_CARPETA RM RMDIR REN COPY\n"
                                 :"ESCRITURA: DESHABILITADA; USA AGENTE ESCRITURA ON\n");
    console_write("UN LOTE ACEPTA HASTA 4 ORDENES SEPARADAS POR SALTO DE LINEA.\n");
}
