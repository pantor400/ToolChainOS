#include "linux_abi.h"
#include "console.h"
#include "elf_loader.h"
#include "fs.h"
#include "interrupts.h"
#include "pmm.h"
#include "util.h"

/* --- Registros de control ------------------------------------------------ */
#define MSR_EFER 0xC0000080u
#define MSR_STAR 0xC0000081u
#define MSR_LSTAR 0xC0000082u
#define MSR_FMASK 0xC0000084u
#define MSR_GS_BASE 0xC0000101u
#define MSR_FS_BASE 0xC0000100u

/* --- Llamadas al sistema x86-64 que usa BusyBox -------------------------- */
enum {
    SYS_READ = 0, SYS_WRITE = 1, SYS_OPEN = 2, SYS_CLOSE = 3,
    SYS_STAT = 4, SYS_FSTAT = 5, SYS_LSTAT = 6, SYS_LSEEK = 8,
    SYS_MMAP = 9, SYS_MPROTECT = 10, SYS_MUNMAP = 11, SYS_BRK = 12,
    SYS_RT_SIGACTION = 13, SYS_RT_SIGPROCMASK = 14, SYS_IOCTL = 16,
    SYS_WRITEV = 20, SYS_ACCESS = 21, SYS_MADVISE = 28,
    SYS_DUP = 32, SYS_DUP2 = 33, SYS_NANOSLEEP = 35,
    SYS_GETPID = 39, SYS_SENDFILE = 40, SYS_EXIT = 60, SYS_UNAME = 63,
    SYS_FCNTL = 72,
    SYS_GETCWD = 79, SYS_CHDIR = 80, SYS_READLINK = 89,
    SYS_GETTIMEOFDAY = 96, SYS_GETUID = 102, SYS_GETGID = 104,
    SYS_GETEUID = 107, SYS_GETEGID = 108, SYS_GETPPID = 110,
    SYS_SIGALTSTACK = 131, SYS_ARCH_PRCTL = 158, SYS_GETTID = 186,
    SYS_TIME = 201, SYS_FUTEX = 202, SYS_SCHED_GETAFFINITY = 204,
    SYS_GETDENTS64 = 217, SYS_SET_TID_ADDRESS = 218,
    SYS_CLOCK_GETTIME = 228, SYS_EXIT_GROUP = 231,
    SYS_OPENAT = 257, SYS_NEWFSTATAT = 262, SYS_READLINKAT = 267,
    SYS_SET_ROBUST_LIST = 273, SYS_PRLIMIT64 = 302,
    SYS_GETRANDOM = 318, SYS_RSEQ = 334
};

#define EPERM 1
#define ENOENT 2
#define EIO 5
#define EBADF 9
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENOTTY 25
#define ERANGE 34
#define ENOSYS 38
#define EMFILE 24

/* --- Tipos del ABI Linux ------------------------------------------------- */
typedef struct { int64_t sec, nsec; } linux_timespec_t;

typedef struct {
    uint64_t dev, ino, nlink;
    uint32_t mode, uid, gid, pad;
    uint64_t rdev, size;
    int64_t blksize, blocks;
    linux_timespec_t atime, mtime, ctime;
    int64_t unused[3];
} linux_stat_t;

typedef struct { char field[65]; } linux_utsname_field_t;
typedef struct { linux_utsname_field_t f[6]; } linux_utsname_t;

typedef struct { uint64_t base, len; } linux_iovec_t;

typedef struct {
    uint64_t ino;
    int64_t off;
    uint16_t reclen;
    uint8_t type;
    char name[];
} linux_dirent64_t;

/* El ABI x86-64 fija el tamano de struct stat: si cambia, avisa el compilador. */
typedef char linux_stat_size_check[(sizeof(linux_stat_t) == 144) ? 1 : -1];

#define LINUX_MAX_ARGS 12
#define LINUX_FDS 16
#define LINUX_MAPS 32
#define LINUX_STACK_PAGES 64
#define O_ACCMODE 3
#define AT_EMPTY_PATH 0x1000ull
#define MAP_FIXED 0x10ull
#define MAP_ANONYMOUS 0x20ull
/* Consultas de terminal: BusyBox mide la salida antes de imprimir. */
#define TCGETS 0x5401ull
#define TIOCGPGRP 0x540Full
#define TIOCGWINSZ 0x5413ull
#define FIONREAD 0x541Bull
#define LINUX_TERM_COLS 80
#define LINUX_TERM_ROWS 24

/* --- Estado -------------------------------------------------------------- */
static int ready;          /* SYSCALL programada y utilizable */
static int active;         /* hay un binario Linux en ejecucion */
static int tracing;        /* LINUX TRACE: vuelca cada llamada */
static int delegated;      /* el shell delego un applet: sin ruido de estado */
static int exit_status;
static uint64_t call_count;
static uint64_t unknown[8];

typedef struct {
    int used, node, is_dir;
    uint64_t offset;
} linux_fd_t;
static linux_fd_t fds[LINUX_FDS];

typedef struct {
    uint64_t phys, virt, pages;
    int used;
} linux_map_t;
static linux_map_t maps[LINUX_MAPS];

/* Region de brk: una sola reserva contigua que musl ve como su monton. */
static uint64_t brk_phys, brk_pages, brk_base, brk_end;

