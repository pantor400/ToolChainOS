#include "gui.h"
#include "font_ui.h"
#include "console.h"
#include "cpuinfo.h"
#include "elf_loader.h"
#include "fs.h"
#include "heap.h"
#include "images.h"
#include "interrupts.h"
#include "keyboard.h"
#include "mouse.h"
#include "net.h"
#include "pmm.h"
#include "rtc.h"
#include "tasks.h"
#include "utf8.h"
#include "util.h"

/* --- Estado global ------------------------------------------------------ */

#define GUI_WINDOW_COUNT 6
#define CURSOR_W 13
#define CURSOR_H 19

enum { WIN_SYSTEM = 1, WIN_FILES, WIN_NET, WIN_INPUT, WIN_APPS, WIN_IMAGES, WIN_APP };
enum { SLOT_SYSTEM = 0, SLOT_FILES, SLOT_NET, SLOT_INPUT, SLOT_APPS, SLOT_IMAGES };

typedef struct {
    int used, kind, x, y, width, height, z;
    char title[40];
} gui_window_t;

typedef struct { int icon; const char *label; int kind; } dock_item_t;

/* Estado de la ventana Archivos: fila marcada, visor de contenido y captura
 * de un nombre cuando se crea o renombra. */
enum { FILES_NONE = 0, FILES_NEW_FILE, FILES_NEW_DIR, FILES_RENAME };
typedef struct {
    int selected, scroll, viewer, view_top, action;
    int drag;                 /* 1 = barra del listado, 2 = barra del visor */
    char input[FS_NAME_MAX];
    size_t input_len;
    char message[72];
} files_state_t;
static files_state_t files;
static int files_ids[FS_MAX_NODES];
/* Visor de imagenes: indice marcado dentro del blob empaquetado. */
static int images_selected;

static struct limine_framebuffer *device;
static gui_surface_t back;
static gui_surface_t app_surface;
static uint64_t app_physical;
static uint64_t app_page_count;
static gui_surface_t *draw_target;
static gui_theme_t palette;
static const gui_font_t *font_small, *font_body, *font_title;
static int clip_x, clip_y, clip_w, clip_h;
static int active, want_exit;
static gui_window_t windows[GUI_WINDOW_COUNT];
static int z_top;
/* Ventana activa: es la unica que recibe el teclado y la que se resalta en el
 * titulo. -1 significa que ninguna lo esta. */
static int focus_slot = -1;
static int scale;
static int margin, bar_height, dock_height, dock_y, content_y, content_height;
static int icon_size, title_height, radius;
static int dragging, drag_slot, drag_dx, drag_dy;
static int last_mouse_x, last_mouse_y, last_buttons, last_key;
static uint32_t red_shift, green_shift, blue_shift;
static int fast_flush;

/* Region pendiente de repintar y de volcar al dispositivo. Solo se reconstruye
 * lo que cambio, asi que mover el puntero no repinta el escritorio entero. */
static int paint_x0, paint_y0, paint_x1, paint_y1;
static int cursor_drawn_x = -1, cursor_drawn_y = -1;
static int dock_hover = -1;
static uint32_t gradient[256];
/* Fondo: si el kernel lleva imagenes empaquetadas, la primera (wallpaper) se
 * escala a la pantalla con tablas de muestreo; si no, queda el degradado. */
static int wall_index = -1, wall_ready;
static uint16_t wall_x[4096], wall_y[4096];

static const dock_item_t dock_items[] = {
    {GUI_ICON_MONITOR, "Sistema", WIN_SYSTEM},
    {GUI_ICON_FOLDER, "Archivos", WIN_FILES},
    {GUI_ICON_GLOBE, "Red", WIN_NET},
    {GUI_ICON_TERMINAL, "Entrada", WIN_INPUT},
    {GUI_ICON_IMAGE, "Imagenes", WIN_IMAGES},
    {GUI_ICON_PLAY, "Apps", WIN_APPS},
};
#define DOCK_ITEMS ((int)(sizeof(dock_items) / sizeof(dock_items[0])))

/* Flecha del puntero en una rejilla de 13x19; '#' relleno, 'X' borde.
 * Se dibuja escalada por enteros, sin bitmap externo. */
static const char cursor_shape[CURSOR_H][CURSOR_W + 1] = {
    "X...........",
    "XX..........",
    "X#X.........",
    "X##X........",
    "X###X.......",
    "X####X......",
    "X#####X.....",
    "X######X....",
    "X#######X...",
    "X########X..",
    "X#####XXXXX.",
    "X##X##X.....",
    "X#X.X##X....",
    "XX..X##X....",
    "X....X##X...",
    ".....X##X...",
    "......X##X..",
    "......X##X..",
    ".......XX...",
};

/* --- Utilidades numericas (sin libc ni coma flotante) -------------------- */

static uint64_t u64_text(char *out, size_t capacity, uint64_t value, int base) {
    const char *digits = "0123456789";
    char buffer[24];
    unsigned count = 0;
    if (!capacity) return 0;
    if (!base) base = 10;
    do {
        buffer[count++] = digits[value % (uint64_t)base];
        value /= (uint64_t)base;
    } while (value && count < sizeof(buffer));
    size_t used = 0;
    while (count && used + 1 < capacity) out[used++] = buffer[--count];
    out[used] = 0;
    return used;
}

static const char *text_u64(uint64_t value) {
    static char buffer[24];
    u64_text(buffer, sizeof(buffer), value, 10);
    return buffer;
}

static int isqrt_int(int value) {
    if (value <= 0) return 0;
    int x = value, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + value / x) / 2; }
    return x;
}

static uint32_t blend_channel(uint32_t dst, uint32_t src, unsigned alpha) {
    return ((((dst >> 16) & 255) * (255 - alpha) + ((src >> 16) & 255) * alpha) / 255 << 16) |
           ((((dst >> 8) & 255) * (255 - alpha) + ((src >> 8) & 255) * alpha) / 255 << 8) |
           (((dst & 255) * (255 - alpha) + (src & 255) * alpha) / 255);
}

static uint32_t to_device(uint32_t rgb) {
    return (((rgb >> 16) & 255u) << red_shift) |
           (((rgb >> 8) & 255u) << green_shift) |
           ((rgb & 255u) << blue_shift);
}

/* --- Superficie y primitivas -------------------------------------------- */

static void clip_to_surface(void) {
    if (!draw_target) return;
    if (clip_x < 0) { clip_w += clip_x; clip_x = 0; }
    if (clip_y < 0) { clip_h += clip_y; clip_y = 0; }
    if (clip_x + clip_w > draw_target->width) clip_w = draw_target->width - clip_x;
    if (clip_y + clip_h > draw_target->height) clip_h = draw_target->height - clip_y;
    if (clip_w < 0) clip_w = 0;
    if (clip_h < 0) clip_h = 0;
}

void gui_set_clip(int x, int y, int width, int height) {
    clip_x = x; clip_y = y; clip_w = width; clip_h = height;
    clip_to_surface();
}

void gui_reset_clip(void) {
    if (!draw_target) return;
    clip_x = 0; clip_y = 0; clip_w = draw_target->width; clip_h = draw_target->height;
}

static void blank_clip(void) { clip_x = clip_y = clip_w = clip_h = 0; }

/* El recorte se anida: el contenido de una app lo necesita sin perder el
 * recorte de la region que se esta repintando. */
static int saved_clip[4];
static void clip_save(void) {
    saved_clip[0] = clip_x; saved_clip[1] = clip_y;
    saved_clip[2] = clip_w; saved_clip[3] = clip_h;
}
static void clip_restore(void) {
    gui_set_clip(saved_clip[0], saved_clip[1], saved_clip[2], saved_clip[3]);
}

static int intersects(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void put_pixel(int x, int y, uint32_t rgb) {
    if (x < clip_x || y < clip_y || x >= clip_x + clip_w || y >= clip_y + clip_h) return;
    draw_target->pixels[(size_t)y * (size_t)draw_target->stride + (size_t)x] = rgb;
}

static void put_pixel_alpha(int x, int y, uint32_t rgb, unsigned alpha) {
    if (alpha == 0) return;
    if (x < clip_x || y < clip_y || x >= clip_x + clip_w || y >= clip_y + clip_h) return;
    uint32_t *pixel = &draw_target->pixels[(size_t)y * (size_t)draw_target->stride + (size_t)x];
    if (alpha >= 255) { *pixel = rgb; return; }
    *pixel = blend_channel(*pixel, rgb, alpha);
}

void gui_fill(int x, int y, int width, int height, uint32_t rgb) {
    if (!draw_target || width <= 0 || height <= 0) return;
    int x0 = x < clip_x ? clip_x : x;
    int y0 = y < clip_y ? clip_y : y;
    int x1 = x + width; if (x1 > clip_x + clip_w) x1 = clip_x + clip_w;
    int y1 = y + height; if (y1 > clip_y + clip_h) y1 = clip_y + clip_h;
    for (int row = y0; row < y1; row++) {
        uint32_t *line = draw_target->pixels + (size_t)row * (size_t)draw_target->stride;
        for (int column = x0; column < x1; column++) line[column] = rgb;
    }
}

void gui_blend(int x, int y, int width, int height, uint32_t rgb, unsigned alpha) {
    if (!draw_target || width <= 0 || height <= 0 || alpha == 0) return;
    int x0 = x < clip_x ? clip_x : x;
    int y0 = y < clip_y ? clip_y : y;
    int x1 = x + width; if (x1 > clip_x + clip_w) x1 = clip_x + clip_w;
    int y1 = y + height; if (y1 > clip_y + clip_h) y1 = clip_y + clip_h;
    for (int row = y0; row < y1; row++) {
        uint32_t *line = draw_target->pixels + (size_t)row * (size_t)draw_target->stride;
        for (int column = x0; column < x1; column++) line[column] = blend_channel(line[column], rgb, alpha);
    }
}

void gui_frame(int x, int y, int width, int height, uint32_t rgb) {
    if (width <= 0 || height <= 0) return;
    gui_fill(x, y, width, 1, rgb);
    gui_fill(x, y + height - 1, width, 1, rgb);
    gui_fill(x, y, 1, height, rgb);
    gui_fill(x + width - 1, y, 1, height, rgb);
}

void gui_line(int x0, int y0, int x1, int y1, uint32_t rgb) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (;;) {
        put_pixel(x0, y0, rgb);
        if (x0 == x1 && y0 == y1) break;
        int twice = 2 * error;
        if (twice >= dy) { error += dy; x0 += sx; }
        if (twice <= dx) { error += dx; y0 += sy; }
    }
}

void gui_circle(int cx, int cy, int r, uint32_t rgb) {
    if (r <= 0) return;
    for (int dy = -r; dy <= r; dy++) {
        int dx = isqrt_int(r * r - dy * dy);
        gui_fill(cx - dx, cy + dy, dx * 2 + 1, 1, rgb);
    }
}

void gui_round(int x, int y, int width, int height, int r, uint32_t rgb) {
    if (width <= 0 || height <= 0) return;
    if (r <= 0) { gui_fill(x, y, width, height, rgb); return; }
    if (r * 2 > width) r = width / 2;
    if (r * 2 > height) r = height / 2;
    if (r <= 0) { gui_fill(x, y, width, height, rgb); return; }
    gui_fill(x, y + r, width, height - 2 * r, rgb);
    gui_fill(x + r, y, width - 2 * r, r, rgb);
    gui_fill(x + r, y + height - r, width - 2 * r, r, rgb);
    for (int row = 0; row < r; row++) {
        int offset = r - 1 - row;
        int dx = isqrt_int(r * r - offset * offset);
        if (dx <= 0) continue;
        gui_fill(x + r - dx, y + row, dx, 1, rgb);
        gui_fill(x + width - r, y + row, dx, 1, rgb);
        gui_fill(x + r - dx, y + height - 1 - row, dx, 1, rgb);
        gui_fill(x + width - r, y + height - 1 - row, dx, 1, rgb);
    }
}

/* Contorno de un pixel que sigue la silueta de gui_round: el marco de una
 * ventana no puede ser recto si su relleno es redondeado, o las esquinas
 * muestran picos grises. Recorre el mismo trazado y, en cada fila, pinta el
 * tramo que la curva gana respecto a la anterior; con un solo pixel por fila
 * la esquina quedaba punteada. */
static void round_frame(int x, int y, int width, int height, int r, uint32_t rgb) {
    if (width <= 0 || height <= 0) return;
    if (r * 2 > width) r = width / 2;
    if (r * 2 > height) r = height / 2;
    if (r <= 0) { gui_frame(x, y, width, height, rgb); return; }
    gui_fill(x + r, y, width - 2 * r, 1, rgb);
    gui_fill(x + r, y + height - 1, width - 2 * r, 1, rgb);
    gui_fill(x, y + r, 1, height - 2 * r, rgb);
    gui_fill(x + width - 1, y + r, 1, height - 2 * r, rgb);
    int previous = 0;                          /* avance de la fila de arriba */
    for (int row = 0; row < r; row++) {
        int offset = r - 1 - row;
        int dx = isqrt_int(r * r - offset * offset);
        int left = x + r - dx;
        int left_span = (row ? x + r - previous : x + r) - left;
        int right_end = x + width - r + dx;
        int right = row ? x + width - r + previous : x + width - r;
        int right_span = right_end - right;
        if (left_span < 1) left_span = 1;
        if (right_span < 1) { right_span = 1; right = right_end - 1; }
        gui_fill(left, y + row, left_span, 1, rgb);
        gui_fill(right, y + row, right_span, 1, rgb);
        gui_fill(left, y + height - 1 - row, left_span, 1, rgb);
        gui_fill(right, y + height - 1 - row, right_span, 1, rgb);
        previous = dx;
    }
}

