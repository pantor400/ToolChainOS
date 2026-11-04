#include "net.h"
#include "console.h"
#include "interrupts.h"
#include "io.h"
#include "pmm.h"
#include "util.h"

/* Registros del RTL8139 (solo E/S, 256 bytes). */
#define R_IDR0    0x00
#define R_TSD0    0x10
#define R_TSAD0   0x20
#define R_RBSTART 0x30
#define R_CR      0x37
#define R_CAPR    0x38
#define R_CBR     0x3a
#define R_IMR     0x3c
#define R_ISR     0x3e
#define R_RCR     0x44
#define R_CONFIG1 0x52
#define R_BMSR    0x64

#define RX_SIZE 8192u
#define TX_SIZE 2048u

#define ETH_IPV4 0x0800u
#define ETH_ARP  0x0806u
#define IP_ICMP  1u
#define IP_UDP   17u

static uint16_t io_base;
static net_info_t info;
static uint8_t *ring;          /* buffer de recepcion (virtual) */
static uint8_t *tx_buffer;     /* 4 buffers de transmision de TX_SIZE */
static uint64_t tx_physical;
static unsigned tx_index;      /* descriptor TX en uso (0-3, round-robin) */
static int initialised;

/* Esperas activas: las rellena el comando y las resuelve handle_frame(). */
static uint8_t arp_want[4];
static uint8_t arp_reply_mac[6];
static volatile int arp_got;
static volatile uint16_t icmp_want_id, icmp_want_seq;
static volatile uint64_t icmp_reply_ticks;
static volatile int icmp_got;
static volatile uint16_t dns_want_id;
static volatile int dns_got;
static uint8_t dns_reply[512];
static volatile unsigned dns_reply_len;
static uint16_t next_id = 0x4f53u;  /* "OS": identifica las consultas del kernel */
static uint16_t ip_id;

/* --- PCI por puertos 0xCF8/0xCFC: no hace falta ACPI ni MMIO. --- */
static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (off & 0xfc);
    outl(0xcf8, address);
    return inl(0xcfc);
}
static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t value) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) | (off & 0xfc);
    outl(0xcf8, address);
    outl(0xcfc, value);
}

static int pci_find(uint16_t vendor, uint16_t device) {
    for (unsigned bus = 0; bus < 8; bus++)
        for (unsigned slot = 0; slot < 32; slot++)
            for (unsigned func = 0; func < 8; func++) {
                uint32_t id = pci_read((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0);
                if ((uint16_t)id == 0xffff) {
                    if (func == 0) break;
                    continue;
                }
                if ((uint16_t)id == vendor && (id >> 16) == device) {
                    info.pci_bus = (uint8_t)bus;
                    info.pci_slot = (uint8_t)slot;
                    info.pci_func = (uint8_t)func;
                    return 1;
                }
            }
    return 0;
}

static void mac_text(const uint8_t *mac, char *out, size_t capacity) {
    const char *hex = "0123456789ABCDEF";
    size_t used = 0;
    for (unsigned i = 0; i < 6 && used + 4 < capacity; i++) {
        if (i) out[used++] = ':';
        out[used++] = hex[(mac[i] >> 4) & 15];
        out[used++] = hex[mac[i] & 15];
    }
    if (capacity) out[used < capacity ? used : capacity - 1] = 0;
}

static void ip_text(const uint8_t *ip, char *out, size_t capacity) {
    size_t used = 0;
    for (unsigned i = 0; i < 4; i++) {
        if (i && used + 1 < capacity) out[used++] = '.';
        char digits[4];
        unsigned count = 0, value = ip[i];
        do { digits[count++] = (char)('0' + value % 10); value /= 10; } while (value);
        while (count && used + 1 < capacity) out[used++] = digits[--count];
    }
    if (capacity) out[used < capacity ? used : capacity - 1] = 0;
}

static void copy_bytes(void *dst, const void *src, size_t count) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (count--) *d++ = *s++;
}

static void read_mac(void) {
    for (unsigned i = 0; i < 6; i++) info.mac[i] = inb((uint16_t)(io_base + R_IDR0 + i));
}