/* --- Simbolos de src/linux_enter.asm ------------------------------------- */
extern uint64_t cpu_rdmsr(uint64_t msr);
extern void cpu_wrmsr(uint64_t msr, uint64_t value);
extern uint16_t cpu_read_cs(void);
extern uint16_t cpu_read_ss(void);
extern uint64_t asm_cr0(void);
extern uint64_t asm_cr4(void);
extern void cpu_write_cr0(uint64_t value);
extern void cpu_write_cr4(uint64_t value);
extern void linux_syscall_entry(void);
extern void linux_call_entry(uint64_t entry, uint64_t stack_top);

static uint64_t fail(int code) { return (uint64_t)-(int64_t)code; }
static int is_dir_node(int node) { return fs_nodes()[node].type == FS_TYPE_DIR; }

/* --- Memoria del programa ------------------------------------------------ */

static int inside_brk(uint64_t address, uint64_t pages) {
    if (!brk_base) return 0;
    return address >= brk_base && address + pages * 4096 <= brk_end;
}

static int inside_map(uint64_t address, uint64_t pages) {
    for (int i = 0; i < LINUX_MAPS; i++)
        if (maps[i].used && address >= maps[i].virt &&
            address + pages * 4096 <= maps[i].virt + maps[i].pages * 4096) return 1;
    return 0;
}

static uint64_t sys_brk(uint64_t requested) {
    if (!brk_base) {
        /* 4 MiB en un solo tramo; si el PMM no los tiene, baja de tamanos. */
        for (uint64_t pages = 1024; pages >= 64; pages /= 2) {
            uint64_t phys = pmm_alloc_run(pages);
            if (!phys) continue;
            brk_phys = phys;
            brk_pages = pages;
            brk_base = (uint64_t)pmm_ptr(phys);
            brk_end = brk_base + pages * 4096;
            zero((void *)brk_base, (size_t)(pages * 4096));
            break;
        }
        if (!brk_base) return fail(ENOMEM);
    }
    /* Sin paginacion por proceso el monton no puede crecer mas alla de la
     * reserva; se informa el break actual y musl cae a mmap. */
    if (!requested || requested <= brk_base || requested > brk_end) return brk_base;
    return requested;
}

static uint64_t sys_mmap(uint64_t address, uint64_t length, uint64_t prot,
                         uint64_t flags, uint64_t fd, uint64_t offset) {
    (void)prot; (void)fd; (void)offset;
    if (!length) return fail(EINVAL);
    uint64_t pages = (length + 4095) / 4096;
    if (pages > 4096) return fail(ENOMEM);
    /* musl reserva con MAP_FIXED la guarda del monton: si el rango ya es
     * memoria del programa se confirma sin tocar el PMM. */
    if ((flags & MAP_FIXED) && (inside_brk(address, pages) || inside_map(address, pages)))
        return address;
    uint64_t phys = pmm_alloc_run(pages);
    if (!phys) return fail(ENOMEM);
    uint8_t *virt = pmm_ptr(phys);
    zero(virt, (size_t)(pages * 4096));
    for (int i = 0; i < LINUX_MAPS; i++) {
        if (maps[i].used) continue;
        maps[i].phys = phys;
        maps[i].virt = (uint64_t)virt;
        maps[i].pages = pages;
        maps[i].used = 1;
        return (uint64_t)virt;
    }
    pmm_free_run(phys, pages);
    return fail(ENOMEM);
}

static uint64_t sys_munmap(uint64_t address, uint64_t length) {
    (void)length;
    for (int i = 0; i < LINUX_MAPS; i++) {
        if (!maps[i].used || maps[i].virt != address) continue;
        pmm_free_run(maps[i].phys, maps[i].pages);
        zero(&maps[i], sizeof(maps[i]));
        return 0;
    }
    return 0;
}

/* Libera todo lo que el programa tomo del PMM antes de volver a la shell. */
static void release_memory(void) {
    for (int i = 0; i < LINUX_MAPS; i++)
        if (maps[i].used) { pmm_free_run(maps[i].phys, maps[i].pages); zero(&maps[i], sizeof(maps[i])); }
    if (brk_base) {
        pmm_free_run(brk_phys, brk_pages);
        brk_phys = brk_pages = brk_base = brk_end = 0;
    }
    zero(fds, sizeof(fds));
}

/* --- Descriptores sobre el sistema de archivos en RAM -------------------- */

static int alloc_fd(int node, int directory) {
    for (int i = 3; i < LINUX_FDS; i++) {
        if (fds[i].used) continue;
        fds[i].used = 1;
        fds[i].node = node;
        fds[i].is_dir = directory;
        fds[i].offset = 0;
        return i;
    }
    return -1;
}

static int resolve_node(const char *path) {
    if (!path || !*path) return -1;
    return fs_find_node(path);
}

static uint64_t sys_open(const char *path, uint64_t flags, uint64_t mode) {
    (void)mode;
    int node = resolve_node(path);
    if (node < 0) return fail(ENOENT);
    if ((flags & O_ACCMODE) != 0 && is_dir_node(node)) return fail(EISDIR);
    int fd = alloc_fd(node, is_dir_node(node));
    if (fd < 0) return fail(EMFILE);
    return (uint64_t)fd;
}

/* openat: el FS resuelve rutas relativas contra su directorio actual, asi que
 * solo se aceptan rutas absolutas o del cwd (dirfd = AT_FDCWD o un fd de
 * directorio abierto por encima del propio cwd). */