/* --- Tipografia --------------------------------------------------------- */

static const gui_glyph_t *find_glyph(const gui_font_t *font, uint32_t codepoint) {
    int low = 0, high = (int)font->count - 1;
    while (low <= high) {
        int middle = (low + high) / 2;
        uint32_t value = font->glyphs[middle].cp;
        if (value == codepoint) return &font->glyphs[middle];
        if (value < codepoint) low = middle + 1; else high = middle - 1;
    }
    return 0;
}

static const gui_glyph_t *glyph_for(const gui_font_t *font, uint32_t codepoint) {
    const gui_glyph_t *glyph = find_glyph(font, codepoint);
    if (!glyph) glyph = find_glyph(font, 0x3f); /* '?' */
    return glyph;
}

static void draw_glyph(const gui_glyph_t *glyph, const gui_font_t *font, int pen_x, int baseline, uint32_t rgb) {
    const uint8_t *bits = font->bits + glyph->offset;
    int top = baseline + glyph->oy;
    for (int row = 0; row < glyph->h; row++) {
        const uint8_t *line = bits + (size_t)row * glyph->row_bytes;
        for (int column = 0; column < glyph->w; column++) {
            unsigned nibble = (column & 1) ? (line[column >> 1] >> 4) : (line[column >> 1] & 15);
            if (!nibble) continue;
            put_pixel_alpha(pen_x + glyph->ox + column, top + row, rgb, nibble * 17);
        }
    }
}

void gui_text(int x, int baseline, const char *text, uint32_t rgb, const gui_font_t *font) {
    if (!draw_target || !text || !font) return;
    size_t length = slen(text), position = 0;
    int pen = x;
    while (position < length) {
        uint32_t codepoint;
        size_t used;
        int status = utf8_decode((const uint8_t *)text + position, length - position, &codepoint, &used);
        if (status != 1) { codepoint = 0x3f; used = 1; }
        position += used;
        if (codepoint == '\n') continue;
        const gui_glyph_t *glyph = glyph_for(font, codepoint);
        if (glyph) {
            draw_glyph(glyph, font, pen, baseline, rgb);
            pen += glyph->advance;
        }
    }
}

int gui_text_width(const char *text, const gui_font_t *font) {
    if (!text || !font) return 0;
    size_t length = slen(text), position = 0;
    int width = 0;
    while (position < length) {
        uint32_t codepoint;
        size_t used;
        int status = utf8_decode((const uint8_t *)text + position, length - position, &codepoint, &used);
        if (status != 1) { codepoint = 0x3f; used = 1; }
        position += used;
        const gui_glyph_t *glyph = glyph_for(font, codepoint);
        if (glyph) width += glyph->advance;
    }
    return width;
}

void gui_text_center(int center_x, int baseline, const char *text, uint32_t rgb, const gui_font_t *font) {
    gui_text(center_x - gui_text_width(text, font) / 2, baseline, text, rgb, font);
}

/* --- Iconos vectoriales (lienzo 24x24, escalables) ---------------------- */

static void icon_line(int x0, int y0, int x1, int y1, uint32_t rgb, int thickness) {
    for (int pass = 0; pass < thickness; pass++) gui_line(x0 + pass, y0, x1 + pass, y1, rgb);
}
static void icon_frame(int x, int y, int w, int h, uint32_t rgb, int thickness) {
    for (int pass = 0; pass < thickness; pass++) gui_frame(x + pass, y + pass, w - pass * 2, h - pass * 2, rgb);
}
/* Triangulo isosceles apuntando a la derecha, relleno por filas. */
static void icon_triangle(int cx, int cy, int radius, uint32_t rgb) {
    for (int dy = -radius; dy <= radius; dy++) {
        int span = radius - (dy < 0 ? -dy : dy);
        int start = cx - radius / 2 + (radius - span) / 2;
        gui_fill(start, cy + dy, span + 1, 1, rgb);
    }
}

void gui_icon(int icon, int x, int y, int size, uint32_t rgb) {
    if (size < 8) size = 8;
    int u = size / 24; if (u < 1) u = 1;      /* grosor de trazo */
    #define IX(v) (x + (v) * size / 24)
    #define IY(v) (y + (v) * size / 24)
    #define IS(v) ((v) * size / 24)
    switch (icon) {
    case GUI_ICON_MONITOR:
        icon_frame(IX(2), IY(4), IS(20), IS(14), rgb, u);
        gui_fill(IX(11), IY(18), IS(2), IS(3), rgb);
        gui_fill(IX(7), IY(21), IS(10), IS(1) + 1, rgb);
        break;
    case GUI_ICON_FOLDER:
        gui_round(IX(2), IY(6), IS(20), IS(14), IS(3), rgb);
        gui_fill(IX(2), IY(4), IS(9), IS(4), rgb);
        gui_fill(IX(2), IY(10), IS(20), IS(2), palette.white);
        break;
    case GUI_ICON_GLOBE:
        gui_circle(IX(12), IY(12), IS(9), rgb);
        gui_circle(IX(12), IY(12), IS(9) - u, palette.panel);
        gui_fill(IX(3), IY(12), IS(18), u, rgb);
        for (int dy = -IS(9); dy <= IS(9); dy++) {
            int dx = isqrt_int(IS(9) * IS(9) - dy * dy);
            int narrow = dx / 2;
            gui_fill(IX(12) - narrow, IY(12) + dy, narrow * 2 + 1, 1, rgb);
        }
        break;
    case GUI_ICON_PLAY:
        icon_triangle(IX(12), IY(12), IS(8), rgb);
        break;
    case GUI_ICON_CLOSE:
        icon_line(IX(7), IY(7), IX(17), IY(17), rgb, u);
        icon_line(IX(17), IY(7), IX(7), IY(17), rgb, u);
        break;
    case GUI_ICON_TERMINAL:
        icon_frame(IX(3), IY(5), IS(18), IS(15), rgb, u);
        icon_line(IX(7), IY(10), IX(10), IY(13), rgb, u);
        icon_line(IX(10), IY(13), IX(7), IY(16), rgb, u);
        gui_fill(IX(13), IY(16), IS(4), u, rgb);
        break;
    case GUI_ICON_INFO:
        gui_circle(IX(12), IY(12), IS(9), rgb);
        gui_circle(IX(12), IY(12), IS(9) - u, palette.panel);
        gui_fill(IX(11), IY(7), IS(2), IS(2), rgb);
        gui_fill(IX(11), IY(11), IS(2), IS(6), rgb);
        break;
    case GUI_ICON_CHIP:
        icon_frame(IX(6), IY(6), IS(12), IS(12), rgb, u);
        gui_fill(IX(10), IY(10), IS(4), IS(4), rgb);
        for (int i = 0; i < 3; i++) {
            gui_fill(IX(4 + i * 6), IY(3), u, IS(3), rgb);
            gui_fill(IX(4 + i * 6), IY(18), u, IS(3), rgb);
        }
        break;
    case GUI_ICON_ARROW:
        icon_line(IX(5), IY(19), IX(17), IY(7), rgb, u);
        icon_line(IX(9), IY(6), IX(18), IY(6), rgb, u);
        icon_line(IX(18), IY(6), IX(18), IY(15), rgb, u);
        break;
    case GUI_ICON_IMAGE:
        icon_frame(IX(2), IY(5), IS(20), IS(15), rgb, u);
        gui_fill(IX(6), IY(9), IS(3), IS(3), rgb);
        icon_line(IX(4), IY(18), IX(10), IY(11), rgb, u);
        icon_line(IX(10), IY(11), IX(14), IY(16), rgb, u);
        icon_line(IX(14), IY(16), IX(17), IY(13), rgb, u);
        icon_line(IX(17), IY(13), IX(21), IY(18), rgb, u);
        break;
    default:
        gui_round(IX(4), IY(4), IS(16), IS(16), IS(4), rgb);
        break;
    }
    #undef IX
    #undef IY
    #undef IS
}

/* --- Composición -------------------------------------------------------- */

int gui_width(void) { return back.width; }
int gui_height(void) { return back.height; }
const gui_theme_t *gui_theme(void) { return &palette; }
const gui_font_t *gui_font_small(void) { return font_small; }
const gui_font_t *gui_font_body(void) { return font_body; }
const gui_font_t *gui_font_title(void) { return font_title; }
int gui_active(void) { return active; }

/* --- Daño, volcado y cursor --------------------------------------------- */

static void invalidate(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > back.width) width = back.width - x;
    if (y + height > back.height) height = back.height - y;
    if (width <= 0 || height <= 0) return;
    if (paint_x0 >= paint_x1) {
        paint_x0 = x; paint_y0 = y; paint_x1 = x + width; paint_y1 = y + height;
        return;
    }
    if (x < paint_x0) paint_x0 = x;
    if (y < paint_y0) paint_y0 = y;
    if (x + width > paint_x1) paint_x1 = x + width;
    if (y + height > paint_y1) paint_y1 = y + height;
}

static int paint_pending(void) { return paint_x0 < paint_x1 && paint_y0 < paint_y1; }
static void paint_clear(void) { paint_x0 = paint_y0 = paint_x1 = paint_y1 = 0; }

/* Copia al dispositivo solo el rectangulo indicado, respetando el formato. */
static void flush_region(int x0, int y0, int x1, int y1) {
    if (!device) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > back.width) x1 = back.width;
    if (y1 > back.height) y1 = back.height;
    if (x1 <= x0 || y1 <= y0) return;
    volatile uint32_t *front = (volatile uint32_t *)device->address;
    size_t stride = (size_t)(device->pitch / 4);
    for (int row = y0; row < y1; row++) {
        volatile uint32_t *destination = front + (size_t)row * stride + (size_t)x0;
        const uint32_t *source = back.pixels + (size_t)row * (size_t)back.stride + (size_t)x0;
        if (fast_flush) {
            for (int column = 0; column < x1 - x0; column++) destination[column] = source[column];
        } else {
            for (int column = 0; column < x1 - x0; column++) destination[column] = to_device(source[column]);
        }
    }
}

static void draw_cursor_on_device(int mx, int my) {
    if (!device) return;
    volatile uint32_t *front = (volatile uint32_t *)device->address;
    int stride = (int)(device->pitch / 4);
    uint32_t body = to_device(palette.cursor), edge = to_device(palette.cursor_edge);
    for (int row = 0; row < CURSOR_H; row++)
        for (int column = 0; column < CURSOR_W; column++) {
            char cell = cursor_shape[row][column];
            if (cell == '.') continue;
            int px = mx + column * scale, py = my + row * scale;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++) {
                    int x = px + sx, y = py + sy;
                    if (x < 0 || y < 0 || x >= (int)device->width || y >= (int)device->height) continue;
                    front[(size_t)y * (size_t)stride + (size_t)x] = (cell == '#') ? body : edge;
                }
        }
}

/* Borra el puntero anterior reponiendo esa zona desde el buffer logico: sin
 * guardar el fondo no hay estelas ni lecturas de MMIO. */
static void cursor_restore(void) {
    if (cursor_drawn_x < 0) return;
    flush_region(cursor_drawn_x, cursor_drawn_y,
                 cursor_drawn_x + CURSOR_W * scale, cursor_drawn_y + CURSOR_H * scale);
    cursor_drawn_x = cursor_drawn_y = -1;
}

void gui_present(void) {
    if (!active || !device) return;
    int pending = paint_pending();
    /* Sin dano y con el puntero quieto no hay nada que volcar: redibujarlo
     * sobre si mismo solo gasta tiempo de framebuffer y parpadea. */
    if (!pending && last_mouse_x == cursor_drawn_x && last_mouse_y == cursor_drawn_y) return;
    /* El cursor vive solo en el dispositivo: se repone siempre el fondo de su
     * posicion anterior antes de pintarlo en la nueva, incluso si el volcado
     * es directo. Sin esto queda un rastro del puntero viejo. */
    cursor_restore();
    if (pending) {
        flush_region(paint_x0, paint_y0, paint_x1, paint_y1);
        paint_clear();
    }
    draw_cursor_on_device(last_mouse_x, last_mouse_y);
    cursor_drawn_x = last_mouse_x;
    cursor_drawn_y = last_mouse_y;
}

/* Espera la proxima interrupcion en vez de girar en vacio: el PIT a 100 Hz
 * marca el ritmo y las IRQ de teclado y raton despiertan antes si hay eventos. */
static void idle_cpu(void) {
    __asm__ volatile("sti; hlt" ::: "memory");
}

/* --- Distribución ------------------------------------------------------- */

