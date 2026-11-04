#include "usb.h"
#include "console.h"
#include "usb_fat.h"
#include "interrupts.h"
#include "io.h"
#include "mmio.h"
#include "pmm.h"
#include "util.h"

/* Subsistema USB 3.x (xHCI) por sondeo, sin interrupciones ni hilos: el
 * controlador se detecta por PCI, sus anillos se montan sobre paginas del PMM
 * y las transferencias se esperan leyendo el anillo de eventos.
 *
 * Alcance: controladoras xHCI, un dispositivo de almacenamiento masivo por
 * BOT/SCSI y lectura/escritura de sectores de 512 bytes. Las controladoras
 * EHCI/OHCI/UHCI antiguas se detectan solo para avisar. */

#define PCI_ADDR 0xcf8
#define PCI_DATA 0xcfc

#define PCI_CLASS_SERIAL_BUS 0x0cu
#define PCI_SUBCLASS_USB     0x03u
#define PCI_PROGIF_XHCI      0x30u
#define PCI_PROGIF_EHCI      0x20u
#define PCI_PROGIF_OHCI      0x10u
#define PCI_PROGIF_UHCI      0x00u

/* Registros operacionales, relativos a MMIO + CAPLENGTH. */
#define XHCI_USBCMD     0x00
#define XHCI_USBSTS     0x04
#define XHCI_PAGESIZE   0x08
#define XHCI_CRCR       0x18
#define XHCI_DCBAAP     0x30
#define XHCI_CONFIG     0x38
#define XHCI_PORT_BASE  0x400

/* Interruptor 0, relativo a RTSOFF. */
#define IR_ERSTSZ       0x08
#define IR_ERSTBA       0x10
#define IR_ERDP         0x18

#define USBCMD_RUN      (1u << 0)
#define USBCMD_HCRST    (1u << 1)
#define USBSTS_HCHALTED (1u << 0)
#define USBSTS_CNR      (1u << 11)

#define PORTSC_CCS  (1u << 0)
#define PORTSC_PED  (1u << 1)
#define PORTSC_PR   (1u << 4)
#define PORTSC_PLS_MASK (0xfu << 5)
#define PORTSC_PLS_U0   (0u << 5)
#define PORTSC_PP   (1u << 9)
#define PORTSC_SPEED_MASK (0xfu << 10)
#define PORTSC_CSC  (1u << 17)
#define PORTSC_PRC  (1u << 21)
#define PORTSC_W1C  (PORTSC_CSC | PORTSC_PRC | (1u << 18) | (1u << 19) | \
                     (1u << 20) | (1u << 22) | (1u << 23))

/* Tipos de TRB. */
#define TRB_NORMAL            1
#define TRB_SETUP             2
#define TRB_DATA              3
#define TRB_STATUS            4
#define TRB_LINK              6
#define TRB_ENABLE_SLOT       9
#define TRB_DISABLE_SLOT      10
#define TRB_ADDRESS_DEVICE    11
#define TRB_CONFIGURE_ENDPOINT 12
#define TRB_EVALUATE_CONTEXT  13
#define TRB_NOOP              23
#define TRB_TRANSFER_EVENT    32
#define TRB_COMMAND_COMPLETION 33
#define TRB_PORT_STATUS_CHANGE 34

#define TRB_TYPE(control) (((control) >> 10) & 0x3f)

#define TRBS 256
typedef struct { uint64_t param; uint32_t status; uint32_t control; } trb_t;

typedef struct { uint64_t physical; void *virtual; } page_t;

typedef struct {
    uint64_t physical;
    trb_t *trbs;
    unsigned enqueue;
    unsigned cycle;
} ring_t;

typedef struct {
    int present;
    uint8_t bus, slot, func;
    uint16_t vendor, device;
    uint64_t mmio_physical;
    volatile uint8_t *base;    /* inicio de la BAR mapeada */
    volatile uint8_t *op;      /* base + CAPLENGTH */
    volatile uint8_t *db;      /* base + DBOFF */
    volatile uint8_t *ir;      /* base + RTSOFF */
    uint8_t cap_length;
    uint16_t hci_version;
    uint8_t max_slots;
    uint8_t max_ports;
    uint32_t hcc_params;
    uint32_t hcs_params2;
    uint16_t db_offset;
    uint32_t rt_offset;
    uint32_t page_size;
    int running;
} xhci_t;

static xhci_t xhci;

static ring_t cmd_ring, event_ring, ep0_ring, bulk_in_ring, bulk_out_ring;
static page_t erst, dcba, input_ctx, device_ctx, cbw_page, csw_page, xfer_page;
static uint8_t slot_id;
static uint8_t device_port;
static uint8_t port_speed_id;
static uint8_t bulk_in_dci, bulk_out_dci;
static uint16_t ep0_mps;
static uint32_t bot_tag;
static uint32_t mass_blocks;
static uint32_t mass_block_size;
static int mass_ready;
static int mass_write_protected;
static char mass_vendor[9], mass_product[17];

/* --- PCI por puertos 0xCF8/0xCFC ----------------------------------------- */

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    outl(PCI_ADDR, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                   ((uint32_t)func << 8) | (off & 0xfcu));
    return inl(PCI_DATA);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t value) {
    outl(PCI_ADDR, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                   ((uint32_t)func << 8) | (off & 0xfcu));
    outl(PCI_DATA, value);
}

/* --- MMIO y registros ---------------------------------------------------- */

static uint32_t op_read(uint32_t offset) { return *(volatile uint32_t *)(xhci.op + offset); }
static void op_write(uint32_t offset, uint32_t value) { *(volatile uint32_t *)(xhci.op + offset) = value; }
static void op_write64(uint32_t offset, uint64_t value) { *(volatile uint64_t *)(xhci.op + offset) = value; }
static void ir_write(uint32_t offset, uint32_t value) { *(volatile uint32_t *)(xhci.ir + offset) = value; }
static void ir_write64(uint32_t offset, uint64_t value) { *(volatile uint64_t *)(xhci.ir + offset) = value; }
static uint32_t bar_read(uint32_t offset) { return *(volatile uint32_t *)(xhci.base + offset); }