static uint64_t sys_openat(uint64_t dirfd, const char *path, uint64_t flags, uint64_t mode) {
    (void)dirfd;
    return sys_open(path, flags, mode);
}

static uint64_t sys_read(uint64_t fd, void *buffer, uint64_t count) {
    if (!buffer) return fail(EFAULT);
    if (fd == 0) return 0; /* teclado: sin datos disponibles todavia */
    if (fd < 3 || fd >= LINUX_FDS || !fds[fd].used) return fail(EBADF);
    if (fds[fd].is_dir) return fail(EISDIR);
    const fs_node_t *node = &fs_nodes()[fds[fd].node];
    uint64_t available = node->size > fds[fd].offset ? (uint64_t)node->size - fds[fd].offset : 0;
    if (count > available) count = available;
    for (uint64_t i = 0; i < count; i++)
        ((uint8_t *)buffer)[i] = (uint8_t)node->content[fds[fd].offset + i];
    fds[fd].offset += count;
    return count;
}

/* La consola de texto no interpreta secuencias de escape, pero los applets
 * preguntan si la salida es un terminal (ioctl) y, al serlo, colorean. Se
 * descartan solo las CSI de atributos (ESC [ ... m): el texto queda limpio y un
 * escape que no sea de color se escribe tal cual. */
static void console_write_plain(const char *text, uint64_t count) {
    uint64_t start = 0;
    for (uint64_t i = 0; i + 1 < count; i++) {
        if ((unsigned char)text[i] != 0x1B || text[i + 1] != '[') continue;
        uint64_t j = i + 2;
        while (j < count && ((unsigned char)text[j] < 0x40 || (unsigned char)text[j] > 0x7E)) j++;
        if (j >= count || text[j] != 'm') continue;
        if (i > start) console_write_text(text + start, (size_t)(i - start));
        start = j + 1;
        i = j;
    }
    if (count > start) console_write_text(text + start, (size_t)(count - start));
}

static uint64_t sys_write(uint64_t fd, const void *buffer, uint64_t count) {
    if (fd == 1 || fd == 2) {
        if (!buffer) return fail(EFAULT);
        console_write_plain((const char *)buffer, count);
        return count;
    }
    if (fd < 3 || fd >= LINUX_FDS || !fds[fd].used) return fail(EBADF);
    return count; /* sobre archivos de solo lectura se acepta y se descarta */
}

/* sendfile es el camino rapido de "cat": el origen ya esta en RAM, asi que se
 * entrega directamente al descriptor de salida. */
static uint64_t sys_sendfile(uint64_t out_fd, uint64_t in_fd, int64_t *offset, uint64_t count) {
    if (in_fd < 3 || in_fd >= LINUX_FDS || !fds[in_fd].used) return fail(EBADF);
    if (fds[in_fd].is_dir) return fail(EISDIR);
    const fs_node_t *node = &fs_nodes()[fds[in_fd].node];
    uint64_t position = offset ? (uint64_t)*offset : fds[in_fd].offset;
    uint64_t available = (uint64_t)node->size > position ? (uint64_t)node->size - position : 0;
    if (count > available) count = available;
    uint64_t done = sys_write(out_fd, node->content + position, count);
    if ((int64_t)done < 0) return done;
    if (offset) *offset = (int64_t)(position + done);
    else fds[in_fd].offset = position + done;
    return done;
}

static uint64_t sys_writev(uint64_t fd, const linux_iovec_t *vectors, uint64_t count) {
    if (!vectors) return fail(EFAULT);
    uint64_t total = 0;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t done = sys_write(fd, (const void *)vectors[i].base, vectors[i].len);
        if ((int64_t)done < 0) return done;
        total += done;
    }
    return total;
}

static uint64_t sys_lseek(uint64_t fd, int64_t offset, uint64_t whence) {
    if (fd < 3 || fd >= LINUX_FDS || !fds[fd].used) return fail(EBADF);
    const fs_node_t *node = &fs_nodes()[fds[fd].node];
    int64_t base = 0;
    if (whence == 1) base = (int64_t)fds[fd].offset;
    else if (whence == 2) base = node->size;
    int64_t target = base + offset;
    if (target < 0) return fail(EINVAL);
    fds[fd].offset = (uint64_t)target;
    return (uint64_t)target;
}

static void stat_of_node(int node, linux_stat_t *status) {
    const fs_node_t *entry = &fs_nodes()[node];
    int directory = entry->type == FS_TYPE_DIR;
    zero(status, sizeof(*status));
    status->ino = (uint64_t)node + 1;
    status->nlink = directory ? 2 : 1;
    status->mode = directory ? 0040755u : 0100644u;
    status->size = directory ? 4096 : (uint64_t)entry->size;
    status->blksize = 4096;
    status->blocks = (int64_t)((status->size + 511) / 512);
}

static void stat_of_console(linux_stat_t *status) {
    zero(status, sizeof(*status));
    status->ino = 1;
    status->nlink = 1;
    status->mode = 0020600u; /* character device */
    status->blksize = 4096;
}

static uint64_t sys_fstat(uint64_t fd, linux_stat_t *status) {
    if (!status) return fail(EFAULT);
    if (fd < 3) { stat_of_console(status); return 0; }
    if (fd >= LINUX_FDS || !fds[fd].used) return fail(EBADF);
    stat_of_node(fds[fd].node, status);
    return 0;
}