static int dock_button_rect(int index, int *x, int *y, int *width, int *height) {
    const gui_font_t *small = font_small;
    int label_height = small->height;
    int button_height = icon_size + label_height + margin * 2 + 4;
    int total = 0;
    int widths[DOCK_ITEMS + 1];
    for (int i = 0; i < DOCK_ITEMS; i++) {
        int label = gui_text_width(dock_items[i].label, small);
        widths[i] = (icon_size > label ? icon_size : label) + margin * 2;
        total += widths[i] + margin;
    }
    const char *exit_label = "Salir";
    int exit_width = gui_text_width(exit_label, small) + margin * 3;
    total += exit_width;
    if (index == DOCK_ITEMS) {
        *x = back.width - exit_width - margin;
        *y = dock_y + (dock_height - button_height) / 2;
        *width = exit_width;
        *height = button_height;
        return 1;
    }
    int start = (back.width - total) / 2;
    int cursor = start;
    for (int i = 0; i < DOCK_ITEMS; i++) {
        if (i == index) {
            *x = cursor;
            *y = dock_y + (dock_height - button_height) / 2;
            *width = widths[i];
            *height = button_height;
            return 1;
        }
        cursor += widths[i] + margin;
    }
    return 0;
}

static void window_default(gui_window_t *window, int kind, int slot) {
    int w = back.width / 3;
    int h = content_height * 3 / 5;
    if (kind == WIN_APPS) { w = back.width / 4; h = content_height * 2 / 5; }
    if (kind == WIN_SYSTEM) { w = back.width / 3; h = content_height * 4 / 5; }
    if (kind == WIN_FILES) { w = back.width * 2 / 5; h = content_height * 3 / 4; }
    if (kind == WIN_IMAGES) { w = back.width / 2; h = content_height * 3 / 4; }
    int step = margin * 4;
    window->used = 1;
    window->kind = kind;
    window->width = w;
    window->height = h;
    window->x = margin + slot * step;
    window->y = content_y + margin + slot * step;
    if (window->x + window->width > back.width - margin) window->x = back.width - window->width - margin;
    if (window->y + window->height > dock_y - margin) window->y = dock_y - window->height - margin;
    window->z = ++z_top;
}

static gui_window_t *window_for(int kind) {
    for (int i = 0; i < GUI_WINDOW_COUNT; i++)
        if (windows[i].used && windows[i].kind == kind) return &windows[i];
    return 0;
}

static int window_content_top(const gui_window_t *window) { return window->y + title_height; }
static int window_content_x(const gui_window_t *window) { return window->x + 1; }
static int window_content_width(const gui_window_t *window) { return window->width - 2; }
static int window_content_height(const gui_window_t *window) { return window->height - title_height - 1; }

static int hit(int x, int y, int width, int height, int px, int py) {
    return px >= x && py >= y && px < x + width && py < y + height;
}

static void invalidate_window(const gui_window_t *window);

static gui_window_t *focused_window(void) {
    if (focus_slot < 0 || focus_slot >= GUI_WINDOW_COUNT) return 0;
    return windows[focus_slot].used ? &windows[focus_slot] : 0;
}

/* Activar una ventana cambia el resaltado del titulo y, con el, a quien va el
 * teclado; el cambio se deja por serie para las pruebas sin capturas. */
static void set_focus(int slot) {
    if (slot == focus_slot) return;
    gui_window_t *previous = focused_window();
    focus_slot = slot;
    gui_window_t *current = focused_window();
    if (previous) invalidate_window(previous);
    if (current) {
        invalidate_window(current);
        console_write("GUI: VENTANA ACTIVA ");
        console_write(current->title);
        console_putc('\n');
    } else {
        console_write("GUI: SIN VENTANA ACTIVA\n");
    }
}

/* Al cerrar la ventana activa hereda el foco la que queda mas arriba. */
static void focus_topmost(void) {
    int best = -1, best_z = 0;
    for (int i = 0; i < GUI_WINDOW_COUNT; i++)
        if (windows[i].used && windows[i].z > best_z) { best_z = windows[i].z; best = i; }
    set_focus(best);
}

/* Subir una ventana cambia el orden z, asi que hay que repintar el area de
 * trabajo; si ya estaba arriba no se toca nada. Una ventana nueva llega con
 * z == 0, el mismo valor que z_top antes de abrir la primera: comparar sin
 * mas la dejaria sin z y redraw_region() nunca la pintaria. Subirla tambien la
 * activa: el clic que la trae al frente es la forma de elegir donde se trabaja. */
static void raise_window(gui_window_t *window) {
    set_focus((int)(window - windows));
    if (window->z > 0 && window->z == z_top) return;
    window->z = ++z_top;
    invalidate(0, content_y, back.width, content_height);
}

/* --- Contenido de ventanas ---------------------------------------------- */

/* Copia 'source' en 'out' cortando por ancho de texto y sin partir un
 * caracter UTF-8: ningun valor se sale de su columna. */
static void fit_text(char *out, size_t capacity, const char *source, int max_width, const gui_font_t *font) {
    if (!capacity) return;
    out[0] = 0;
    size_t length = slen(source), position = 0, used = 0;
    int width = 0;
    while (position < length) {
        uint32_t codepoint;
        size_t bytes;
        if (utf8_decode((const uint8_t *)source + position, length - position, &codepoint, &bytes) != 1) {
            codepoint = 0x3f; bytes = 1;
        }
        const gui_glyph_t *glyph = glyph_for(font, codepoint);
        int advance = glyph ? glyph->advance : 0;
        if (width + advance > max_width || used + bytes + 1 > capacity) break;
        for (size_t i = 0; i < bytes; i++) out[used++] = source[position + i];
        position += bytes;
        width += advance;
    }
    out[used] = 0;
}

static void draw_value_row(int x, int y, int width, const char *label, const char *value) {
    const gui_font_t *small = font_small;
    int label_width = gui_text_width(label, small);
    gui_text(x, y + small->ascent, label, palette.text_dim, small);
    char fitted[64];
    int available = width - label_width - margin * 2;
    fit_text(fitted, sizeof(fitted), value, available > 0 ? available : 0, small);
    gui_text(x + width - gui_text_width(fitted, small), y + small->ascent, fitted, palette.text, small);
}

static void format_uptime(char *out, size_t capacity) {
    uint64_t total = timer_ticks() / 100;      /* PIT a 100 Hz */
    uint64_t hours = total / 3600, minutes = (total / 60) % 60, seconds = total % 60;
    size_t used = u64_text(out, capacity, hours, 10);
    if (used + 6 >= capacity) return;
    out[used++] = ':';
    out[used++] = (char)('0' + (int)(minutes / 10));
    out[used++] = (char)('0' + (int)(minutes % 10));
    out[used++] = ':';
    out[used++] = (char)('0' + (int)(seconds / 10));
    out[used++] = (char)('0' + (int)(seconds % 10));
    out[used] = 0;
}

static void format_mib(char *out, size_t capacity, uint64_t pages) {
    size_t used = u64_text(out, capacity, pages * 4, 10);
    copystr(out + used, " MiB", capacity - used);
}

/* La ventana Sistema resume CPU, memoria, tareas, red y entrada en una sola
 * columna; cada fila se omite si ya no cabe en la ventana. */
static void draw_system_content(int x, int y, int width, int height) {
    const gui_font_t *small = font_small;
    int row = small->height + small->line_gap + 3;
    int cursor = y + margin;
    char buffer[64];
    #define SYSTEM_ROW(text, label, value) do { \
        (void)(text); \
        if (cursor + small->height <= y + height) draw_value_row(x, cursor, width, label, value); \
        cursor += row; \
    } while (0)
    cpu_brand(buffer, sizeof(buffer));
    if (!buffer[0]) cpu_vendor(buffer, sizeof(buffer));
    SYSTEM_ROW(0, "CPU", buffer);
    u64_text(buffer, sizeof(buffer), cpu_cores(), 10);
    SYSTEM_ROW(0, "Nucleos", buffer);
    pmm_info_t memory = pmm_info();
    u64_text(buffer, sizeof(buffer), memory.total_pages * 4, 10);
    size_t used = slen(buffer);
    copystr(buffer + used, " MiB", sizeof(buffer) - used);
    SYSTEM_ROW(0, "RAM detectada", buffer);
    format_mib(buffer, sizeof(buffer), memory.free);
    SYSTEM_ROW(0, "RAM libre", buffer);
    heap_info_t heap = heap_info();
    u64_text(buffer, sizeof(buffer), heap.used / 1024, 10);
    used = slen(buffer);
    buffer[used++] = '/';
    u64_text(buffer + used, sizeof(buffer) - used, heap.pages * 4, 10);
    used = slen(buffer);
    buffer[used++] = 'K';
    buffer[used] = 0;
    SYSTEM_ROW(0, "Heap", buffer);
    tasks_info_t tasks = tasks_info();
    u64_text(buffer, sizeof(buffer), tasks.processes, 10);
    used = slen(buffer);
    buffer[used++] = '/';
    u64_text(buffer + used, sizeof(buffer) - used, tasks.threads, 10);
    SYSTEM_ROW(0, "Procesos/Hilos", buffer);
    format_uptime(buffer, sizeof(buffer));
    SYSTEM_ROW(0, "Uptime", buffer);
    u64_text(buffer, sizeof(buffer), (uint64_t)back.width, 10);
    used = slen(buffer);
    buffer[used++] = 'x';
    u64_text(buffer + used, sizeof(buffer) - used, (uint64_t)back.height, 10);
    SYSTEM_ROW(0, "Resolucion", buffer);
    net_info_t net = net_status();
    if (net.present) { char ip[20]; net_ip_text(ip, sizeof(ip)); SYSTEM_ROW(0, "Red", ip); }
    else SYSTEM_ROW(0, "Red", "sin NIC");
    mouse_state_t mouse = mouse_state();
    SYSTEM_ROW(0, "Raton", mouse.ready ? "activo" : "ausente");
    SYSTEM_ROW(0, "Teclado", keyboard_layout_name());
    draw_value_row(x, cursor, width, "Despachos", text_u64(tasks.dispatches));
    cursor += row;
    const fs_node_t *nodes = fs_nodes();
    unsigned stored = 0;
    for (int i = 0; i < FS_MAX_NODES; i++) if (nodes[i].type != FS_TYPE_FREE) stored++;
    u64_text(buffer, sizeof(buffer), stored, 10);
    SYSTEM_ROW(0, "Archivos", buffer);
    #undef SYSTEM_ROW
}

/* Filas del explorador: -1 representa ".." (subir) y el resto son indices
 * de fs_nodes() hijos del directorio actual. */
static int files_rows(int *ids, int capacity) {
    int count = 0;
    const fs_node_t *nodes = fs_nodes();
    int cwd = fs_cwd();
    if (cwd != 0 && count < capacity) ids[count++] = -1;
    for (int i = 0; i < FS_MAX_NODES && count < capacity; i++) {
        if (i == cwd || nodes[i].type == FS_TYPE_FREE) continue;
        if (nodes[i].parent_idx != cwd) continue;
        ids[count++] = i;
    }
    return count;
}

static int files_row_height(void) { return font_small->height + font_small->line_gap + 4; }

/* Zonas del explorador: cabecera con la ruta, listado/visor, acciones y
 * linea de estado. Las filas visibles salen del espacio restante. */
typedef struct {
    int x, y, width, height, header, body, body_end, rows, actions;
} files_layout_t;

static int files_action_height(void) { return font_small->height + margin * 2; }
static int files_status_height(void) { return font_small->height + 4; }

static void files_layout(const gui_window_t *window, files_layout_t *out) {
    out->x = window_content_x(window) + margin;
    out->y = window_content_top(window) + margin;
    out->width = window_content_width(window) - margin * 2;
    out->height = window_content_height(window) - margin * 2;
    if (out->width < 0) out->width = 0;
    if (out->height < 0) out->height = 0;
    out->header = out->y + margin + font_small->height + font_small->line_gap + 4;
    out->body = out->header + 6;
    out->actions = out->y + out->height - files_action_height() - files_status_height();
    out->body_end = out->actions - 6;
    out->rows = (out->body_end - out->body) / files_row_height();
    if (out->rows < 1) out->rows = 1;
}

/* --- Barra de desplazamiento vertical ------------------------------------ */

#define SCROLLBAR_WIDTH 8

static void draw_scrollbar(int x, int y, int width, int height, int total, int visible, int first) {
    if (total <= visible || height <= 0) return;
    gui_fill(x, y, width, height, palette.panel_alt);
    gui_frame(x, y, width, height, palette.border);
    int thumb = height * visible / total;
    if (thumb < 18) thumb = 18;
    if (thumb > height) thumb = height;
    int span = height - thumb;
    int max_first = total - visible;
    int offset = max_first > 0 ? span * first / max_first : 0;
    if (offset < 0) offset = 0;
    if (offset > span) offset = span;
    gui_round(x + 1, y + offset + 1, width - 2, thumb - 2, 2, palette.accent_dim);
}

/* Primera fila que corresponde a un clic o arrastre en la pista. */
static int scrollbar_first(int y, int height, int total, int visible, int py) {
    int thumb = height * visible / total;
    if (thumb < 18) thumb = 18;
    if (thumb > height) thumb = height;
    int span = height - thumb;
    int max_first = total - visible;
    int offset = py - y - thumb / 2;
    if (offset < 0) offset = 0;
    if (offset > span) offset = span;
    return span > 0 ? max_first * offset / span : 0;
}

