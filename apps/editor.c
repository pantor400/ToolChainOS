#include "host_api.h"
#include "fs.h"

/* Editor de texto de la interfaz grafica (ABI v4).
 *
 * Se abre con una plantilla en blanco sin nombre; la barra superior ofrece
 * los botones Nuevo, Abrir y Guardar. Abrir y Guardar muestran un selector
 * interno con el arbol del sistema de archivos, para elegir que archivo leer
 * y en que carpeta dejarlo. El texto se desplaza con una barra vertical
 * cuando no cabe en el area visible. No usa libc. */

#define PANEL  0xffffff
#define BG     0xf4f7fc
#define TEXT   0x14213a
#define DIM    0x5a6b85
#define ACCENT 0x2470e8
#define SOFT   0xdce8fb
#define BORDER 0xc9d5e6
#define TRACK  0xdfe8f5
#define THUMB  0x6f93c8
#define SHADE  0xc3d0e2
#define WHITE  0xffffff

/* El sistema de archivos guarda como maximo 1023 bytes utiles por archivo. */
#define CAPACITY   (FS_FILE_MAX_SIZE - 1)
#define LINE_MAX   220
#define LINE_H     20
#define TOOLBAR    38
#define STATUS     26
#define SCROLL_W   10
#define EDGE       12
#define ROW_H      20
#define SMALL_H    18

enum { DLG_NONE = 0, DLG_OPEN, DLG_SAVE };
enum { BTN_NEW = 0, BTN_OPEN, BTN_SAVE, BTN_COUNT };

static host_api_t *os;

static char text[CAPACITY + 1];
static int length, cursor, view, follow = 1;
static char path[FS_PATH_MAX];              /* "" = plantilla sin nombre */
static char message[72] = "Plantilla en blanco";

static int dialog;
static int dlg_cwd, dlg_sel, dlg_scroll, dlg_drag;
static int dlg_ids[FS_MAX_NODES];
static char dlg_name[FS_NAME_MAX];
static int dlg_name_len;
/* Los archivos internos del sistema (/etc, /boot, /proc, /wallpapers) se abren
 * para consultarlos, pero el editor no deja modificarlos ni guardarlos. */
static int readonly;

static int dragging_bar;
static int dirty = 1;

/* --- Cadenas sin libc ---------------------------------------------------- */

static int text_len(const char *value) {
    int count = 0;
    while (value && value[count]) count++;
    return count;
}

/* Copia con un bucle simple: sin libc el compilador no puede asumir memcpy. */
static void copy_plain(char *out, int capacity, const char *source) {
    int index = 0;
    while (index + 1 < capacity && source && source[index]) { out[index] = source[index]; index++; }
    out[index] = 0;
}

static void append_to(char *out, int capacity, const char *source) {
    int used = text_len(out);
    for (int index = 0; source && source[index] && used + 1 < capacity; index++)
        out[used++] = source[index];
    out[used] = 0;
}

static void append_int(char *out, int capacity, int value) {
    char digits[12];
    int count = 0;
    if (value < 0) { append_to(out, capacity, "-"); value = -value; }
    do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value);
    while (count > 0) {
        char one[2];
        one[0] = digits[--count];
        one[1] = 0;
        append_to(out, capacity, one);
    }
}

static const char *base_name_of(const char *value) {
    const char *name = value && *value ? value : "";
    for (const char *cursor = name; *cursor; cursor++) if (*cursor == '/') name = cursor + 1;
    return name;
}

/* Marcador por el puerto serie: las pruebas no necesitan capturas. */
static void say(const char *prefix, const char *detail) {
    char line[FS_PATH_MAX + 48];
    copy_plain(line, (int)sizeof(line), prefix);
    append_to(line, (int)sizeof(line), detail);
    append_to(line, (int)sizeof(line), "\n");
    os->write(line);
}

static void set_message(const char *value) {
    copy_plain(message, (int)sizeof(message), value);
}

/* --- Bufer de edicion --------------------------------------------------- */

static int line_of(int position) {
    int line = 0;
    for (int i = 0; i < position && i < length; i++) if (text[i] == '\n') line++;
    return line;
}

static int line_start(int line) {
    int start = 0;
    while (line > 0 && start < length) { if (text[start] == '\n') line--; start++; }
    return start;
}

