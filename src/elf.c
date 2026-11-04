#include "elf_loader.h"
#include "console.h"
#include "fs.h"
#include "gui.h"
#include "host_api.h"
#include "interrupts.h"
#include "keyboard.h"
#include "pmm.h"
#include "shell.h"
#include "util.h"

/* Cabeceras ELF64 minimas: no se enlaza libelf ni se usa <elf.h>. */
#define ELF_MAGIC 0x464c457fu
#define ET_DYN 3
#define EM_X86_64 62
#define PT_LOAD 1
#define SHT_RELA 4
#define R_X86_64_RELATIVE 8

typedef struct {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} elf64_header_t;

typedef struct {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} elf64_phdr_t;

typedef struct {
    uint32_t name, type;
    uint64_t flags, addr, offset, size;
    uint32_t link, info;
    uint64_t addralign, entsize;
} elf64_shdr_t;

typedef struct { uint64_t offset, info, addend; } elf64_rela_t;

static struct limine_module_response *module_list;
static int busy;
static int last_code;

/* --- Tabla de servicios expuesta a la aplicacion ------------------------ */

static void api_write(const char *text) { if (text) console_write(text); }
static int api_window(const char *title, int width, int height) { return gui_app_window(title, width, height); }
static int api_width(void) { return gui_app_width(); }
static int api_height(void) { return gui_app_height(); }
static void api_clear(uint32_t rgb) { gui_app_clear(rgb); }
static void api_rect(int x, int y, int w, int h, uint32_t rgb) { gui_app_rect(x, y, w, h, rgb); }
static void api_frame(int x, int y, int w, int h, uint32_t rgb) { gui_app_frame(x, y, w, h, rgb); }
static void api_text(int x, int y, const char *text, uint32_t rgb, int scale) { gui_app_text(x, y, text, rgb, scale); }
static int api_text_width(const char *text, int scale) {
    return gui_text_width(text, scale > 1 ? gui_font_body() : gui_font_small());
}
static void api_icon(int icon, int x, int y, int size, uint32_t rgb) { gui_app_icon(icon, x, y, size, rgb); }
static int api_pump(host_event_t *event) {
    gui_event_t internal;
    int alive = gui_pump(&internal);
    if (event) {
        event->type = internal.type;
        event->x = internal.x;
        event->y = internal.y;
        event->button = internal.button;
        event->key = internal.key;
    }
    return alive;
}
static void api_present(void) { gui_app_present(); }
static uint64_t api_ticks(void) { return timer_ticks(); }
static void api_close(void) { gui_app_close(); }
static int api_finish(int code) { last_code = code; return 0; }

/* --- v3: archivos, copia de seguridad del cwd y ejecucion ---------------- */

static const char *api_read_file(const char *path, uint32_t *size) { return fs_read_file(path, size); }
static int api_write_file(const char *path, const char *text) {
    char error[64]; error[0] = 0;
    return fs_write_path(path, text, error, sizeof(error));
}
static int api_make_dir(const char *dir, const char *name) {
    char error[64]; error[0] = 0;
    return fs_mkdir(dir, name, error, sizeof(error));
}
static int api_remove_node(const char *path) {
    char error[64]; error[0] = 0;
    return fs_remove(path, 0, error, sizeof(error));
}
static int api_rename_node(const char *path, const char *name) {
    char error[64]; error[0] = 0;
    return fs_rename(path, name, error, sizeof(error));
}
static const struct fs_node *api_nodes(void) { return fs_nodes(); }
static int api_node_count(void) { return FS_MAX_NODES; }
static int api_cwd(void) { return fs_cwd(); }
static int api_chdir(int index) { return fs_set_cwd(index); }
static void api_cwd_path(char *buffer, int capacity) {
    if (!buffer || capacity <= 0) return;
    fs_get_cwd_path(buffer, (size_t)capacity);
}
static int api_exec(const char *line, char *out, int capacity) {
    if (!out || capacity <= 0) return 0;
    return shell_exec(line, out, (size_t)capacity);
}
static int api_key(void) { return keyboard_special(); }

static const host_api_t host = {
    .abi_version = HOST_ABI_VERSION,
    .size = sizeof(host_api_t),
    .write = api_write, .window = api_window, .width = api_width, .height = api_height,
    .clear = api_clear, .rect = api_rect, .frame = api_frame, .text = api_text,
    .text_width = api_text_width, .icon = api_icon, .pump = api_pump,
    .present = api_present, .ticks = api_ticks, .close = api_close, .finish = api_finish,
    .read_file = api_read_file, .write_file = api_write_file, .make_dir = api_make_dir,
    .remove_node = api_remove_node, .rename_node = api_rename_node,
    .nodes = api_nodes, .node_count = api_node_count,
    .cwd = api_cwd, .chdir = api_chdir, .cwd_path = api_cwd_path,
    .exec = api_exec, .key = api_key,
};