/* Lineas que ocupa el archivo abierto en el visor (al menos una). */
static int files_viewer_lines(int node_index) {
    const char *content = fs_nodes()[node_index].content;
    int lines = 1;
    for (const char *cursor = content; *cursor; cursor++) if (*cursor == '\n') lines++;
    return lines;
}

/* Devuelve 1 si la barra es necesaria y deja su rectangulo y el estado de
 * desplazamiento (listado o visor). */
static int files_scrollbar_geometry(const files_layout_t *layout, int *bx, int *by, int *bw, int *bh,
                                    int *total, int *visible, int *first) {
    int count = files_rows(files_ids, FS_MAX_NODES);
    int id = (files.selected >= 0 && files.selected < count) ? files_ids[files.selected] : -1;
    *bx = layout->x + layout->width - SCROLLBAR_WIDTH;
    *by = layout->body;
    *bw = SCROLLBAR_WIDTH;
    *bh = layout->body_end - layout->body;
    if (*bh <= 0) return 0;
    if (files.viewer && id >= 0) {
        *total = files_viewer_lines(id);
        *first = files.view_top;
    } else {
        *total = count;
        *first = files.scroll;
    }
    *visible = layout->rows;
    return *total > *visible;
}

/* --- Botones de la ventana Archivos ------------------------------------- */

enum { FILES_BTN_NEW = 0, FILES_BTN_DIR, FILES_BTN_RENAME, FILES_BTN_DELETE, FILES_BTN_EDIT, FILES_BTN_COUNT };

static const char *files_button_label(int index) {
    static const char *labels[FILES_BTN_COUNT] = { "Nuevo", "Carpeta", "Renombrar", "Borrar", "Editar" };
    return (index >= 0 && index < FILES_BTN_COUNT) ? labels[index] : "";
}

static int files_button_rect(const files_layout_t *layout, int index, int *bx, int *by, int *bw, int *bh) {
    int cursor = layout->x, height = font_small->height + margin;
    for (int i = 0; i < FILES_BTN_COUNT; i++) {
        int width = gui_text_width(files_button_label(i), font_small) + margin * 3;
        if (cursor + width > layout->x + layout->width) return 0;
        if (i == index) {
            *bx = cursor; *by = layout->actions + 2; *bw = width; *bh = height;
            return 1;
        }
        cursor += width + margin;
    }
    return 0;
}

static void draw_files_actions(const files_layout_t *layout, int file_selected,
                               int has_selection, int locked_selected, int locked_dir) {
    mouse_state_t mouse = mouse_state();
    for (int i = 0; i < FILES_BTN_COUNT; i++) {
        int x, y, w, h;
        if (!files_button_rect(layout, i, &x, &y, &w, &h)) continue;
        int enabled = 1;
        if (i == FILES_BTN_EDIT) enabled = file_selected;
        else if (i == FILES_BTN_RENAME || i == FILES_BTN_DELETE) enabled = has_selection && !locked_selected;
        else enabled = !locked_dir;
        int hovered = enabled && hit(x, y, w, h, mouse.x, mouse.y);
        gui_round(x, y, w, h, 4, hovered ? palette.accent_soft : palette.panel_alt);
        gui_frame(x, y, w, h, hovered ? palette.accent : palette.border);
        gui_text_center(x + w / 2, y + (h - font_small->height) / 2 + font_small->ascent,
                        files_button_label(i), enabled ? palette.text : palette.text_dim, font_small);
    }
}

/* Ruta absoluta del nodo marcado; se define junto a las acciones, mas abajo. */
static void files_node_path(int id, char *out, size_t capacity);

/* Visor de texto: muestra el contenido del nodo desde la primera linea
 * visible, sin copiar el archivo entero en cada repintado. */
static void draw_files_viewer(int x, int y, int width, int height, int node_index, int rows) {
    const gui_font_t *small = font_small;
    const fs_node_t *nodes = fs_nodes();
    int line = files_row_height();
    (void)height;
    gui_text(x, y + small->ascent, nodes[node_index].name, palette.text, small);
    int top = y + small->height + small->line_gap + 6;
    gui_fill(x, top, width, 1, palette.border);
    top += 6;
    if (rows < 1) return;
    const char *cursor = nodes[node_index].content;
    int skipped = 0;
    while (*cursor && skipped < files.view_top) { if (*cursor == '\n') skipped++; cursor++; }
    for (int row = 0; row < rows && *cursor; row++) {
        char buffer[96];
        size_t used = 0;
        while (*cursor && *cursor != '\n' && used + 1 < sizeof(buffer)) buffer[used++] = *cursor++;
        if (*cursor == '\n') cursor++;
        buffer[used] = 0;
        gui_text(x, top + row * line + small->ascent, buffer, palette.text, small);
    }
}

static void draw_files_content(const gui_window_t *window) {
    files_layout_t layout;
    files_layout(window, &layout);
    const gui_font_t *small = font_small;
    if (layout.width <= 0 || layout.height <= 0) return;
    int x = layout.x, width = layout.width;
    char path[FS_PATH_MAX];
    fs_get_cwd_path(path, sizeof(path));
    gui_text(x, layout.y + margin + small->ascent, path, palette.accent_dim, small);
    gui_fill(x, layout.header, width, 1, palette.border);

    int count = files_rows(files_ids, FS_MAX_NODES);
    if (files.selected >= count) files.selected = count ? count - 1 : 0;
    if (files.selected < 0) files.selected = 0;
    int rows = layout.rows;
    int line = files_row_height();
    int id = (files.selected < count) ? files_ids[files.selected] : -1;

    if (files.viewer && id >= 0) {
        draw_files_viewer(x, layout.body, width - SCROLLBAR_WIDTH - 4, layout.body_end - layout.body, id, rows);
    } else {
        if (files.selected < files.scroll) files.scroll = files.selected;
        if (files.selected >= files.scroll + rows) files.scroll = files.selected - rows + 1;
        if (files.scroll < 0) files.scroll = 0;
        const fs_node_t *nodes = fs_nodes();
        for (int row = 0; row < rows; row++) {
            int position = files.scroll + row;
            if (position >= count) break;
            int node = files_ids[position];
            int ry = layout.body + row * line;
            int is_dir = node < 0 || nodes[node].type == FS_TYPE_DIR;
            int internal = node >= 0 && fs_is_system(node);
            if (position == files.selected)
                gui_round(x - margin / 2, ry - 2, width + margin, line, radius / 2, palette.accent_soft);
            gui_icon(is_dir ? GUI_ICON_FOLDER : (internal ? GUI_ICON_INFO : GUI_ICON_TERMINAL), x, ry + 1,
                     small->height - 2,
                     is_dir ? palette.accent : (internal ? palette.warn : palette.text_dim));
            gui_text(x + small->height + 6, ry + small->ascent, node < 0 ? ".." : nodes[node].name,
                     is_dir ? palette.accent_dim : (internal ? palette.warn : palette.text), small);
            if (node >= 0 && nodes[node].type == FS_TYPE_FILE) {
                const char *size = text_u64(nodes[node].size);
                int size_x = x + width - SCROLLBAR_WIDTH - gui_text_width(size, small) - 4;
                gui_text(size_x, ry + small->ascent, size, palette.text_dim, small);
                /* RO: archivo interno que se puede leer pero no modificar. */
                if (internal)
                    gui_text(size_x - gui_text_width("RO", small) - margin, ry + small->ascent,
                             "RO", palette.warn, small);
            }
        }
    }

    int bx, by, bw, bh, total, visible, first;
    if (files_scrollbar_geometry(&layout, &bx, &by, &bw, &bh, &total, &visible, &first))
        draw_scrollbar(bx, by, bw, bh, total, visible, first);

    int file_selected = id >= 0 && fs_nodes()[id].type == FS_TYPE_FILE;
    draw_files_actions(&layout, file_selected, id >= 0, id >= 0 && fs_is_system(id), fs_is_system(fs_cwd()));
    int status_y = layout.actions + files_action_height();
    char hint[96];
    if (files.action) {
        const char *prompt = files.action == FILES_NEW_FILE ? "Nuevo archivo: " :
                             files.action == FILES_NEW_DIR ? "Nueva carpeta: " : "Nuevo nombre: ";
        size_t used = 0;
        while (prompt[used] && used + 1 < sizeof(hint)) { hint[used] = prompt[used]; used++; }
        for (size_t i = 0; files.input[i] && used + 1 < sizeof(hint); i++) hint[used++] = files.input[i];
        hint[used++] = '_';
        hint[used] = 0;
        gui_text(x, status_y + small->ascent, hint, palette.accent_dim, small);
        return;
    }
    /* Sin mensaje se muestra la ruta absoluta del elemento marcado: cada fila
     * del explorador dice donde vive dentro del sistema. */
    if (!files.message[0] && id >= 0) {
        char location[FS_PATH_MAX];
        files_node_path(id, location, sizeof(location));
        fit_text(hint, sizeof(hint), location, width, small);
        gui_text(x, status_y + small->ascent, hint, palette.accent_dim, small);
        return;
    }
    fit_text(hint, sizeof(hint),
             files.message[0] ? files.message : "Flechas mueven  ENTER abre  E edita  D borra",
             width, small);
    gui_text(x, status_y + small->ascent, hint, palette.text_dim, small);
}

static void draw_net_content(int x, int y, int width, int height) {
    const gui_font_t *small = font_small;
    int row = small->height + small->line_gap + 4;
    int cursor = y + margin;
    int bottom = y + height - margin;
    net_info_t net = net_status();
    char mac[24], ip[20], gateway[20], dns[20], ping[48];
    net_mac_text(mac, sizeof(mac));
    net_ip_text(ip, sizeof(ip));
    net_format_ip(net.gateway, gateway, sizeof(gateway));
    net_format_ip(net.dns, dns, sizeof(dns));
    if (!net.present) {
        gui_text(x, cursor + small->ascent, "Sin RTL8139 en el bus PCI", palette.danger, small);
        cursor += row;
        gui_text(x, cursor + small->ascent, "QEMU: -device rtl8139,netdev=...", palette.text_dim, small);
        return;
    }
    draw_value_row(x, cursor, width, "MAC", mac); cursor += row;
    draw_value_row(x, cursor, width, "IP", ip); cursor += row;
    draw_value_row(x, cursor, width, "Puerta", gateway); cursor += row;
    draw_value_row(x, cursor, width, "DNS", dns); cursor += row;
    draw_value_row(x, cursor, width, "Enlace", net.link ? "activo" : "sin enlace"); cursor += row;
    draw_value_row(x, cursor, width, "ARP", net.arp_ok ? "resuelto" : "pendiente"); cursor += row;
    if (net.pings == 0) {
        copystr(ping, "sin pruebas (RED PING)", sizeof(ping));
    } else if (net.ping_ok) {
        char address[20];
        net_format_ip(net.ping_ip, address, sizeof(address));
        copystr(ping, address, sizeof(ping));
        size_t used = slen(ping);
        if (net.ping_ms == 0) {
            if (used + 6 < sizeof(ping)) copystr(ping + used, " <10ms", sizeof(ping) - used);
        } else if (used + 8 < sizeof(ping)) {
            ping[used++] = ' ';
            const char *ms = text_u64(net.ping_ms);
            for (size_t i = 0; ms[i] && used + 3 < sizeof(ping); i++) ping[used++] = ms[i];
            ping[used++] = 'm'; ping[used++] = 's'; ping[used] = 0;
        }
    } else {
        copystr(ping, "sin respuesta", sizeof(ping));
    }
    if (cursor + row < bottom) { draw_value_row(x, cursor, width, "Ping", ping); cursor += row; }
    if (cursor + row < bottom) { draw_value_row(x, cursor, width, "Resuelto", net.resolved_ok ? net.resolved_name : "-"); cursor += row; }
    if (cursor + row < bottom) { draw_value_row(x, cursor, width, "TX", text_u64(net.tx_frames)); cursor += row; }
    if (cursor + row < bottom) draw_value_row(x, cursor, width, "RX", text_u64(net.rx_frames));
}

static void draw_input_content(int x, int y, int width, int height) {
    const gui_font_t *small = font_small;
    int row = small->height + small->line_gap + 4;
    int cursor = y + margin;
    mouse_state_t mouse = mouse_state();
    char buffer[40];
    u64_text(buffer, sizeof(buffer), (uint64_t)mouse.x, 10);
    size_t used = slen(buffer);
    buffer[used++] = ',';
    u64_text(buffer + used, sizeof(buffer) - used, (uint64_t)mouse.y, 10);
    draw_value_row(x, cursor, width, "Puntero", buffer); cursor += row;
    draw_value_row(x, cursor, width, "Boton", (mouse.buttons & 1) ? "izquierdo" : "-"); cursor += row;
    draw_value_row(x, cursor, width, "Teclado", keyboard_layout_name()); cursor += row;
    if (last_key > 0 && cursor + row < y + height) {
        buffer[0] = (char)(last_key >= 32 && last_key < 127 ? last_key : '.');
        buffer[1] = 0;
        draw_value_row(x, cursor, width, "Ultima tecla", buffer);
    }
}