static uint64_t sys_newfstatat(uint64_t dirfd, const char *path, linux_stat_t *status, uint64_t flags) {
    (void)flags;
    if (!status) return fail(EFAULT);
    if (!path || !*path) {
        if ((flags & AT_EMPTY_PATH) || dirfd < 3) { return sys_fstat(dirfd, status); }
        return fail(ENOENT);
    }
    int node = resolve_node(path);
    if (node < 0) return fail(ENOENT);
    stat_of_node(node, status);
    return 0;
}

static uint64_t sys_getdents64(uint64_t fd, void *buffer, uint64_t count) {
    if (fd < 3 || fd >= LINUX_FDS || !fds[fd].used) return fail(EBADF);
    if (!fds[fd].is_dir) return fail(ENOTDIR);
    if (!buffer) return fail(EFAULT);
    const fs_node_t *nodes = fs_nodes();
    uint8_t *out = (uint8_t *)buffer;
    uint64_t used = 0;
    int directory = fds[fd].node;
    for (int i = (int)fds[fd].offset + 1; i < FS_MAX_NODES; i++) {
        if (i == 0 || !nodes[i].type || nodes[i].parent_idx != directory) continue;
        size_t length = slen(nodes[i].name) + 1;
        uint64_t reclen = (uint64_t)(sizeof(linux_dirent64_t) + length);
        reclen = (reclen + 7) & ~(uint64_t)7;
        if (used + reclen > count) break;
        linux_dirent64_t *entry = (linux_dirent64_t *)(out + used);
        entry->ino = (uint64_t)i + 1;
        entry->off = (int64_t)(used + reclen);
        entry->reclen = (uint16_t)reclen;
        entry->type = nodes[i].type == FS_TYPE_DIR ? 4 : 8;
        for (size_t c = 0; c < length; c++) entry->name[c] = nodes[i].name[c];
        used += reclen;
        fds[fd].offset = (uint64_t)i;
        if (used + sizeof(linux_dirent64_t) + 2 > count) break;
    }
    return used;
}

static uint64_t sys_close(uint64_t fd) {
    if (fd < 3) return 0;
    if (fd >= LINUX_FDS || !fds[fd].used) return fail(EBADF);
    zero(&fds[fd], sizeof(fds[fd]));
    return 0;
}

static uint64_t sys_fcntl(uint64_t fd, uint64_t command, uint64_t argument) {
    (void)argument;
    if (fd >= LINUX_FDS || (fd >= 3 && !fds[fd].used)) return fail(EBADF);
    if (command == 0) { /* F_DUPFD */
        int copy = alloc_fd(fds[fd].node, fds[fd].is_dir);
        return copy < 0 ? fail(EMFILE) : (uint64_t)copy;
    }
    return 0;
}

static uint64_t sys_dup(uint64_t fd) {
    if (fd >= LINUX_FDS || (fd >= 3 && !fds[fd].used)) return fail(EBADF);
    if (fd < 3) return fd;
    int copy = alloc_fd(fds[fd].node, fds[fd].is_dir);
    return copy < 0 ? fail(EMFILE) : (uint64_t)copy;
}

static uint64_t sys_dup2(uint64_t oldfd, uint64_t newfd) {
    if (oldfd >= LINUX_FDS || (oldfd >= 3 && !fds[oldfd].used)) return fail(EBADF);
    if (newfd >= LINUX_FDS) return fail(EBADF);
    if (oldfd == newfd) return newfd;
    fds[newfd] = fds[oldfd];
    fds[newfd].used = 1;
    return newfd;
}

/* --- Terminal ------------------------------------------------------------ */

typedef struct { uint16_t rows, cols, xpixel, ypixel; } linux_winsize_t;

/* BusyBox pregunta el tamano del terminal antes de maquetar la salida. Si se
 * responde ENOTTY la estructura queda sin inicializar y el ancho que lee (un
 * resto de la pila) desborda el buffer al imprimir. Aqui si hay respuesta:
 * 80x24, que es el area de texto de la consola. */
static uint64_t sys_ioctl(uint64_t fd, uint64_t request, uint64_t argument) {
    if (fd > 2) return fail(ENOTTY); /* sobre archivos del FS no hay tty */
    if (!argument) return fail(EFAULT);
    switch (request) {
    case TCGETS:
        zero((void *)argument, 60); /* struct termios x86-64 */
        return 0;
    case TIOCGWINSZ: {
        linux_winsize_t *size = (linux_winsize_t *)argument;
        size->rows = LINUX_TERM_ROWS;
        size->cols = LINUX_TERM_COLS;
        size->xpixel = 0;
        size->ypixel = 0;
        return 0;
    }
    case TIOCGPGRP:
        *(int32_t *)argument = 1;
        return 0;
    case FIONREAD:
        *(int32_t *)argument = 0;
        return 0;
    default:
        return fail(ENOTTY);
    }
}

/* --- Tiempo, identidad y sistema ----------------------------------------- */

static uint64_t now_seconds(void) { return timer_ticks() / 100; }

static uint64_t sys_clock_gettime(uint64_t clock, linux_timespec_t *value) {
    (void)clock;
    if (!value) return fail(EFAULT);
    uint64_t ticks = timer_ticks();
    value->sec = (int64_t)(ticks / 100);
    value->nsec = (int64_t)((ticks % 100) * 10000000);
    return 0;
}

