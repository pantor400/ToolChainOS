#include "images.h"
#include <stddef.h>

/* El blob lo genera scripts/build_images.py y objcopy lo enlaza desde
 * build/assets/ con una ruta relativa, de modo que los simbolos no dependen de
 * donde este el arbol de compilacion. El formato esta descrito en ese script;
 * aqui solo se valida antes de usarlo. */

extern const uint8_t _binary_assets_images_bin_start[];
extern const uint8_t _binary_assets_images_bin_end[];

#define HEADER_SIZE 16

typedef struct {
    char name[32];
    int width, height;
    const uint16_t *pixels;
    const char *extension;
} image_entry_t;

/* Codigos del ultimo u32 de cada entrada; el 0 corresponde a los blobs que se
 * generaron antes de que el campo llevara la extension. */
static const char *const extensions[] = { ".jpg", ".jpg", ".jpeg", ".png" };
#define EXTENSION_COUNT ((int)(sizeof(extensions) / sizeof(extensions[0])))

static image_entry_t entries[IMAGES_MAX];
static int count;

static uint32_t read_u32(const uint8_t *source) {
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
           ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static uint32_t read_u16(const uint8_t *source) {
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8);
}

/* Validacion estricta del blob: magic, numero de entradas y que cada imagen
 * quepa entera entre el inicio y el final de los datos enlazados. */
static int validate(const uint8_t *start, const uint8_t *end) {
    if (!start || !end || end < start) return 0;
    size_t size = (size_t)(end - start);
    if (size < HEADER_SIZE) return 0;
    if (start[0] != 'T' || start[1] != 'C' || start[2] != 'O' || start[3] != 'S' ||
        start[4] != 'I' || start[5] != 'M' || start[6] != 'G' || start[7] != '1')
        return 0;
    uint32_t stored = read_u32(start + 8);
    if (!stored || stored > IMAGES_MAX) return 0;
    if (HEADER_SIZE + (size_t)stored * 48 > size) return 0;
    for (uint32_t i = 0; i < stored; i++) {
        const uint8_t *entry = start + HEADER_SIZE + (size_t)i * 48;
        uint32_t width = read_u16(entry + 32), height = read_u16(entry + 34);
        uint32_t offset = read_u32(entry + 36), bytes = read_u32(entry + 40);
        if (!width || !height || width > 4096 || height > 4096) return 0;
        if (offset < HEADER_SIZE || (size_t)offset > size) return 0;
        if (bytes != (uint32_t)width * (uint32_t)height * 2u) return 0;
        if ((size_t)offset + bytes > size) return 0;
        if (!entry[0] || entry[31]) return 0;
    }
    return 1;
}

void images_init(void) {
    count = 0;
    const uint8_t *start = _binary_assets_images_bin_start;
    const uint8_t *end = _binary_assets_images_bin_end;
    if (!validate(start, end)) return;
    uint32_t stored = read_u32(start + 8);
    for (uint32_t i = 0; i < stored; i++) {
        const uint8_t *entry = start + HEADER_SIZE + (size_t)i * 48;
        size_t used = 0;
        while (used + 1 < sizeof(entries[i].name) && entry[used]) {
            entries[i].name[used] = (char)entry[used];
            used++;
        }
        entries[i].name[used] = 0;
        entries[i].width = (int)read_u16(entry + 32);
        entries[i].height = (int)read_u16(entry + 34);
        entries[i].pixels = (const uint16_t *)(start + read_u32(entry + 36));
        uint32_t extension = read_u32(entry + 44);
        entries[i].extension = extensions[extension < (uint32_t)EXTENSION_COUNT ? extension : 0];
        count++;
    }
}

int images_count(void) { return count; }

const char *images_name(int index) {
    return (index >= 0 && index < count) ? entries[index].name : "";
}

const char *images_extension(int index) {
    return (index >= 0 && index < count) ? entries[index].extension : "";
}

int images_width(int index) { return (index >= 0 && index < count) ? entries[index].width : 0; }
int images_height(int index) { return (index >= 0 && index < count) ? entries[index].height : 0; }

const uint16_t *images_data(int index) {
    return (index >= 0 && index < count) ? entries[index].pixels : 0;
}

/* Expansion RGB565 -> RGB888 replicando los bits altos, sin coma flotante. */
uint32_t images_pixel(int index, int x, int y) {
    if (index < 0 || index >= count) return 0;
    if (x < 0 || y < 0 || x >= entries[index].width || y >= entries[index].height) return 0;
    return images_rgb(entries[index].pixels[(size_t)y * (size_t)entries[index].width + (size_t)x]);
}

int images_wallpaper(void) { return count > 0 ? 0 : -1; }