static void draw_apps_content(int x, int y, int width, int height) {
    const gui_font_t *small = font_small;
    (void)width;
    int row = small->height + small->line_gap + 6;
    int cursor = y + margin;
    unsigned count = elf_count();
    if (!count) {
        gui_text(x, cursor + small->ascent, "Sin aplicaciones ELF", palette.text_dim, small);
        cursor += row;
        gui_text(x, cursor + small->ascent, "make apps  (modulos Limine)", palette.text_dim, small);
        return;
    }
    for (unsigned i = 0; i < count && cursor + row < y + height; i++) {
        gui_icon(GUI_ICON_PLAY, x, cursor + 2, small->height - 2, palette.accent);
        gui_text(x + small->height + 6, cursor + small->ascent, elf_name(i), palette.text, small);
        cursor += row;
    }
}

/* --- Visor de imagenes --------------------------------------------------- */

static void invalidate_window(const gui_window_t *window);

static void images_invalidate(void) {
    gui_window_t *window = window_for(WIN_IMAGES);
    if (window) invalidate_window(window);
}

/* Cambia la imagen marcada y deja el cambio por serie: las pruebas no
 * necesitan capturas para saber que se mostro. */
static void images_select(int index) {
    int count = images_count();
    if (index < 0) index = 0;
    if (index >= count) index = count ? count - 1 : 0;
    images_selected = index;
    if (count) {
        console_write("GUI: IMAGEN ");
        console_write(images_name(index));
        console_write(" ");
        console_dec((uint64_t)images_width(index));
        console_write("x");
        console_dec((uint64_t)images_height(index));
        console_putc('\n');
    }
    images_invalidate();
}

/* Devuelve 1 si consume la tecla; ESC se deja al escritorio. */
static int images_key(int key) {
    if (!window_for(WIN_IMAGES)) return 0;
    int count = images_count();
    if (key == KB_KEY_UP && images_selected > 0) { images_select(images_selected - 1); return 1; }
    if (key == KB_KEY_DOWN && images_selected + 1 < count) { images_select(images_selected + 1); return 1; }
    if (key == 27) return 0;
    return 1;
}

static int images_list_width(int width) {
    int list = width / 3;
    return list < 150 ? 150 : list;
}

static void draw_images_content(int x, int y, int width, int height) {
    const gui_font_t *small = font_small;
    int count = images_count();
    char buffer[72];
    gui_text(x, y + small->ascent, "Imagenes empaquetadas", palette.accent_dim, small);
    gui_fill(x, y + small->height + 4, width, 1, palette.border);
    int top = y + small->height + 10;
    if (!count) {
        gui_text(x, top + small->ascent, "Sin imagenes: copia images/*.jpg y ejecuta make iso.",
                 palette.text_dim, small);
        return;
    }
    if (images_selected < 0 || images_selected >= count) images_selected = 0;
    int row = small->height + small->line_gap + 4;
    int list_width = images_list_width(width);
    for (int i = 0; i < count; i++) {
        int line_y = top + i * row;
        if (i == images_selected)
            gui_round(x - margin / 2, line_y - 2, list_width + margin, row, radius / 2, palette.accent_soft);
        gui_text(x, line_y + small->ascent, images_name(i),
                 i == images_selected ? palette.text : palette.text_dim, small);
        u64_text(buffer, sizeof(buffer), (uint64_t)images_width(i), 10);
        size_t used = slen(buffer);
        buffer[used++] = 'x';
        u64_text(buffer + used, sizeof(buffer) - used, (uint64_t)images_height(i), 10);
        gui_text(x + list_width - gui_text_width(buffer, small), line_y + small->ascent,
                 buffer, palette.text_dim, small);
    }
    /* Vista previa: escala entera por muestreo, centrada y sin deformar. */
    int view_x = x + list_width + margin, view_y = top;
    int view_w = width - list_width - margin;
    int view_h = height - (top - y) - small->height - 14;
    if (view_w < 40 || view_h < 40) return;
    gui_fill(view_x, view_y, view_w, view_h, palette.panel_alt);
    gui_frame(view_x, view_y, view_w, view_h, palette.border);
    int source_w = images_width(images_selected), source_h = images_height(images_selected);
    if (!source_w || !source_h) return;
    int draw_w = view_w - 8;
    int draw_h = draw_w * source_h / source_w;
    if (draw_h > view_h - 8) {
        draw_h = view_h - 8;
        draw_w = draw_h * source_w / source_h;
    }
    int origin_x = view_x + (view_w - draw_w) / 2;
    int origin_y = view_y + (view_h - draw_h) / 2;
    const uint16_t *pixels = images_data(images_selected);
    clip_save();
    gui_set_clip(origin_x, origin_y, draw_w, draw_h);
    for (int line_y = 0; line_y < draw_h; line_y++) {
        int source_y = line_y * source_h / draw_h;
        const uint16_t *source = pixels + (size_t)source_y * (size_t)source_w;
        uint32_t *destination = draw_target->pixels +
            (size_t)(origin_y + line_y) * (size_t)draw_target->stride + (size_t)origin_x;
        for (int column = 0; column < draw_w; column++)
            destination[column] = images_rgb(source[column * source_w / draw_w]);
    }
    clip_restore();
    gui_frame(origin_x - 1, origin_y - 1, draw_w + 2, draw_h + 2, palette.border);
    /* Pie: ruta real del archivo dentro del sistema y ayuda de teclas. Cada
     * imagen del blob tiene su ficha en /wallpapers, asi el visor y el
     * explorador hablan del mismo archivo. */
    char location[96];
    copystr(location, "/wallpapers/", sizeof(location));
    size_t used = slen(location);
    copystr(location + used, images_name(images_selected), sizeof(location) - used);
    used = slen(location);
    copystr(location + used, images_extension(images_selected), sizeof(location) - used);
    gui_text(x, y + height - small->height - 2, location, palette.accent_dim, small);
    gui_text(x + width - gui_text_width("Flechas cambian  ESC cierra", small),
             y + height - small->height - 2, "Flechas cambian  ESC cierra", palette.text_dim, small);
}

static void draw_window_content(const gui_window_t *window) {
    int x = window_content_x(window) + margin;
    int y = window_content_top(window) + margin;
    int width = window_content_width(window) - margin * 2;
    int height = window_content_height(window) - margin * 2;
    if (width <= 0 || height <= 0) return;
    switch (window->kind) {
    case WIN_SYSTEM: draw_system_content(x, y, width, height); break;
    case WIN_FILES: draw_files_content(window); break;
    case WIN_NET: draw_net_content(x, y, width, height); break;
    case WIN_INPUT: draw_input_content(x, y, width, height); break;
    case WIN_APPS: draw_apps_content(x, y, width, height); break;
    case WIN_IMAGES: draw_images_content(x, y, width, height); break;
    case WIN_APP:
        if (app_surface.pixels) {
            clip_save();
            gui_set_clip(x, y, width, height);
            int copy_w = width < app_surface.width ? width : app_surface.width;
            int copy_h = height < app_surface.height ? height : app_surface.height;
            for (int row = 0; row < copy_h; row++) {
                uint32_t *destination = draw_target->pixels +
                    (size_t)(y + row) * (size_t)draw_target->stride + (size_t)x;
                const uint32_t *source = app_surface.pixels + (size_t)row * (size_t)app_surface.stride;
                for (int column = 0; column < copy_w; column++) destination[column] = source[column];
            }
            clip_restore();
        }
        break;
    default: break;
    }
}

static void draw_window(const gui_window_t *window) {
    int x = window->x, y = window->y, w = window->width, h = window->height;
    /* Solo la ventana activa lleva el titulo y el marco en color de acento:
     * es la pista de a donde van las teclas. */
    int focused = (int)(window - windows) == focus_slot;
    uint32_t title_face = focused ? palette.accent_soft : palette.panel_alt;
    uint32_t border = focused ? palette.accent : palette.border;
    /* Sombra opaca de un pixel: mezclar la ventana entera con gui_blend, canal
     * a canal, era el mayor coste de cada frame. Se redondea igual que el panel
     * para que la esquina no asome cuadrada junto al marco. */
    gui_round(x + margin / 2, y + margin, w, h, radius, palette.border);
    gui_round(x, y, w, h, radius, palette.panel);
    gui_round(x, y, w, title_height, radius, title_face);
    gui_fill(x, y + title_height - radius, w, radius, title_face);
    gui_fill(x, y + title_height - 1, w, 1, border);
    gui_icon(window->kind == WIN_APP ? GUI_ICON_PLAY :
             window->kind == WIN_FILES ? GUI_ICON_FOLDER :
             window->kind == WIN_IMAGES ? GUI_ICON_IMAGE :
             window->kind == WIN_NET ? GUI_ICON_GLOBE :
             window->kind == WIN_APPS ? GUI_ICON_PLAY :
             window->kind == WIN_INPUT ? GUI_ICON_TERMINAL : GUI_ICON_MONITOR,
             x + margin, y + (title_height - icon_size) / 2, icon_size,
             focused ? palette.accent_dim : palette.accent);
    gui_text(x + margin + icon_size + margin / 2,
             y + (title_height - font_body->height) / 2 + font_body->ascent,
             window->title, focused ? palette.accent_dim : palette.text, font_body);
    int close_radius = title_height / 4 + 2;
    int close_x = x + w - margin - close_radius * 2;
    int close_y = y + (title_height - close_radius * 2) / 2;
    gui_circle(close_x + close_radius, close_y + close_radius, close_radius, palette.danger);
    int cross = close_radius * 3 / 2;
    gui_icon(GUI_ICON_CLOSE, close_x + close_radius - cross / 2, close_y + close_radius - cross / 2,
             cross, palette.white);
    draw_window_content(window);
    /* El marco sigue la silueta redondeada del panel: recto dejaba un pico en
     * cada esquina, mas visible aun con el azul de la ventana activa. */
    round_frame(x, y, w, h, radius, border);
}

/* Tablas de muestreo del wallpaper: para cada columna y fila de la pantalla se
 * guarda la coordenada de origen. Asi el repintado solo hace dos lecturas y una
 * conversion por pixel, sin divisiones dentro del bucle. */
static void background_build_tables(void) {
    wall_ready = 0;
    wall_index = images_wallpaper();
    if (wall_index < 0) return;
    int width = images_width(wall_index), height = images_height(wall_index);
    if (!width || !height || back.width <= 0 || back.height <= 0) return;
    if (back.width > (int)(sizeof(wall_x) / sizeof(wall_x[0])) ||
        back.height > (int)(sizeof(wall_y) / sizeof(wall_y[0]))) return;
    for (int x = 0; x < back.width; x++) wall_x[x] = (uint16_t)(x * width / back.width);
    for (int y = 0; y < back.height; y++) wall_y[y] = (uint16_t)(y * height / back.height);
    wall_ready = 1;
}

/* Color del fondo en un punto: imagen escalada o degradado. */
static uint32_t background_pixel_at(int x, int y) {
    if (wall_ready && x >= 0 && y >= 0 && x < back.width && y < back.height) {
        const uint16_t *source = images_data(wall_index);
        return images_rgb(source[(size_t)wall_y[y] * (size_t)images_width(wall_index) +
                                  (size_t)wall_x[x]]);
    }
    return gradient[(unsigned)(y * 255 / (back.height ? back.height : 1)) & 255u];
}

/* Fondo del rectangulo danado. Con imagen se copia el wallpaper escalado; sin
 * imagenes empaquetadas se conserva el degradado precalculado. */
static void draw_background_rect(int x0, int y0, int x1, int y1) {
    if (x1 > back.width) x1 = back.width;
    if (y1 > back.height) y1 = back.height;
    const uint16_t *wall = wall_ready ? images_data(wall_index) : 0;
    int wall_width = wall_ready ? images_width(wall_index) : 0;
    for (int row = y0; row < y1; row++) {
        uint32_t *line = back.pixels + (size_t)row * (size_t)back.stride;
        if (wall) {
            const uint16_t *source = wall + (size_t)wall_y[row] * (size_t)wall_width;
            for (int column = x0; column < x1; column++) line[column] = images_rgb(source[wall_x[column]]);
            continue;
        }
        uint32_t rgb = gradient[(unsigned)(row * 255 / (back.height ? back.height : 1)) & 255u];
        for (int column = x0; column < x1; column++) line[column] = rgb;
    }
}

/* Reloj de la barra. El PIT marca cuando releer el CMOS (una vez por segundo)
 * y solo se invalida la barra cuando cambia el minuto. */
static char clock_text[8] = "--:--";
static int clock_valid;
static int clock_minute = -1;
static uint64_t clock_second = ~(uint64_t)0;

static void clock_refresh(void) {
    uint64_t second = timer_ticks() / 100;      /* PIT a 100 Hz */
    if (second == clock_second) return;
    clock_second = second;
    char text[8];
    if (!rtc_text(text, sizeof(text))) { clock_valid = 0; return; }
    clock_valid = 1;
    copystr(clock_text, text, sizeof(clock_text));
    int minute = ((text[0] - '0') * 10 + (text[1] - '0')) * 60 +
                 (text[3] - '0') * 10 + (text[4] - '0');
    if (minute == clock_minute) return;
    clock_minute = minute;
    invalidate(0, 0, back.width, bar_height);
}

