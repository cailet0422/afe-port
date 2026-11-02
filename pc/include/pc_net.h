/* pc_net.h - UDP sockets and addresses (IPv4) for the multiplayer (pc_mp.c)
 *
 * Plain C, no game code, so the unit tests use it as it is. Windows uses
 * Winsock (ws2_32), everything else BSD sockets. Sockets are non-blocking. */
#ifndef PC_NET_H
#define PC_NET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t ip;   /* host byte order: 127.0.0.1 = 0x7F000001 */
    uint16_t port; /* host byte order */
} PcNetAddr;

typedef struct PcNetSocket PcNetSocket;

int pc_net_init(void);

/* A UDP socket bound to port (0: any), broadcast allowed. NULL on failure. */
PcNetSocket* pc_net_open(uint16_t port);
/* The same, sharing the port with other programs (the LAN discovery port) */
PcNetSocket* pc_net_open_shared(uint16_t port);
void pc_net_close(PcNetSocket* s);
/* The port it is bound to */
uint16_t pc_net_port(PcNetSocket* s);

/* 1 if sent */
int pc_net_send(PcNetSocket* s, const PcNetAddr* to, const void* data, size_t len);
/* Bytes received (>0), 0 if nothing is waiting, -1 on error */
int pc_net_recv(PcNetSocket* s, PcNetAddr* from, void* buf, size_t cap);

/* Joins/receives the multicast group (SSDP: 239.255.255.250) */
int pc_net_join_multicast(PcNetSocket* s, uint32_t group);

/* Blocking name lookup ("router.bittorrent.com"); 1 on success. Call it
 * from a thread. */
int pc_net_resolve(const char* host, uint16_t port, PcNetAddr* out);

/* The address this machine uses to reach the internet (or the LAN), from a
 * connected UDP socket: no packet is sent. 0 if none. */
uint32_t pc_net_local_ip(void);

/* The directed broadcast addresses of this machine's IPv4 networks (and
 * 255.255.255.255), up to max; returns how many */
int pc_net_broadcast_addrs(uint32_t* out, int max);

int pc_net_addr_equal(const PcNetAddr* a, const PcNetAddr* b);
/* "192.168.0.2:47070" */
void pc_net_addr_str(const PcNetAddr* a, char* buf, size_t n);
/* "1.2.3.4" or "1.2.3.4:5"; 1 on success */
int pc_net_addr_parse(const char* s, uint16_t default_port, PcNetAddr* out);
/* 10/8, 172.16/12, 192.168/16, 100.64/10, 127/8, 169.254/16 */
int pc_net_ip_private(uint32_t ip);

/* Milliseconds from some fixed moment */
uint32_t pc_net_ms(void);
void pc_net_sleep(uint32_t ms);

/* --- Blocking TCP, for UPnP (pc_mp_upnp.c) --- */
/* Sends request to ip:port and reads the reply into buf (NUL-terminated);
 * returns its length or -1. timeout_ms for each step. */
int pc_net_tcp_exchange(const PcNetAddr* to, const char* request, size_t req_len, char* buf, size_t cap,
                        int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* PC_NET_H */
