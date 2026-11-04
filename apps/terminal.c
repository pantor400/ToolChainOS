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

#define MAX_LINES  300
/* Tope de bytes por linea. El corte real no es por columnas: se mide el ancho
 * con la fuente de la interfaz para que el texto use todo el area visible. */
#define LINE_COLS  160
#define LINE_HEIGHT 18
#define INPUT_MAX  96
#define HISTORY     8
/* Tope de la salida de una orden. HELP ocupa ~2.6 KiB: con un buffer menor la
 * ultima linea se cortaba a media palabra y el texto parecia perdido. */
#define RESULT_MAX 4096
#define TOP_Y      36
#define BOTTOM     60
#define SCROLL_W   10
#define TRACK      0x1b2a40
#define THUMB      0x74d0ff

static char lines[MAX_LINES][LINE_COLS + 1];
static int  lengths[MAX_LINES];
static int  widths[MAX_LINES];          /* ancho en pixeles de cada linea */
static int  total = 1, start_line = 0;
/* Lineas desplazadas hacia arriba desde el final; 0 sigue la salida nueva. */
static int  scroll_back, dragging;
/* Ancho util del area de texto y API para medir glifos. */
static host_api_t *host;
static int  wrap_width;

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
    widths[slot] = 0;
    lines[slot][0] = 0;
    total++;
    if (total - start_line > MAX_LINES) start_line = total - MAX_LINES;
}

/* Ancho en pixeles de la linea en construccion. */
static int line_width(int slot) {
    return host ? host->text_width(lines[slot], 0) : widths[slot];
}

static void put_bytes(const char *bytes, int count) {
    if (count <= 0) return;
    int advance = 0;
    if (host) {
        char piece[5];
        for (int i = 0; i < count && i < 4; i++) piece[i] = bytes[i];
        piece[count] = 0;
        advance = host->text_width(piece, 0);
    }
    int slot = (total - 1) % MAX_LINES;
    /* Sin sitio para la palabra en curso: se intenta bajar la ultima palabra
     * completa a la linea siguiente; si la linea es una sola palabra, se parte. */
    if (lengths[slot] && widths[slot] + advance > wrap_width) {
        int cut = -1;
        for (int i = lengths[slot] - 1; i > 0; i--) if (lines[slot][i] == ' ') { cut = i; break; }
        if (cut > 0) {
            char tail[LINE_COLS + 1];
            int used = 0;
            for (int i = cut + 1; i < lengths[slot]; i++) tail[used++] = lines[slot][i];
            tail[used] = 0;
            lengths[slot] = cut;
            lines[slot][cut] = 0;
            widths[slot] = line_width(slot);
            open_line();
            slot = (total - 1) % MAX_LINES;
            for (int i = 0; i < used; i++) lines[slot][i] = tail[i];
            lengths[slot] = used;
            lines[slot][used] = 0;
            widths[slot] = line_width(slot);
        } else {
            open_line();
            slot = (total - 1) % MAX_LINES;
        }
    }
    /* Tope de seguridad para cadenas sin espacios o medidas desproporcionadas. */
    if (lengths[slot] + count > LINE_COLS) {
        open_line();
        slot = (total - 1) % MAX_LINES;
    }
    for (int i = 0; i < count; i++) lines[slot][lengths[slot]++] = bytes[i];
    lines[slot][lengths[slot]] = 0;
    widths[slot] += advance;
}

static void put_char(char value) { put_bytes(&value, 1); }

/* Recorre la salida UTF-8 y entrega cada caracter completo: el ajuste de linea
 * se hace por pixeles, no partiendo secuencias multibyte. */
static void append_text(const char *text) {
    for (int i = 0; text && text[i]; ) {
        unsigned char value = (unsigned char)text[i];
        if (value == '\r') { i++; continue; }
        if (value == '\n') { open_line(); i++; continue; }
        if (value == '\t') { for (int k = 0; k < 4; k++) put_char(' '); i++; continue; }
        if (value < 32) { i++; continue; }
        int count = 1;
        if ((value & 0xe0) == 0xc0) count = 2;
        else if ((value & 0xf0) == 0xe0) count = 3;
        else if ((value & 0xf8) == 0xf0) count = 4;
        for (int k = 1; k < count; k++)
            if (!text[i + k] || ((unsigned char)text[i + k] & 0xc0) != 0x80) { count = 1; break; }
        put_bytes(text + i, count);
        i += count;
    }
}

static void clear_screen(void) {
    total = 1;
    start_line = 0;
    open_line();
}

static char *line_at(int index) { return lines[index % MAX_LINES]; }