static void draw_top_bar(void) {
    gui_fill(0, 0, back.width, bar_height, palette.panel);
    gui_fill(0, bar_height - 1, back.width, 1, palette.border);
    gui_icon(GUI_ICON_MONITOR, margin, (bar_height - icon_size) / 2, icon_size, palette.accent);
    int baseline = (bar_height - font_body->height) / 2 + font_body->ascent;
    gui_text(margin + icon_size + margin / 2, baseline, "ToolchainOS", palette.text, font_body);
    int small_baseline = (bar_height - font_small->height) / 2 + font_small->ascent;
    int right = back.width - margin;
    /* Reloj a la derecha del todo: es el dato que se consulta de un vistazo. */
    if (clock_valid) {
        int clock_width = gui_text_width(clock_text, font_small);
        gui_text(right - clock_width, small_baseline, clock_text, palette.text, font_small);
        right -= clock_width + margin * 2;
    }
    char buffer[32];
    u64_text(buffer, sizeof(buffer), (uint64_t)back.width, 10);
    size_t used = slen(buffer);
    buffer[used++] = 'x';
    u64_text(buffer + used, sizeof(buffer) - used, (uint64_t)back.height, 10);
    int width = gui_text_width(buffer, font_small);
    gui_text(right - width, small_baseline, buffer, palette.text_dim, font_small);
    right -= width + margin * 2;
    /* La barra ya no muestra las coordenadas del puntero: un valor que cambia
     * en cada movimiento obligaba a repintarla todo el tiempo. */
    char ip[20];
    net_ip_text(ip, sizeof(ip));
    width = gui_text_width(ip, font_small);
    gui_text(right - width, small_baseline, ip, palette.text_dim, font_small);
}

static void draw_dock(void) {
    gui_round(margin, dock_y, back.width - margin * 2, dock_height - margin / 2, radius, palette.panel);
    gui_frame(margin, dock_y, back.width - margin * 2, dock_height - margin / 2, palette.border);
    mouse_state_t mouse = mouse_state();
    for (int i = 0; i <= DOCK_ITEMS; i++) {
        int x, y, w, h;
        if (!dock_button_rect(i, &x, &y, &w, &h)) continue;
        int hovered = hit(x, y, w, h, mouse.x, mouse.y);
        int open = i < DOCK_ITEMS && window_for(dock_items[i].kind) != 0;
        uint32_t face = open ? palette.accent_soft : (hovered ? palette.panel_alt : palette.panel);
        gui_round(x, y, w, h, radius / 2, face);
        if (hovered || open) gui_frame(x, y, w, h, palette.accent);
        if (i == DOCK_ITEMS) {
            gui_text_center(x + w / 2, y + h / 2 + font_small->ascent / 2, "Salir",
                            palette.danger, font_small);
            continue;
        }
        const dock_item_t *item = &dock_items[i];
        int center = x + w / 2;
        gui_icon(item->icon, center - icon_size / 2, y + margin,
                 icon_size, open ? palette.accent_dim : palette.accent);
        gui_text_center(center, y + margin + icon_size + font_small->height - 2,
                        item->label, palette.text, font_small);
    }
}

/* Reconstruye solo el rectangulo indicado: fondo, ventanas en orden z, barra
 * superior y dock. El recorte deja fuera todo lo que no cambia. */
static void redraw_region(int x0, int y0, int x1, int y1) {
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > back.width) x1 = back.width;
    if (y1 > back.height) y1 = back.height;
    if (x1 <= x0 || y1 <= y0) return;
    int w = x1 - x0, h = y1 - y0;
    draw_target = &back;
    gui_set_clip(x0, y0, w, h);
    draw_background_rect(x0, y0, x1, y1);
    for (int z = 1; z <= z_top; z++)
        for (int i = 0; i < GUI_WINDOW_COUNT; i++)
            if (windows[i].used && windows[i].z == z &&
                intersects(windows[i].x, windows[i].y, windows[i].width, windows[i].height, x0, y0, w, h))
                draw_window(&windows[i]);
    if (intersects(0, 0, back.width, bar_height, x0, y0, w, h)) draw_top_bar();
    if (intersects(0, dock_y, back.width, dock_height, x0, y0, w, h)) draw_dock();
    gui_reset_clip();
}

/* Pinta en el buffer logico la union de dano pendiente; gui_present() se
 * encarga despues de volcarla al dispositivo. */
static void report_close_cleanup(void);
static void repaint_pending(void) {
    if (!paint_pending()) return;
    redraw_region(paint_x0, paint_y0, paint_x1, paint_y1);
    report_close_cleanup();
}

static void desktop_draw(void) { redraw_region(0, 0, back.width, back.height); }

/* Repintado completo que ademas marca todo como danado antes de volcar. */
static void desktop_full_present(void) {
    desktop_draw();
    invalidate(0, 0, back.width, back.height);
    gui_present();
}

static void invalidate_window(const gui_window_t *window) {
    invalidate(window->x, window->y, window->width + margin, window->height + margin);
}

/* --- Cierre de ventanas ------------------------------------------------- */

/* Cerrada la ventana hay que devolver su rectangulo al fondo: sin invalidar
 * el area, el escritorio conserva el ultimo fotograma hasta que otra accion
 * la repinte. El chequeo deja por serie si el area quedo limpia. */
static char closed_title[40];
static int close_x0, close_y0, close_x1, close_y1, close_check;

static void close_window(gui_window_t *window) {
    if (!window || !window->used) return;
    invalidate_window(window);
    close_x0 = window->x;
    close_y0 = window->y;
    close_x1 = window->x + window->width;
    close_y1 = window->y + window->height;
    close_check = 1;
    copystr(closed_title, window->title, sizeof(closed_title));
    if (window->kind == WIN_FILES) files.drag = 0;
    window->used = 0;
    if (dragging && &windows[drag_slot] == window) dragging = 0;
    /* El foco no puede quedar en una ventana cerrada: hereda la de mas arriba. */
    if (focus_slot == (int)(window - windows)) focus_topmost();
    invalidate(0, dock_y, back.width, dock_height);
}

/* El centro de la ventana cerrada debe mostrar el fondo del escritorio; si
 * otra ventana lo cubre se informa CUBIERTA en vez de dar un falso positivo. */
static void report_close_cleanup(void) {
    if (!close_check) return;
    close_check = 0;
    int cx = (close_x0 + close_x1) / 2, cy = (close_y0 + close_y1) / 2;
    if (cx < 0 || cy < 0 || cx >= back.width || cy >= back.height) return;
    int covered = 0;
    for (int i = 0; i < GUI_WINDOW_COUNT; i++)
        if (windows[i].used &&
            hit(windows[i].x, windows[i].y, windows[i].width, windows[i].height, cx, cy)) covered = 1;
    uint32_t pixel = back.pixels[(size_t)cy * (size_t)back.stride + (size_t)cx];
    uint32_t background = background_pixel_at(cx, cy);
    console_write("GUI: VENTANA CERRADA ");
    console_write(closed_title);
    if (covered) console_write(" CUBIERTA\n");
    else if (pixel == background) console_write(" LIMPIA\n");
    else console_write(" CON RESTOS\n");
}

/* --- Handoff del escritorio a las aplicaciones -------------------------- */

static char launch_path[FS_PATH_MAX];

void gui_launch_file_set(const char *path) { copystr(launch_path, path ? path : "", sizeof(launch_path)); }
const char *gui_launch_file(void) { return launch_path; }
void gui_launch_file_clear(void) { launch_path[0] = 0; }

int gui_launch_editor(const char *path) {
    int index = elf_find("editor.elf");
    if (index < 0) { console_write("GUI: EDITOR NO DISPONIBLE EN LA ISO.\n"); return 0; }
    gui_launch_file_set(path);
    return elf_run((unsigned)index);
}

/* --- Acciones de la ventana Archivos ------------------------------------ */

static gui_window_t *files_window(void) { return window_for(WIN_FILES); }

/* open_window se define mas abajo, junto a toggle_window. */
static void open_window(int kind);

static void files_touch(void) {
    gui_window_t *window = files_window();
    if (window) invalidate_window(window);
}

static void files_status(const char *text) {
    copystr(files.message, text, sizeof(files.message));
    files_touch();
}

static int files_selected_node(void) {
    int count = files_rows(files_ids, FS_MAX_NODES);
    if (files.selected < 0 || files.selected >= count) return -1;
    return files_ids[files.selected];
}

/* Ruta absoluta del nodo marcado: la necesitan RM y REN del sistema de archivos. */
static void files_node_path(int id, char *out, size_t capacity) {
    if (!capacity) return;
    char cwd_path[FS_PATH_MAX];
    fs_get_cwd_path(cwd_path, sizeof(cwd_path));
    size_t used = slen(cwd_path);
    if (used + 1 >= capacity) used = capacity - 1;
    for (size_t i = 0; i < used; i++) out[i] = cwd_path[i];
    out[used] = 0;
    if (used && out[used - 1] != '/' && used + 1 < capacity) out[used++] = '/';
    copystr(out + used, fs_nodes()[id].name, capacity - used);
}

