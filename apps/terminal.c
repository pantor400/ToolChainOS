#include "host_api.h"

/* Terminal grafica de la interfaz. Ejecuta ordenes del shell con host_api v3
 * (api->exec captura la salida por serie) y guarda un historial navegable con
 * las flechas. No usa libc. */

#define BG     0xf4f7fc
#define PANEL  0xffffff
#define TEXT   0x14213a
#define DIM    0x5a6b85
#define ACCENT 0x2470e8
#define BORDER 0xc9d5e6
#define SCREEN 0x0e1622
#define CODE   0xd8e6f7
#define PROMPT 0x74d0ff
#define OKC    0x2fbf71
#define ERR    0xd92b2b

#define MAX_LINES  400
#define LINE_COLS  82
#define LINE_HEIGHT 18
#define INPUT_MAX  96
#define HISTORY     8
#define RESULT_MAX 640
#define TOP_Y      36
#define BOTTOM     60

static char lines[MAX_LINES][LINE_COLS + 1];
static int  lengths[MAX_LINES];
static int  total = 1, start_line = 0;

static char input[INPUT_MAX];
static int  input_len;

static char history[HISTORY][INPUT_MAX];
static int  history_count, history_used, history_pos;

static int same(const char *left, const char *right) {
    int i = 0;
    while (left[i] && right[i]) { if (left[i] != right[i]) return 0; i++; }
    return left[i] == right[i];
}

static void open_line(void) {
    int slot = total % MAX_LINES;
    lengths[slot] = 0;
    lines[slot][0] = 0;
    total++;
    if (total - start_line > MAX_LINES) start_line = total - MAX_LINES;
}

static void put_char(char value) {
    int slot = (total - 1) % MAX_LINES;
    if (lengths[slot] >= LINE_COLS) {
        open_line();
        slot = (total - 1) % MAX_LINES;
    }
    lines[slot][lengths[slot]++] = value;
    lines[slot][lengths[slot]] = 0;
}

static void append_text(const char *text) {
    for (int i = 0; text && text[i]; i++) {
        unsigned char value = (unsigned char)text[i];
        if (value == '\r') continue;
        if (value == '\n') { open_line(); continue; }
        if (value == '\t') { for (int k = 0; k < 4; k++) put_char(' '); continue; }
        if (value < 32) continue;
        put_char((char)value);
    }
}

static void clear_screen(void) {
    total = 1;
    start_line = 0;
    open_line();
}

static char *line_at(int index) { return lines[index % MAX_LINES]; }

static void load_history(int pos) {
    input_len = 0;
    if (pos < history_count) {
        const char *source = history[pos % HISTORY];
        while (source[input_len] && input_len + 1 < INPUT_MAX) {
            input[input_len] = source[input_len];
            input_len++;
        }
    }
    input[input_len] = 0;
}

static void save_history(void) {
    if (!input_len) return;
    int slot = history_used % HISTORY;
    for (int i = 0; i < input_len && i + 1 < INPUT_MAX; i++) history[slot][i] = input[i];
    history[slot][input_len] = 0;
    history_used++;
    if (history_count < HISTORY) history_count++;
    history_pos = history_count;
}

static void execute(host_api_t *api, int *closing) {
    input[input_len] = 0;
    append_text("OS> ");
    append_text(input);
    append_text("\n");
    int handled = 0;
    if (!input_len) handled = 1;
    else if (same(input, "limpiar") || same(input, "clear") || same(input, "cls")) {
        clear_screen();
        handled = 1;
    } else if (same(input, "salir") || same(input, "exit") || same(input, "quit")) {
        api->close();
        *closing = 1;
        handled = 1;
    }
    if (!handled) {
        static char result[RESULT_MAX];
        result[0] = 0;
        int status = api->exec(input, result, (int)sizeof(result));
        if (status < 0) append_text("Orden no permitida desde una aplicacion.\n");
        else if (!result[0]) append_text("(sin salida)\n");
        else append_text(result);
    }
    save_history();
    input_len = 0;
    input[0] = 0;
}

