#include "host_api.h"
#include "fs.h"

/* Hoja de calculo de la interfaz grafica (ABI v5).
 *
 * Rejilla de 8 columnas (A-H) por 16 filas. Cada celda guarda texto o una
 * formula que empieza por '=': numeros, referencias (A1), sumas y restas,
 * productos y divisiones, parentesis y las funciones SUMA, PROMEDIO, MIN, MAX
 * y CONTAR sobre una celda o un rango (A1:B3). Los valores son enteros: no
 * hay coma flotante ni libc dentro de la aplicacion.
 *
 * Teclado: las flechas mueven la celda activa, RET baja de fila, TAB avanza de
 * columna, BACKSPACE borra el ultimo caracter y SUPR vacia la celda. HOME y
 * END llevan a la primera y a la ultima columna. El raton elige celda y pulsa
 * los botones Guardar, Abrir y Limpiar.
 *
 * Persistencia: la hoja se guarda como CSV en hoja.csv del directorio actual,
 * de modo que la terminal y el resto de la shell pueden leerla y editarla. */

#define ROWS 16
#define COLS 8
#define CELL_MAX 24
#define TOOLBAR_H 40
#define FORMULA_H 26
#define HEAD_H 20
#define ROW_H 24
#define STATUS_H 44
#define HEAD_X 38
#define EDGE 8
#define BTN_W 104
#define BTN_H 26
#define BTN_Y 7
#define CSV_NAME "hoja.csv"

#define BG     0xf4f7fc
#define PANEL  0xffffff
#define TEXT   0x14213a
#define DIM    0x5a6b85
#define ACCENT 0x2470e8
#define SOFT   0xdbe9ff
#define BORDER 0xc9d5e6
#define HEAD   0xe8eef8
#define ERR    0xd92b2b
#define OKC    0x15803d

enum { BTN_SAVE = 0, BTN_OPEN, BTN_CLEAR, BTN_COUNT };

static host_api_t *os;
static char cells[ROWS][COLS][CELL_MAX + 1];
static int row, column, press_button = -1;
static char message[72] = "Flechas mueven, RET baja, TAB avanza";
static int dirty = 1;

/* --- Cadenas sin libc ---------------------------------------------------- */

static int text_len(const char *value) {
    int count = 0;
    while (value && value[count]) count++;
    return count;
}

static int same(const char *left, const char *right) {
    int index = 0;
    while (left[index] && right[index]) {
        char a = left[index], b = right[index];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return 0;
        index++;
    }
    return left[index] == right[index];
}

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

static void append_int(char *out, int capacity, long long value) {
    char digits[20];
    int count = 0;
    int negative = value < 0;
    unsigned long long magnitude = negative ? (unsigned long long)(-value) : (unsigned long long)value;
    do { digits[count++] = (char)('0' + (int)(magnitude % 10u)); magnitude /= 10u; } while (magnitude);
    if (negative) append_to(out, capacity, "-");
    while (count > 0) {
        char one[2];
        one[0] = digits[--count];
        one[1] = 0;
        append_to(out, capacity, one);
    }
}

/* Marcador por el puerto serie: las pruebas no necesitan capturas. */
static void say(const char *prefix, const char *detail) {
    char line[160];
    copy_plain(line, (int)sizeof(line), prefix);
    append_to(line, (int)sizeof(line), detail);
    append_to(line, (int)sizeof(line), "\n");
    os->write(line);
}

static void set_message(const char *value) {
    copy_plain(message, (int)sizeof(message), value);
}

static char upper_char(char value) {
    return value >= 'a' && value <= 'z' ? (char)(value - 'a' + 'A') : value;
}