/* --- Servicios del cargador -------------------------------------------- */

int elf_init(struct limine_module_response *modules) {
    module_list = modules;
    busy = 0;
    last_code = 0;
    return modules != 0;
}

unsigned elf_count(void) {
    if (!module_list) return 0;
    return (unsigned)module_list->module_count;
}

static const char *base_name(const char *path) {
    const char *name = path;
    for (const char *cursor = path; cursor && *cursor; cursor++) if (*cursor == '/') name = cursor + 1;
    return name;
}

const char *elf_name(unsigned index) {
    if (index >= elf_count()) return "";
    return base_name(module_list->modules[index]->path);
}

size_t elf_size(unsigned index) {
    if (index >= elf_count()) return 0;
    return (size_t)module_list->modules[index]->size;
}

int elf_busy(void) { return busy; }

static void describe_error(const char *message) {
    console_write("ELF: "); console_write(message); console_putc('\n');
}

/* Copia segmentos, aplica R_X86_64_RELATIVE y ejecuta. Se rechaza cualquier
 * otro tipo de reubicacion: el ABI exige PIE estatico sin simbolos externos. */
static int load_and_run(const uint8_t *image, size_t size, const char *name) {
    if (size < sizeof(elf64_header_t)) { describe_error("CABECERA INCOMPLETA"); return 0; }
    const elf64_header_t *header = (const elf64_header_t *)image;
    if (*(const uint32_t *)header->ident != ELF_MAGIC) { describe_error("NO ES ELF"); return 0; }
    if (header->ident[4] != 2 || header->ident[5] != 1) { describe_error("NO ES ELF64 LSB"); return 0; }
    if (header->machine != EM_X86_64) { describe_error("MAQUINA DISTINTA DE X86-64"); return 0; }
    if (header->type != ET_DYN) { describe_error("SE REQUIERE PIE (ET_DYN)"); return 0; }
    if (!header->phnum || header->phoff + (uint64_t)header->phnum * header->phentsize > size) {
        describe_error("TABLA DE SEGMENTOS INVALIDA");
        return 0;
    }

    uint64_t lowest = ~0ull, highest = 0;
    const elf64_phdr_t *programs = (const elf64_phdr_t *)(image + header->phoff);
    for (unsigned i = 0; i < header->phnum; i++) {
        if (programs[i].type != PT_LOAD || !programs[i].memsz) continue;
        if (programs[i].vaddr < lowest) lowest = programs[i].vaddr;
        uint64_t end = programs[i].vaddr + programs[i].memsz;
        if (end > highest) highest = end;
    }
    if (lowest == ~0ull || highest <= lowest) { describe_error("SIN SEGMENTOS CARGABLES"); return 0; }
    uint64_t span_start = lowest & ~0xfffull;
    uint64_t span_end = (highest + 0xfff) & ~0xfffull;
    uint64_t pages = (span_end - span_start) / 4096;
    uint64_t physical = pmm_alloc_run(pages);
    if (!physical) { describe_error("SIN MEMORIA PARA LA IMAGEN"); return 0; }
    uint8_t *destination = pmm_ptr(physical);
    zero(destination, (size_t)(pages * 4096));
    uint64_t bias = (uint64_t)destination - span_start;

    for (unsigned i = 0; i < header->phnum; i++) {
        if (programs[i].type != PT_LOAD || !programs[i].memsz) continue;
        if (programs[i].offset + programs[i].filesz > size) {
            describe_error("SEGMENTO FUERA DEL MODULO");
            pmm_free_run(physical, pages);
            return 0;
        }
        uint8_t *target = (uint8_t *)(bias + programs[i].vaddr);
        const uint8_t *source = image + programs[i].offset;
        for (uint64_t n = 0; n < programs[i].filesz; n++) target[n] = source[n];
        for (uint64_t n = programs[i].filesz; n < programs[i].memsz; n++) target[n] = 0;
    }

    /* Reubicaciones dinamicas: sh_info == 0 identifica a .rela.dyn; las
     * secciones .rela.text son PC-relativas y no necesitan parcheo. */
    unsigned applied = 0;
    if (header->shoff && header->shnum && header->shentsize >= sizeof(elf64_shdr_t) &&
        header->shoff + (uint64_t)header->shnum * header->shentsize <= size) {
        const elf64_shdr_t *sections = (const elf64_shdr_t *)(image + header->shoff);
        for (unsigned i = 0; i < header->shnum; i++) {
            if (sections[i].type != SHT_RELA || sections[i].info != 0) continue;
            if (sections[i].offset + sections[i].size > size) continue;
            if (sections[i].entsize && sections[i].entsize < sizeof(elf64_rela_t)) continue;
            uint64_t count = sections[i].size / sizeof(elf64_rela_t);
            const elf64_rela_t *entries = (const elf64_rela_t *)(image + sections[i].offset);
            for (uint64_t r = 0; r < count; r++) {
                uint32_t kind = (uint32_t)(entries[r].info & 0xffffffffu);
                if (kind != R_X86_64_RELATIVE) {
                    console_write("ELF: REUBICACION NO SOPORTADA TIPO=");
                    console_dec(kind); console_putc('\n');
                    pmm_free_run(physical, pages);
                    return 0;
                }
                uint64_t *slot = (uint64_t *)(bias + entries[r].offset);
                *slot = bias + (uint64_t)entries[r].addend;
                applied++;
            }
        }
    }

    uint64_t stack_pages = 4;
    uint64_t stack_physical = pmm_alloc_run(stack_pages);
    if (!stack_physical) {
        describe_error("SIN MEMORIA PARA LA PILA");
        pmm_free_run(physical, pages);
        return 0;
    }
    uint8_t *stack = pmm_ptr(stack_physical);
    uint64_t stack_top = (uint64_t)stack + stack_pages * 4096 - 16;

    console_write("ELF: EJECUTANDO "); console_write(name);
    console_write(" ENTRY="); console_hex(bias + header->entry);
    console_write(" PAGINAS="); console_dec(pages);
    console_write(" RELOCS="); console_dec(applied); console_putc('\n');

    busy = 1;
    last_code = 0;
    elf_call_app(bias + header->entry, stack_top, &host);
    busy = 0;

    gui_app_close();
    pmm_free_run(stack_physical, stack_pages);
    pmm_free_run(physical, pages);
    console_write("ELF: FIN "); console_write(name);
    console_write(" CODIGO="); console_signed(last_code); console_putc('\n');
    return 1;
}