/* Final de la linea: el '\n' que la cierra o el final del bufer. */
static int line_end(int line) {
    int next = line_start(line + 1);
    return next >= length ? length : next - 1;
}

static int text_lines(void) {
    int lines = 1;
    for (int i = 0; i < length; i++) if (text[i] == '\n') lines++;
    return lines;
}

static void insert_byte(int byte) {
    if (length + 1 > CAPACITY) { set_message("Limite de 1023 bytes alcanzado"); return; }
    for (int i = length; i > cursor; i--) text[i] = text[i - 1];
    text[cursor] = (char)byte;
    length++;
    cursor++;
    text[length] = 0;
}

static void remove_before(void) {
    if (!cursor) return;
    int start = cursor - 1;
    while (start > 0 && ((unsigned char)text[start] & 0xc0) == 0x80) start--;
    for (int i = start; i < length - 1; i++) text[i] = text[i + 1];
    length--;
    cursor = start;
    text[length] = 0;
}

static void remove_at(void) {
    if (cursor >= length) return;
    int end = cursor + 1;
    while (end < length && ((unsigned char)text[end] & 0xc0) == 0x80) end++;
    for (int i = cursor; i + (end - cursor) < length; i++) text[i] = text[i + (end - cursor)];
    length -= end - cursor;
    text[length] = 0;
}

static void move_left(void) {
    if (!cursor) return;
    int start = cursor - 1;
    while (start > 0 && ((unsigned char)text[start] & 0xc0) == 0x80) start--;
    cursor = start;
}

static void move_right(void) {
    if (cursor >= length) return;
    cursor++;
    while (cursor < length && ((unsigned char)text[cursor] & 0xc0) == 0x80) cursor++;
}

static void move_up(void) {
    int line = line_of(cursor);
    if (!line) { cursor = 0; return; }
    int column = cursor - line_start(line);
    int previous = line_start(line - 1);
    int limit = line_end(line - 1);
    cursor = previous + column > limit ? limit : previous + column;
}

static void move_down(void) {
    int line = line_of(cursor), start = line_start(line);
    int next = line_start(line + 1);
    if (next <= start || next > length) { cursor = length; return; }
    int column = cursor - start;
    int limit = line_end(line + 1);
    cursor = next + column > limit ? limit : next + column;
}

/* --- Abrir y guardar ---------------------------------------------------- */

static void new_buffer(void) {
    length = cursor = view = 0;
    follow = 1;
    readonly = 0;
    text[0] = 0;
    path[0] = 0;
    set_message("Plantilla en blanco: escribe y Guardar");
    say("editor.elf: PLANTILLA ", "NUEVA");
}

static int load_file(const char *target) {
    uint32_t size = 0;
    const char *content = os->read_file(target, &size);
    if (!content) {
        set_message("Archivo no encontrado");
        say("editor.elf: SIN ARCHIVO ", target);
        return 0;
    }
    if ((int)size > CAPACITY) size = CAPACITY;
    for (uint32_t i = 0; i < size; i++) text[i] = content[i];
    length = (int)size;
    text[length] = 0;
    cursor = length;
    view = 0;
    follow = 1;
    copy_plain(path, (int)sizeof(path), target);
    readonly = os->is_system(target) ? 1 : 0;
    if (readonly) {
        set_message("Archivo del sistema: solo lectura");
        say("editor.elf: SOLO LECTURA ", target);
    } else {
        set_message("Abierto");
        say("editor.elf: ABIERTO ", target);
    }
    return 1;
}

static int store_file(const char *target) {
    /* El FS tambien lo rechaza; comprobarlo aqui deja un mensaje claro y evita
     * que el editor pierda el texto que el usuario acaba de escribir. */
    if (os->is_system(target)) {
        set_message("Archivo del sistema: no se puede guardar");
        say("editor.elf: SOLO LECTURA ", target);
        return 0;
    }
    if (!os->write_file(target, text)) {
        set_message("No se pudo guardar");
        say("editor.elf: ERROR GUARDADO ", target);
        return 0;
    }
    copy_plain(path, (int)sizeof(path), target);
    readonly = 0;
    set_message("Guardado");
    say("editor.elf: GUARDADO ", target);
    return 1;
}