static int is_digit(char value) { return value >= '0' && value <= '9'; }
static int is_alpha(char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

static void cell_name(int r, int c, char *out, int capacity) {
    if (capacity < 3) { if (capacity > 0) out[0] = 0; return; }
    out[0] = (char)('A' + c);
    char digits[8];
    int count = 0, number = r + 1;
    do { digits[count++] = (char)('0' + number % 10); number /= 10; } while (number);
    int used = 1;
    while (count > 0 && used + 1 < capacity) out[used++] = digits[--count];
    out[used] = 0;
}

/* --- Calculo de formulas ------------------------------------------------- */

static int calc_error;                  /* 0 correcto, 1 error, 2 referencia circular */

typedef struct { const char *cursor; } parser_t;

static long long eval_cell(int r, int c, int depth);
static long long parse_expr(parser_t *parser, int depth);

static void skip_ws(parser_t *parser) { while (*parser->cursor == ' ') parser->cursor++; }

/* Referencia A1..H16 sin consumir funciones: devuelve 0 si no la hay. */
static int parse_ref(parser_t *parser, int *r, int *c) {
    skip_ws(parser);
    if (!is_alpha(*parser->cursor)) return 0;
    int index = upper_char(*parser->cursor) - 'A';
    parser->cursor++;
    if (!is_digit(*parser->cursor)) return 0;
    int number = 0;
    while (is_digit(*parser->cursor)) { number = number * 10 + (*parser->cursor - '0'); parser->cursor++; }
    if (index < 0 || index >= COLS || number < 1 || number > ROWS) { calc_error = 1; return 0; }
    *c = index;
    *r = number - 1;
    return 1;
}

static long long parse_function(parser_t *parser, const char *name, int depth) {
    if (*parser->cursor != '(') { calc_error = 1; return 0; }
    parser->cursor++;
    int r0, c0, r1, c1;
    if (!parse_ref(parser, &r0, &c0)) { calc_error = 1; return 0; }
    r1 = r0; c1 = c0;
    skip_ws(parser);
    if (*parser->cursor == ':') {
        parser->cursor++;
        if (!parse_ref(parser, &r1, &c1)) { calc_error = 1; return 0; }
        skip_ws(parser);
    }
    if (*parser->cursor != ')') { calc_error = 1; return 0; }
    parser->cursor++;
    if (r0 > r1) { int swap = r0; r0 = r1; r1 = swap; }
    if (c0 > c1) { int swap = c0; c0 = c1; c1 = swap; }
    long long total = 0, minimum = 0, maximum = 0, count = 0;
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) {
            if (!cells[r][c][0]) continue;
            long long value = eval_cell(r, c, depth);
            if (calc_error) return 0;
            if (!count || value < minimum) minimum = value;
            if (!count || value > maximum) maximum = value;
            total += value;
            count++;
        }
    if (same(name, "SUMA") || same(name, "SUM")) return total;
    if (same(name, "CONTAR") || same(name, "COUNT")) return count;
    if (!count) return 0;
    if (same(name, "PROMEDIO") || same(name, "AVERAGE")) return total / count;
    if (same(name, "MIN") || same(name, "MINIMO")) return minimum;
    if (same(name, "MAX") || same(name, "MAXIMO")) return maximum;
    calc_error = 1;
    return 0;
}

static long long parse_factor(parser_t *parser, int depth) {
    skip_ws(parser);
    if (calc_error) return 0;
    if (*parser->cursor == '-') { parser->cursor++; return -parse_factor(parser, depth); }
    if (*parser->cursor == '+') { parser->cursor++; return parse_factor(parser, depth); }
    if (*parser->cursor == '(') {
        parser->cursor++;
        long long value = parse_expr(parser, depth);
        skip_ws(parser);
        if (*parser->cursor != ')') { calc_error = 1; return 0; }
        parser->cursor++;
        return value;
    }
    if (is_alpha(*parser->cursor)) {
        char name[12];
        int used = 0;
        while (is_alpha(*parser->cursor) && used + 1 < (int)sizeof(name))
            name[used++] = upper_char(*parser->cursor++);
        name[used] = 0;
        skip_ws(parser);
        if (*parser->cursor == '(') return parse_function(parser, name, depth);
        if (used == 1 && is_digit(*parser->cursor)) {
            int number = 0;
            while (is_digit(*parser->cursor)) { number = number * 10 + (*parser->cursor - '0'); parser->cursor++; }
            int index = name[0] - 'A';
            if (index < 0 || index >= COLS || number < 1 || number > ROWS) { calc_error = 1; return 0; }
            return eval_cell(number - 1, index, depth);
        }
        calc_error = 1;
        return 0;
    }
    if (is_digit(*parser->cursor)) {
        long long value = 0;
        while (is_digit(*parser->cursor)) { value = value * 10 + (*parser->cursor - '0'); parser->cursor++; }
        return value;
    }
    calc_error = 1;
    return 0;
}

