#ifndef HOST_API_H
#define HOST_API_H
#include <stdint.h>

/* Contrato estable entre el kernel y las aplicaciones ELF. Las apps se
 * enlazan con apps/app.ld como PIE estatico y reciben este puntero en RDI.
 * Cambiar el orden o el tipo de un campo obliga a subir HOST_ABI_VERSION.
 * v3 anade sistema de archivos, teclas extendidas y ejecucion de ordenes. */
#define HOST_ABI_VERSION 3u

struct fs_node;

typedef struct { int type, x, y, button; uint32_t key; } host_event_t;
enum { HOST_EV_NONE = 0, HOST_EV_MOVE, HOST_EV_DOWN, HOST_EV_UP, HOST_EV_KEY, HOST_EV_CLOSE };
/* Teclas extendidas que llegan como HOST_EV_KEY (mismos codigos que KB_KEY_*). */
enum { HOST_KEY_UP = 0x101, HOST_KEY_DOWN, HOST_KEY_LEFT, HOST_KEY_RIGHT,
       HOST_KEY_HOME, HOST_KEY_END, HOST_KEY_DELETE };
#define HOST_KEY_TAB 9

enum {
    HOST_ICON_MONITOR = 0, HOST_ICON_FOLDER, HOST_ICON_GLOBE, HOST_ICON_PLAY,
    HOST_ICON_CLOSE, HOST_ICON_TERMINAL, HOST_ICON_INFO, HOST_ICON_CHIP,
    HOST_ICON_ARROW
};

typedef struct host_api {
    uint32_t abi_version, size;
    void (*write)(const char *text);
    int  (*window)(const char *title, int width, int height);
    int  (*width)(void);
    int  (*height)(void);
    void (*clear)(uint32_t rgb);
    void (*rect)(int x, int y, int width, int height, uint32_t rgb);
    void (*frame)(int x, int y, int width, int height, uint32_t rgb);
    void (*text)(int x, int y, const char *text, uint32_t rgb, int scale);
    int  (*text_width)(const char *text, int scale);
    void (*icon)(int icon, int x, int y, int size, uint32_t rgb);
    int  (*pump)(host_event_t *event);
    void (*present)(void);
    uint64_t (*ticks)(void);
    void (*close)(void);
    /* Registra el codigo de salida; la app debe devolver de main a continuacion. */
    int  (*finish)(int code);
    /* --- v3 ------------------------------------------------------------- */
    /* Sistema de archivos en RAM compartido con la shell. Las rutas son
     * relativas al directorio actual o absolutas si empiezan por '/'. */
    const char *(*read_file)(const char *path, uint32_t *size);
    int  (*write_file)(const char *path, const char *text);
    int  (*make_dir)(const char *dir, const char *name);
    int  (*remove_node)(const char *path);
    int  (*rename_node)(const char *path, const char *name);
    const struct fs_node *(*nodes)(void);
    int  (*node_count)(void);
    int  (*cwd)(void);
    int  (*chdir)(int index);
    void (*cwd_path)(char *buffer, int capacity);
    /* Ejecuta una orden del shell y devuelve su salida por serie capturada. */
    int  (*exec)(const char *line, char *out, int capacity);
    /* Siguiente tecla extendida (KB_KEY_*); 0 si no hay ninguna pendiente. */
    int  (*key)(void);
} host_api_t;

#endif