/* Ruta absoluta del nombre indicado dentro del directorio del selector; las
 * rutas que ya empiezan por '/' se respetan tal cual. */
static void build_path(char *out, int capacity, const char *name) {
    if (name[0] == '/') { copy_plain(out, capacity, name); return; }
    char here[FS_PATH_MAX];
    here[0] = 0;
    os->cwd_path(here, (int)sizeof(here));
    copy_plain(out, capacity, here);
    if (!out[0]) copy_plain(out, capacity, "/");
    if (out[text_len(out) - 1] != '/') append_to(out, capacity, "/");
    append_to(out, capacity, name);
}

/* --- Barra de desplazamiento ------------------------------------------- */

static void draw_bar(host_api_t *api, int x, int y, int width, int height,
                     int total, int visible, int first) {
    if (total <= visible || height <= 0) return;
    api->rect(x, y, width, height, TRACK);
    api->frame(x, y, width, height, BORDER);
    int thumb = height * visible / total;
    if (thumb < 16) thumb = 16;
    if (thumb > height) thumb = height;
    int span = height - thumb;
    int max_first = total - visible;
    int offset = max_first > 0 ? span * first / max_first : 0;
    if (offset < 0) offset = 0;
    if (offset > span) offset = span;
    api->rect(x + 1, y + offset + 1, width - 2, thumb - 2, THUMB);
}

static int bar_first(int y, int height, int total, int visible, int py) {
    int thumb = height * visible / total;
    if (thumb < 16) thumb = 16;
    if (thumb > height) thumb = height;
    int span = height - thumb;
    int max_first = total - visible;
    int offset = py - y - thumb / 2;
    if (offset < 0) offset = 0;
    if (offset > span) offset = span;
    return span > 0 ? max_first * offset / span : 0;
}

static int text_rows(host_api_t *api) {
    int top = TOOLBAR + 4, bottom = api->height() - STATUS;
    int rows = (bottom - top - 8) / LINE_H;
    return rows < 1 ? 1 : rows;
}

static int text_bar_geometry(host_api_t *api, int *bx, int *by, int *bw, int *bh) {
    int rows = text_rows(api);
    if (text_lines() <= rows) return 0;
    *bx = api->width() - SCROLL_W - 6;
    *by = TOOLBAR + 6;
    *bw = SCROLL_W;
    *bh = (api->height() - STATUS) - (TOOLBAR + 6) - 8;
    return *bh > 0;
}

/* --- Selector de archivos: geometria y dibujo --------------------------- */

typedef struct {
    int x, y, w, h;
    int list_y, list_h, rows;
    int name_y;
} dialog_layout_t;

static const char *dialog_primary_label(void) {
    return dialog == DLG_SAVE ? "Guardar" : "Abrir";
}

static void dialog_layout(host_api_t *api, dialog_layout_t *out) {
    int width = api->width(), height = api->height();
    int w = width - 80;
    if (w > 560) w = 560;
    if (w < 280) w = width - 20;
    int h = height - TOOLBAR - STATUS - 28;
    out->w = w;
    out->h = h;
    out->x = (width - w) / 2;
    out->y = TOOLBAR + 14;
    int header = 78;
    int footer = (dialog == DLG_SAVE) ? 76 : 44;
    out->list_y = out->y + header;
    out->list_h = (out->y + h - footer) - out->list_y;
    if (out->list_h < ROW_H) out->list_h = ROW_H;
    out->rows = out->list_h / ROW_H;
    if (out->rows < 1) out->rows = 1;
    out->name_y = out->y + h - footer + 4;
}

static int dialog_button_rect(const dialog_layout_t *layout, int index,
                              int *bx, int *by, int *bw, int *bh) {
    int height = 24, y = layout->y + layout->h - height - 10;
    int first = os->text_width(dialog_primary_label(), 0) + 28;
    int second = os->text_width("Cancelar", 0) + 28;
    if (index == 0) { *bx = layout->x + 12; *by = y; *bw = first; *bh = height; return 1; }
    if (index == 1) { *bx = layout->x + 12 + first + 8; *by = y; *bw = second; *bh = height; return 1; }
    return 0;
}

static void dialog_name_rect(const dialog_layout_t *layout, int *fx, int *fy, int *fw) {
    *fx = layout->x + 12;
    *fy = layout->name_y;
    *fw = layout->w - 24;
}