static void reset_chip(void) {
    outb((uint16_t)(io_base + R_CONFIG1), 0x00); /* encender */
    outb((uint16_t)(io_base + R_CR), 0x10);      /* reset por software */
    for (unsigned i = 0; i < 100000 && (inb((uint16_t)(io_base + R_CR)) & 0x10); i++)
        __asm__ volatile("pause");
}

int net_init(void) {
    zero(&info, sizeof(info));
    info.ip[0] = 10; info.ip[1] = 0; info.ip[2] = 2; info.ip[3] = 15;
    info.gateway[0] = 10; info.gateway[1] = 0; info.gateway[2] = 2; info.gateway[3] = 2;
    info.dns[0] = 10; info.dns[1] = 0; info.dns[2] = 2; info.dns[3] = 3;
    initialised = 0;
    if (!pci_find(0x10ec, 0x8139)) return 0;
    uint32_t bar = pci_read(info.pci_bus, info.pci_slot, info.pci_func, 0x10);
    if (!(bar & 1)) return 0; /* se exige BAR de E/S, no MMIO */
    io_base = (uint16_t)(bar & 0xfffc);
    /* Comando PCI: habilitar E/S (bit 0) y bus master (bit 2) para DMA. */
    uint32_t command = pci_read(info.pci_bus, info.pci_slot, info.pci_func, 0x04);
    pci_write(info.pci_bus, info.pci_slot, info.pci_func, 0x04, command | 0x05);
    info.vendor_id = 0x10ec;
    info.device_id = (uint16_t)pci_read(info.pci_bus, info.pci_slot, info.pci_func, 0x00) >> 16;
    info.io_base = io_base;

    reset_chip();
    read_mac();
    info.present = 1;

    /* Ring de recepcion de 8 KiB + 16 de margen y 4 descriptores de transmision. */
    uint64_t rx_pages = (RX_SIZE + 16 + PMM_PAGE - 1) / PMM_PAGE;
    uint64_t rx_physical = pmm_alloc_run(rx_pages);
    uint64_t tx_pages = (TX_SIZE * 4 + PMM_PAGE - 1) / PMM_PAGE;
    tx_physical = pmm_alloc_run(tx_pages);
    if (!rx_physical || !tx_physical) {
        if (rx_physical) pmm_free_run(rx_physical, rx_pages);
        if (tx_physical) pmm_free_run(tx_physical, tx_pages);
        info.present = 0;
        return 0;
    }
    ring = pmm_ptr(rx_physical);
    tx_buffer = pmm_ptr(tx_physical);
    zero(ring, RX_SIZE + 16);
    zero(tx_buffer, TX_SIZE * 4);

    outl((uint16_t)(io_base + R_RBSTART), (uint32_t)rx_physical);
    outl((uint16_t)(io_base + R_RCR), 0x0f | (1u << 7)); /* acepta todo + WRAP */
    outb((uint16_t)(io_base + R_CR), 0x0c);              /* receptor + transmisor */
    tx_index = 0;
    for (unsigned i = 0; i < 4; i++)
        outl((uint16_t)(io_base + R_TSAD0 + 4 * i), (uint32_t)(tx_physical + i * TX_SIZE));
    outw((uint16_t)(io_base + R_IMR), 0);                /* todo por sondeo */
    outw((uint16_t)(io_base + R_ISR), 0xffff);
    info.link = (inw((uint16_t)(io_base + R_BMSR)) & 0x04) != 0;
    initialised = 1;
    return 1;
}

/* La 8139 rota entre cuatro descriptores: escribir siempre TSD0 deja la cola
 * parada tras el primer envio y QEMU responde igual que el chip real. */
static int transmit(const uint8_t *frame, size_t length) {
    if (!initialised || length > TX_SIZE) return 0;
    unsigned slot = tx_index;
    uint8_t *buffer = tx_buffer + slot * TX_SIZE;
    copy_bytes(buffer, frame, length);
    unsigned size = length < 60 ? 60u : (unsigned)length;
    if (size > length) zero(buffer + length, size - length); /* relleno minimo */
    outl((uint16_t)(io_base + R_TSD0 + 4 * slot), size);
    tx_index = (slot + 1) & 3u;
    for (unsigned i = 0; i < 200000; i++) {
        uint32_t status = inl((uint16_t)(io_base + R_TSD0 + 4 * slot));
        if (status & (1u << 15)) { /* TOK/TxStatOK: transmitido */
            info.tx_frames++;
            return 1;
        }
        __asm__ volatile("pause");
    }
    info.tx_errors++;
    return 0;
}

