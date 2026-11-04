#include "usb_fat.h"
#include "console.h"
#include "fs.h"
#include "usb.h"
#include "util.h"

/* Lectura de medios USB: tabla de particiones, deteccion de volumenes cifrados
 * o ajenos y FAT12/16/32 en modo lectura. El acceso a sectores lo presta
 * usb_read() del controlador; aqui solo se interpreta el contenido. */

#define SECTOR 512
#define MAX_TOKEN 32

typedef struct {
    int mounted;
    int fat32;
    uint32_t partition_lba;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t data_lba;
    uint32_t root_cluster;
    uint32_t sectors_per_cluster;
    uint32_t bytes_per_cluster;
    uint32_t root_entries;
    uint32_t num_fats;
    uint32_t fat_sectors;
    uint32_t total_sectors;
    uint32_t clusters;
    int fat16_table;
    char label[12];
} fat_t;

typedef struct {
    uint8_t type;
    uint32_t lba;
    uint32_t sectors;
} partition_t;

static fat_t volume;
static uint8_t sector[SECTOR];
static uint32_t cached_lba;
static int cached_valid;
static int partition_count;
static partition_t partitions[4];
static int encrypted;
static int foreign;             /* volumen no interpretado (NTFS, exFAT...) */
static int partitioned;
static char warning[64];

static void say(const char *text) { console_write(text); }

static void say_dec(const char *prefix, uint64_t value) {
    console_write(prefix);
    console_dec(value);
    console_putc('\n');
}

static void set_warning(const char *text) {
    unsigned i = 0;
    while (text[i] && i + 1 < sizeof(warning)) { warning[i] = text[i]; i++; }
    warning[i] = 0;
}

static void copy_entry(uint8_t *destination, const uint8_t *source, unsigned length) {
    for (unsigned i = 0; i < length; i++) destination[i] = source[i];
}

static uint32_t le16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32);
}

static int read_sector(uint32_t lba) {
    if (cached_valid && cached_lba == lba) return 1;
    if (!usb_read(lba, sector)) return 0;
    cached_lba = lba;
    cached_valid = 1;
    return 1;
}

static int bytes_match(const uint8_t *data, const char *text, unsigned count) {
    for (unsigned i = 0; i < count; i++)
        if (data[i] != (uint8_t)text[i]) return 0;
    return 1;
}

static int is_fat_type(uint8_t type) {
    return type == 0x01 || type == 0x04 || type == 0x06 ||
           type == 0x0b || type == 0x0c || type == 0x0e;
}

static void copy_label(char *destination, unsigned capacity, const uint8_t *entry) {
    unsigned used = 0;
    if (!capacity) return;
    for (unsigned i = 0; i < 11 && used + 1 < capacity; i++)
        if (entry[i] != ' ') destination[used++] = (char)entry[i];
    destination[used] = 0;
}

static void copy_name(char *destination, unsigned capacity, const uint8_t *entry) {
    unsigned used = 0;
    if (!capacity) return;
    for (unsigned i = 0; i < 8 && used + 1 < capacity; i++) {
        if (entry[i] == ' ') break;
        destination[used++] = (char)entry[i];
    }
    if (entry[8] != ' ' && used + 2 < capacity) {
        destination[used++] = '.';
        for (unsigned i = 8; i < 11 && used + 1 < capacity; i++) {
            if (entry[i] == ' ') break;
            destination[used++] = (char)entry[i];
        }
    }
    destination[used] = 0;
}

/* --- Deteccion de cifrado y volumenes ajenos ----------------------------- */

/* La particion EFI de una tabla GPT se monta desde inspect_gpt(). */
static int mount_fat(uint32_t start);
static int bpb_valid(const uint8_t *b);

static void inspect_volume_boot_record(uint32_t lba, uint8_t type) {
    if (!read_sector(lba)) return;
    if (bytes_match(sector, "LUKS", 4)) {
        encrypted = 1;
        set_warning("LUKS");
        return;
    }
    if (bytes_match(sector + 3, "-FVE-FS-", 8) ||
        bytes_match(sector + 3, "-BFS-", 5)) {
        encrypted = 1;
        set_warning("BITLOCKER");
        return;
    }
    /* Si el sector de arranque es un BPB valido, es FAT y lo monta quien
     * llama; aqui solo se clasifican los volumenes que no se interpretan. */
    if (bpb_valid(sector)) return;
    if (bytes_match(sector + 3, "EXFAT   ", 8)) {
        foreign = 1;
        set_warning("EXFAT");
        return;
    }
    if (bytes_match(sector + 3, "NTFS    ", 8) || type == 0x07) {
        foreign = 1;
        set_warning("NTFS");
    }
}