static void button_draw(host_api_t *api, int x, int y, int width, int height,
                        const char *label, uint32_t fill, uint32_t border, uint32_t ink) {
    api->rect(x, y, width, height, fill);
    api->frame(x, y, width, height, border);
    int text_w = api->text_width(label, 0);
    api->text(x + (width - text_w) / 2, y + (height - SMALL_H) / 2, label, ink, 0);
}

static void draw_fit(host_api_t *api, int x, int y, int max_width,
                     const char *value, uint32_t color) {
    char buffer[FS_PATH_MAX + 8];
    copy_plain(buffer, (int)sizeof(buffer), value);
    int used = text_len(buffer);
    while (used > 0 && api->text_width(buffer, 0) > max_width) buffer[--used] = 0;
    api->text(x, y, buffer, color, 0);
}

/* --- Selector de archivos: navegacion ---------------------------------- */

/* Hijos del directorio actual mas la fila ".." (-1) cuando no estamos en '/'. */
static int list_rows(int *ids, int capacity) {
    const fs_node_t *nodes = os->nodes();
    int cwd = os->cwd(), count = 0;
    if (cwd != 0 && count < capacity) ids[count++] = -1;
    for (int i = 1; i < os->node_count() && count < capacity; i++) {
        if (nodes[i].type == FS_TYPE_FREE) continue;
        if (nodes[i].parent_idx != cwd) continue;
        ids[count++] = i;
    }
    return count;
}

/* Un nodo del selector es interno si el kernel lo marca como del sistema. */
static int node_is_system(int id) {
    if (id < 0) return 0;
    char target[FS_PATH_MAX];
    build_path(target, (int)sizeof(target), os->nodes()[id].name);
    return os->is_system(target);
}

static void dlg_up(void) {
    int cwd = os->cwd();
    if (cwd == 0) return;
    if (!os->chdir(os->nodes()[cwd].parent_idx)) return;
    dlg_sel = 0;
    dlg_scroll = 0;
}

static void dlg_enter_dir(int index) {
    if (!os->chdir(index)) return;
    dlg_sel = 0;
    dlg_scroll = 0;
}

static void dlg_finish(void) {
    if (dialog && dlg_cwd >= 0) os->chdir(dlg_cwd);
    dialog = DLG_NONE;
    dlg_drag = 0;
}

static void dlg_open(int mode) {
    dialog = mode;
    dlg_cwd = os->cwd();
    dlg_sel = 0;
    dlg_scroll = 0;
    dlg_drag = 0;
    dlg_name[0] = 0;
    dlg_name_len = 0;
    if (mode == DLG_SAVE && path[0]) {
        copy_plain(dlg_name, (int)sizeof(dlg_name), base_name_of(path));
        dlg_name_len = text_len(dlg_name);
    }
    set_message(mode == DLG_SAVE ? "Elige carpeta y nombre" : "Elige el archivo");
    say("editor.elf: SELECTOR ", mode == DLG_SAVE ? "GUARDAR" : "ABRIR");
}

static void dlg_cancel(void) {
    dlg_finish();
    set_message("Cancelado");
}

static void dlg_save(void) {
    if (!dlg_name_len) { set_message("Escribe un nombre de archivo"); return; }
    char target[FS_PATH_MAX];
    build_path(target, (int)sizeof(target), dlg_name);
    if (os->is_system(target)) {
        set_message("Archivo del sistema: elige otro nombre");
        return;
    }
    dlg_finish();
    store_file(target);
}

static void dlg_activate(void) {
    int count = list_rows(dlg_ids, FS_MAX_NODES);
    if (!count) return;
    if (dlg_sel >= count) dlg_sel = count - 1;
    int id = dlg_ids[dlg_sel];
    const fs_node_t *nodes = os->nodes();
    if (id < 0) { dlg_up(); return; }
    if (nodes[id].type == FS_TYPE_DIR) { dlg_enter_dir(id); return; }
    char target[FS_PATH_MAX];
    build_path(target, (int)sizeof(target), nodes[id].name);
    dlg_finish();
    load_file(target);
}