int elf_run(unsigned index) {
    if (busy) { describe_error("YA HAY UNA APLICACION EN EJECUCION"); return 0; }
    if (index >= elf_count()) { describe_error("INDICE DE MODULO INVALIDO"); return 0; }
    if (!gui_active()) { describe_error("LA INTERFAZ GRAFICA NO ESTA ACTIVA"); return 0; }
    struct limine_file *module = module_list->modules[index];
    return load_and_run((const uint8_t *)module->address, (size_t)module->size, base_name(module->path));
}

void elf_list(void) {
    unsigned count = elf_count();
    if (!count) {
        console_write("ELF: SIN MODULOS. COPIA APLICACIONES CON 'make apps' Y RECONSTRUYE LA ISO.\n");
        return;
    }
    console_write("APLICACIONES ELF DISPONIBLES (MODULOS LIMINE):\n");
    for (unsigned i = 0; i < count; i++) {
        console_write("  "); console_dec(i); console_write("  ");
        console_write(elf_name(i));
        console_write("  "); console_dec((uint64_t)elf_size(i)); console_write(" BYTES\n");
    }
    console_write("EJECUTAR: ELF RUN <indice> O GUI > APPS.\n");
}

void elf_command(const char *args) {
    const char *cursor = args;
    while (*cursor == ' ') cursor++;
    if (!*cursor) { elf_list(); return; }
    char word[8]; size_t length = 0;
    while (*cursor && *cursor != ' ' && length + 1 < sizeof(word)) word[length++] = *cursor++;
    word[length] = 0;
    if (ci_same(word, "LIST") || ci_same(word, "LISTAR")) { elf_list(); return; }
    if (ci_same(word, "RUN") || ci_same(word, "EJECUTAR")) {
        while (*cursor == ' ') cursor++;
        uint64_t index = 0;
        if (!udecimal(cursor, &index)) { console_write("USO: ELF RUN <indice>\n"); return; }
        elf_run((unsigned)index);
        return;
    }
    console_write("USO: ELF | ELF RUN <indice> | ELF LIST\n");
}