static long long parse_term(parser_t *parser, int depth) {
    long long value = parse_factor(parser, depth);
    for (;;) {
        skip_ws(parser);
        if (*parser->cursor == '*') { parser->cursor++; value *= parse_factor(parser, depth); }
        else if (*parser->cursor == '/') {
            parser->cursor++;
            long long divisor = parse_factor(parser, depth);
            if (calc_error) return 0;
            if (!divisor) { calc_error = 1; return 0; }
            value /= divisor;
        } else break;
        if (calc_error) return 0;
    }
    return value;
}

static long long parse_expr(parser_t *parser, int depth) {
    long long value = parse_term(parser, depth);
    for (;;) {
        skip_ws(parser);
        if (*parser->cursor == '+') { parser->cursor++; value += parse_term(parser, depth); }
        else if (*parser->cursor == '-') { parser->cursor++; value -= parse_term(parser, depth); }
        else break;
        if (calc_error) return 0;
    }
    return value;
}

/* Valor de un texto sin formula: entero inicial; el texto no numerico vale 0. */
static long long plain_number(const char *text) {
    int index = 0, negative = 0;
    long long value = 0;
    while (text[index] == ' ') index++;
    if (text[index] == '-') { negative = 1; index++; }
    if (!is_digit(text[index])) return 0;
    while (is_digit(text[index])) { value = value * 10 + (text[index] - '0'); index++; }
    return negative ? -value : value;
}

static long long eval_cell(int r, int c, int depth) {
    if (depth > 8) { calc_error = 2; return 0; }
    const char *text = cells[r][c];
    if (!text[0] || text[0] != '=') return plain_number(text);
    parser_t parser;
    parser.cursor = text + 1;
    long long value = parse_expr(&parser, depth + 1);
    skip_ws(&parser);
    if (!calc_error && *parser.cursor) calc_error = 1;
    return value;
}

/* Texto que se pinta en una celda: el valor calculado o el error. */
static void cell_display(int r, int c, char *out, int capacity) {
    const char *text = cells[r][c];
    if (!text[0]) { out[0] = 0; return; }
    if (text[0] != '=') { copy_plain(out, capacity, text); return; }
    calc_error = 0;
    parser_t parser;
    parser.cursor = text + 1;
    long long value = parse_expr(&parser, 1);
    skip_ws(&parser);
    if (!calc_error && *parser.cursor) calc_error = 1;
    if (calc_error == 2) { copy_plain(out, capacity, "#CICLO"); return; }
    if (calc_error) { copy_plain(out, capacity, "#ERROR"); return; }
    out[0] = 0;
    append_int(out, capacity, value);
}

static int numeric_text(const char *text) {
    int index = 0;
    if (text[index] == '-') index++;
    if (!is_digit(text[index])) return 0;
    while (is_digit(text[index])) index++;
    return text[index] == 0;
}

/* --- Persistencia CSV ---------------------------------------------------- */

static void csv_path(char *out, int capacity) {
    char cwd[FS_PATH_MAX];
    os->cwd_path(cwd, (int)sizeof(cwd));
    int used = text_len(cwd);
    if (used + 1 >= capacity) used = capacity - 1;
    for (int i = 0; i < used; i++) out[i] = cwd[i];
    out[used] = 0;
    if (used && out[used - 1] != '/' && used + 1 < capacity) out[used++] = '/';
    copy_plain(out + used, capacity - used, CSV_NAME);
}

static void clear_sheet(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) cells[r][c][0] = 0;
}

