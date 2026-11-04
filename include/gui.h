#ifndef GUI_H
#define GUI_H
#include <stdint.h>
#include <stddef.h>
#include <limine.h>
#include "gui_font.h"

/* Superficie RGB canonica 0x00RRGGBB. El framebuffer real se rellena en
 * gui_present() aplicando los desplazamientos de mascara de Limine, asi el
 * resto del codigo (y las aplicaciones ELF) no conoce el formato del hardware. */
typedef struct { uint32_t *pixels; int width, height, stride; } gui_surface_t;

/* Tema claro: superficies blancas sobre un fondo azul muy suave. */
typedef struct {
    uint32_t bg_top, bg_bottom, panel, panel_alt, border, shadow;
    uint32_t text, text_dim, text_inv;
    uint32_t accent, accent_dim, accent_soft;
    uint32_t ok, warn, danger, white, cursor, cursor_edge;
} gui_theme_t;

typedef enum {
    GUI_ICON_MONITOR = 0, GUI_ICON_FOLDER, GUI_ICON_GLOBE, GUI_ICON_PLAY,
    GUI_ICON_CLOSE, GUI_ICON_TERMINAL, GUI_ICON_INFO, GUI_ICON_CHIP,
    GUI_ICON_ARROW, GUI_ICON_IMAGE, GUI_ICON_COUNT
} gui_icon_t;

typedef struct { int type, x, y, button; uint32_t key; } gui_event_t;
enum { GUI_EV_NONE = 0, GUI_EV_MOVE, GUI_EV_DOWN, GUI_EV_UP, GUI_EV_KEY, GUI_EV_CLOSE };

int  gui_init(struct limine_framebuffer *framebuffer);
void gui_shutdown(void);
int  gui_active(void);
void gui_run(void);

int  gui_width(void);
int  gui_height(void);

const gui_theme_t *gui_theme(void);
const gui_font_t *gui_font_small(void);
const gui_font_t *gui_font_body(void);
const gui_font_t *gui_font_title(void);

/* Primitivas con recorte; dibujan en el buffer de la interfaz. */
void gui_fill(int x, int y, int width, int height, uint32_t rgb);
void gui_frame(int x, int y, int width, int height, uint32_t rgb);
void gui_round(int x, int y, int width, int height, int radius, uint32_t rgb);
void gui_line(int x0, int y0, int x1, int y1, uint32_t rgb);
void gui_circle(int cx, int cy, int radius, uint32_t rgb);
void gui_blend(int x, int y, int width, int height, uint32_t rgb, unsigned alpha);
void gui_text(int x, int baseline, const char *text, uint32_t rgb, const gui_font_t *font);
int  gui_text_width(const char *text, const gui_font_t *font);
void gui_text_center(int center_x, int baseline, const char *text, uint32_t rgb, const gui_font_t *font);
void gui_icon(int icon, int x, int y, int size, uint32_t rgb);
void gui_present(void);
void gui_set_clip(int x, int y, int width, int height);
void gui_reset_clip(void);

/* Ventana de aplicacion ELF: la app dibuja en su propia superficie y la
 * interfaz la compone con el resto de ventanas. */
int  gui_app_window(const char *title, int width, int height);
void gui_app_close(void);
void gui_app_clear(uint32_t rgb);
void gui_app_rect(int x, int y, int width, int height, uint32_t rgb);
void gui_app_frame(int x, int y, int width, int height, uint32_t rgb);
void gui_app_text(int x, int y, const char *text, uint32_t rgb, int scale);
void gui_app_icon(int icon, int x, int y, int size, uint32_t rgb);
void gui_app_present(void);
int  gui_pump(gui_event_t *event);
int  gui_app_width(void);
int  gui_app_height(void);

/* Ruta que el escritorio entrega a la proxima aplicacion (Archivos > Editar o
 * GUI EDIT). El editor la consume al arrancar mediante host_api launch_file. */
void gui_launch_file_set(const char *path);
const char *gui_launch_file(void);
void gui_launch_file_clear(void);
/* Abre el editor con esa ruta; 0 si el modulo no esta en la ISO. */
int  gui_launch_editor(const char *path);

#endif