static void draw(host_api_t *api) {
    int width = api->width(), height = api->height();
    api->clear(BG);
    api->text(16, 8, "Terminal", TEXT, 1);
    char path[64];
    path[0] = 0;
    api->cwd_path(path, (int)sizeof(path));
    if (path[0]) api->text(16 + api->text_width("Terminal", 1) + 20, 12, path, DIM, 0);

    int area_top = TOP_Y;
    int input_y = height - BOTTOM;
    int area_height = input_y - area_top - 10;
    if (area_height < LINE_HEIGHT) area_height = LINE_HEIGHT;
    api->rect(16, area_top, width - 32, area_height, SCREEN);
    api->frame(16, area_top, width - 32, area_height, 0x22344f);

    int rows = (area_height - 14) / LINE_HEIGHT;
    if (rows < 1) rows = 1;
    int last = total - 1;
    int first = last - rows + 1;
    if (first < start_line) first = start_line;
    if (first < 0) first = 0;
    int y = area_top + 8;
    for (int index = first; index <= last; index++) {
        api->text(26, y, line_at(index), CODE, 0);
        y += LINE_HEIGHT;
    }

    /* Fila de entrada con el comando en curso. */
    api->rect(16, input_y, width - 32, 26, PANEL);
    api->frame(16, input_y, width - 32, 26, BORDER);
    api->text(26, input_y + 6, "OS>", PROMPT, 0);
    int pen = 26 + api->text_width("OS> ", 0);
    input[input_len] = 0;
    api->text(pen, input_y + 6, input, TEXT, 0);
    api->rect(pen + api->text_width(input, 0), input_y + 6, 2, 13, ACCENT);

    api->text(18, height - 24,
              "Enter ejecuta  flechas historial  limpiar  salir  ESC cierra", DIM, 0);
}

int main(host_api_t *api) {
    if (!api || api->abi_version != HOST_ABI_VERSION) {
        if (api) api->write("terminal.elf: ABI incompatible\n");
        return 1;
    }
    if (!api->window("Terminal", 720, 430)) {
        api->write("terminal.elf: no se pudo abrir la ventana\n");
        return 2;
    }
    total = 1;
    start_line = 0;
    open_line();
    input_len = 0;
    input[0] = 0;
    history_count = history_used = history_pos = 0;
    append_text("Terminal del sistema. Escribe help para ver las ordenes.\n");
    append_text("limpiar vacia la pantalla y salir cierra la ventana.\n");

    int closing = 0;
    int dirty = 1;
    host_event_t event;
    while (!closing) {
        if (!api->pump(&event)) break;
        if (event.type == HOST_EV_KEY) {
            int key = (int)event.key;
            dirty = 1;
            if (key == 27) break;
            else if (key == '\n') execute(api, &closing);
            else if (key == '\b') { if (input_len) input[--input_len] = 0; }
            else if (key == HOST_KEY_UP) {
                if (history_pos > 0) history_pos--;
                load_history(history_pos);
            } else if (key == HOST_KEY_DOWN) {
                if (history_pos < history_count) history_pos++;
                load_history(history_pos);
            } else if (key == HOST_KEY_LEFT || key == HOST_KEY_RIGHT ||
                       key == HOST_KEY_HOME || key == HOST_KEY_END ||
                       key == HOST_KEY_DELETE || key == HOST_KEY_TAB) {
                /* Sin cursor movible: se ignoran para no corromper la linea. */
            } else if (key >= 32 && key < 127 && input_len + 1 < INPUT_MAX) {
                input[input_len++] = (char)key;
                input[input_len] = 0;
            }
        } else if (event.type != HOST_EV_NONE) dirty = 1;
        if (dirty && !closing) {
            draw(api);
            api->present();
            dirty = 0;
        }
    }
    api->write("terminal.elf: fin correcto\n");
    return api->finish(0);
}