static void dialog_key(int key) {
    int count = list_rows(dlg_ids, FS_MAX_NODES);
    if (key == 'q' || key == 'Q') { dlg_cancel(); return; }
    if (key == HOST_KEY_TAB) {
        dialog = (dialog == DLG_SAVE) ? DLG_OPEN : DLG_SAVE;
        set_message(dialog == DLG_SAVE ? "Elige carpeta y nombre" : "Elige el archivo");
        say("editor.elf: SELECTOR ", dialog == DLG_SAVE ? "GUARDAR" : "ABRIR");
        return;
    }
    if (key == HOST_KEY_UP) { if (dlg_sel > 0) dlg_sel--; return; }
    if (key == HOST_KEY_DOWN) { if (dlg_sel + 1 < count) dlg_sel++; return; }
    if (key == HOST_KEY_LEFT) { dlg_up(); return; }
    if (key == HOST_KEY_RIGHT) {
        if (dlg_sel < count) {
            int id = dlg_ids[dlg_sel];
            if (id >= 0 && os->nodes()[id].type == FS_TYPE_DIR) dlg_enter_dir(id);
        }
        return;
    }
    if (key == '\n') {
        if (dialog == DLG_SAVE) dlg_save();
        else dlg_activate();
        return;
    }
    if (dialog != DLG_SAVE) return;
    if (key == '\b') {
        if (dlg_name_len) dlg_name[--dlg_name_len] = 0;
        return;
    }
    /* El nombre no admite espacios ni barras: el FS los rechaza. */
    if (key >= 32 && key < 127 && key != ' ' && key != '/' &&
        dlg_name_len + 1 < (int)sizeof(dlg_name)) {
        dlg_name[dlg_name_len++] = (char)key;
        dlg_name[dlg_name_len] = 0;
    }
}

static void dialog_press(int px, int py) {
    dialog_layout_t layout;
    dialog_layout(os, &layout);
    for (int index = 0; index < 2; index++) {
        int bx, by, bw, bh;
        if (!dialog_button_rect(&layout, index, &bx, &by, &bw, &bh)) continue;
        if (px < bx || px > bx + bw || py < by || py > by + bh) continue;
        if (index == 1) { dlg_cancel(); return; }
        if (dialog == DLG_SAVE) dlg_save();
        else dlg_activate();
        return;
    }
    int count = list_rows(dlg_ids, FS_MAX_NODES);
    if (count > layout.rows) {
        int bx = layout.x + layout.w - SCROLL_W - 8, by = layout.list_y, bw = SCROLL_W;
        int bh = layout.list_h;
        if (px >= bx && px <= bx + bw && py >= by && py <= by + bh) {
            dlg_scroll = bar_first(by, bh, count, layout.rows, py);
            dlg_drag = 1;
            return;
        }
    }
    if (py < layout.list_y || py >= layout.list_y + layout.list_h) return;
    int row = (py - layout.list_y) / ROW_H;
    int position = dlg_scroll + row;
    if (row < 0 || position >= count) return;
    dlg_sel = position;
    int id = dlg_ids[position];
    if (id < 0) { dlg_up(); return; }
    if (os->nodes()[id].type == FS_TYPE_DIR) { dlg_enter_dir(id); return; }
}

static void dialog_drag_to(int py) {
    if (!dlg_drag) return;
    dialog_layout_t layout;
    dialog_layout(os, &layout);
    int count = list_rows(dlg_ids, FS_MAX_NODES);
    if (count <= layout.rows) return;
    dlg_scroll = bar_first(layout.list_y, layout.list_h, count, layout.rows, py);
}