static void save_sheet(void) {
    static char buffer[FS_FILE_MAX_SIZE];
    char path[FS_PATH_MAX];
    csv_path(path, (int)sizeof(path));
    buffer[0] = 0;
    int used = 0;
    for (int r = 0; r < ROWS; r++) {
        int last = -1;
        for (int c = 0; c < COLS; c++) if (cells[r][c][0]) last = c;
        if (last < 0) continue;
        for (int c = 0; c <= last; c++) {
            const char *text = cells[r][c];
            for (int i = 0; text[i] && used + 2 < (int)sizeof(buffer); i++) buffer[used++] = text[i];
            buffer[used] = 0;
            if (c < last && used + 2 < (int)sizeof(buffer)) { buffer[used++] = ','; buffer[used] = 0; }
        }
        if (used + 2 < (int)sizeof(buffer)) { buffer[used++] = '\n'; buffer[used] = 0; }
    }
    if (!os->write_file(path, buffer)) {
        set_message("No se pudo guardar");
        say("hoja.elf: ERROR GUARDADO ", path);
        return;
    }
    set_message("Hoja guardada");
    say("hoja.elf: GUARDADO ", path);
}

static void load_sheet(void) {
    char path[FS_PATH_MAX];
    csv_path(path, (int)sizeof(path));
    uint32_t size = 0;
    const char *data = os->read_file(path, &size);
    if (!data) {
        set_message("No hay hoja.csv: usa Guardar");
        say("hoja.elf: SIN ARCHIVO ", path);
        return;
    }
    clear_sheet();
    row = 0; column = 0;
    int r = 0, c = 0, used = 0;
    for (uint32_t i = 0; i < size; i++) {
        char value = data[i];
        if (value == '\r') continue;
        if (value == ',' || value == ';' || value == '\n') {
            if (r < ROWS && c < COLS) cells[r][c][used] = 0;
            used = 0;
            if (value == '\n') { r++; c = 0; }
            else c++;
            if (r >= ROWS) break;
            continue;
        }
        if (r < ROWS && c < COLS && used < CELL_MAX) cells[r][c][used++] = value;
    }
    if (r < ROWS && c < COLS) cells[r][c][used] = 0;
    set_message("Hoja cargada");
    say("hoja.elf: ABIERTO ", path);
}

/* --- Dibujo -------------------------------------------------------------- */

static void button_rect(int index, int *x, int *y, int *width, int *height) {
    *x = EDGE + index * (BTN_W + EDGE);
    *y = BTN_Y;
    *width = BTN_W;
    *height = BTN_H;
}

static void grid_metrics(int *grid_x, int *cw, int *grid_top, int *rh) {
    *grid_x = EDGE + HEAD_X;
    *cw = (os->width() - *grid_x - EDGE) / COLS;
    *grid_top = TOOLBAR_H + FORMULA_H;
    *rh = ROW_H;
}

static void draw_clipped(int x, int y, int max_width, const char *text, uint32_t color) {
    if (max_width <= 0 || !text) return;
    if (os->text_width(text, 0) <= max_width) { os->text(x, y, text, color, 0); return; }
    char buffer[CELL_MAX + 1];
    int used = 0;
    while (text[used] && used < CELL_MAX) {
        buffer[used] = text[used];
        buffer[used + 1] = 0;
        if (os->text_width(buffer, 0) > max_width) break;
        used++;
    }
    buffer[used] = 0;
    os->text(x, y, buffer, color, 0);
}