static uint64_t sys_gettimeofday(uint64_t *tv, uint64_t *tz) {
    (void)tz;
    if (!tv) return fail(EFAULT);
    uint64_t ticks = timer_ticks();
    tv[0] = ticks / 100;
    tv[1] = (ticks % 100) * 10000;
    return 0;
}

static uint64_t sys_uname(linux_utsname_t *name) {
    if (!name) return fail(EFAULT);
    zero(name, sizeof(*name));
    copystr(name->f[0].field, "Linux", sizeof(name->f[0].field));
    copystr(name->f[1].field, "toolchain", sizeof(name->f[1].field));
    copystr(name->f[2].field, "6.18.60-toolchain", sizeof(name->f[2].field));
    copystr(name->f[3].field, "#1 SMP ToolchainOS", sizeof(name->f[3].field));
    copystr(name->f[4].field, "x86_64", sizeof(name->f[4].field));
    copystr(name->f[5].field, "(none)", sizeof(name->f[5].field));
    return 0;
}

static uint64_t sys_getcwd(char *buffer, uint64_t size) {
    if (!buffer) return fail(EFAULT);
    char path[FS_PATH_MAX];
    fs_get_cwd_path(path, sizeof(path));
    size_t length = slen(path);
    if (size < length + 1) return fail(ERANGE);
    for (size_t i = 0; i <= length; i++) buffer[i] = path[i];
    return (uint64_t)buffer;
}

static uint64_t sys_chdir(const char *path) {
    int node = resolve_node(path);
    if (node < 0) return fail(ENOENT);
    if (!is_dir_node(node)) return fail(ENOTDIR);
    return fs_set_cwd(node) ? 0 : fail(EINVAL);
}

static uint64_t sys_getrandom(void *buffer, uint64_t length, uint64_t flags) {
    (void)flags;
    if (!buffer && length) return fail(EFAULT);
    uint64_t state = timer_ticks() * 6364136223846793005ull + 1442695040888963407ull;
    uint8_t *out = (uint8_t *)buffer;
    for (uint64_t i = 0; i < length; i++) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        out[i] = (uint8_t)(state >> 33);
    }
    return length;
}

static uint64_t sys_prlimit64(uint64_t pid, uint64_t resource, const uint64_t *new_limit, uint64_t *old_limit) {
    (void)pid; (void)resource; (void)new_limit;
    if (old_limit) {
        old_limit[0] = 8 * 1024 * 1024;
        old_limit[1] = 8 * 1024 * 1024;
    }
    return 0;
}

static uint64_t sys_sched_getaffinity(uint64_t pid, uint64_t length, void *mask) {
    (void)pid;
    if (!mask) return fail(EFAULT);
    if (length < 8) return fail(EINVAL);
    ((uint64_t *)mask)[0] = 1;
    return 8;
}

static uint64_t sys_nanosleep(const linux_timespec_t *request, linux_timespec_t *remaining) {
    if (!request) return fail(EFAULT);
    if (remaining) zero(remaining, sizeof(*remaining));
    uint64_t target = timer_ticks() + (uint64_t)request->sec * 100 + (uint64_t)request->nsec / 10000000;
    while (timer_ticks() < target) __asm__ volatile("pause");
    return 0;
}

/* --- Entrada de SYSCALL -------------------------------------------------- */

static void note_unknown(uint64_t number) {
    for (int i = 0; i < 8; i++) if (unknown[i] == number + 1) return;
    for (int i = 0; i < 8; i++) {
        if (unknown[i]) continue;
        unknown[i] = number + 1;
        console_write("LINUX: SYSCALL NO IMPLEMENTADA NR=");
        console_dec(number); console_putc('\n');
        return;
    }
}

static void trace_call(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3) {
    if (!tracing || call_count > 400) return;
    console_write("  sys "); console_dec(number);
    console_write(" "); console_hex(a1);
    console_write(" "); console_hex(a2);
    console_write(" "); console_hex(a3); console_putc('\n');
}

