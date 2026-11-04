#include "host_api.h"

/* Editor de texto de la interfaz grafica. Guarda y abre archivos del sistema
 * de archivos del kernel a traves de host_api v3, sin libc. */

#define PANEL 0xffffff
#define BG    0xf4f7fc
#define TEXT  0x14213a
#define DIM   0x5a6b85
#define ACCENT 0x2470e8
#define BORDER 0xc9d5e6
#define OKC   0x15803d

#define CAPACITY 2048
#define LINE_MAX 160
#define LINE_HEIGHT 22
#define TOP 46

static char text[CAPACITY + 1];
static int length, cursor;
static char file_name[40] = "nota.txt";
static char message[56] = "TAB: comandos";
static char command[64];
static int command_len, command_mode;
static int quit;

static void copy(char *out, int capacity, const char *source) {
    int index = 0;
    while (source && source[index] && index + 1 < capacity) { out[index] = source[index]; index++; }
    out[index] = 0;
}

static void set_message(const char *source) { copy(message, sizeof(message), source); }

static void insert(int byte) {
    if (length + 1 > CAPACITY) { set_message("Bufer lleno"); return; }
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

/* Final de la linea: el '\n' que la cierra o el final del bufer. */
static int line_end(int line) {
    int next = line_start(line + 1);
    return next >= length ? length : next - 1;
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

static void draw(host_api_t *api) {
    int width = api->width(), height = api->height();
    api->clear(BG);
    api->text(16, 8, file_name, TEXT, 1);
    api->text(16 + api->text_width(file_name, 1) + 24, 12, message, DIM, 0);
    api->rect(16, 34, width - 32, 1, BORDER);
    int rows = (height - TOP - 40) / LINE_HEIGHT;
    if (rows < 1) rows = 1;
    int cursor_line = line_of(cursor);
    int first = cursor_line - rows + 1;
    if (first < 0) first = 0;
    for (int row = 0; row < rows; row++) {
        int start = line_start(first + row);
        if (start > length) break;
        char buffer[LINE_MAX];
        int used = 0;
        for (int i = start; i < length && text[i] != '\n' && used < LINE_MAX - 1; i++) buffer[used++] = text[i];
        buffer[used] = 0;
        api->text(20, TOP + row * LINE_HEIGHT, buffer, TEXT, 0);
    }
    /* Cursor: barra fina a continuacion del texto de su linea. */
    int start = line_start(cursor_line);
    char prefix[LINE_MAX];
    int used = 0;
    for (int i = start; i < cursor && used < LINE_MAX - 1; i++) prefix[used++] = text[i];
    prefix[used] = 0;
    api->rect(20 + api->text_width(prefix, 0), TOP + (cursor_line - first) * LINE_HEIGHT + 2,
              2, LINE_HEIGHT - 6, ACCENT);
    api->rect(16, height - 34, width - 32, 1, BORDER);
    if (command_mode) {
        char line[96];
        copy(line, sizeof(line), ":");
        int size = 1;
        for (int i = 0; i < command_len && size + 1 < (int)sizeof(line); i++) line[size++] = command[i];
        line[size++] = '_';
        line[size] = 0;
        api->text(20, height - 28, line, ACCENT, 0);
    } else {
        char status[64];
        copy(status, sizeof(status), "Linea ");
        int size = 0;
        while (status[size]) size++;
        int value = cursor_line + 1;
        char digits[8];
        int count = 0;
        do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value);
        while (count && size + 1 < (int)sizeof(status)) status[size++] = digits[--count];
        status[size] = 0;
        api->text(20, height - 28, status, DIM, 0);
        api->text(width - 190, height - 28, "TAB comandos  ESC salir", DIM, 0);
    }
}

static void run_command(host_api_t *api) {
    command[command_len] = 0;
    if (!command_len) { command_mode = 0; return; }
    char verb = command[0];
    const char *rest = command + 1;
    while (*rest == ' ') rest++;
    if (verb == 'q') { quit = 1; api->close(); command_mode = 0; return; }
    if (verb == 'w' || verb == 'o') {
        char target[40];
        copy(target, sizeof(target), *rest ? rest : file_name);
        if (verb == 'w') {
            if (api->write_file(target, text)) {
                copy(file_name, sizeof(file_name), target);
                set_message("Guardado");
            } else set_message("No se pudo guardar");
        } else {
            uint32_t size = 0;
            const char *content = api->read_file(target, &size);
            if (!content) set_message("Archivo no encontrado");
            else {
                if ((int)size > CAPACITY) size = CAPACITY;
                for (uint32_t i = 0; i < size; i++) text[i] = content[i];
                length = (int)size;
                text[length] = 0;
                cursor = length;
                copy(file_name, sizeof(file_name), target);
                set_message("Abierto");
            }
        }
    } else if (verb == 'n') {
        length = cursor = 0;
        text[0] = 0;
        set_message("Bufer nuevo");
    } else set_message("Comandos: w o n q");
    command_mode = 0;
    command_len = 0;
}

int main(host_api_t *api) {
    if (!api || api->abi_version != HOST_ABI_VERSION) {
        if (api) api->write("editor.elf: ABI incompatible\n");
        return 1;
    }
    if (!api->window("Editor de texto", 720, 470)) {
        api->write("editor.elf: no se pudo abrir la ventana\n");
        return 2;
    }
    text[0] = 0;
    length = cursor = 0;
    quit = 0;
    /* Solo se repinta cuando llega un evento: evita fotogramas vacios y el
     * consiguiente volcado innecesario de la ventana al framebuffer. */
    int dirty = 1;
    host_event_t event;
    while (!quit) {
        if (!api->pump(&event)) break;
        if (event.type == HOST_EV_KEY) {
            int key = (int)event.key;
            dirty = 1;
            if (command_mode) {
                if (key == HOST_KEY_TAB) { command_mode = 0; command_len = 0; }
                else if (key == '\n') run_command(api);
                else if (key == '\b') { if (command_len) command[--command_len] = 0; }
                else if (key >= 32 && key < 127 && command_len + 1 < (int)sizeof(command))
                    command[command_len++] = (char)key;
            } else if (key == HOST_KEY_TAB) {
                command_mode = 1;
                command_len = 0;
                command[0] = 0;
                set_message("Escribe w nombre, o nombre, n o q");
            } else if (key == '\n') insert('\n');
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
            else if (key >= 32 && key < 127) insert(key);
        } else if (event.type != HOST_EV_NONE) dirty = 1;
        if (dirty) {
            draw(api);
            api->present();
            dirty = 0;
        }
    }
    api->write("editor.elf: fin correcto\n");
    return api->finish(0);
}