/* Compara el final del nombre con una extension, sin distinguir mayusculas. */
static int name_ends_with(const char *name, const char *suffix) {
    size_t name_len = slen(name), suffix_len = slen(suffix);
    if (suffix_len > name_len) return 0;
    const char *tail = name + name_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        char a = tail[i], b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

/* Los JPEG/PNG del sistema de archivos se corresponden por nombre con las
 * imagenes empaquetadas en el blob, asi /wallpapers/Logo.jpg abre Logo. Si el
 * nombre no esta en el blob se avisa en vez de mostrar otra imagen: un archivo
 * con extension de imagen pero sin pixeles detras seria un enganno. */
static int files_image_for(const char *name) {
    int count = images_count();
    for (int i = 0; i < count; i++) {
        const char *image = images_name(i);
        size_t used = 0;
        while (name[used] && name[used] != '.' && image[used]) {
            char a = name[used], b = image[used];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
            used++;
        }
        if ((!name[used] || name[used] == '.') && !image[used]) return i;
    }
    return -1;
}

/* Abre el visor de imagenes con la imagen que corresponde al archivo. */
static void files_open_image(int id) {
    int index = files_image_for(fs_nodes()[id].name);
    if (index < 0) {
        files_status("Esta imagen no esta empaquetada: anadela en images/ y ejecuta make iso");
        return;
    }
    open_window(WIN_IMAGES);
    images_select(index);
    files_status("Visor de imagenes");
}

static void files_open_selected(void) {
    const fs_node_t *nodes = fs_nodes();
    int id = files_selected_node();
    if (id < 0) {                              /* fila ".." */
        int cwd = fs_cwd();
        if (cwd != 0) fs_set_cwd(nodes[cwd].parent_idx);
        files.selected = 0; files.scroll = 0; files.viewer = 0; files.view_top = 0;
        copystr(files.message, "Directorio anterior", sizeof(files.message));
        files_touch();
        return;
    }
    if (nodes[id].type == FS_TYPE_DIR) {
        fs_set_cwd(id);
        files.selected = 0; files.scroll = 0; files.viewer = 0; files.view_top = 0;
        copystr(files.message, "Directorio abierto", sizeof(files.message));
    } else if (name_ends_with(nodes[id].name, ".jpg") || name_ends_with(nodes[id].name, ".jpeg") ||
               name_ends_with(nodes[id].name, ".png")) {
        files_open_image(id);
        return;
    } else {
        files.viewer = 1; files.view_top = 0;
        copystr(files.message, "Q o retroceso para volver al listado", sizeof(files.message));
    }
    files_touch();
}

static void files_delete(void) {
    const fs_node_t *nodes = fs_nodes();
    int id = files_selected_node();
    if (id < 0) { files_status("Selecciona un elemento para borrar"); return; }
    if (fs_is_system(id)) { files_status("Archivo del sistema: solo lectura"); return; }
    char path[FS_PATH_MAX], error[64];
    error[0] = 0;
    files_node_path(id, path, sizeof(path));
    if (fs_remove(path, nodes[id].type == FS_TYPE_DIR, error, sizeof(error))) {
        if (files.selected > 0) files.selected--;
        files_status("Elemento borrado");
    } else {
        files_status(error[0] ? error : "No se pudo borrar");
    }
}

static void files_begin(int action) {
    if (action == FILES_RENAME) {
        int id = files_selected_node();
        if (id < 0) { files_status("Selecciona un elemento para renombrar"); return; }
        if (fs_is_system(id)) { files_status("Archivo del sistema: solo lectura"); return; }
    } else if (fs_is_system(fs_cwd())) {
        files_status("Carpeta del sistema: solo lectura");
        return;
    }
    files.action = action;
    files.input_len = 0;
    files.input[0] = 0;
    files_touch();
}

static void files_commit(void) {
    char error[64], cwd_path[FS_PATH_MAX];
    error[0] = 0;
    fs_get_cwd_path(cwd_path, sizeof(cwd_path));
    int action = files.action;
    if (!files.input_len) { files.action = FILES_NONE; files_status("Nombre vacio"); return; }
    if (action != FILES_RENAME && fs_is_system(fs_cwd())) {
        files.action = FILES_NONE;
        files.input_len = 0;
        files.input[0] = 0;
        files_status("Carpeta del sistema: solo lectura");
        return;
    }
    int ok = 0;
    if (action == FILES_NEW_FILE) ok = fs_create_file(cwd_path, files.input, "", error, sizeof(error));
    else if (action == FILES_NEW_DIR) ok = fs_mkdir(cwd_path, files.input, error, sizeof(error));
    else if (action == FILES_RENAME) {
        int id = files_selected_node();
        if (id >= 0) {
            char path[FS_PATH_MAX];
            files_node_path(id, path, sizeof(path));
            ok = fs_rename(path, files.input, error, sizeof(error));
        }
    }
    files.action = FILES_NONE;
    files.input_len = 0;
    files.input[0] = 0;
    files_status(ok ? "Operacion completada" : (error[0] ? error : "No se pudo completar"));
}

/* Abre el archivo marcado en el editor ELF, que recibe la ruta por el handoff
 * de host_api (launch_file). */
static void files_edit(void) {
    const fs_node_t *nodes = fs_nodes();
    int id = files_selected_node();
    if (id < 0 || nodes[id].type != FS_TYPE_FILE) { files_status("Selecciona un archivo"); return; }
    char path[FS_PATH_MAX];
    files_node_path(id, path, sizeof(path));
    if (fs_is_system(id)) {
        console_write("GUI: EDITOR SOLO LECTURA ");
        console_write(path);
        console_write("\n");
        files_status("Archivo del sistema: se abre en solo lectura");
    } else {
        files_status("Abriendo el editor");
    }
    gui_launch_editor(path);
    files_touch();
}

static void files_button_action(int index) {
    switch (index) {
    case FILES_BTN_NEW: files_begin(FILES_NEW_FILE); break;
    case FILES_BTN_DIR: files_begin(FILES_NEW_DIR); break;
    case FILES_BTN_RENAME: files_begin(FILES_RENAME); break;
    case FILES_BTN_DELETE: files_delete(); break;
    case FILES_BTN_EDIT: files_edit(); break;
    default: break;
    }
}

/* Arrastre del pulgar: el listado se desplaza y el visor tambien. */
static void files_drag_bar(int py) {
    gui_window_t *window = files_window();
    if (!window || !files.drag) return;
    files_layout_t layout;
    files_layout(window, &layout);
    int bx, by, bw, bh, total, visible, first;
    if (!files_scrollbar_geometry(&layout, &bx, &by, &bw, &bh, &total, &visible, &first)) return;
    int value = scrollbar_first(by, bh, total, visible, py);
    if (files.drag == 2) {
        if (value == files.view_top) return;
        files.view_top = value;
    } else {
        if (value == files.scroll) return;
        files.scroll = value;
    }
    files_touch();
}

/* Devuelve 1 si la ventana Archivos consume la tecla; 0 solo con ESC cuando
 * no hay nada abierto dentro de la ventana (el escritorio se cierra). */
static int files_key(int key) {
    if (!files_window()) return 0;
    if (files.action) {                        /* captura de un nombre */
        if (key == 27) {
            files.action = FILES_NONE;
            files.input_len = 0;
            files.input[0] = 0;
            files_touch();
            return 1;
        }
        if (key == '\n') { files_commit(); return 1; }
        if (key == '\b') {
            if (files.input_len) files.input[--files.input_len] = 0;
            files_touch();
            return 1;
        }
        if (key >= 32 && key < 127 && files.input_len + 1 < sizeof(files.input)) {
            files.input[files.input_len++] = (char)key;
            files.input[files.input_len] = 0;
            files_touch();
        }
        return 1;
    }
    int count = files_rows(files_ids, FS_MAX_NODES);
    switch (key) {
    case KB_KEY_UP:
        if (files.viewer) { if (files.view_top > 0) files.view_top--; }
        else if (files.selected > 0) files.selected--;
        break;
    case KB_KEY_DOWN:
        if (files.viewer) files.view_top++;
        else if (files.selected + 1 < count) files.selected++;
        break;
    case KB_KEY_LEFT:
        if (files.viewer) { files.viewer = 0; break; }
        if (fs_cwd() != 0) { fs_set_cwd(fs_nodes()[fs_cwd()].parent_idx); files.selected = 0; files.scroll = 0; }
        break;
    case KB_KEY_RIGHT:
    case '\n':
        files_open_selected();
        return 1;
    case '\b':
        if (files.viewer) { files.viewer = 0; break; }
        if (fs_cwd() != 0) { fs_set_cwd(fs_nodes()[fs_cwd()].parent_idx); files.selected = 0; files.scroll = 0; }
        break;
    case 27:
        if (files.viewer) { files.viewer = 0; break; }
        return 0;                              /* ESC cierra el escritorio */
    case 'q': case 'Q':
        if (files.viewer) { files.viewer = 0; break; }
        return 1;
    case 'n': case 'N': files_begin(FILES_NEW_FILE); return 1;
    case 'c': case 'C': files_begin(FILES_NEW_DIR); return 1;
    case 'r': case 'R': files_begin(FILES_RENAME); return 1;
    case 'd': case 'D': case 'x': case 'X': files_delete(); return 1;
    case 'e': case 'E': files_edit(); return 1;
    default:
        return 1;
    }
    files_touch();
    return 1;
}

/* Boton del dock bajo el puntero, -1 si ninguno. Se usa para no repintar el
 * dock cuando el raton sigue sobre el mismo boton. */
static int dock_index_at(int px, int py) {
    for (int i = 0; i <= DOCK_ITEMS; i++) {
        int x, y, w, h;
        if (!dock_button_rect(i, &x, &y, &w, &h)) continue;
        if (hit(x, y, w, h, px, py)) return i;
    }
    return -1;
}

/* --- Ventanas: apertura, cierre y arrastre ------------------------------ */

static void toggle_window(int kind) {
    gui_window_t *existing = window_for(kind);
    if (existing) { close_window(existing); return; }
    open_window(kind);
}

/* Abre (o sube si ya existe) la ventana del dock sin alternar su estado. */
static void open_window(int kind) {
    int slot = 0;
    switch (kind) {
    case WIN_FILES: slot = SLOT_FILES; break;
    case WIN_NET: slot = SLOT_NET; break;
    case WIN_INPUT: slot = SLOT_INPUT; break;
    case WIN_APPS: slot = SLOT_APPS; break;
    case WIN_IMAGES: slot = SLOT_IMAGES; break;
    default: slot = SLOT_SYSTEM; break;
    }
    if (windows[slot].used) { raise_window(&windows[slot]); return; }
    window_default(&windows[slot], kind, slot);
    switch (kind) {
    case WIN_SYSTEM: copystr(windows[slot].title, "Sistema", sizeof(windows[slot].title)); break;
    case WIN_FILES: copystr(windows[slot].title, "Archivos", sizeof(windows[slot].title)); break;
    case WIN_NET: copystr(windows[slot].title, "Red", sizeof(windows[slot].title)); break;
    case WIN_INPUT: copystr(windows[slot].title, "Entrada", sizeof(windows[slot].title)); break;
    case WIN_IMAGES: copystr(windows[slot].title, "Imagenes", sizeof(windows[slot].title)); break;
    default: copystr(windows[slot].title, "Aplicaciones", sizeof(windows[slot].title)); break;
    }
    set_focus(slot);                           /* la recien abierta queda activa */
    if (kind == WIN_IMAGES) {
        console_write("GUI: VISOR IMAGENES ");
        console_dec((uint64_t)images_count());
        console_write(" IMAGENES\n");
        images_select(images_selected);
    }
    invalidate_window(&windows[slot]);
    invalidate(0, dock_y, back.width, dock_height);
}

static int close_button_hit(const gui_window_t *window, int px, int py) {
    int r = title_height / 4 + 2;
    int cx = window->x + window->width - margin - r;
    int cy = window->y + title_height / 2;
    int dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r + r;
}

static void app_surface_release(void) {
    if (app_physical && app_page_count) pmm_free_run(app_physical, app_page_count);
    app_physical = 0;
    app_page_count = 0;
    zero(&app_surface, sizeof(app_surface));
}

void gui_app_close(void) {
    gui_window_t *window = window_for(WIN_APP);
    int closed = window != 0;
    if (window) close_window(window);
    app_surface_release();
    /* Con GUI RUN no habra otro ciclo de eventos: el area de la app se
     * devuelve al escritorio y se vuelca en el acto para no dejar restos. */
    if (closed && active) {
        repaint_pending();
        gui_present();
    }
}

int gui_app_window(const char *title, int width, int height) {
    if (!active) return 0;
    if (width < 120) width = 120;
    if (height < 80) height = 80;
    if (width > back.width - margin * 4) width = back.width - margin * 4;
    if (height > content_height - margin * 4) height = content_height - margin * 4;
    gui_window_t *window = window_for(WIN_APP);
    if (!window) {
        for (int i = 0; i < GUI_WINDOW_COUNT; i++)
            if (!windows[i].used) { window = &windows[i]; break; }
        if (!window) return 0;
        zero(window, sizeof(*window));
    }
    if (app_surface.width != width || app_surface.height != height) {
        app_surface_release();
        uint64_t bytes = (uint64_t)width * (uint64_t)height * 4;
        uint64_t pages = (bytes + 4095) / 4096;
        uint64_t physical = pmm_alloc_run(pages);
        if (!physical) return 0;
        app_physical = physical;
        app_page_count = pages;
        app_surface.pixels = pmm_ptr(physical);
        app_surface.width = width;
        app_surface.height = height;
        app_surface.stride = width;
        zero(app_surface.pixels, (size_t)width * (size_t)height * 4);
    }
    window->used = 1;
    window->kind = WIN_APP;
    /* El marco reserva un margen a cada lado: sin el, la superficie de la app
     * se dibujaria en el interior y sus ultimas columnas quedarian ocultas. */
    window->width = width + margin * 2 + 2;
    window->height = height + title_height + margin * 2 + 1;
    if (window->width > back.width - margin * 2) window->width = back.width - margin * 2;
    if (window->height > dock_y - margin * 2) window->height = dock_y - margin * 2;
    window->x = (back.width - window->width) / 2;
    window->y = content_y + (content_height - window->height) / 2;
    copystr(window->title, title ? title : "Aplicacion", sizeof(window->title));
    raise_window(window);
    invalidate_window(window);
    return 1;
}

int gui_app_width(void) { return app_surface.width; }
int gui_app_height(void) { return app_surface.height; }

static void app_begin(void) {
    draw_target = &app_surface;
    gui_reset_clip();
}
static void app_end(void) {
    draw_target = &back;
    gui_reset_clip();
}

void gui_app_clear(uint32_t rgb) {
    if (!app_surface.pixels) return;
    app_begin();
    gui_fill(0, 0, app_surface.width, app_surface.height, rgb);
    app_end();
}

void gui_app_rect(int x, int y, int width, int height, uint32_t rgb) {
    if (!app_surface.pixels) return;
    app_begin(); gui_fill(x, y, width, height, rgb); app_end();
}

void gui_app_frame(int x, int y, int width, int height, uint32_t rgb) {
    if (!app_surface.pixels) return;
    app_begin(); gui_frame(x, y, width, height, rgb); app_end();
}

void gui_app_text(int x, int y, const char *text, uint32_t rgb, int text_scale) {
    if (!app_surface.pixels) return;
    const gui_font_t *font = text_scale > 1 ? font_body : font_small;
    app_begin();
    gui_text(x, y + font->ascent, text, rgb, font);
    app_end();
}

void gui_app_icon(int icon, int x, int y, int size, uint32_t rgb) {
    if (!app_surface.pixels) return;
    app_begin(); gui_icon(icon, x, y, size, rgb); app_end();
}

/* La ventana de la app dibuja en su propia superficie; publicar el frame solo
 * invalida su area de contenido y la vuelca al dispositivo. */
void gui_app_present(void) {
    gui_window_t *window = window_for(WIN_APP);
    if (!window) return;
    invalidate(window_content_x(window) + margin, window_content_top(window) + margin,
               app_surface.width, app_surface.height);
    repaint_pending();
    gui_present();
}

/* --- Bucle de eventos --------------------------------------------------- */

static void handle_press(int px, int py) {
    for (int i = 0; i <= DOCK_ITEMS; i++) {
        int x, y, w, h;
        if (!dock_button_rect(i, &x, &y, &w, &h)) continue;
        if (!hit(x, y, w, h, px, py)) continue;
        if (i == DOCK_ITEMS) { want_exit = 1; return; }
        toggle_window(dock_items[i].kind);
        return;
    }
    for (int z = z_top; z >= 1; z--)
        for (int i = 0; i < GUI_WINDOW_COUNT; i++) {
            gui_window_t *window = &windows[i];
            if (!window->used || window->z != z) continue;
            if (!hit(window->x, window->y, window->width, window->height, px, py)) continue;
            /* Cerrar antes de subir la ventana evita repintar dos veces y deja
             * el area de la ventana cerrada marcada como danada. */
            if (close_button_hit(window, px, py)) {
                if (window->kind == WIN_APP) gui_app_close();
                else close_window(window);
                return;
            }
            raise_window(window);
            if (py < window->y + title_height) {
                dragging = 1;
                drag_slot = i;
                drag_dx = px - window->x;
                drag_dy = py - window->y;
                return;
            }
            if (window->kind == WIN_FILES) {
                files_layout_t layout;
                files_layout(window, &layout);
                for (int button = 0; button < FILES_BTN_COUNT; button++) {
                    int bx, by, bw, bh;
                    if (!files_button_rect(&layout, button, &bx, &by, &bw, &bh)) continue;
                    if (!hit(bx, by, bw, bh, px, py)) continue;
                    files_button_action(button);
                    return;
                }
                int bx, by, bw, bh, total, visible, first;
                if (files_scrollbar_geometry(&layout, &bx, &by, &bw, &bh, &total, &visible, &first) &&
                    hit(bx, by, bw, bh, px, py)) {
                    int value = scrollbar_first(by, bh, total, visible, py);
                    if (files.viewer) files.view_top = value; else files.scroll = value;
                    files.drag = files.viewer ? 2 : 1;
                    files_touch();
                    return;
                }
                int row = (py - layout.body) / files_row_height();
                int count = files_rows(files_ids, FS_MAX_NODES);
                int position = files.scroll + row;
                if (row >= 0 && position >= 0 && position < count) {
                    if (files.selected == position && !files.viewer) files_open_selected();
                    else { files.selected = position; files.viewer = 0; files.view_top = 0; files_touch(); }
                }
                return;
            }
            if (window->kind == WIN_APPS) {
                int y = window_content_top(window) + margin;
                int row = font_small->height + font_small->line_gap + 6;
                int index = (py - y) / row;
                if (index >= 0 && (unsigned)index < elf_count()) elf_run((unsigned)index);
            }
            if (window->kind == WIN_IMAGES) {
                int list_width = images_list_width(window_content_width(window) - margin * 2);
                if (px <= window_content_x(window) + margin + list_width) {
                    int top = window_content_top(window) + margin + font_small->height + 10;
                    int row = font_small->height + font_small->line_gap + 4;
                    int index = (py - top) / row;
                    if (index >= 0 && index < images_count()) images_select(index);
                }
            }
            return;
        }
}

static void handle_release(void) { dragging = 0; files.drag = 0; }

static void handle_drag(int px, int py) {
    if (files.drag) { files_drag_bar(py); return; }
    if (!dragging) return;
    gui_window_t *window = &windows[drag_slot];
    if (!window->used) { dragging = 0; return; }
    int old_x = window->x, old_y = window->y;
    window->x = px - drag_dx;
    window->y = py - drag_dy;
    if (window->x < 0) window->x = 0;
    if (window->y < bar_height) window->y = bar_height;
    if (window->x + window->width > back.width) window->x = back.width - window->width;
    if (window->y + window->height > dock_y) window->y = dock_y - window->height;
    if (window->x == old_x && window->y == old_y) return;
    /* Solo se repintan las dos posiciones afectadas, no el escritorio entero. */
    invalidate(old_x, old_y, window->width + margin, window->height + margin);
    invalidate_window(window);
}

int gui_pump(gui_event_t *event) {
    if (!active) return 0;
    event->type = GUI_EV_NONE;
    event->x = event->y = event->button = 0;
    event->key = 0;
    mouse_poll();
    net_poll();
    clock_refresh();
    mouse_state_t mouse = mouse_state();
    gui_window_t *window = window_for(WIN_APP);
    if (!window) return 0;
    int origin_x = window_content_x(window) + margin;
    int origin_y = window_content_top(window) + margin;
    if (mouse.x != last_mouse_x || mouse.y != last_mouse_y) {
        event->type = GUI_EV_MOVE;
        event->x = mouse.x - origin_x;
        event->y = mouse.y - origin_y;
    }
    int button_now = mouse.buttons & 1;
    if (button_now != (last_buttons & 1)) {
        event->type = button_now ? GUI_EV_DOWN : GUI_EV_UP;
        event->button = 1;
        event->x = mouse.x - origin_x;
        event->y = mouse.y - origin_y;
    }
    int special = keyboard_special();
    int key = console_read();
    /* Las teclas solo llegan a la ventana activa: si la app perdio el foco se
     * consumen y se descartan para que no acaben en otra ventana. */
    if (focus_slot != (int)(window - windows)) { special = 0; key = -1; }
    if (special) {
        /* Las flechas llegan por su propia cola: la app las recibe como
         * GUI_EV_KEY con el codigo KB_KEY_*. */
        last_key = special;
        event->type = GUI_EV_KEY;
        event->key = (uint32_t)special;
    } else if (key >= 0) {
        last_key = key;
        if (key == 27) { gui_app_close(); event->type = GUI_EV_CLOSE; return 0; }
        event->type = GUI_EV_KEY;
        event->key = (uint32_t)key;
    }
    /* Arrastrar la ventana de la app y cerrarla con su boton. */
    if (event->type == GUI_EV_DOWN && hit(window->x, window->y, window->width, title_height, mouse.x, mouse.y)) {
        if (close_button_hit(window, mouse.x, mouse.y)) {
            gui_app_close();
            event->type = GUI_EV_CLOSE;
            return 0;
        }
        dragging = 1;
        drag_slot = (int)(window - windows);
        drag_dx = mouse.x - window->x;
        drag_dy = mouse.y - window->y;
    }
    if (event->type == GUI_EV_UP) dragging = 0;
    handle_drag(mouse.x, mouse.y);
    int dirty = paint_pending();
    last_mouse_x = mouse.x;
    last_mouse_y = mouse.y;
    last_buttons = mouse.buttons;
    repaint_pending();
    gui_present();
    /* Sin eventos ni dano se cede la CPU hasta la proxima interrupcion. */
    if (!dirty && event->type == GUI_EV_NONE) idle_cpu();
    return 1;
}

/* Teclado del escritorio: solo responde la ventana activa. Con Archivos e
 * Imagenes abiertas a la vez, una flecha mueve la seleccion de una sola.
 * Devuelve 1 si la ventana consumio la tecla; 0 solo con ESC, que cierra la
 * sesion grafica cuando la ventana activa no lo necesita. */
static int active_key(int key) {
    gui_window_t *window = focused_window();
    if (!window) return 0;
    if (window->kind == WIN_FILES) return files_key(key);
    if (window->kind == WIN_IMAGES) return images_key(key);
    return 0;
}

void gui_run(void) {
    if (!active) return;
    want_exit = 0;
    int previous_buttons = mouse_state().buttons & 1;
    dock_hover = dock_index_at(last_mouse_x, last_mouse_y);
    desktop_full_present();
    for (;;) {
        mouse_poll();
        net_poll();
        clock_refresh();
        int special = keyboard_special();
        int key = console_read();
        if (special) {
            last_key = special;
            active_key(special);
        } else if (key >= 0) {
            last_key = key;
            /* ESC cierra el escritorio salvo que la ventana activa lo use. */
            if (key == 27) { if (!active_key(27)) break; }
            else active_key(key);
        }
        mouse_state_t mouse = mouse_state();
        int moved = mouse.x != last_mouse_x || mouse.y != last_mouse_y;
        int hover = dock_index_at(mouse.x, mouse.y);
        if (hover != dock_hover) {
            dock_hover = hover;
            invalidate(margin, dock_y, back.width - margin * 2, dock_height);
        }
        handle_drag(mouse.x, mouse.y);
        int button_now = mouse.buttons & 1;
        if (button_now && !previous_buttons) handle_press(mouse.x, mouse.y);
        if (!button_now && previous_buttons) handle_release();
        previous_buttons = button_now;
        last_mouse_x = mouse.x;
        last_mouse_y = mouse.y;
        last_buttons = mouse.buttons;
        int dirty = paint_pending();
        repaint_pending();
        if (dirty || moved || dragging) gui_present();
        if (want_exit) break;
        /* Nada que hacer: dormir hasta la siguiente IRQ (PIT, teclado o raton). */
        if (!dirty && !moved && !dragging) idle_cpu();
    }
    gui_shutdown();
}

/* --- Arranque y apagado ------------------------------------------------- */

static int validate(struct limine_framebuffer *framebuffer) {
    return framebuffer && framebuffer->bpp == 32 &&
           framebuffer->memory_model == LIMINE_FRAMEBUFFER_RGB &&
           framebuffer->red_mask_size == 8 && framebuffer->green_mask_size == 8 &&
           framebuffer->blue_mask_size == 8 &&
           framebuffer->width >= 640 && framebuffer->height >= 400;
}

int gui_init(struct limine_framebuffer *framebuffer) {
    if (active) return 1;
    if (!validate(framebuffer)) return 0;
    device = framebuffer;
    red_shift = framebuffer->red_mask_shift;
    green_shift = framebuffer->green_mask_shift;
    blue_shift = framebuffer->blue_mask_shift;
    fast_flush = (red_shift == 16 && green_shift == 8 && blue_shift == 0);
    palette = (gui_theme_t){
        .bg_top = 0xeef3fa, .bg_bottom = 0xdde6f3,
        .panel = 0xffffff, .panel_alt = 0xf4f7fc, .border = 0xc9d5e6,
        .shadow = 0x20364f,
        .text = 0x14213a, .text_dim = 0x5a6b85, .text_inv = 0xffffff,
        .accent = 0x2470e8, .accent_dim = 0x1a4fb0, .accent_soft = 0xdbe9ff,
        .ok = 0x15803d, .warn = 0xb45309, .danger = 0xd92b2b,
        .white = 0xffffff, .cursor = 0xffffff, .cursor_edge = 0x101828,
    };
    for (int i = 0; i < 256; i++)
        gradient[i] = blend_channel(palette.bg_top, palette.bg_bottom, (unsigned)i);
    scale = framebuffer->width >= 1600 ? 2 : 1;
    font_small = framebuffer->width >= 1600 ? &gui_font_reg21 : &gui_font_reg15;
    font_body = framebuffer->width >= 1600 ? &gui_font_reg30 : &gui_font_reg21;
    font_title = framebuffer->width >= 1600 ? &gui_font_bold40 : &gui_font_bold30;
    margin = font_body->height / 3 + 2;
    icon_size = font_body->height;
    bar_height = font_body->height + margin;
    dock_height = icon_size + font_small->height + margin * 2 + 12;
    dock_y = framebuffer->height - dock_height;
    content_y = bar_height;
    content_height = dock_y - bar_height;
    title_height = font_body->height + margin;
    radius = margin * 2;

    back.width = (int)framebuffer->width;
    back.height = (int)framebuffer->height;
    back.stride = back.width;
    uint64_t bytes = (uint64_t)back.width * (uint64_t)back.height * 4;
    uint64_t pages = (bytes + 4095) / 4096;
    uint64_t physical = pmm_alloc_run(pages);
    if (!physical) {
        console_write("GUI: SIN MEMORIA CONTIGUA PARA EL DOBLE BUFFER.\n");
        back.width = back.height = back.stride = 0;
        return 0;
    }
    back.pixels = pmm_ptr(physical);
    zero(back.pixels, (size_t)bytes);
    /* Imagenes empaquetadas: la primera hace de fondo del escritorio. */
    images_init();
    background_build_tables();
    if (wall_ready) {
        console_write("GUI: FONDO IMAGEN ");
        console_write(images_name(wall_index));
        console_write(" ");
        console_dec((uint64_t)images_width(wall_index));
        console_write("x");
        console_dec((uint64_t)images_height(wall_index));
        console_putc('\n');
    } else {
        console_write("GUI: FONDO GRADIENTE SIN IMAGENES\n");
    }
    for (int i = 0; i < GUI_WINDOW_COUNT; i++) zero(&windows[i], sizeof(windows[i]));
    zero(&app_surface, sizeof(app_surface));
    app_physical = 0;
    app_page_count = 0;
    z_top = 0;
    focus_slot = -1;
    dragging = 0;
    want_exit = 0;
    last_buttons = 0;
    last_key = 0;
    last_mouse_x = back.width / 2;
    last_mouse_y = back.height / 2;
    mouse_set_bounds(back.width, back.height);
    mouse_center();
    console_suspend_screen(1);
    active = 1;
    draw_target = &back;
    gui_reset_clip();
    dock_hover = -1;
    cursor_drawn_x = cursor_drawn_y = -1;
    clock_refresh();          /* el primer fotograma ya lleva la hora */
    desktop_full_present();
    return 1;
}

void gui_shutdown(void) {
    if (!active) return;
    gui_app_close();
    active = 0;
    draw_target = &back;
    blank_clip();
    console_suspend_screen(0);
    console_clear();
    console_write("GUI: SESION FINALIZADA; CONSOLA RESTAURADA.\n");
    /* Traza para las pruebas: la posicion final del puntero debe ser la suma
     * exacta de los movimientos inyectados y DESCARTES no debe crecer. */
    mouse_state_t pointer = mouse_state();
    console_write("GUI: PUNTERO X="); console_dec((uint64_t)pointer.x);
    console_write(" Y="); console_dec((uint64_t)pointer.y);
    console_write(" PAQUETES="); console_dec((uint64_t)pointer.packets);
    console_write(" DESCARTES="); console_dec((uint64_t)pointer.discards);
    console_putc('\n');
}