/* Suma de comprobacion de Internet (RFC 1071): cabecera IPv4 e ICMP. */
static uint16_t checksum16(const uint8_t *data, unsigned length) {
    uint32_t sum = 0;
    for (unsigned i = 0; i + 1 < length; i += 2) sum += (uint32_t)((data[i] << 8) | data[i + 1]);
    if (length & 1u) sum += (uint32_t)data[length - 1] << 8;
    while (sum >> 16) sum = (sum & 0xffffu) + (sum >> 16);
    return (uint16_t)(~sum & 0xffffu);
}

static int same_ip(const uint8_t *left, const uint8_t *right) {
    for (unsigned i = 0; i < 4; i++) if (left[i] != right[i]) return 0;
    return 1;
}

/* Ethernet + IPv4 + carga (ICMP o UDP). El checksum UDP queda a cero, que es
 * valido en IPv4 y evita repetir la pseudo-cabecera. */
static unsigned build_ipv4(uint8_t *frame, const uint8_t *dst_mac, const uint8_t *dst_ip,
                           uint8_t protocol, const uint8_t *payload, unsigned payload_len) {
    for (unsigned i = 0; i < 6; i++) frame[i] = dst_mac[i];
    for (unsigned i = 0; i < 6; i++) frame[6 + i] = info.mac[i];
    frame[12] = (uint8_t)(ETH_IPV4 >> 8); frame[13] = (uint8_t)(ETH_IPV4 & 0xff);
    uint8_t *ip = frame + 14;
    unsigned total = 20 + payload_len;
    ip[0] = 0x45; ip[1] = 0;
    ip[2] = (uint8_t)(total >> 8); ip[3] = (uint8_t)(total & 0xff);
    ip_id++;
    ip[4] = (uint8_t)(ip_id >> 8); ip[5] = (uint8_t)(ip_id & 0xff);
    ip[6] = 0; ip[7] = 0;                  /* sin fragmentacion */
    ip[8] = 64; ip[9] = protocol;
    ip[10] = 0; ip[11] = 0;
    for (unsigned i = 0; i < 4; i++) { ip[12 + i] = info.ip[i]; ip[16 + i] = dst_ip[i]; }
    uint16_t sum = checksum16(ip, 20);
    ip[10] = (uint8_t)(sum >> 8); ip[11] = (uint8_t)(sum & 0xff);
    for (unsigned i = 0; i < payload_len; i++) ip[20 + i] = payload[i];
    return 14 + total;
}

static void build_arp_request(uint8_t *frame, const uint8_t *target) {
    for (unsigned i = 0; i < 6; i++) frame[i] = 0xff;
    for (unsigned i = 0; i < 6; i++) frame[6 + i] = info.mac[i];
    frame[12] = (uint8_t)(ETH_ARP >> 8); frame[13] = (uint8_t)(ETH_ARP & 0xff);
    frame[14] = 0x00; frame[15] = 0x01;            /* Ethernet */
    frame[16] = 0x08; frame[17] = 0x00;            /* IPv4 */
    frame[18] = 6; frame[19] = 4;                  /* longitudes */
    frame[20] = 0x00; frame[21] = 0x01;            /* request */
    for (unsigned i = 0; i < 6; i++) frame[22 + i] = info.mac[i];
    for (unsigned i = 0; i < 4; i++) { frame[28 + i] = info.ip[i]; arp_want[i] = target[i]; }
    for (unsigned i = 0; i < 6; i++) frame[32 + i] = 0;
    for (unsigned i = 0; i < 4; i++) frame[38 + i] = target[i];
    zero(frame + 42, 60 - 42);
}

/* Resuelve la MAC de cualquier IPv4 del segmento: la red de usuario de QEMU
 * responde tambien por la puerta y por el DNS. */