static void db_write(uint32_t target, uint32_t value) {
    *(volatile uint32_t *)(xhci.db + 4u * target) = value;
}

static void say(const char *text) { console_write(text); }

static void say_hex(const char *prefix, uint64_t value) {
    console_write(prefix);
    console_hex(value);
    console_putc('\n');
}

static void say_dec(const char *prefix, uint64_t value) {
    console_write(prefix);
    console_dec(value);
    console_putc('\n');
}

static void say_byte(uint8_t value) {
    static const char digits[] = "0123456789abcdef";
    console_putc(digits[value >> 4]);
    console_putc(digits[value & 0xfu]);
}

static void copy_bytes(void *destination, const void *source, unsigned length) {
    unsigned char *to = destination;
    const unsigned char *from = source;
    while (length--) *to++ = *from++;
}

/* --- Deteccion ----------------------------------------------------------- */

static const char *progif_name(uint8_t progif) {
    switch (progif) {
        case PCI_PROGIF_XHCI: return "XHCI";
        case PCI_PROGIF_EHCI: return "EHCI";
        case PCI_PROGIF_OHCI: return "OHCI";
        case PCI_PROGIF_UHCI: return "UHCI";
        default: return "USB";
    }
}

static uint64_t bar0_address(uint8_t bus, uint8_t slot, uint8_t func, uint32_t bar) {
    if ((bar & 0x6u) == 0x4u) {
        uint64_t high = pci_read(bus, slot, func, 0x14);
        return (bar & ~0xfu) | (high << 32);
    }
    return bar & ~0xfu;
}

static int detect_controller(void) {
    for (uint8_t bus = 0; bus < 8; bus++)
        for (uint8_t slot = 0; slot < 32; slot++)
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read(bus, slot, func, 0);
                if ((uint16_t)id == 0xffffu) {
                    if (!func) break;
                    continue;
                }
                uint32_t class_reg = pci_read(bus, slot, func, 0x08);
                uint8_t base_class = (uint8_t)(class_reg >> 24);
                uint8_t subclass = (uint8_t)(class_reg >> 16);
                uint8_t progif = (uint8_t)(class_reg >> 8);
                if (base_class != PCI_CLASS_SERIAL_BUS || subclass != PCI_SUBCLASS_USB) continue;
                if (progif != PCI_PROGIF_XHCI) {
                    console_write("USB: CONTROLADORA ");
                    console_write(progif_name(progif));
                    console_write(" PRESENTE; SOLO SE IMPLEMENTA XHCI.\n");
                    continue;
                }
                /* Memoria (bit 1) y bus master (bit 2) para MMIO y DMA. */
                uint32_t command = pci_read(bus, slot, func, 0x04);
                pci_write(bus, slot, func, 0x04, command | 0x6u);
                uint32_t bar = pci_read(bus, slot, func, 0x10);
                if (bar & 1u) continue;                 /* xHCI siempre es BAR de memoria */
                uint64_t physical = bar0_address(bus, slot, func, bar);
                /* xHCI ocupa como mucho 64 KiB; el hueco PCI no esta en el
                 * mapa de Limine, asi que mmio_map crea las paginas. */
                void *mapped = physical
                    ? (pmm_map_covers(physical, 0x1000)
                        ? pmm_ptr(physical) : mmio_map(physical, 0x10000))
                    : 0;
                if (!mapped) {
                    say_hex("USB: BAR MMIO NO MAPEABLE EN ", physical);
                    continue;
                }
                zero(&xhci, sizeof(xhci));
                xhci.present = 1;
                xhci.bus = bus;
                xhci.slot = slot;
                xhci.func = func;
                xhci.vendor = (uint16_t)id;
                xhci.device = (uint16_t)(id >> 16);
                xhci.mmio_physical = physical;
                xhci.base = (volatile uint8_t *)mapped;
                xhci.cap_length = (uint8_t)bar_read(0x00);
                xhci.hci_version = (uint16_t)(bar_read(0x00) >> 16);
                uint32_t params1 = bar_read(0x04);
                xhci.max_slots = (uint8_t)(params1 & 0xffu);
                xhci.max_ports = (uint8_t)((params1 >> 24) & 0xffu);
                xhci.hcc_params = bar_read(0x10);
                xhci.hcs_params2 = bar_read(0x08);
                xhci.db_offset = (uint16_t)(bar_read(0x14) & 0xffffu);
                xhci.rt_offset = bar_read(0x18) & ~0x1fu;
                xhci.op = xhci.base + xhci.cap_length;
                xhci.db = xhci.base + xhci.db_offset;
                /* Los primeros 0x20 bytes del espacio de ejecucion son MFINDEX;
                 * el conjunto del interruptor 0 empieza despues. */
                xhci.ir = xhci.base + xhci.rt_offset + 0x20;

                console_write("USB: XHCI ");
                console_dec(bus); console_putc(':'); console_dec(slot);
                console_putc('.'); console_dec(func);
                console_write(" MMIO="); console_hex(physical);
                console_write(" CAPLENGTH="); console_hex(xhci.cap_length);
                console_write(" HCIVERSION="); console_hex(xhci.hci_version);
                console_write(" PUERTOS="); console_dec(xhci.max_ports);
                console_putc('\n');
                return 1;
            }
    return 0;
}

/* --- Puesta en marcha ---------------------------------------------------- */