static void draw(void) {
    int width = os->width(), height = os->height();
    int grid_x, cw, grid_top, rh;
    grid_metrics(&grid_x, &cw, &grid_top, &rh);
    os->clear(BG);

    os->rect(0, 0, width, TOOLBAR_H, PANEL);
    os->rect(0, TOOLBAR_H - 1, width, 1, BORDER);
    static const char *labels[BTN_COUNT] = { "Guardar", "Abrir", "Limpiar" };
    for (int index = 0; index < BTN_COUNT; index++) {
        int bx, by, bw, bh;
        button_rect(index, &bx, &by, &bw, &bh);
        os->rect(bx, by, bw, bh, index == press_button ? SOFT : PANEL);
        os->frame(bx, by, bw, bh, index == press_button ? ACCENT : BORDER);
        os->text(bx + 12, by + 4, labels[index], index == press_button ? ACCENT : TEXT, 0);
    }
    os->text(width - 210, BTN_Y + 4, "CSV en el directorio actual", DIM, 0);

    /* Barra de formula: nombre de la celda y su contenido editable. */
    os->rect(0, TOOLBAR_H, width, FORMULA_H, PANEL);
    os->rect(0, TOOLBAR_H, width, 1, BORDER);
    char name[8];
    cell_name(row, column, name, (int)sizeof(name));
    os->text(EDGE, TOOLBAR_H + 4, name, ACCENT, 0);
    int name_width = os->text_width(name, 0);
    os->rect(EDGE + name_width + 8, TOOLBAR_H + 3, 1, FORMULA_H - 6, BORDER);
    draw_clipped(EDGE + name_width + 16, TOOLBAR_H + 4, width - name_width - 40,
                 cells[row][column], TEXT);

    /* Cabecera de columnas y numeros de fila. */
    for (int c = 0; c < COLS; c++) {
        int x = grid_x + c * cw;
        os->rect(x, grid_top, cw, HEAD_H, HEAD);
        os->frame(x, grid_top, cw, HEAD_H, BORDER);
        char letter[2] = { (char)('A' + c), 0 };
        int text_x = x + (cw - os->text_width(letter, 0)) / 2;
        os->text(text_x, grid_top + 1, letter, c == column ? ACCENT : DIM, 0);
    }
    for (int r = 0; r < ROWS; r++) {
        int y = grid_top + HEAD_H + r * rh;
        os->rect(EDGE, y, HEAD_X, rh, HEAD);
        os->frame(EDGE, y, HEAD_X, rh, BORDER);
        char number[8];
        number[0] = 0;
        append_int(number, (int)sizeof(number), r + 1);
        os->text(EDGE + 6, y + 3, number, r == row ? ACCENT : DIM, 0);
    }

    /* Celdas: fondo blanco, la activa en azul suave con marco de acento. */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            int x = grid_x + c * cw;
            int y = grid_top + HEAD_H + r * rh;
            int active = r == row && c == column;
            os->rect(x, y, cw, rh, active ? SOFT : PANEL);
            os->frame(x, y, cw, rh, active ? ACCENT : BORDER);
            char value[CELL_MAX + 8];
            cell_display(r, c, value, (int)sizeof(value));
            if (!value[0]) continue;
            uint32_t color = TEXT;
            if (value[0] == '#') color = ERR;
            else if (cells[r][c][0] == '=') color = ACCENT;
            int text_x = x + 5;
            if (numeric_text(value)) text_x = x + cw - 5 - os->text_width(value, 0);
            draw_clipped(text_x, y + 3, cw - 10, value, color);
        }

    /* Pie: mensaje de estado y atajos. */
    int status_y = height - STATUS_H;
    os->rect(0, status_y, width, STATUS_H, PANEL);
    os->rect(0, status_y, width, 1, BORDER);
    os->text(EDGE, status_y + 4, message, DIM, 0);
    os->text(EDGE, status_y + 22, "RET baja  TAB avanza  SUPR vacia  HOME/END columna", DIM, 0);
    os->text(width - 200, status_y + 4, "=SUMA(A1:B3)  =B1-A1", ACCENT, 0);
}

/* --- Teclado y raton ----------------------------------------------------- */

static void commit_cell(void) {
    const char *text = cells[row][column];
    if (!text[0]) return;
    char name[8];
    cell_name(row, column, name, (int)sizeof(name));
    char line[160];
    if (text[0] != '=') {
        copy_plain(line, (int)sizeof(line), "hoja.elf: CELDA ");
        append_to(line, (int)sizeof(line), name);
        append_to(line, (int)sizeof(line), " ");
        append_to(line, (int)sizeof(line), text);
        append_to(line, (int)sizeof(line), "\n");
        os->write(line);
        return;
    }
    calc_error = 0;
    parser_t parser;
    parser.cursor = text + 1;
    long long value = parse_expr(&parser, 1);
    skip_ws(&parser);
    if (!calc_error && *parser.cursor) calc_error = 1;
    copy_plain(line, (int)sizeof(line), "hoja.elf: FORMULA ");
    append_to(line, (int)sizeof(line), name);
    append_to(line, (int)sizeof(line), " ");
    append_to(line, (int)sizeof(line), text);
    append_to(line, (int)sizeof(line), " -> ");
    if (calc_error) append_to(line, (int)sizeof(line), calc_error == 2 ? "CIRCULAR" : "ERROR");
    else append_int(line, (int)sizeof(line), value);
    append_to(line, (int)sizeof(line), "\n");
    os->write(line);
}