static void dialog_draw(host_api_t *api) {
    dialog_layout_t layout;
    dialog_layout(api, &layout);
    api->rect(layout.x + 3, layout.y + 3, layout.w, layout.h, SHADE);
    api->rect(layout.x, layout.y, layout.w, layout.h, PANEL);
    api->frame(layout.x, layout.y, layout.w, layout.h, BORDER);
    api->text(layout.x + 12, layout.y + 8,
              dialog == DLG_SAVE ? "Guardar como" : "Abrir archivo", TEXT, 1);
    char here[FS_PATH_MAX];
    here[0] = 0;
    api->cwd_path(here, (int)sizeof(here));
    draw_fit(api, layout.x + 12, layout.y + 34, layout.w - 24, here, DIM);
    api->text(layout.x + 12, layout.y + 52,
              dialog == DLG_SAVE ? "Escribe el nombre  Enter guarda  Tab abrir  Q cancelar"
                                 : "Enter abre  Flechas navegan  Tab guardar  Q cancelar", DIM, 0);
    api->rect(layout.x + 12, layout.y + 64, layout.w - 24, 1, BORDER);

    int count = list_rows(dlg_ids, FS_MAX_NODES);
    const fs_node_t *nodes = api->nodes();
    if (dlg_sel >= count) dlg_sel = count ? count - 1 : 0;
    if (dlg_sel < 0) dlg_sel = 0;
    if (dlg_sel < dlg_scroll) dlg_scroll = dlg_sel;
    if (dlg_sel >= dlg_scroll + layout.rows) dlg_scroll = dlg_sel - layout.rows + 1;
    if (dlg_scroll < 0) dlg_scroll = 0;
    for (int row = 0; row < layout.rows; row++) {
        int position = dlg_scroll + row;
        if (position >= count) break;
        int id = dlg_ids[position];
        int top = layout.list_y + row * ROW_H;
        int is_dir = id < 0 || nodes[id].type == FS_TYPE_DIR;
        int internal = !is_dir && node_is_system(id);
        if (position == dlg_sel) api->rect(layout.x + 8, top - 2, layout.w - 16, ROW_H, SOFT);
        api->icon(is_dir ? HOST_ICON_FOLDER : HOST_ICON_TERMINAL, layout.x + 14, top + 3, 12,
                  is_dir ? ACCENT : DIM);
        api->text(layout.x + 32, top + 1, id < 0 ? ".." : nodes[id].name,
                  is_dir ? ACCENT : (internal ? DIM : TEXT), 0);
        /* Los ficheros internos se pueden abrir, pero no sobrescribir. */
        if (internal) {
            const char *tag = "sistema";
            api->text(layout.x + layout.w - 20 - api->text_width(tag, 0), top + 1, tag, DIM, 0);
        }
    }
    if (count > layout.rows)
        draw_bar(api, layout.x + layout.w - SCROLL_W - 8, layout.list_y, SCROLL_W,
                 layout.list_h, count, layout.rows, dlg_scroll);

    if (dialog == DLG_SAVE) {
        int fx, fy, fw;
        dialog_name_rect(&layout, &fx, &fy, &fw);
        api->text(fx, fy + 3, "Nombre", DIM, 0);
        int label = api->text_width("Nombre", 0) + 10;
        api->rect(fx + label, fy, fw - label, 24, BG);
        api->frame(fx + label, fy, fw - label, 24, ACCENT);
        api->text(fx + label + 6, fy + 3, dlg_name, TEXT, 0);
        api->rect(fx + label + 6 + api->text_width(dlg_name, 0), fy + 4, 2, 16, ACCENT);
    }

    for (int index = 0; index < 2; index++) {
        int bx, by, bw, bh;
        if (!dialog_button_rect(&layout, index, &bx, &by, &bw, &bh)) continue;
        int primary = index == 0;
        button_draw(api, bx, by, bw, bh, primary ? dialog_primary_label() : "Cancelar",
                    primary ? ACCENT : BG, primary ? ACCENT : BORDER,
                    primary ? WHITE : TEXT);
    }
}

/* --- Editor: dibujo ----------------------------------------------------- */

static int toolbar_button(int index, int *bx, int *by, int *bw, int *bh) {
    static const char *labels[BTN_COUNT] = { "Nuevo", "Abrir", "Guardar" };
    int x = EDGE, y = 8, height = 22;
    for (int i = 0; i < BTN_COUNT; i++) {
        int width = os->text_width(labels[i], 0) + 22;
        if (i == index) { *bx = x; *by = y; *bw = width; *bh = height; return 1; }
        x += width + 6;
    }
    return 0;
}

static const char *toolbar_label(int index) {
    static const char *labels[BTN_COUNT] = { "Nuevo", "Abrir", "Guardar" };
    return (index >= 0 && index < BTN_COUNT) ? labels[index] : "";
}