/* GPT: se reconocen la particion de sistema EFI (FAT con el cargador), la de
 * datos basicos de Microsoft (NTFS/BitLocker) y la de datos Linux (LUKS). Las
 * demas no se tocan: solo se informa de lo que se puede interpretar. */
static int guid_is(const uint8_t *entry, const uint8_t *wanted) {
    for (unsigned i = 0; i < 16; i++)
        if (entry[i] != wanted[i]) return 0;
    return 1;
}

static int guid_empty(const uint8_t *entry) {
    for (unsigned i = 0; i < 16; i++)
        if (entry[i]) return 0;
    return 1;
}

static void inspect_gpt(void) {
    static const uint8_t esp[16] = {
        0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
        0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
    static const uint8_t basic_data[16] = {
        0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
        0x87, 0xc0, 0x68, 0xb6, 0xb6, 0xe7, 0xe5, 0x84};
    static const uint8_t linux_data[16] = {
        0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
        0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4};
    if (!read_sector(1)) return;
    if (!bytes_match(sector, "EFI PART", 8)) return;
    /* Cabecera GPT: 72 = LBA de la tabla de entradas, 80 = numero de entradas,
     * 84 = tamano de cada entrada. */
    uint64_t table = le64(sector + 72);
    uint32_t count = le32(sector + 80);
    uint32_t size = le32(sector + 84);
    if (!count || !size || size > 512) return;
    for (uint32_t i = 0; i < count && i < 16; i++) {
        uint64_t offset = table * 512 + (uint64_t)i * size;
        uint32_t target = (uint32_t)(offset / 512);
        if (!read_sector(target)) return;
        const uint8_t *entry = sector + (offset % 512);
        if (guid_empty(entry)) continue;
        uint64_t lba = le64(entry + 32);
        if (guid_is(entry, esp)) {
            /* La particion EFI es FAT y lleva /EFI/BOOT: se puede listar. */
            if (!volume.mounted && lba <= 0xffffffffu) mount_fat((uint32_t)lba);
            continue;
        }
        if (guid_is(entry, basic_data)) {
            if (lba <= 0xffffffffu) inspect_volume_boot_record((uint32_t)lba, 0);
            if (!volume.mounted && !encrypted && !foreign && lba <= 0xffffffffu)
                mount_fat((uint32_t)lba);       /* FAT con GUID de Microsoft */
            continue;
        }
        if (lba > 0xffffffffu) continue;
        if (guid_is(entry, linux_data))
            inspect_volume_boot_record((uint32_t)lba, 0x83);
        else
            inspect_volume_boot_record((uint32_t)lba, 0);
        /* GPT sin GUID conocido (por ejemplo la particion de Limine en la
         * ISO hibrida): solo se monta si su sector de arranque es FAT. */
        if (!volume.mounted && !encrypted && !foreign)
            mount_fat((uint32_t)lba);
    }
}

static void parse_partitions(void) {
    for (unsigned i = 0; i < 4; i++) {
        const uint8_t *entry = sector + 446 + i * 16;
        uint8_t type = entry[4];
        if (!type) continue;
        partitions[partition_count].type = type;
        partitions[partition_count].lba = le32(entry + 8);
        partitions[partition_count].sectors = le32(entry + 12);
        partition_count++;
    }
}

/* --- FAT ------------------------------------------------------------------ */

static int bpb_valid(const uint8_t *b) {
    if (le16(b + 11) != SECTOR) return 0;
    uint32_t cluster = b[13];
    if (!cluster || (cluster & (cluster - 1)) || cluster > 128) return 0;
    if (!le16(b + 14)) return 0;                 /* sectores reservados */
    uint32_t fats = b[16];
    if (fats != 1 && fats != 2) return 0;
    if (!le16(b + 19) && !le32(b + 32)) return 0;
    return 1;
}

static int mount_fat(uint32_t start) {
    if (!read_sector(start)) return 0;
    const uint8_t *b = sector;
    if (!bpb_valid(b)) return 0;
    volume.partition_lba = start;
    volume.sectors_per_cluster = b[13];
    volume.bytes_per_cluster = SECTOR * volume.sectors_per_cluster;
    volume.num_fats = b[16];
    volume.root_entries = le16(b + 17);
    volume.fat_sectors = le16(b + 22);
    volume.total_sectors = le16(b + 19) ? le16(b + 19) : le32(b + 32);
    if (!volume.fat_sectors) volume.fat_sectors = le32(b + 36);   /* FAT32 */
    volume.fat32 = volume.root_entries == 0 && le16(b + 22) == 0;
    volume.root_cluster = volume.fat32 ? le32(b + 44) : 0;
    volume.fat_lba = start + le16(b + 14);
    uint32_t root_sectors = ((volume.root_entries * 32) + (SECTOR - 1)) / SECTOR;
    volume.root_lba = volume.fat_lba + volume.num_fats * volume.fat_sectors;
    volume.data_lba = volume.root_lba + root_sectors;
    if (volume.total_sectors <= volume.data_lba - start) return 0;
    volume.clusters = (volume.total_sectors - (volume.data_lba - start)) /
                      volume.sectors_per_cluster;
    if (!volume.fat_sectors || !volume.clusters) return 0;
    if (volume.fat32 && (volume.root_cluster < 2 || volume.root_cluster >= volume.clusters + 2))
        return 0;
    volume.label[0] = 0;
    if (volume.fat32) copy_label(volume.label, sizeof(volume.label), b + 71);
    else copy_label(volume.label, sizeof(volume.label), b + 43);
    volume.fat16_table = !volume.fat32 && volume.clusters >= 4085;
    volume.mounted = 1;
    return 1;
}

static uint32_t next_cluster(uint32_t cluster) {
    if (volume.fat32) {
        uint32_t offset = cluster * 4;
        uint32_t lba = volume.fat_lba + offset / SECTOR;
        if (!read_sector(lba)) return 0xffffffffu;
        return le32(sector + (offset % SECTOR)) & 0x0fffffffu;
    }
    if (volume.fat16_table) {
        uint32_t offset = cluster * 2;
        uint32_t lba = volume.fat_lba + offset / SECTOR;
        if (!read_sector(lba)) return 0xffffffffu;
        return le16(sector + (offset % SECTOR));
    }
    uint32_t offset = cluster + cluster / 2;
    uint32_t lba = volume.fat_lba + offset / SECTOR;
    if (!read_sector(lba)) return 0xffffffffu;
    uint32_t value = le16(sector + (offset % SECTOR));
    return (cluster & 1u) ? (value >> 4) : (value & 0x0fffu);
}

static int chain_end(uint32_t cluster) {
    if (volume.fat32) return cluster >= 0x0ffffff8u;
    if (volume.fat16_table) return cluster >= 0xfff8u;
    return cluster >= 0x0ff8u;
}

/* --- Recorrido del directorio raiz --------------------------------------- */

typedef struct {
    const char *name;
    uint8_t entry[32];
    int found;
} find_ctx;

static int name_matches(const uint8_t *entry, const char *wanted) {
    char name[16];
    copy_name(name, sizeof(name), entry);
    return ci_same(name, wanted);
}

static void visit_entry(const uint8_t *entry, void *context) {
    find_ctx *search = context;
    if (entry[0] == 0xe5 || entry[0] == 0x00) return;
    if ((entry[11] & 0x3f) == 0x0f) return;          /* nombre largo */
    if (entry[11] & 0x08) return;                    /* etiqueta de volumen */
    if (!search->found && search->name && name_matches(entry, search->name)) {
        copy_entry(search->entry, entry, 32);
        search->found = 1;
    }
}

static void walk_root(void (*visit)(const uint8_t *, void *), void *context) {
    uint8_t block[32];
    if (!volume.mounted) return;
    if (!volume.fat32) {
        uint32_t entries = volume.root_entries;
        for (uint32_t i = 0; i < entries; i++) {
            uint32_t offset = i * 32;
            if (!read_sector(volume.root_lba + offset / SECTOR)) return;
            for (unsigned j = 0; j < 32; j++) block[j] = sector[(offset % SECTOR) + j];
            if (block[0] == 0x00) return;
            visit(block, context);
        }
        return;
    }
    uint32_t cluster = volume.root_cluster;
    uint32_t guard = 0;
    while (cluster >= 2 && !chain_end(cluster) && guard++ < volume.clusters + 1) {
        for (uint32_t s = 0; s < volume.sectors_per_cluster; s++) {
            if (!read_sector(volume.data_lba + (cluster - 2) * volume.sectors_per_cluster + s))
                return;
            for (unsigned offset = 0; offset < SECTOR; offset += 32) {
                for (unsigned j = 0; j < 32; j++) block[j] = sector[offset + j];
                if (block[0] == 0x00) return;
                visit(block, context);
            }
        }
        cluster = next_cluster(cluster);
    }
}

/* --- Lectura de archivos ------------------------------------------------- */

static int read_file(const uint8_t *entry, char *destination, uint32_t capacity,
                     uint32_t *size_out) {
    uint32_t size = le32(entry + 28);
    uint32_t cluster = le16(entry + 26);
    if (volume.fat32) cluster |= le16(entry + 20) << 16;
    uint32_t copied = 0;
    uint32_t guard = 0;
    while (cluster >= 2 && !chain_end(cluster) && guard++ < volume.clusters + 1) {
        for (uint32_t s = 0; s < volume.sectors_per_cluster; s++) {
            if (!read_sector(volume.data_lba + (cluster - 2) * volume.sectors_per_cluster + s))
                return 0;
            for (unsigned offset = 0; offset < SECTOR && copied < size; offset++) {
                if (copied < capacity) destination[copied] = (char)sector[offset];
                copied++;
            }
        }
        if (copied >= size) break;
        cluster = next_cluster(cluster);
    }
    *size_out = size;
    return copied > 0 || size == 0;
}

static const char *volume_kind(void) {
    if (!volume.mounted) return "SIN FAT";
    return volume.fat32 ? "FAT32" : (volume.fat16_table ? "FAT16" : "FAT12");
}

void usb_fat_probe(void) {
    partition_count = 0;
    partitioned = 0;
    encrypted = 0;
    foreign = 0;
    warning[0] = 0;
    volume.mounted = 0;
    cached_valid = 0;
    if (!usb_available() || !usb_capacity()) return;
    if (!read_sector(0)) return;
    int bootsector = sector[510] == 0x55 && sector[511] == 0xaa;
    if (bootsector && bpb_valid(sector)) {
        /* Superfloppy: el sistema de archivos empieza en el sector 0. */
        mount_fat(0);
        return;
    }
    if (bootsector) parse_partitions();
    partitioned = partition_count > 0;
    if (!partitioned) {
        inspect_volume_boot_record(0, 0);
        return;
    }
    uint32_t chosen = 0;
    for (int i = 0; i < partition_count; i++) {
        if (partitions[i].type == 0xee) { inspect_gpt(); continue; }
        if (!chosen && is_fat_type(partitions[i].type)) chosen = partitions[i].lba;
        inspect_volume_boot_record(partitions[i].lba, partitions[i].type);
    }
    if (chosen && !encrypted) mount_fat(chosen);
    if (encrypted) volume.mounted = 0;
}

void usb_fat_report(void) {
    if (!usb_capacity()) return;
    for (int i = 0; i < partition_count; i++) {
        console_write("USB: PARTICION "); console_dec(i + 1);
        console_write(" TIPO="); console_hex(partitions[i].type);
        console_write(" LBA="); console_dec(partitions[i].lba);
        console_write(" SECTORES="); console_dec(partitions[i].sectors);
        console_putc('\n');
    }
    if (!partitioned) say("USB: SIN TABLA DE PARTICIONES (SUPERFLOPPY)\n");
    if (encrypted) {
        console_write("USB: MEDIO CIFRADO ");
        console_write(warning[0] ? warning : "DESCONOCIDO");
        console_write(": NO SE MONTA NI SE INTENTA DESCIFRAR\n");
    } else if (foreign) {
        console_write("USB: VOLUMEN ");
        console_write(warning[0] ? warning : "AJENO");
        console_write(" DETECTADO: SOLO SE INFORMA, NO SE INTERPRETA\n");
    }
    if (volume.mounted) {
        console_write("USB: SISTEMA ");
        console_write(volume_kind());
        if (volume.label[0]) { console_write(" ETIQUETA=\""); console_write(volume.label); console_putc('"'); }
        console_putc('\n');
        say_dec("USB: CLUSTER_BYTES=", volume.bytes_per_cluster);
        say_dec("USB: CLUSTERS=", volume.clusters);
    }
}

/* --- Listado, volcado e importacion -------------------------------------- */

typedef struct {
    unsigned shown;
    unsigned limit;
} list_ctx;

static void list_entry(const uint8_t *entry, void *context) {
    list_ctx *list = context;
    if (entry[0] == 0xe5 || entry[0] == 0x00) return;
    if ((entry[11] & 0x3f) == 0x0f) return;
    if (entry[11] & 0x08) return;
    if (list->shown >= list->limit) return;
    char name[16];
    copy_name(name, sizeof(name), entry);
    if (!name[0]) return;
    list->shown++;
    console_write("USB: ");
    console_write(name);
    if (entry[11] & 0x10) console_write(" <DIR>");
    else { console_write(" "); console_dec(le32(entry + 28)); console_write(" BYTES"); }
    console_putc('\n');
}

void usb_fat_list(const char *path) {
    (void)path;
    if (!volume.mounted) {
        if (encrypted) say("USB: EL MEDIO ESTA CIFRADO; NO SE LISTA.\n");
        else if (foreign) say("USB: EL VOLUMEN NO ES FAT; NO SE LISTA.\n");
        else say("USB: NO HAY SISTEMA FAT MONTADO.\n");
        return;
    }
    list_ctx list = {0, 64};
    walk_root(list_entry, &list);
    if (!list.shown) say("USB: EL DIRECTORIO RAIZ ESTA VACIO.\n");
}

int usb_fat_cat(const char *name) {
    if (!volume.mounted || !*name) return 0;
    find_ctx search = {name, {0}, 0};
    walk_root(visit_entry, &search);
    if (!search.found) return 0;
    if (search.entry[11] & 0x10) {
        say("USB: ES UN DIRECTORIO.\n");
        return 0;
    }
    char text[FS_FILE_MAX_SIZE + 1];
    uint32_t size = 0;
    if (!read_file(search.entry, text, FS_FILE_MAX_SIZE, &size)) return 0;
    uint32_t shown = size < FS_FILE_MAX_SIZE ? size : FS_FILE_MAX_SIZE;
    text[shown] = 0;
    console_write(text);
    if (!shown || text[shown - 1] != '\n') console_putc('\n');
    if (size > shown) say("USB: ARCHIVO TRUNCADO AL MOSTRAR.\n");
    return 1;
}

int usb_fat_import(const char *name) {
    if (!volume.mounted || !*name) return 0;
    find_ctx search = {name, {0}, 0};
    walk_root(visit_entry, &search);
    if (!search.found) return 0;
    if (search.entry[11] & 0x10) {
        say("USB: LOS DIRECTORIOS NO SE IMPORTAN.\n");
        return 0;
    }
    static char content[FS_FILE_MAX_SIZE];
    uint32_t size = 0;
    if (!read_file(search.entry, content, FS_FILE_MAX_SIZE, &size)) return 0;
    uint32_t stored = size < FS_FILE_MAX_SIZE - 1 ? size : FS_FILE_MAX_SIZE - 1;
    content[stored] = 0;
    char target[16];
    copy_name(target, sizeof(target), search.entry);
    char directory[FS_PATH_MAX];
    fs_get_cwd_path(directory, sizeof(directory));
    char error[64];
    error[0] = 0;
    if (!fs_create_file(directory, target, content, error, sizeof(error))) {
        console_write("USB: NO SE PUDO IMPORTAR: ");
        console_write(error[0] ? error : "ERROR DEL SISTEMA DE ARCHIVOS");
        console_putc('\n');
        return 0;
    }
    console_write("USB: IMPORTADO /");
    console_write(target);
    console_write(" BYTES="); console_dec(stored);
    if (size > stored) console_write(" (TRUNCADO A 1023)");
    console_putc('\n');
    return 1;
}

int usb_fat_mounted(void) { return volume.mounted; }