uint64_t linux_dispatch(uint64_t number, uint64_t a1, uint64_t a2, uint64_t a3,
                        uint64_t a4, uint64_t a5, uint64_t a6) {
    if (!active) return fail(ENOSYS);
    call_count++;
    trace_call(number, a1, a2, a3);
    switch (number) {
    case SYS_READ: return sys_read(a1, (void *)a2, a3);
    case SYS_WRITE: return sys_write(a1, (const void *)a2, a3);
    case SYS_OPEN: return sys_open((const char *)a1, a2, a3);
    case SYS_OPENAT: return sys_openat(a1, (const char *)a2, a3, a4);
    case SYS_CLOSE: return sys_close(a1);
    case SYS_STAT:
    case SYS_LSTAT: return sys_newfstatat(0, (const char *)a1, (linux_stat_t *)a2, a3);
    case SYS_FSTAT: return sys_fstat(a1, (linux_stat_t *)a2);
    case SYS_NEWFSTATAT: return sys_newfstatat(a1, (const char *)a2, (linux_stat_t *)a3, a4);
    case SYS_LSEEK: return sys_lseek(a1, (int64_t)a2, a3);
    case SYS_MMAP: return sys_mmap(a1, a2, a3, a4, a5, a6);
    case SYS_MPROTECT: return 0;
    case SYS_MUNMAP: return sys_munmap(a1, a2);
    case SYS_MADVISE: return 0;
    case SYS_BRK: return sys_brk(a1);
    case SYS_RT_SIGACTION:
        if (a3) zero((void *)a3, 32);
        return 0;
    case SYS_RT_SIGPROCMASK:
        if (a3) zero((void *)a3, 8);
        return 0;
    case SYS_SIGALTSTACK: return 0;
    case SYS_IOCTL: return sys_ioctl(a1, a2, a3);
    case SYS_WRITEV: return sys_writev(a1, (const linux_iovec_t *)a2, a3);
    case SYS_ACCESS:
        return resolve_node((const char *)a1) < 0 ? fail(ENOENT) : 0;
    case SYS_DUP: return sys_dup(a1);
    case SYS_DUP2: return sys_dup2(a1, a2);
    case SYS_FCNTL: return sys_fcntl(a1, a2, a3);
    case SYS_NANOSLEEP: return sys_nanosleep((const linux_timespec_t *)a1, (linux_timespec_t *)a2);
    case SYS_SENDFILE: return sys_sendfile(a1, a2, (int64_t *)a3, a4);
    case SYS_GETPID:
    case SYS_GETTID:
    case SYS_SET_TID_ADDRESS: return 1;
    case SYS_GETPPID: return 0;
    case SYS_GETUID:
    case SYS_GETEUID: return 0;
    case SYS_GETGID:
    case SYS_GETEGID: return 0;
    case SYS_EXIT:
    case SYS_EXIT_GROUP:
        exit_status = (int)(int64_t)a1;
        linux_abort();
    case SYS_UNAME: return sys_uname((linux_utsname_t *)a1);
    case SYS_GETCWD: return sys_getcwd((char *)a1, a2);
    case SYS_CHDIR: return sys_chdir((const char *)a1);
    case SYS_READLINK:
    case SYS_READLINKAT: return fail(ENOENT);
    case SYS_GETTIMEOFDAY: return sys_gettimeofday((uint64_t *)a1, (uint64_t *)a2);
    case SYS_TIME:
        if (a1) *(int64_t *)a1 = (int64_t)now_seconds();
        return now_seconds();
    case SYS_ARCH_PRCTL:
        if (a1 == 0x1002) { cpu_wrmsr(MSR_FS_BASE, a2); return 0; }
        if (a1 == 0x1001) { cpu_wrmsr(MSR_GS_BASE, a2); return 0; }
        if (a1 == 0x1003) { if (!a2) return fail(EFAULT); *(uint64_t *)a2 = cpu_rdmsr(MSR_FS_BASE); return 0; }
        return fail(EINVAL);
    case SYS_FUTEX: return fail(ENOSYS);
    case SYS_SCHED_GETAFFINITY: return sys_sched_getaffinity(a1, a2, (void *)a3);
    case SYS_GETDENTS64: return sys_getdents64(a1, (void *)a2, a3);
    case SYS_CLOCK_GETTIME: return sys_clock_gettime(a1, (linux_timespec_t *)a2);
    case SYS_SET_ROBUST_LIST: return 0;
    case SYS_PRLIMIT64: return sys_prlimit64(a1, a2, (const uint64_t *)a3, (uint64_t *)a4);
    case SYS_GETRANDOM: return sys_getrandom((void *)a1, a2, a3);
    case SYS_RSEQ: return fail(ENOSYS);
    default: break;
    }
    note_unknown(number);
    return fail(ENOSYS);
}

/* --- Pila inicial: argc, argv, entorno vacio y auxv ---------------------- */

static uint64_t push_word(uint64_t sp, uint64_t value) {
    sp -= 8;
    *(uint64_t *)sp = value;
    return sp;
}

static uint64_t push_text(uint64_t sp, const char *text) {
    size_t length = slen(text) + 1;
    sp = (sp - length) & ~(uint64_t)7;
    for (size_t i = 0; i < length; i++) ((char *)sp)[i] = text[i];
    return sp;
}

static uint64_t build_stack(uint8_t *stack, uint64_t size, int argc, char *argv[],
                            const elf_image_t *image) {
    uint64_t sp = ((uint64_t)stack + size) & ~(uint64_t)15;
    uint64_t arguments[LINUX_MAX_ARGS];
    for (int i = 0; i < argc; i++) { sp = push_text(sp, argv[i]); arguments[i] = sp; }
    sp = push_text(sp, argv[0]);
    uint64_t executable = sp;
    sp = (sp - 16) & ~(uint64_t)15;
    uint64_t random = sp;
    uint64_t state = timer_ticks() * 6364136223846793005ull + 1442695040888963407ull;
    for (int i = 0; i < 16; i++) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        ((uint8_t *)random)[i] = (uint8_t)(state >> 33);
    }
    struct { uint64_t key, value; } auxv[] = {
        {3, image->phoff},          /* AT_PHDR */
        {4, image->phentsize},      /* AT_PHENT */
        {5, image->phnum},          /* AT_PHNUM */
        {6, 4096},                  /* AT_PAGESZ */
        {7, 0},                     /* AT_BASE: sin interprete */
        {8, 0},                     /* AT_FLAGS */
        {9, image->entry},          /* AT_ENTRY */
        {11, 0}, {12, 0}, {13, 0}, {14, 0}, /* UID/EUID/GID/EGID */
        {16, 0},                    /* AT_HWCAP */
        {17, 100},                  /* AT_CLKTCK */
        {23, 0},                    /* AT_SECURE */
        {25, random},               /* AT_RANDOM */
        {31, executable},           /* AT_EXECFN */
    };
    unsigned entries = (unsigned)(sizeof(auxv) / sizeof(auxv[0]));
    /* argc + argv + NULL + envp NULL + auxv + AT_NULL, en palabras de 8. */
    uint64_t words = 3 + (uint64_t)argc + 2 * (entries + 1);
    sp &= ~(uint64_t)15;
    if (words & 1) sp -= 8;
    sp = push_word(sp, 0);
    sp = push_word(sp, 0); /* AT_NULL */
    for (unsigned i = entries; i-- > 0;) {
        sp = push_word(sp, auxv[i].value);
        sp = push_word(sp, auxv[i].key);
    }
    sp = push_word(sp, 0); /* entorno vacio */
    sp = push_word(sp, 0); /* fin de argv */
    for (int i = argc; i-- > 0;) sp = push_word(sp, arguments[i]);
    sp = push_word(sp, (uint64_t)argc);
    return sp;
}