static void draw_editor(host_api_t *api) {
    int width = api->width(), height = api->height();
    api->clear(BG);

    /* Barra de herramientas: botones a la izquierda y archivo a la derecha. */
    api->rect(0, 0, width, TOOLBAR, PANEL);
    api->rect(0, TOOLBAR - 1, width, 1, BORDER);
    for (int index = 0; index < BTN_COUNT; index++) {
        int bx, by, bw, bh;
        if (!toolbar_button(index, &bx, &by, &bw, &bh)) continue;
        int primary = index == BTN_SAVE;
        button_draw(api, bx, by, bw, bh, toolbar_label(index),
                    primary ? ACCENT : BG, primary ? ACCENT : BORDER,
                    primary ? WHITE : TEXT);
    }
    int bx, by, bw, bh;
    int name_x = EDGE;
    if (toolbar_button(BTN_COUNT - 1, &bx, &by, &bw, &bh)) name_x = bx + bw + 14;
    char label[FS_PATH_MAX + 20];
    copy_plain(label, (int)sizeof(label), path[0] ? path : "(sin nombre: elige Guardar)");
    if (readonly) append_to(label, (int)sizeof(label), "   [solo lectura]");
    draw_fit(api, name_x, 11, width - name_x - EDGE, label,
             readonly ? DIM : (path[0] ? TEXT : DIM));

    /* Area de texto con la barra vertical cuando el contenido no cabe. */
    int rows = text_rows(api);
    int total = text_lines();
    if (follow) {
        int line = line_of(cursor);
        if (line < view) view = line;
        if (line >= view + rows) view = line - rows + 1;
    }
    int max_view = total - rows;
    if (max_view < 0) max_view = 0;
    if (view > max_view) view = max_view;
    if (view < 0) view = 0;
    int top = TOOLBAR + 4;
    for (int row = 0; row < rows; row++) {
        int line = view + row;
        if (line >= total) break;
        int start = line_start(line);
        char buffer[LINE_MAX];
        int used = 0;
        for (int i = start; i < length && text[i] != '\n' && used < LINE_MAX - 1; i++)
            buffer[used++] = text[i];
        buffer[used] = 0;
        api->text(EDGE + 4, top + row * LINE_H, buffer, TEXT, 0);
    }
    if (text_bar_geometry(api, &bx, &by, &bw, &bh))
        draw_bar(api, bx, by, bw, bh, total, rows, view);

    /* Cursor: barra fina a continuacion del texto de su linea. */
    int cursor_line = line_of(cursor);
    if (cursor_line >= view && cursor_line < view + rows) {
        int start = line_start(cursor_line);
        char prefix[LINE_MAX];
        int used = 0;
        for (int i = start; i < cursor && used < LINE_MAX - 1; i++) prefix[used++] = text[i];
        prefix[used] = 0;
        api->rect(EDGE + 4 + api->text_width(prefix, 0),
                  top + (cursor_line - view) * LINE_H + 2, 2, LINE_H - 5, ACCENT);
    }

    /* Barra de estado: posicion del cursor y ultimo mensaje. */
    int status_y = height - STATUS;
    api->rect(0, status_y, width, STATUS, PANEL);
    api->rect(0, status_y, width, 1, BORDER);
    char status[64];
    status[0] = 0;
    append_to(status, (int)sizeof(status), "Linea ");
    append_int(status, (int)sizeof(status), cursor_line + 1);
    append_to(status, (int)sizeof(status), "  Col ");
    append_int(status, (int)sizeof(status), cursor - line_start(cursor_line) + 1);
    append_to(status, (int)sizeof(status), "  ");
    append_int(status, (int)sizeof(status), length);
    append_to(status, (int)sizeof(status), " / 1023 B");
    api->text(EDGE, status_y + 4, status, DIM, 0);
    int room = width - EDGE * 2 - api->text_width(status, 0) - 16;
    if (room > 40) {
        char tail[72];
        copy_plain(tail, (int)sizeof(tail), message);
        int used = text_len(tail);
        while (used > 0 && api->text_width(tail, 0) > room) tail[--used] = 0;
        api->text(width - EDGE - api->text_width(tail, 0), status_y + 4, tail,
                  path[0] ? DIM : ACCENT, 0);
    }
}

/* --- Editor: raton y teclado ------------------------------------------- */