static int wait_status(uint32_t mask, int set, unsigned rounds) {
    for (unsigned i = 0; i < rounds; i++) {
        uint32_t status = op_read(XHCI_USBSTS);
        if (((status & mask) != 0) == set) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int controller_stop(void) {
    uint32_t command = op_read(XHCI_USBCMD);
    op_write(XHCI_USBCMD, command & ~USBCMD_RUN);
    return wait_status(USBSTS_HCHALTED, 1, 200000);
}

static int controller_reset(void) {
    if (!controller_stop()) return 0;
    op_write(XHCI_USBCMD, op_read(XHCI_USBCMD) | USBCMD_HCRST);
    for (unsigned i = 0; i < 200000; i++) {
        if (!(op_read(XHCI_USBCMD) & USBCMD_HCRST)) break;
        __asm__ volatile("pause");
    }
    if (op_read(XHCI_USBCMD) & USBCMD_HCRST) return 0;
    if (!wait_status(USBSTS_CNR, 0, 200000)) return 0;
    xhci.page_size = op_read(XHCI_PAGESIZE);
    return 1;
}

static int controller_run(void) {
    op_write(XHCI_USBCMD, USBCMD_RUN);          /* sin interrupciones: sondeo */
    if (!wait_status(USBSTS_HCHALTED, 0, 200000)) return 0;
    xhci.running = 1;
    return 1;
}

static uint32_t port_status(unsigned port) {
    return op_read(XHCI_PORT_BASE + 0x10u * (port - 1));
}

static void port_write(unsigned port, uint32_t value) {
    op_write(XHCI_PORT_BASE + 0x10u * (port - 1), value);
}

static unsigned port_speed_mbps(uint8_t speed) {
    switch (speed) {
        case 1: return 12;      /* full */
        case 2: return 1;       /* low */
        case 3: return 480;     /* high */
        case 4: return 5000;    /* super */
        case 5: return 10000;   /* super plus */
        default: return 0;
    }
}

/* --- Anillos y memoria DMA ---------------------------------------------- */

static int page_alloc(page_t *page) {
    uint64_t physical = pmm_alloc();
    if (!physical) return 0;
    void *virtual_address = pmm_ptr(physical);
    if (!virtual_address) { pmm_free(physical); return 0; }
    zero(virtual_address, PMM_PAGE);
    page->physical = physical;
    page->virtual = virtual_address;
    return 1;
}

static int ring_init(ring_t *ring) {
    page_t page;
    if (!page_alloc(&page)) return 0;
    ring->physical = page.physical;
    ring->trbs = page.virtual;
    ring->enqueue = 0;
    ring->cycle = 1;
    /* El ultimo TRB enlaza con el principio y conserva el ciclo. */
    ring->trbs[TRBS - 1].param = page.physical;
    ring->trbs[TRBS - 1].status = 0;
    ring->trbs[TRBS - 1].control = (TRB_LINK << 10) | (1u << 1) | 1u;
    return 1;
}

static trb_t *ring_reserve(ring_t *ring) {
    if (ring->enqueue >= TRBS - 1) {
        ring->trbs[TRBS - 1].control = (TRB_LINK << 10) | (1u << 1) | ring->cycle;
        ring->enqueue = 0;
        ring->cycle ^= 1;
    }
    trb_t *trb = &ring->trbs[ring->enqueue++];
    trb->param = 0;
    trb->status = 0;
    trb->control = ring->cycle;
    return trb;
}

static unsigned ctx_bytes(void) { return (xhci.hcc_params & 4u) ? 64u : 32u; }
static uint32_t *ctx_at(unsigned index) {
    return (uint32_t *)((uint8_t *)input_ctx.virtual + index * ctx_bytes());
}
static uint32_t *icc(void) { return ctx_at(0); }
static uint32_t *slot_ctx(void) { return ctx_at(1); }
static uint32_t *ep_ctx(unsigned dci) { return ctx_at(1 + dci); }

/* --- Eventos ------------------------------------------------------------- */

static int next_event(trb_t *event) {
    trb_t *trb = &event_ring.trbs[event_ring.enqueue];
    if ((trb->control & 1u) != event_ring.cycle) return 0;
    *event = *trb;
    event_ring.enqueue++;
    if (event_ring.enqueue >= TRBS) {
        event_ring.enqueue = 0;
        event_ring.cycle ^= 1;
    }
    ir_write64(IR_ERDP, (event_ring.physical + (uint64_t)event_ring.enqueue * 16) | (1u << 3));
    return 1;
}

/* 1 tick del PIT = 10 ms. */
static uint64_t deadline_ms(unsigned milliseconds) {
    return timer_ticks() + (milliseconds / 10u) + 1u;
}

static int wait_event(uint32_t type, unsigned milliseconds, trb_t *event) {
    uint64_t deadline = deadline_ms(milliseconds);
    for (;;) {
        trb_t candidate;
        if (next_event(&candidate) && TRB_TYPE(candidate.control) == type) {
            *event = candidate;
            return 1;
        }
        if (timer_ticks() >= deadline) return 0;
        __asm__ volatile("pause");
    }
}

/* Evento de transferencia del canal indicado: en los eventos el endpoint y el
 * slot viajan en el dword de control, y el codigo y el residuo en el estado. */
static int wait_transfer(uint32_t dci, unsigned milliseconds, trb_t *event) {
    uint64_t deadline = deadline_ms(milliseconds);
    for (;;) {
        trb_t candidate;
        if (next_event(&candidate) && TRB_TYPE(candidate.control) == TRB_TRANSFER_EVENT) {
            uint32_t endpoint = (candidate.control >> 16) & 0x1fu;
            uint32_t slot = (candidate.control >> 24) & 0xffu;
            if (endpoint == dci && slot == slot_id) {
                *event = candidate;
                return 1;
            }
        }
        if (timer_ticks() >= deadline) return 0;
        __asm__ volatile("pause");
    }
}

static int command(uint32_t type, uint32_t slot, uint64_t parameter, trb_t *event) {
    trb_t *trb = ring_reserve(&cmd_ring);
    trb->param = parameter;
    trb->status = 0;
    trb->control |= (type << 10) | (slot << 24);   /* el slot va en control[31:24] */
    db_write(0, 0);
    if (!wait_event(TRB_COMMAND_COMPLETION, 500, event)) {
        say("USB: SIN RESPUESTA AL COMANDO\n");
        return 0;
    }
    uint8_t code = (uint8_t)(event->status >> 24);
    if (code != 1) {
        console_write("USB: COMANDO "); console_dec(type);
        console_write(" CODIGO="); console_dec(code); console_putc('\n');
        return 0;
    }
    return 1;
}

/* --- Enumeracion --------------------------------------------------------- */

static int port_reset(unsigned port) {
    uint32_t status = port_status(port);
    if (!(status & PORTSC_CCS)) return 0;
    port_write(port, PORTSC_PP | PORTSC_PR);
    uint64_t deadline = deadline_ms(500);
    for (;;) {
        status = port_status(port);
        if (!(status & PORTSC_PR) && (status & PORTSC_PED)) break;
        if (timer_ticks() >= deadline) {
            say("USB: EL PUERTO NO SE HABILITA\n");
            return 0;
        }
    }
    port_write(port, PORTSC_PP | (status & PORTSC_W1C));
    return 1;
}

static void input_context_reset(void) {
    zero(input_ctx.virtual, PMM_PAGE);
}

static void slot_context_fill(uint32_t port, uint8_t speed, unsigned entries) {
    uint32_t *slot = slot_ctx();
    slot[0] = ((uint32_t)speed << 20) | ((uint32_t)entries << 27);
    slot[1] = port << 16;
    slot[2] = 0;
    slot[3] = 0;
}

static void ep_context_fill(uint32_t *ep, uint8_t type, uint16_t mps, uint64_t ring_physical) {
    ep[0] = 0;
    ep[1] = ((uint32_t)mps << 16) | (3u << 1) | ((uint32_t)type << 3);
    ep[2] = (uint32_t)(ring_physical & 0xffffffF0u) | 1u;   /* DCS = 1 */
    ep[3] = (uint32_t)(ring_physical >> 32);
    ep[4] = 8;                                              /* longitud media */
}

static int address_device(uint32_t port, uint8_t speed, uint16_t mps) {
    zero(device_ctx.virtual, PMM_PAGE);
    ((uint64_t *)dcba.virtual)[slot_id] = device_ctx.physical;
    input_context_reset();
    icc()[1] = 0x3u;                       /* anadir slot y EP0 */
    slot_context_fill(port, speed, 1);
    ep_context_fill(ep_ctx(1), 4, mps, ep0_ring.physical);
    trb_t event;
    if (!command(TRB_ADDRESS_DEVICE, slot_id, input_ctx.physical, &event)) return 0;
    return 1;
}

static int evaluate_ep0_mps(uint16_t mps) {
    if (mps == ep0_mps) return 1;
    input_context_reset();
    icc()[1] = 0x3u;
    slot_context_fill(device_port, port_speed_id, 1);
    ep_context_fill(ep_ctx(1), 4, mps, ep0_ring.physical);
    trb_t event;
    if (!command(TRB_EVALUATE_CONTEXT, slot_id, input_ctx.physical, &event)) return 0;
    ep0_mps = mps;
    return 1;
}

static int ep0_transfer(uint8_t request_type, uint8_t request, uint16_t value,
                        uint16_t index, void *buffer, uint16_t length, int in) {
    trb_t *setup = ring_reserve(&ep0_ring);
    /* El paquete de setup (8 bytes) va entero en el parametro: peticion,
     * valor, indice y longitud. */
    setup->param = (uint64_t)request_type | ((uint64_t)request << 8) |
                   ((uint64_t)value << 16) | ((uint64_t)index << 32) |
                   ((uint64_t)length << 48);
    setup->status = 8;
    setup->control |= (TRB_SETUP << 10) | (1u << 6) |
                      ((length ? (in ? 2u : 3u) : 0u) << 16);
    if (length) {
        trb_t *data = ring_reserve(&ep0_ring);
        data->param = xfer_page.physical;
        data->status = length;
        data->control |= (TRB_DATA << 10) | (in ? (1u << 16) : 0u);
        if (buffer != xfer_page.virtual)
            copy_bytes(xfer_page.virtual, buffer, in ? 0u : (unsigned)length);
    }
    trb_t *status = ring_reserve(&ep0_ring);
    status->control |= (TRB_STATUS << 10) | (in ? 0u : (1u << 16)) | (1u << 5);
    db_write(slot_id, 1);
    trb_t event;
    if (!wait_transfer(1, 1000, &event)) {
        say("USB: TIEMPO AGOTADO EN EL CANAL 0\n");
        return 0;
    }
    uint8_t code = (uint8_t)(event.status >> 24);
    if (code != 1) {
        console_write("USB: CONTROL CODIGO="); console_dec(code); console_putc('\n');
        return 0;
    }
    if (length && in && buffer != xfer_page.virtual)
        copy_bytes(buffer, xfer_page.virtual, length);
    return 1;
}

static int get_descriptor(uint8_t type, uint8_t index, uint8_t *buffer, uint16_t length) {
    if (length > PMM_PAGE) return 0;
    return ep0_transfer(0x80, 6, (uint16_t)((type << 8) | index), 0, buffer, length, 1);
}

static int set_configuration(uint8_t value) {
    return ep0_transfer(0x00, 9, value, 0, 0, 0, 0);
}

/* Transferencia masiva contra una pagina DMA concreta: cada etapa BOT usa la
 * suya para no pisar los datos del medio. */
static int bulk_transfer(int in, uint64_t physical, uint32_t length) {
    if (!length) return 1;
    uint32_t dci = in ? bulk_in_dci : bulk_out_dci;
    trb_t *trb = ring_reserve(in ? &bulk_in_ring : &bulk_out_ring);
    trb->param = physical;
    trb->status = length;
    trb->control |= (TRB_NORMAL << 10) | (1u << 5) | (in ? (1u << 16) : 0u);
    db_write(slot_id, dci);
    trb_t event;
    if (!wait_transfer(dci, 5000, &event)) {
        say("USB: TIEMPO AGOTADO EN MASA\n");
        return 0;
    }
    uint8_t code = (uint8_t)(event.status >> 24);
    if (code != 1 && code != 13) {           /* 13 = short packet */
        console_write("USB: MASA CODIGO="); console_dec(code); console_putc('\n');
        return 0;
    }
    return 1;
}

/* --- BOT / SCSI ---------------------------------------------------------- */

static int bot_command(const uint8_t *cb, uint8_t cb_length, uint32_t data_length, int in) {
    if (data_length > PMM_PAGE) return 0;
    uint8_t *cbw = cbw_page.virtual;
    zero(cbw, 32);
    cbw[0] = 'U'; cbw[1] = 'S'; cbw[2] = 'B'; cbw[3] = 'C';
    uint32_t tag = ++bot_tag;
    cbw[4] = (uint8_t)tag; cbw[5] = (uint8_t)(tag >> 8);
    cbw[6] = (uint8_t)(tag >> 16); cbw[7] = (uint8_t)(tag >> 24);
    cbw[8] = (uint8_t)data_length; cbw[9] = (uint8_t)(data_length >> 8);
    cbw[10] = (uint8_t)(data_length >> 16); cbw[11] = (uint8_t)(data_length >> 24);
    cbw[12] = in ? 0x80 : 0x00;
    cbw[13] = 0;                              /* LUN 0 */
    cbw[14] = cb_length;
    copy_bytes(cbw + 15, cb, cb_length);
    if (!bulk_transfer(0, cbw_page.physical, 31)) return 0;
    if (data_length && !bulk_transfer(in, xfer_page.physical, data_length)) return 0;
    uint8_t *csw = csw_page.virtual;
    zero(csw, 32);
    if (!bulk_transfer(1, csw_page.physical, 13)) return 0;
    if (csw[0] != 'U' || csw[1] != 'S' || csw[2] != 'B' || csw[3] != 'S') {
        say("USB: FIRMA CSW INVALIDA\n");
        return 0;
    }
    if (csw[8] || csw[9] || csw[10] || csw[11]) {
        console_write("USB: CSW STATUS="); console_dec(csw[12]);
        console_write(" RESIDUO="); console_dec(csw[8] | (csw[9] << 8));
        console_write(" FIRMA="); console_hex(((uint32_t)csw[0] << 24) | (csw[1] << 16) | (csw[2] << 8) | csw[3]);
        console_write(" TAG="); console_dec(cbw[4] | (cbw[5] << 8));
        console_putc('\n');
        return 0;
    }
    return 1;
}

static int scsi_test_unit_ready(void) {
    static const uint8_t cb[6] = {0x00, 0, 0, 0, 0, 0};
    return bot_command(cb, 6, 0, 0);
}

static int scsi_inquiry(uint8_t *out, unsigned length) {
    if (length > PMM_PAGE) length = PMM_PAGE;
    uint8_t cb[6] = {0x12, 0, 0, 0, (uint8_t)length, 0};
    if (!bot_command(cb, 6, (uint32_t)length, 1)) return 0;
    copy_bytes(out, xfer_page.virtual, length);
    return 1;
}

static int scsi_read_capacity(uint32_t *blocks, uint32_t *block_size) {
    static const uint8_t cb[10] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (!bot_command(cb, 10, 8, 1)) return 0;
    const uint8_t *data = xfer_page.virtual;
    uint32_t last = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                    ((uint32_t)data[2] << 8) | data[3];
    uint32_t size = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                    ((uint32_t)data[6] << 8) | data[7];
    if (last == 0xffffffffu) {
        /* Medio grande: READ CAPACITY(16) da el ultimo LBA de 64 bits. */
        static const uint8_t cb16[16] = {0x9e, 0x10, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 32, 0, 0};
        if (!bot_command(cb16, 16, 32, 1)) return 0;
        uint64_t last64 = 0;
        for (unsigned i = 0; i < 8; i++) last64 = (last64 << 8) | data[i];
        size = ((uint32_t)data[8] << 24) | ((uint32_t)data[9] << 16) |
               ((uint32_t)data[10] << 8) | data[11];
        if (!size || last64 >= 0x7fffffffu) return 0;
        last = (uint32_t)last64;
    }
    if (!size) return 0;
    *blocks = last + 1;
    *block_size = size;
    return 1;
}

static int scsi_read10(uint32_t lba, uint16_t count) {
    uint8_t cb[10] = {0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    cb[2] = (uint8_t)(lba >> 24); cb[3] = (uint8_t)(lba >> 16);
    cb[4] = (uint8_t)(lba >> 8); cb[5] = (uint8_t)lba;
    cb[7] = (uint8_t)(count >> 8); cb[8] = (uint8_t)count;
    return bot_command(cb, 10, (uint32_t)count * 512u, 1);
}

static int scsi_write10(uint32_t lba, uint16_t count) {
    uint8_t cb[10] = {0x2a, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    cb[2] = (uint8_t)(lba >> 24); cb[3] = (uint8_t)(lba >> 16);
    cb[4] = (uint8_t)(lba >> 8); cb[5] = (uint8_t)lba;
    cb[7] = (uint8_t)(count >> 8); cb[8] = (uint8_t)count;
    return bot_command(cb, 10, (uint32_t)count * 512u, 0);
}

/* MODE SENSE(6): el byte 2 de la cabecera lleva el bit de proteccion contra
 * escritura. No se intenta saltar: solo se informa. */
static int scsi_write_protected(void) {
    uint8_t cb[6] = {0x1a, 0, 0x3f, 0, 192, 0};
    if (!bot_command(cb, 6, 192, 1)) return 0;
    return (((const uint8_t *)xfer_page.virtual)[2] & 0x80u) != 0;
}

/* --- Configuracion del dispositivo de masa ------------------------------- */

static int configure_endpoints(uint8_t in_ep, uint16_t in_mps, uint8_t out_ep, uint16_t out_mps) {
    bulk_in_dci = (uint8_t)((in_ep & 0x0fu) * 2u + 1u);
    bulk_out_dci = (uint8_t)((out_ep & 0x0fu) * 2u);
    input_context_reset();
    uint8_t entries = bulk_in_dci > bulk_out_dci ? bulk_in_dci : bulk_out_dci;
    icc()[1] = 0x1u | (1u << bulk_in_dci) | (1u << bulk_out_dci);
    slot_context_fill(1, port_speed_id, entries);
    ep_context_fill(ep_ctx(bulk_out_dci), 2, out_mps, bulk_out_ring.physical);
    ep_context_fill(ep_ctx(bulk_in_dci), 6, in_mps, bulk_in_ring.physical);
    trb_t event;
    if (!command(TRB_CONFIGURE_ENDPOINT, slot_id, input_ctx.physical, &event)) return 0;
    return 1;
}

static void copy_ascii(char *destination, unsigned capacity, const uint8_t *source, unsigned length) {
    unsigned i = 0;
    if (!capacity) return;
    while (i + 1 < capacity && i < length && source[i]) {
        uint8_t c = source[i];
        destination[i] = (c >= 0x20 && c < 0x7f) ? (char)c : ' ';
        i++;
    }
    while (i && destination[i - 1] == ' ') i--;
    destination[i] = 0;
}

static int mount_mass_storage(const uint8_t *configuration, unsigned total) {
    unsigned offset = 0;
    int found = 0;
    uint8_t in_ep = 0, out_ep = 0;
    uint16_t in_mps = 512, out_mps = 512;
    while (offset + 2 <= total) {
        uint8_t length = configuration[offset];
        uint8_t type = configuration[offset + 1];
        if (!length || offset + length > total) break;
        if (type == 4 && length >= 9) {                 /* interfaz */
            uint8_t alt = configuration[offset + 3];
            uint8_t endpoints = configuration[offset + 4];
            uint8_t class = configuration[offset + 5];
            uint8_t subclass = configuration[offset + 6];
            uint8_t protocol = configuration[offset + 7];
            if (class == 0x08 && subclass == 0x06 && protocol == 0x50 && alt == 0 && endpoints >= 2) {
                unsigned scan = offset + length;
                in_ep = out_ep = 0;
                while (scan + 2 <= total) {
                    uint8_t elen = configuration[scan];
                    uint8_t etype = configuration[scan + 1];
                    if (!elen || scan + elen > total) break;
                    if (etype == 5 && elen >= 7) {       /* endpoint */
                        uint8_t address = configuration[scan + 2];
                        uint8_t attributes = configuration[scan + 3];
                        uint16_t mps = (uint16_t)(configuration[scan + 4] |
                                                  (configuration[scan + 5] << 8));
                        if ((attributes & 0x3u) == 0x2u) {
                            if (address & 0x80u) { in_ep = address; in_mps = mps & 0x7ffu; }
                            else { out_ep = address; out_mps = mps & 0x7ffu; }
                        }
                    } else if (etype == 4) {
                        break;
                    }
                    scan += elen;
                }
                if (in_ep && out_ep) { found = 1; break; }
            }
        }
        offset += length;
    }
    if (!found) return 0;
    if (!in_mps || in_mps > 1024) in_mps = 512;
    if (!out_mps || out_mps > 1024) out_mps = 512;
    if (!ring_init(&bulk_in_ring) || !ring_init(&bulk_out_ring)) return 0;
    if (!set_configuration(1)) {
        say("USB: SET_CONFIGURATION FALLIDO\n");
        return 0;
    }
    if (!configure_endpoints(in_ep, in_mps, out_ep, out_mps)) {
        say("USB: CONFIGURE ENDPOINT FALLIDO\n");
        return 0;
    }
    if (!scsi_test_unit_ready() || !scsi_test_unit_ready()) {
        say("USB: EL MEDIO NO RESPONDE A TEST UNIT READY\n");
        return 0;
    }
    uint8_t inquiry[36];
    zero(inquiry, sizeof(inquiry));
    if (scsi_inquiry(inquiry, sizeof(inquiry))) {
        copy_ascii(mass_vendor, sizeof(mass_vendor), inquiry + 8, 8);
        copy_ascii(mass_product, sizeof(mass_product), inquiry + 16, 16);
        console_write("USB: MEDIO \"");
        console_write(mass_vendor);
        console_putc(' ');
        console_write(mass_product);
        console_write("\"\n");
    }
    mass_write_protected = scsi_write_protected();
    if (!scsi_read_capacity(&mass_blocks, &mass_block_size)) {
        say("USB: EL MEDIO NO INFORMA CAPACIDAD\n");
        return 0;
    }
    if (mass_block_size != 512) {
        say_dec("USB: TAMANO DE SECTOR NO SOPORTADO=", mass_block_size);
        return 0;
    }
    mass_ready = 1;
    say_dec("USB: ALMACENAMIENTO SECTORES=", mass_blocks);
    console_write("USB: ESCRITURA ");
    console_write(mass_write_protected ? "BLOQUEADA (MEDIO PROTEGIDO)\n" : "DISPONIBLE\n");
    return 1;
}

static int enumerate_port(unsigned port) {
    uint32_t status = port_status(port);
    if (!(status & PORTSC_CCS)) return 0;
    if (!port_reset(port)) return 0;
    device_port = (uint8_t)port;
    port_speed_id = (uint8_t)((port_status(port) & PORTSC_SPEED_MASK) >> 10);
    console_write("USB: PUERTO "); console_dec(port);
    console_write(" HABILITADO VELOCIDAD="); console_dec(port_speed_mbps(port_speed_id));
    console_write(" MBPS\n");
    trb_t event;
    if (!command(TRB_ENABLE_SLOT, 0, 0, &event)) return 0;
    slot_id = (uint8_t)(event.control >> 24);
    if (!slot_id) { say("USB: SIN SLOT LIBRE\n"); return 0; }
    ep0_mps = port_speed_id == 2 ? 8 : (port_speed_id >= 4 ? 512 : 64);
    if (!address_device(port, port_speed_id, ep0_mps)) {
        say("USB: ADDRESS DEVICE FALLIDO\n");
        return 0;
    }
    uint8_t device[18];
    zero(device, sizeof(device));
    if (!get_descriptor(1, 0, device, 8)) {
        say("USB: EL DISPOSITIVO NO RESPONDE AL DESCRIPTOR\n");
        return 0;
    }
    uint16_t mps = device[7];
    if (mps != ep0_mps && mps >= 8 && mps <= 512) evaluate_ep0_mps(mps);
    if (!get_descriptor(1, 0, device, 18)) return 0;
    uint8_t buffer[9];
    zero(buffer, sizeof(buffer));
    if (!get_descriptor(2, 0, buffer, 9)) return 0;
    uint16_t total = (uint16_t)(buffer[2] | (buffer[3] << 8));
    if (!total || total > PMM_PAGE) return 0;
    uint8_t *configuration = xfer_page.virtual;
    if (!get_descriptor(2, 0, configuration, total)) return 0;
    console_write("USB: DISPOSITIVO VID=");
    console_hex(device[8] | (device[9] << 8));
    console_write(" PID=");
    console_hex(device[10] | (device[11] << 8));
    console_write(" CLASE="); console_dec(device[4]);
    console_putc('\n');
    if (!mount_mass_storage(configuration, total)) {
        say("USB: EL DISPOSITIVO NO ES ALMACENAMIENTO MASIVO COMPATIBLE\n");
        return 0;
    }
    return 1;
}

static void report_ports(void) {
    unsigned connected = 0;
    for (unsigned port = 1; port <= xhci.max_ports; port++) {
        uint32_t status = port_status(port);
        if (!(status & PORTSC_CCS)) continue;
        connected++;
        uint8_t speed = (uint8_t)((status & PORTSC_SPEED_MASK) >> 10);
        console_write("USB: PUERTO "); console_dec(port);
        console_write(" CONECTADO VELOCIDAD="); console_dec(port_speed_mbps(speed));
        console_write(" MBPS");
        if (status & PORTSC_PED) console_write(" HABILITADO");
        if ((status & PORTSC_PLS_MASK) == PORTSC_PLS_U0) console_write(" U0");
        console_putc('\n');
    }
    if (!connected) say("USB: SIN DISPOSITIVOS CONECTADOS\n");
}

/* --- API publica --------------------------------------------------------- */

void usb_init(void) {
    zero(&xhci, sizeof(xhci));
    slot_id = 0;
    mass_ready = 0;
    mass_blocks = 0;
    if (!detect_controller()) {
        say("USB: SIN CONTROLADORA XHCI\n");
        return;
    }
    if (!controller_reset()) {
        say("USB: RESET DEL CONTROLADOR FALLIDO\n");
        return;
    }
    if (!ring_init(&cmd_ring) || !ring_init(&event_ring) || !ring_init(&ep0_ring) ||
        !page_alloc(&erst) || !page_alloc(&dcba) || !page_alloc(&input_ctx) ||
        !page_alloc(&device_ctx) || !page_alloc(&cbw_page) || !page_alloc(&csw_page) ||
        !page_alloc(&xfer_page)) {
        say("USB: SIN MEMORIA PARA LOS ANILLOS\n");
        return;
    }
    *(uint64_t *)erst.virtual = event_ring.physical;
    ((uint32_t *)erst.virtual)[2] = TRBS;
    op_write64(XHCI_DCBAAP, dcba.physical);
    op_write(XHCI_CONFIG, xhci.max_slots);
    op_write64(XHCI_CRCR, cmd_ring.physical | 1u);
    ir_write(IR_ERSTSZ, 1);
    ir_write64(IR_ERSTBA, erst.physical);
    ir_write64(IR_ERDP, event_ring.physical | (1u << 3));
    if (!controller_run()) {
        say("USB: EL CONTROLADOR NO ARRANCA\n");
        return;
    }
    say("USB: XHCI LISTO RUN=1\n");
    for (unsigned port = 1; port <= xhci.max_ports; port++)
        enumerate_port(port);
    report_ports();
    if (mass_ready) {
        usb_fat_probe();
        /* Informe automatico: particiones, FAT montada y aviso de medios
         * cifrados o ajenos sin que haga falta pedir USB INFO. */
        usb_fat_report();
    }
}

int usb_available(void) { return xhci.present && xhci.running; }
uint32_t usb_capacity(void) { return mass_ready ? mass_blocks : 0; }
int usb_read(uint32_t lba, void *buffer) {
    if (!mass_ready || !scsi_read10(lba, 1)) return 0;
    copy_bytes(buffer, xfer_page.virtual, 512);
    return 1;
}
int usb_write(uint32_t lba, const void *buffer) {
    if (!mass_ready || mass_write_protected) return 0;
    copy_bytes(xfer_page.virtual, buffer, 512);
    return scsi_write10(lba, 1);
}

static int word_is(const char *text, const char *word) {
    while (*word) {
        char left = *text, right = *word;
        if (left >= 'a' && left <= 'z') left -= 32;
        if (right >= 'a' && right <= 'z') right -= 32;
        if (left != right) return 0;
        text++; word++;
    }
    return *text == 0 || *text == ' ';
}

static const char *after_word(const char *text) {
    while (*text && *text != ' ') text++;
    while (*text == ' ') text++;
    return text;
}

void usb_command(const char *arg) {
    while (*arg == ' ') arg++;
    if (!*arg || word_is(arg, "INFO")) {
        if (!xhci.present) {
            say("USB: NO HAY CONTROLADORA XHCI DETECTADA.\n");
            return;
        }
        console_write("USB: XHCI "); console_dec(xhci.bus); console_putc(':');
        console_dec(xhci.slot); console_putc('.'); console_dec(xhci.func);
        console_write(" MMIO="); console_hex(xhci.mmio_physical);
        console_write(" CAPLENGTH="); console_hex(xhci.cap_length);
        console_write(" HCIVERSION="); console_hex(xhci.hci_version);
        console_write(" PUERTOS="); console_dec(xhci.max_ports);
        console_write(" SLOTS="); console_dec(xhci.max_slots);
        console_write(" PAGE_SIZE="); console_dec(xhci.page_size);
        console_putc('\n');
        console_write("USB: ESTADO ");
        console_write(xhci.running ? "RUN\n" : "HALTED\n");
        if (mass_ready) {
            say_dec("USB: SECTORES=", mass_blocks);
            say_dec("USB: SECTOR_BYTES=", mass_block_size);
            console_write("USB: ESCRITURA ");
            console_write(mass_write_protected ? "BLOQUEADA\n" : "DISPONIBLE\n");
        } else {
            say("USB: SIN MEDIO DE ALMACENAMIENTO LISTO\n");
        }
        usb_fat_report();
        report_ports();
        return;
    }
    if (word_is(arg, "PUERTOS")) {
        if (!xhci.present) { say("USB: NO HAY CONTROLADORA XHCI DETECTADA.\n"); return; }
        report_ports();
        return;
    }
    if (word_is(arg, "REINICIAR")) {
        if (!xhci.present) { say("USB: NO HAY CONTROLADORA XHCI DETECTADA.\n"); return; }
        mass_ready = 0;
        usb_init();
        return;
    }
    if (word_is(arg, "LEER") || word_is(arg, "READ")) {
        if (!mass_ready) { say("USB: NO HAY MEDIO DE ALMACENAMIENTO LISTO.\n"); return; }
        const char *number = after_word(arg);
        uint64_t lba = 0;
        if (!*number || !udecimal(number, &lba) || lba >= mass_blocks) {
            say("USO: USB LEER <SECTOR>\n");
            return;
        }
        if (!usb_read((uint32_t)lba, xfer_page.virtual)) {
            say("USB: LECTURA FALLIDA\n");
            return;
        }
        const uint8_t *sector = xfer_page.virtual;
        for (unsigned row = 0; row < 4; row++) {
            console_write("USB: ");
            console_dec(lba);
            console_write("+");
            console_dec(row * 16);
            console_write("  ");
            for (unsigned column = 0; column < 16; column++) {
                say_byte(sector[row * 16 + column]);
                console_putc(' ');
            }
            console_putc('\n');
        }
        return;
    }
    if (word_is(arg, "ESCRIBIR") || word_is(arg, "WRITE")) {
        if (!mass_ready) { say("USB: NO HAY MEDIO DE ALMACENAMIENTO LISTO.\n"); return; }
        if (mass_write_protected) {
            say("USB: EL MEDIO ESTA PROTEGIDO CONTRA ESCRITURA; NO SE ESCRIBE.\n");
            return;
        }
        const char *rest = after_word(arg);
        uint64_t lba = 0;
        const char *text = rest;
        while (*text && *text != ' ') text++;
        char number[24];
        unsigned digits = (unsigned)(text - rest);
        if (!digits || digits >= sizeof(number)) { say("USO: USB ESCRIBIR <SECTOR> <TEXTO>\n"); return; }
        for (unsigned i = 0; i < digits; i++) number[i] = rest[i];
        number[digits] = 0;
        if (!udecimal(number, &lba) || lba >= mass_blocks) {
            say("USO: USB ESCRIBIR <SECTOR> <TEXTO>\n");
            return;
        }
        while (*text == ' ') text++;
        zero(xfer_page.virtual, 512);
        unsigned length = 0;
        while (text[length] && length < 511) length++;
        copy_bytes(xfer_page.virtual, text, length);
        if (!usb_write((uint32_t)lba, xfer_page.virtual)) {
            say("USB: ESCRITURA FALLIDA\n");
            return;
        }
        console_write("USB: SECTOR "); console_dec(lba);
        console_write(" ESCRITO BYTES="); console_dec(length); console_putc('\n');
        return;
    }
    if (word_is(arg, "LISTAR") || word_is(arg, "LS")) {
        usb_fat_list(after_word(arg));
        return;
    }
    if (word_is(arg, "VER") || word_is(arg, "CAT")) {
        const char *name = after_word(arg);
        if (!*name || !usb_fat_cat(name)) {
            if (*name) say("USB: ARCHIVO NO ENCONTRADO EN FAT.\n");
            else say("USO: USB VER <ARCHIVO>\n");
        }
        return;
    }
    if (word_is(arg, "IMPORTAR") || word_is(arg, "IMPORT")) {
        const char *name = after_word(arg);
        if (!*name || !usb_fat_import(name)) {
            if (*name) say("USB: NO SE PUDO IMPORTAR EL ARCHIVO.\n");
            else say("USO: USB IMPORTAR <ARCHIVO>\n");
        }
        return;
    }
    say("USO: USB [INFO|PUERTOS|LISTAR|VER <A>|IMPORTAR <A>|LEER <S>|ESCRIBIR <S> <T>|REINICIAR]\n");
}