static void press_at(int x, int y) {
    for (int index = 0; index < BTN_COUNT; index++) {
        int bx, by, bw, bh;
        button_rect(index, &bx, &by, &bw, &bh);
        if (x < bx || x > bx + bw || y < by || y > by + bh) continue;
        if (index == BTN_SAVE) save_sheet();
        else if (index == BTN_OPEN) load_sheet();
        else { clear_sheet(); row = 0; column = 0; set_message("Hoja vacia"); say("hoja.elf: HOJA VACIA ", ""); }
        press_button = index;
        return;
    }
    int grid_x, cw, grid_top, rh;
    grid_metrics(&grid_x, &cw, &grid_top, &rh);
    if (x < grid_x || y < grid_top + HEAD_H) return;
    int c = (x - grid_x) / cw;
    int r = (y - grid_top - HEAD_H) / rh;
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return;
    row = r;
    column = c;
}

static void edit_char(int character) {
    int used = text_len(cells[row][column]);
    if (used >= CELL_MAX) { set_message("Celda llena (24 caracteres)"); return; }
    cells[row][column][used] = (char)character;
    cells[row][column][used + 1] = 0;
}

static void erase_char(void) {
    int used = text_len(cells[row][column]);
    if (!used) return;
    cells[row][column][used - 1] = 0;
}

static void key_press(int key) {
    if (key >= 32 && key < 127) { edit_char(key); return; }
    if (key == '\b') { erase_char(); return; }
    if (key == '\n') { commit_cell(); row = (row + 1) % ROWS; return; }
    if (key == HOST_KEY_TAB) { commit_cell(); column = (column + 1) % COLS; return; }
    if (key == HOST_KEY_UP) { row = (row + ROWS - 1) % ROWS; return; }
    if (key == HOST_KEY_DOWN) { row = (row + 1) % ROWS; return; }
    if (key == HOST_KEY_LEFT) { column = (column + COLS - 1) % COLS; return; }
    if (key == HOST_KEY_RIGHT) { column = (column + 1) % COLS; return; }
    if (key == HOST_KEY_HOME) { column = 0; return; }
    if (key == HOST_KEY_END) { column = COLS - 1; return; }
    if (key == HOST_KEY_DELETE) {
        cells[row][column][0] = 0;
        set_message("Celda vaciada");
    }
}

/* --- Arranque ------------------------------------------------------------ */

int main(host_api_t *api) {
    if (!api || api->abi_version != HOST_ABI_VERSION) {
        if (api) api->write("hoja.elf: ABI incompatible\n");
        return 1;
    }
    os = api;
    if (!api->window("Hoja de calculo", 800, 520)) {
        api->write("hoja.elf: no se pudo abrir la ventana\n");
        return 2;
    }
    clear_sheet();
    row = column = 0;
    char start[64];
    copy_plain(start, (int)sizeof(start), "hoja.elf: HOJA 8X16 LISTA ABI=");
    append_int(start, (int)sizeof(start), (long long)api->abi_version);
    append_to(start, (int)sizeof(start), "\n");
    api->write(start);

    host_event_t event;
    while (1) {
        if (!api->pump(&event)) break;
        if (event.type == HOST_EV_DOWN) {
            dirty = 1;
            press_at(event.x, event.y);
        } else if (event.type == HOST_EV_UP) {
            if (press_button >= 0) { press_button = -1; dirty = 1; }
        } else if (event.type == HOST_EV_MOVE) {
            dirty = 1;
        } else if (event.type == HOST_EV_KEY) {
            dirty = 1;
            key_press((int)event.key);
        } else if (event.type == HOST_EV_CLOSE) {
            break;
        }
        if (dirty) {
            draw();
            api->present();
            dirty = 0;
        }
    }
    api->write("hoja.elf: fin correcto\n");
    return api->finish(0);
}
