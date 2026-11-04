#ifndef NET_H
#define NET_H
#include <stdint.h>
#include <stddef.h>

/* Driver RTL8139 por puertos de E/S: no requiere mapear BAR de MMIO, que es
 * el unico tipo de registro que el HHDM de Limine no garantiza cubrir.
 * Sobre Ethernet + ARP se añade lo mínimo para comprobar Internet: IPv4 con
 * ICMP (ping) y UDP para consultas DNS. No hay TCP/IP ni DHCP (la IP es fija,
 * pensada para la red de usuario de QEMU: 10.0.2.15/24, puerta 10.0.2.2 y
 * DNS 10.0.2.3). */

typedef struct {
    int present;             /* dispositivo PCI encontrado e inicializado */
    int link;                /* cable/enlace activo */
    int arp_ok;              /* se obtuvo la MAC de la puerta */
    int resolved_ok;         /* ultima consulta DNS correcta */
    int ping_ok;             /* ultimo ping con respuesta */
    uint16_t vendor_id, device_id;
    uint16_t io_base;
    uint8_t pci_bus, pci_slot, pci_func;
    uint8_t mac[6];
    uint8_t peer_mac[6];     /* MAC de la puerta tras un ARP correcto */
    uint8_t ip[4], gateway[4], dns[4];
    uint8_t resolved[4];     /* ultima IP resuelta por DNS */
    uint8_t ping_ip[4];      /* destino del ultimo ping */
    unsigned ping_ms;        /* ida y vuelta del ultimo ping, en ms */
    char resolved_name[32];  /* nombre de la ultima consulta DNS */
    uint64_t tx_frames, rx_frames, tx_errors, rx_errors;
    uint64_t pings, dns_queries;
} net_info_t;

int net_init(void);
void net_poll(void);
/* Envia un ARP request a la puerta y espera la respuesta. Devuelve 1 si llego. */
int net_arp_probe(void);
/* Ping ICMP a una IPv4 textual ("10.0.2.2"). Devuelve 1 si hay respuesta,
 * 0 si no llega y -1 si la direccion no es valida. */
int net_ping_text(const char *ip, unsigned *rtt_ms);
/* Resuelve un nombre con el DNS de QEMU (UDP/53). 1 si 'out' tiene la IPv4. */
int net_dns_text(const char *name, uint8_t out[4]);
net_info_t net_status(void);
void net_format_ip(const uint8_t ip[4], char *out, size_t capacity);
void net_mac_text(char *out, size_t capacity);
void net_ip_text(char *out, size_t capacity);
void net_command(const char *args);

#endif