/* --- Busqueda del binario y ejecucion ------------------------------------ */

static int starts_with(const char *text, const char *prefix) {
    while (*prefix) if (*text++ != *prefix++) return 0;
    return 1;
}

static int linux_module(void) {
    int index = elf_find("busybox.static");
    if (index >= 0) return index;
    for (unsigned i = 0; i < elf_count(); i++)
        if (starts_with(elf_name(i), "busybox")) return (int)i;
    return -1;
}

static int linux_run(int index, int argc, char *argv[]) {
    size_t size = 0;
    const uint8_t *data = elf_module_data((unsigned)index, &size);
    if (!data || !size) { console_write("LINUX: MODULO VACIO\n"); return -1; }
    elf_image_t image;
    if (!elf_load_image(data, size, &image)) return -1;
    uint64_t physical = pmm_alloc_run(LINUX_STACK_PAGES);
    if (!physical) {
        console_write("LINUX: SIN MEMORIA PARA LA PILA\n");
        elf_free_image(&image);
        return -1;
    }
    uint8_t *stack = pmm_ptr(physical);
    zero(stack, (size_t)(LINUX_STACK_PAGES * 4096));
    uint64_t sp = build_stack(stack, LINUX_STACK_PAGES * 4096, argc, argv, &image);

    uint64_t saved_fs = cpu_rdmsr(MSR_FS_BASE);
    /* Cuando el shell delega un applet, el preambulo y el resumen son ruido:
     * el usuario pidio el listado, no un informe de la personalidad Linux. */
    if (!delegated) {
        console_write("LINUX: EJECUTANDO "); console_write(argv[0]);
        console_write(" ENTRY="); console_hex(image.entry);
        console_write(" PILA="); console_hex(sp);
        console_write(" RELOCS="); console_dec(image.relocs); console_putc('\n');
    }
    if (tracing) {
        console_write("LINUX: ARGC="); console_dec((uint64_t)argc);
        for (int i = 0; i < argc; i++) {
            console_write(" ["); console_write(argv[i]); console_putc(']');
        }
        console_putc('\n');
    }

    active = 1;
    exit_status = 0;
    call_count = 0;
    zero(unknown, sizeof(unknown));
    linux_call_entry(image.entry, sp);
    active = 0;
    cpu_wrmsr(MSR_FS_BASE, saved_fs);

    uint64_t calls = call_count;
    release_memory();
    pmm_free_run(physical, LINUX_STACK_PAGES);
    elf_free_image(&image);
    if (!delegated) {
        console_write("LINUX: FIN CODIGO="); console_signed(exit_status);
        console_write(" SYSCALLS="); console_dec(calls); console_putc('\n');
    }
    return exit_status;
}

/* --- Orden del shell ----------------------------------------------------- */

static void linux_list(void) {
    console_write("PERSONALIDAD LINUX: BINARIOS EN LA ISO\n");
    int found = 0;
    for (unsigned i = 0; i < elf_count(); i++) {
        if (!starts_with(elf_name(i), "busybox")) continue;
        console_write("  "); console_write(elf_name(i));
        console_write("  "); console_dec((uint64_t)elf_size(i)); console_write(" BYTES\n");
        found = 1;
    }
    if (!found) console_write("  NINGUNO: EJECUTA 'make linux-bin' Y RECONSTRUYE LA ISO.\n");
    console_write("USO: LINUX <APPLET> [ARGUMENTOS] | LINUX --LIST | LINUX TRACE | LINUX INFO\n");
}

static char arguments[192];