static void editor_press(int px, int py) {
    for (int index = 0; index < BTN_COUNT; index++) {
        int bx, by, bw, bh;
        if (!toolbar_button(index, &bx, &by, &bw, &bh)) continue;
        if (px < bx || px > bx + bw || py < by || py > by + bh) continue;
        if (index == BTN_NEW) { new_buffer(); return; }
        if (index == BTN_OPEN) { dlg_open(DLG_OPEN); return; }
        if (!path[0]) { dlg_open(DLG_SAVE); return; }
        store_file(path);
        return;
    }
    int bx, by, bw, bh;
    if (!text_bar_geometry(os, &bx, &by, &bw, &bh)) return;
    if (px < bx || px > bx + bw || py < by || py > by + bh) return;
    int rows = text_rows(os);
    view = bar_first(by, bh, text_lines(), rows, py);
    follow = 0;
    dragging_bar = 1;
}

static void text_drag_to(int py) {
    int bx, by, bw, bh;
    if (!text_bar_geometry(os, &bx, &by, &bw, &bh)) return;
    int rows = text_rows(os);
    view = bar_first(by, bh, text_lines(), rows, py);
    follow = 0;
}

static void editor_key(int key) {
    follow = 1;
    dirty = 1;
    /* En un archivo del sistema se permite navegar, abrir el selector y cambiar
     * de archivo, pero no alterar el contenido. */
    int editing = key == '\n' || key == '\b' || key == HOST_KEY_DELETE || (key >= 32 && key < 127);
    if (readonly && editing) {
        set_message("Archivo del sistema: solo lectura");
        return;
    }
    if (key == '\n') insert_byte('\n');
    else if (key == '\b') remove_before();
    else if (key == HOST_KEY_DELETE) remove_at();
    else if (key == HOST_KEY_LEFT) move_left();
    else if (key == HOST_KEY_RIGHT) move_right();
    else if (key == HOST_KEY_UP) move_up();
    else if (key == HOST_KEY_DOWN) move_down();
    else if (key == HOST_KEY_HOME) cursor = line_start(line_of(cursor));
    else if (key == HOST_KEY_END) {
        int line = line_of(cursor);
        int next = line_start(line + 1);
        cursor = (next > 0 && next <= length) ? next - 1 : length;
    }
    else if (key == HOST_KEY_TAB) { dlg_open(DLG_OPEN); return; }
    else if (key >= 32 && key < 127) insert_byte(key);
}

/* --- Arranque ----------------------------------------------------------- */

int main(host_api_t *api) {
    if (!api || api->abi_version != HOST_ABI_VERSION) {
        if (api) api->write("editor.elf: ABI incompatible\n");
        return 1;
    }
    os = api;
    if (!api->window("Editor de texto", 760, 500)) {
        api->write("editor.elf: no se pudo abrir la ventana\n");
        return 2;
    }
    new_buffer();
    dialog = DLG_NONE;
    dragging_bar = 0;

    /* Handoff del escritorio: Archivos > Editar o GUI EDIT <archivo>. */
    char initial[FS_PATH_MAX];
    initial[0] = 0;
    copy_plain(initial, (int)sizeof(initial), api->launch_file());
    if (initial[0]) {
        char absolute[FS_PATH_MAX];
        build_path(absolute, (int)sizeof(absolute), initial);
        say("editor.elf: HANDOFF ", absolute);
        load_file(absolute);
    }

    host_event_t event;
    while (1) {
        if (!api->pump(&event)) break;
        if (event.type == HOST_EV_DOWN) {
            dirty = 1;
            if (dialog) dialog_press(event.x, event.y);
            else editor_press(event.x, event.y);
        } else if (event.type == HOST_EV_UP) {
            dragging_bar = 0;
            dlg_drag = 0;
        } else if (event.type == HOST_EV_MOVE) {
            if (dragging_bar) text_drag_to(event.y);
            else if (dlg_drag) dialog_drag_to(event.y);
            else dirty = 1;
        } else if (event.type == HOST_EV_KEY) {
            dirty = 1;
            if (dialog) dialog_key((int)event.key);
            else editor_key((int)event.key);
        } else if (event.type == HOST_EV_CLOSE) {
            break;
        }
        if (dirty) {
            draw_editor(api);
            if (dialog) dialog_draw(api);
            api->present();
            dirty = 0;
        }
    }
    api->write("editor.elf: fin correcto\n");
    return api->finish(0);
}