static int arp_resolve(const uint8_t *target, uint8_t *mac_out) {
    if (!initialised) return 0;
    uint8_t frame[60];
    build_arp_request(frame, target);
    arp_got = 0;
    if (!transmit(frame, sizeof(frame))) return 0;
    uint64_t deadline = timer_ticks() + 50; /* 0,5 s a 100 Hz */
    while (timer_ticks() < deadline) {
        net_poll();
        if (arp_got) {
            for (unsigned i = 0; i < 6; i++) mac_out[i] = arp_reply_mac[i];
            return 1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

/* Demultiplexa Ethernet: ARP reply, ICMP echo reply y UDP del puerto 53.
 * Todo lo demas (TCP, IPv6...) se descarta: no hay pila TCP/IP. */
static void handle_frame(const uint8_t *frame, unsigned length) {
    if (length < 42) { info.rx_errors++; return; }
    unsigned type = ((unsigned)frame[12] << 8) | frame[13];
    if (type == ETH_ARP) {
        if (frame[20] != 0x00 || frame[21] != 0x02) return; /* solo reply */
        const uint8_t *sender_mac = frame + 22, *sender_ip = frame + 28;
        if (!same_ip(sender_ip, arp_want)) return;
        for (unsigned i = 0; i < 6; i++) arp_reply_mac[i] = sender_mac[i];
        arp_got = 1;
        if (same_ip(sender_ip, info.gateway)) {
            for (unsigned i = 0; i < 6; i++) info.peer_mac[i] = sender_mac[i];
            info.arp_ok = 1;
        }
        return;
    }
    if (type != ETH_IPV4 || length < 34) return;
    const uint8_t *ip = frame + 14;
    unsigned header = (unsigned)(ip[0] & 15u) * 4u;
    if ((ip[0] >> 4) != 4 || header < 20 || 14 + header + 8 > length) return;
    const uint8_t *transport = ip + header;
    if (ip[9] == IP_ICMP) {
        if (transport[0] != 0) return;                     /* solo echo reply */
        uint16_t id = (uint16_t)(((unsigned)transport[4] << 8) | transport[5]);
        uint16_t seq = (uint16_t)(((unsigned)transport[6] << 8) | transport[7]);
        if (!icmp_got && id == icmp_want_id && seq == icmp_want_seq) {
            icmp_reply_ticks = timer_ticks();
            icmp_got = 1;
        }
        return;
    }
    if (ip[9] == IP_UDP) {
        unsigned udp_len = ((unsigned)transport[4] << 8) | transport[5];
        unsigned sport = ((unsigned)transport[0] << 8) | transport[1];
        if (udp_len < 8 || 14 + header + udp_len > length || sport != 53) return;
        unsigned copy = udp_len - 8;
        if (copy > (unsigned)sizeof(dns_reply)) copy = (unsigned)sizeof(dns_reply);
        if (copy < 12) return;
        if ((((unsigned)transport[8] << 8) | transport[9]) != dns_want_id) return;
        for (unsigned i = 0; i < copy; i++) dns_reply[i] = transport[8 + i];
        dns_reply_len = copy;
        dns_got = 1;
    }
}

void net_poll(void) {
    if (!initialised) return;
    uint16_t capr = inw((uint16_t)(io_base + R_CAPR));
    uint16_t cbr = inw((uint16_t)(io_base + R_CBR));
    for (unsigned guard = 0; guard < 16 && capr != cbr; guard++) {
        uint16_t offset = (uint16_t)((capr + 16) % RX_SIZE);
        volatile uint16_t *header = (volatile uint16_t *)(ring + offset);
        uint16_t status = header[0];
        uint16_t length = header[1];
        if (!(status & 1) || length < 4 || length > RX_SIZE) { /* no completo */
            info.rx_errors++;
            break;
        }
        unsigned payload = (unsigned)length - 4;
        if (offset + 4 + payload > RX_SIZE + 16) { info.rx_errors++; break; }
        handle_frame(ring + offset + 4, payload);
        info.rx_frames++;
        uint16_t advance = (uint16_t)((payload + 4 + 3) & ~3u);
        capr = (uint16_t)((offset + 4 + advance - 16 + RX_SIZE) % RX_SIZE);
        outw((uint16_t)(io_base + R_CAPR), capr);
        info.link = (inw((uint16_t)(io_base + R_BMSR)) & 0x04) != 0;
    }
}

int net_arp_probe(void) {
    uint8_t mac[6];
    if (!initialised) return 0;
    info.arp_ok = 0;
    if (!arp_resolve(info.gateway, mac)) return 0;
    for (unsigned i = 0; i < 6; i++) info.peer_mac[i] = mac[i];
    info.arp_ok = 1;
    return 1;
}

/* --- IPv4: ICMP ping y DNS por UDP -------------------------------------- */

static int parse_ip(const char *text, uint8_t out[4]) {
    for (unsigned part = 0; part < 4; part++) {
        if (*text < '0' || *text > '9') return 0;
        unsigned value = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10u + (unsigned)(*text - '0');
            if (value > 255) return 0;
            text++;
        }
        out[part] = (uint8_t)value;
        if (part < 3) {
            if (*text != '.') return 0;
            text++;
        }
    }
    return *text == 0;
}

static int ping_ip(const uint8_t *target) {
    if (!initialised) return 0;
    uint8_t mac[6];
    if (!arp_resolve(target, mac)) return 0;
    uint8_t payload[32];
    zero(payload, sizeof(payload));
    payload[0] = 8;                              /* echo request */
    next_id++;
    uint16_t id = next_id, seq = next_id;
    payload[4] = (uint8_t)(id >> 8); payload[5] = (uint8_t)(id & 0xff);
    payload[6] = (uint8_t)(seq >> 8); payload[7] = (uint8_t)(seq & 0xff);
    for (unsigned i = 8; i < sizeof(payload); i++) payload[i] = (uint8_t)i;
    uint16_t sum = checksum16(payload, sizeof(payload));
    payload[2] = (uint8_t)(sum >> 8); payload[3] = (uint8_t)(sum & 0xff);

    uint8_t frame[64];
    unsigned length = build_ipv4(frame, mac, target, (uint8_t)IP_ICMP, payload, sizeof(payload));
    icmp_want_id = id;
    icmp_want_seq = seq;
    icmp_got = 0;
    info.pings++;
    if (!transmit(frame, length)) return 0;
    uint64_t start = timer_ticks();
    uint64_t deadline = start + 100;             /* 1 s a 100 Hz */
    for (unsigned i = 0; i < 4; i++) info.ping_ip[i] = target[i];
    while (timer_ticks() < deadline) {
        net_poll();
        if (icmp_got) {
            info.ping_ok = 1;
            info.ping_ms = (unsigned)((icmp_reply_ticks - start) * 10u);
            return 1;
        }
        __asm__ volatile("pause");
    }
    info.ping_ok = 0;
    info.ping_ms = 0;
    return 0;
}

int net_ping_text(const char *ip, unsigned *rtt_ms) {
    uint8_t target[4];
    if (!ip || !parse_ip(ip, target)) return -1;
    int ok = ping_ip(target);
    if (rtt_ms) *rtt_ms = info.ping_ms;
    return ok;
}

static unsigned dns_encode_name(uint8_t *out, unsigned capacity, const char *name) {
    unsigned used = 0, start = 0;
    while (name[start]) {
        unsigned label = 0;
        while (name[start + label] && name[start + label] != '.') label++;
        if (!label || label > 63 || used + 1 + label >= capacity) return 0;
        out[used++] = (uint8_t)label;
        for (unsigned i = 0; i < label; i++) out[used++] = (uint8_t)name[start + i];
        start += label;
        if (name[start] == '.') start++;
    }
    if (!used || used + 1 > capacity) return 0;
    out[used++] = 0;
    return used;
}

/* Salta un nombre DNS, incluidos los punteros de compresion (0xC0xx). */
static unsigned dns_skip_name(const uint8_t *message, unsigned length, unsigned position) {
    while (position < length) {
        uint8_t value = message[position];
        if (!value) return position + 1;
        if ((value & 0xc0u) == 0xc0u) return position + 2;
        position += (unsigned)value + 1;
    }
    return length;
}

static int dns_first_ipv4(const uint8_t *message, unsigned length, uint16_t id, uint8_t out[4]) {
    if (length < 12) return 0;
    if ((((unsigned)message[0] << 8) | message[1]) != id) return 0;
    if (!(message[2] & 0x80u)) return 0;                 /* debe ser respuesta */
    unsigned questions = ((unsigned)message[4] << 8) | message[5];
    unsigned answers = ((unsigned)message[6] << 8) | message[7];
    unsigned position = 12;
    for (unsigned i = 0; i < questions && position < length; i++)
        position = dns_skip_name(message, length, position) + 4;
    for (unsigned i = 0; i < answers && position + 10 <= length; i++) {
        position = dns_skip_name(message, length, position);
        if (position + 10 > length) return 0;
        unsigned type = ((unsigned)message[position] << 8) | message[position + 1];
        unsigned klass = ((unsigned)message[position + 2] << 8) | message[position + 3];
        unsigned size = ((unsigned)message[position + 8] << 8) | message[position + 9];
        position += 10;
        if (position + size > length) return 0;
        if (type == 1 && klass == 1 && size == 4) {
            for (unsigned k = 0; k < 4; k++) out[k] = message[position + k];
            return 1;
        }
        position += size;
    }
    return 0;
}

int net_dns_text(const char *name, uint8_t out[4]) {
    if (!initialised || !name || !*name) return 0;
    uint8_t mac[6];
    if (!arp_resolve(info.dns, mac)) return 0;
    uint8_t payload[160];
    zero(payload, sizeof(payload));
    next_id++;
    uint16_t id = next_id;
    unsigned sport = 0xc000u + (unsigned)(id & 0x3fffu);
    payload[0] = (uint8_t)(sport >> 8); payload[1] = (uint8_t)(sport & 0xff);
    payload[2] = 0; payload[3] = 53;
    uint8_t *query = payload + 8;
    query[0] = (uint8_t)(id >> 8); query[1] = (uint8_t)(id & 0xff);
    query[2] = 0x01;                                        /* recursion deseada */
    query[5] = 1;                                           /* 1 pregunta */
    unsigned used = 12 + dns_encode_name(query + 12, sizeof(payload) - 8 - 12 - 4, name);
    if (used <= 12) return 0;
    query[used++] = 0; query[used++] = 1;                   /* tipo A */
    query[used++] = 0; query[used++] = 1;                   /* clase IN */
    unsigned total = 8 + used;
    payload[4] = (uint8_t)(total >> 8); payload[5] = (uint8_t)(total & 0xff);
    uint8_t frame[256];
    unsigned length = build_ipv4(frame, mac, info.dns, (uint8_t)IP_UDP, payload, total);
    dns_want_id = id;
    dns_got = 0;
    dns_reply_len = 0;
    info.dns_queries++;
    if (!transmit(frame, length)) return 0;
    uint64_t deadline = timer_ticks() + 300;                /* 3 s */
    while (timer_ticks() < deadline) {
        net_poll();
        if (dns_got) break;
        __asm__ volatile("pause");
    }
    info.resolved_ok = 0;
    if (!dns_got || !dns_first_ipv4(dns_reply, dns_reply_len, id, info.resolved)) return 0;
    info.resolved_ok = 1;
    copystr(info.resolved_name, name, sizeof(info.resolved_name));
    for (unsigned i = 0; i < 4; i++) out[i] = info.resolved[i];
    return 1;
}

net_info_t net_status(void) { return info; }

void net_mac_text(char *out, size_t capacity) { mac_text(info.mac, out, capacity); }
void net_ip_text(char *out, size_t capacity) { ip_text(info.ip, out, capacity); }
void net_format_ip(const uint8_t ip[4], char *out, size_t capacity) { ip_text(ip, out, capacity); }

/* Prefijo sin distinguir mayusculas: la shell pasa "PING <ip>" como args. */
static int ci_prefix(const char *text, const char *prefix) {
    while (*prefix) {
        char a = *text++, b = *prefix++;
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b) return 0;
    }
    return 1;
}

static void command_internet(void) {
    uint8_t address[4];
    char text[24];
    unsigned ms = 0;
    console_write("INTERNET: PUERTA 10.0.2.2 ...\n");
    int ok = net_ping_text("10.0.2.2", &ms);
    if (ok < 0) { console_write("INTERNET: IP DE PUERTA INVALIDA.\n"); return; }
    if (ok) {
        console_write("  PING 10.0.2.2 OK EN ");
        if (ms) { console_dec(ms); console_write(" MS\n"); }
        else console_write("<10 MS\n");
    } else {
        console_write("  PING 10.0.2.2 SIN RESPUESTA: REVISA -netdev user Y EL ENLACE.\n");
        return;
    }
    console_write("INTERNET: DNS 10.0.2.3 EXAMPLE.COM ...\n");
    if (!net_dns_text("example.com", address)) {
        console_write("  DNS SIN RESPUESTA: EL ANFITRION NO RESUELVE O NO HAY INTERNET.\n");
        return;
    }
    net_format_ip(address, text, sizeof(text));
    console_write("  DNS EXAMPLE.COM = "); console_write(text); console_putc('\n');
    console_write("INTERNET: RED DE USUARIO DE QEMU OPERATIVA (ICMP + DNS).\n");
}

void net_command(const char *args) {
    const char *argument = args;
    while (*argument == ' ') argument++;
    if (ci_same(argument, "ARP")) {
        int ok = net_arp_probe();
        console_write(ok ? "ARP: RESPUESTA RECIBIDA\n" : "ARP: SIN RESPUESTA (ENLACE O RED DE QEMU)\n");
        return;
    }
    if (!info.present) {
        console_write("RED: NO SE ENCONTRO RTL8139 EN EL BUS PCI.\n");
        console_write("ARRANCA CON -device rtl8139,netdev=... PARA PROBARLO.\n");
        return;
    }
    if (ci_prefix(argument, "PING")) {
        const char *target = argument + 4;
        while (*target == ' ') target++;
        if (!*target) {
            console_write("USO: RED PING <IP>   EJEMPLO: RED PING 10.0.2.2\n");
            return;
        }
        unsigned ms = 0;
        int ok = net_ping_text(target, &ms);
        if (ok < 0) { console_write("PING: IP INVALIDA\n"); return; }
        console_write("PING: "); console_write(target);
        if (ok) {
            console_write(" RESPUESTA EN ");
            if (ms) { console_dec(ms); console_write(" MS\n"); }
            else console_write("<10 MS\n");
        } else {
            console_write(" SIN RESPUESTA\n");
        }
        return;
    }
    if (ci_prefix(argument, "DNS")) {
        const char *name = argument + 3;
        while (*name == ' ') name++;
        if (!*name) {
            console_write("USO: RED DNS <NOMBRE>   EJEMPLO: RED DNS example.com\n");
            return;
        }
        uint8_t address[4];
        char text[24];
        console_write("DNS: CONSULTANDO "); console_write(name); console_write("...\n");
        if (!net_dns_text(name, address)) {
            console_write("DNS: SIN RESPUESTA DE 10.0.2.3\n");
            return;
        }
        net_format_ip(address, text, sizeof(text));
        console_write("DNS: "); console_write(name); console_write(" = ");
        console_write(text); console_putc('\n');
        return;
    }
    if (ci_prefix(argument, "INTERNET") || ci_prefix(argument, "INET")) {
        command_internet();
        return;
    }
    char mac[24], ip[20], peer[24], dns[24], host[24];
    mac_text(info.mac, mac, sizeof(mac));
    ip_text(info.ip, ip, sizeof(ip));
    mac_text(info.peer_mac, peer, sizeof(peer));
    ip_text(info.dns, dns, sizeof(dns));
    ip_text(info.gateway, host, sizeof(host));
    console_write("RED: RTL8139 PCI ");
    console_dec(info.pci_bus); console_putc(':'); console_dec(info.pci_slot);
    console_putc('.'); console_dec(info.pci_func);
    console_write(" IO="); console_hex(io_base);
    console_write("\nMAC="); console_write(mac);
    console_write(" IP="); console_write(ip);
    console_write(" PUERTA="); console_write(host);
    console_write(" DNS="); console_write(dns);
    console_write("\nENLACE="); console_write(info.link ? "SI" : "NO");
    console_write(" ARP="); console_write(info.arp_ok ? peer : "PENDIENTE");
    console_write(" PINGS="); console_dec(info.pings);
    console_write(" DNS_CONSULTAS="); console_dec(info.dns_queries);
    console_write("\nTX="); console_dec(info.tx_frames);
    console_write(" RX="); console_dec(info.rx_frames);
    console_write(" ERR_TX="); console_dec(info.tx_errors);
    console_write(" ERR_RX="); console_dec(info.rx_errors);
    console_write("\nRED: ETHERNET + ARP + ICMP + DNS; SIN TCP/IP NI DHCP (IP FIJA 10.0.2.15/24).\n");
    console_write("USO: RED ARP | RED PING <IP> | RED DNS <NOMBRE> | RED INTERNET\n");
}