/* El eco de la orden empieza siempre en una linea nueva: si la salida anterior
 * no termino con salto de linea, el comando no se pega a su ultima palabra. */
static void start_output_line(void) {
    if (lengths[(total - 1) % MAX_LINES]) open_line();
}

/* --- Area de texto y barra de desplazamiento --------------------------- */

static void screen_geometry(host_api_t *api, int *sx, int *sy, int *sw, int *sh, int *rows) {
    int area_top = TOP_Y;
    int input_y = api->height() - BOTTOM;
    int area_height = input_y - area_top - 10;
    if (area_height < LINE_HEIGHT) area_height = LINE_HEIGHT;
    *sx = 16;
    *sy = area_top;
    *sw = api->width() - 32;
    *sh = area_height;
    int visible = (area_height - 14) / LINE_HEIGHT;
    *rows = visible < 1 ? 1 : visible;
}

/* Ancho disponible para el texto: area menos el margen izquierdo y la pista de
 * la barra, para que nada quede tapado ni se salga del recuadro. */
static void update_wrap(host_api_t *api) {
    int sx, sy, sw, sh, rows;
    screen_geometry(api, &sx, &sy, &sw, &sh, &rows);
    int available = sw - 10 - (SCROLL_W + 8);
    wrap_width = available > 40 ? available : 40;
}

/* Barra dentro del marco de la pantalla, a la derecha del texto. */
static void scroll_geometry(host_api_t *api, int *bx, int *by, int *bw, int *bh, int *rows) {
    int sx, sy, sw, sh;
    screen_geometry(api, &sx, &sy, &sw, &sh, rows);
    *bx = sx + sw - SCROLL_W - 5;
    *by = sy + 6;
    *bw = SCROLL_W;
    *bh = sh - 12;
}

static void draw_scroll(host_api_t *api, int x, int y, int width, int height,
                        int total_lines, int visible, int first) {
    api->rect(x, y, width, height, TRACK);
    api->frame(x, y, width, height, 0x33506f);
    int thumb = height * visible / total_lines;
    if (thumb < 16) thumb = 16;
    if (thumb > height) thumb = height;
    int span = height - thumb;
    int max_first = total_lines - visible;
    int offset = max_first > 0 ? span * first / max_first : 0;
    if (offset < 0) offset = 0;
    if (offset > span) offset = span;
    api->rect(x + 1, y + offset + 1, width - 2, thumb - 2, THUMB);
}

/* Primera linea visible que corresponde a un clic o arrastre en la pista. */
static int scroll_first(int y, int height, int total_lines, int visible, int py) {
    int thumb = height * visible / total_lines;
    if (thumb < 16) thumb = 16;
    if (thumb > height) thumb = height;
    int span = height - thumb;
    int max_first = total_lines - visible;
    int offset = py - y - thumb / 2;
    if (offset < 0) offset = 0;
    if (offset > span) offset = span;
    return span > 0 ? max_first * offset / span : 0;
}

/* Coloca el texto segun el punto pulsado en la barra. */
static void scroll_to(host_api_t *api, int py) {
    int bx, by, bw, bh, rows;
    scroll_geometry(api, &bx, &by, &bw, &bh, &rows);
    int visible_total = total - start_line;
    if (visible_total <= rows) return;
    int value = scroll_first(by, bh, visible_total, rows, py);
    scroll_back = visible_total - rows - value;
    if (scroll_back < 0) scroll_back = 0;
}

/* 1 si el puntero cae sobre la barra y esta tiene recorrido. */
static int scroll_hit(host_api_t *api, int px, int py) {
    int bx, by, bw, bh, rows;
    scroll_geometry(api, &bx, &by, &bw, &bh, &rows);
    if (px < bx || px > bx + bw || py < by || py > by + bh) return 0;
    return total - start_line > rows;
}

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
    update_wrap(api);
    start_output_line();
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
    scroll_back = 0;                 /* la salida nueva vuelve al final */
}

/* Dibuja el texto sin salir de su recuadro: si no cabe en max_width se corta
 * por caracteres completos, nunca a mitad de una secuencia UTF-8. El caso
 * normal (cabe entero) se mide de una vez y se pinta tal cual. */