void linux_command(const char *args) {
    const char *cursor = args;
    while (*cursor == ' ') cursor++;
    if (!*cursor) { linux_list(); return; }
    /* La palabra se inspecciona para reconocer LIST/TRACE/INFO, pero si resulta
     * ser un applet hay que reenviarla: "linux echo hola" pasa "echo" a argv[1]. */
    const char *begin = cursor;
    char word[16];
    size_t used = 0;
    while (*cursor && *cursor != ' ' && used + 1 < sizeof(word)) word[used++] = *cursor++;
    word[used] = 0;
    if (ci_same(word, "LIST") || ci_same(word, "LISTAR")) { linux_list(); return; }
    if (ci_same(word, "TRACE")) {
        tracing = !tracing;
        console_write(tracing ? "LINUX: TRAZA DE SYSCALLS ACTIVADA.\n"
                              : "LINUX: TRAZA DE SYSCALLS DESACTIVADA.\n");
        return;
    }
    if (ci_same(word, "INFO")) {
        console_write("LINUX: SYSCALL "); console_write(ready ? "DISPONIBLE" : "NO DISPONIBLE");
        console_write(" CS="); console_hex(cpu_read_cs());
        console_write(" SS="); console_hex(cpu_read_ss());
        console_write(" LSTAR="); console_hex(cpu_rdmsr(MSR_LSTAR)); console_putc('\n');
        console_write("LINUX: FD MAX="); console_dec(LINUX_FDS);
        console_write(" MAPAS MAX="); console_dec(LINUX_MAPS);
        console_write(" TRAZA="); console_write(tracing ? "SI\n" : "NO\n");
        return;
    }
    cursor = begin; /* no era una orden del kernel: es el applet */
    if (!ready) {
        console_write("LINUX: LA INSTRUCCION SYSCALL NO ESTA DISPONIBLE EN ESTE ENTORNO.\n");
        return;
    }
    if (elf_busy()) {
        console_write("LINUX: HAY UNA APLICACION GRAFICA EN EJECUCION.\n");
        return;
    }
    int index = linux_module();
    if (index < 0) {
        console_write("LINUX: FALTA EL BINARIO. EJECUTA 'make linux-bin' Y RECONSTRUYE LA ISO.\n");
        return;
    }
    /* argv[0] es el nombre del modulo: BusyBox reconoce el prefijo y toma
     * argv[1] como applet. Los demas argumentos se copian tal como llegaron. */
    int argc = 0;
    char *vector[LINUX_MAX_ARGS];
    size_t position = 0;
    const char *name = elf_name((unsigned)index);
    for (size_t i = 0; name[i] && position + 1 < sizeof(arguments); i++)
        arguments[position++] = name[i];
    arguments[position++] = 0;
    vector[argc++] = arguments;
    while (*cursor == ' ') cursor++;
    while (*cursor && argc < LINUX_MAX_ARGS) {
        char *token = &arguments[position];
        while (*cursor && *cursor != ' ' && position + 1 < sizeof(arguments))
            arguments[position++] = *cursor++;
        arguments[position++] = 0;
        vector[argc++] = token;
        while (*cursor == ' ') cursor++;
    }
    linux_run(index, argc, vector);
}

/* --- Arranque ------------------------------------------------------------ */

int linux_abi_init(void) {
    uint16_t cs = cpu_read_cs();
    uint16_t ss = cpu_read_ss();
    /* SYSCALL carga CS desde STAR y SS = CS + 8: si el GDT del cargador no
     * sigue ese convenio, no se toca nada y la orden informara del motivo. */
    if (ss != (uint16_t)(cs + 8)) {
        console_write("LINUX ABI: GDT INCOMPATIBLE CS="); console_hex(cs);
        console_write(" SS="); console_hex(ss); console_putc('\n');
        ready = 0;
        return 0;
    }
    uint64_t efer = cpu_rdmsr(MSR_EFER);
    cpu_wrmsr(MSR_EFER, efer | 1); /* SCE: habilita SYSCALL/SYSRET */
    cpu_wrmsr(MSR_STAR, ((uint64_t)cs) << 32);
    cpu_wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)linux_syscall_entry);
    cpu_wrmsr(MSR_FMASK, 0); /* el programa no pierde interrupciones al entrar */
    /* El kernel se compila con -mgeneral-regs-only y nunca activo SSE, pero
     * musl lo usa en memcpy: sin OSFXSR cada instruccion SSE daria #UD. */
    uint64_t cr0 = asm_cr0();
    cpu_write_cr0((cr0 & ~((1ull << 2) | (1ull << 3))) | (1ull << 1));
    cpu_write_cr4(asm_cr4() | (1ull << 9) | (1ull << 10));
    ready = 1;
    console_write("LINUX ABI: SYSCALL ACTIVA CS="); console_hex(cs);
    console_write(" LSTAR="); console_hex(cpu_rdmsr(MSR_LSTAR)); console_putc('\n');
    return 1;
}

int linux_busy(void) { return active; }

/* --- Delegacion de ordenes del shell en BusyBox --------------------------- */

/* El shell pregunta primero aqui: si la ISO trae el binario, el applet real
 * hace el trabajo y no se ejecuta la version propia. En la ISO entregada no
 * hay binario, asi que el comportamiento y las pruebas de siempre no cambian. */
int linux_applet(const char *applet, const char *args) {
    if (!ready || !applet || !*applet) return 0;
    if (active || elf_busy()) return 0;   /* ya hay algo en ejecucion */
    if (linux_module() < 0) return 0;     /* sin binario: codigo propio */
    char line[192];
    size_t used = 0;
    while (*applet && used + 1 < sizeof(line)) line[used++] = *applet++;
    if (args) {
        while (*args == ' ') args++;
        if (*args) line[used++] = ' ';
        while (*args && used + 1 < sizeof(line)) line[used++] = *args++;
    }
    line[used] = 0;
    delegated = 1;
    linux_command(line);
    delegated = 0;
    return 1;
}