static void draw_clipped(host_api_t *api, int x, int y, int max_width,
                         const char *text, uint32_t color) {
    if (max_width <= 0 || !text) return;
    if (api->text_width(text, 0) <= max_width) {
        api->text(x, y, text, color, 0);
        return;
    }
    char buffer[LINE_COLS + 1];
    int used = 0, width = 0;
    for (int i = 0; text[i]; ) {
        int count = 1;
        unsigned char value = (unsigned char)text[i];
        if ((value & 0xe0) == 0xc0) count = 2;
        else if ((value & 0xf0) == 0xe0) count = 3;
        else if ((value & 0xf8) == 0xf0) count = 4;
        if (used + count + 1 > (int)sizeof(buffer)) break;
        char piece[5];
        for (int k = 0; k < count; k++) {
            if (!text[i + k]) { count = k; break; }
            piece[k] = text[i + k];
        }
        piece[count] = 0;
        int advance = api->text_width(piece, 0);
        if (width + advance > max_width) break;
        for (int k = 0; k < count; k++) buffer[used++] = piece[k];
        width += advance;
        i += count;
    }
    buffer[used] = 0;
    api->text(x, y, buffer, color, 0);
}

static void draw(host_api_t *api) {
    int width = api->width(), height = api->height();
    update_wrap(api);
    api->clear(BG);
    api->text(16, 8, "Terminal", TEXT, 1);
    char path[64];
    path[0] = 0;
    api->cwd_path(path, (int)sizeof(path));
    int path_x = 16 + api->text_width("Terminal", 1) + 20;
    if (path[0]) draw_clipped(api, path_x, 12, width - path_x - 8, path, DIM);

    int input_y = height - BOTTOM;
    int sx, sy, sw, sh, rows;
    screen_geometry(api, &sx, &sy, &sw, &sh, &rows);
    api->rect(sx, sy, sw, sh, SCREEN);
    api->frame(sx, sy, sw, sh, 0x22344f);

    int last = total - 1;
    int visible_total = last - start_line + 1;
    int max_back = visible_total - rows;
    if (max_back < 0) max_back = 0;
    if (scroll_back > max_back) scroll_back = max_back;
    if (scroll_back < 0) scroll_back = 0;
    int first = last - rows + 1 - scroll_back;
    if (first < start_line) first = start_line;
    int y = sy + 8;
    for (int index = first, drawn = 0; index <= last && drawn < rows; index++, drawn++) {
        /* El ancho util deja libre la pista de la barra: una linea que no
         * quepa se recorta aqui en vez de invadir el marco de la ventana. */
        draw_clipped(api, sx + 10, y, wrap_width, line_at(index), CODE);
        y += LINE_HEIGHT;
    }
    /* Barra vertical solo cuando el texto no cabe en la pantalla. */
    if (visible_total > rows) {
        int bx, by, bw, bh, bar_rows;
        scroll_geometry(api, &bx, &by, &bw, &bh, &bar_rows);
        draw_scroll(api, bx, by, bw, bh, visible_total, rows, first - start_line);
    }

    /* Fila de entrada con el comando en curso. */
    api->rect(16, input_y, width - 32, 26, PANEL);
    api->frame(16, input_y, width - 32, 26, BORDER);
    api->text(26, input_y + 6, "OS>", PROMPT, 0);
    int pen = 26 + api->text_width("OS> ", 0);
    input[input_len] = 0;
    /* Si el comando no cabe, se muestran sus ultimos caracteres: el cursor
     * sigue visible y no se pinta fuera del campo de entrada. */
    int room = width - 32 - pen - 12;
    const char *shown = input;
    while (*shown && api->text_width(shown, 0) > room) shown++;
    api->text(pen, input_y + 6, shown, TEXT, 0);
    api->rect(pen + api->text_width(shown, 0), input_y + 6, 2, 13, ACCENT);

    draw_clipped(api, 18, height - 24, width - 26,
                 "Enter ejecuta  flechas historial  barra derecha desplaza  ESC cierra", DIM);
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
    host = api;
    update_wrap(api);
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
            } else if (key == HOST_KEY_HOME) {
                scroll_back = MAX_LINES;      /* el dibujo lo ajusta al inicio */
            } else if (key == HOST_KEY_END) {
                scroll_back = 0;
            } else if (key == HOST_KEY_LEFT || key == HOST_KEY_RIGHT ||
                       key == HOST_KEY_DELETE || key == HOST_KEY_TAB) {
                /* Sin cursor movible: se ignoran para no corromper la linea. */
            } else if (key >= 32 && key < 127 && input_len + 1 < INPUT_MAX) {
                input[input_len++] = (char)key;
                input[input_len] = 0;
            }
        } else if (event.type == HOST_EV_DOWN) {
            if (scroll_hit(api, event.x, event.y)) {
                scroll_to(api, event.y);
                dragging = 1;
            }
            dirty = 1;
        } else if (event.type == HOST_EV_UP) {
            dragging = 0;
        } else if (event.type == HOST_EV_MOVE) {
            if (dragging) scroll_to(api, event.y);
            dirty = 1;
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
