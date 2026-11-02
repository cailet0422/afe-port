/* pc_dht.h - finding rooms on the internet without a server of our own
 *
 * The BitTorrent "Mainline" DHT (BEP 5) is a public table of peers kept by
 * millions of nodes: anyone can announce "I am at ip:port for key K" and
 * anyone can look up who announced K. The multiplayer uses it as its lobby:
 *   - a public room announces itself under a key everybody knows, guests
 *     look that key up and then ask each address for its room (pc_mp.c);
 *   - for NAT hole punching, a guest announces itself under a key made from
 *     the room it wants; the room looks that key up and sends a packet to
 *     each guest it finds, which opens its router for the guest's packets.
 * It runs as a read-only node ("ro": BEP 43) on the game's own UDP socket,
 * so the address it announces is the one the game is reached at
 * (implied_port). STUN (RFC 5389) asks a public server what that address
 * looks like from outside. No game code: the unit tests run it. */
#ifndef PC_DHT_H
#define PC_DHT_H

#include "pc_net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PcDht PcDht;

#define PC_DHT_MAX_LOOKUPS 4

PcDht* pc_dht_new(PcNetSocket* sock);
void pc_dht_free(PcDht* d);

/* Where lookups start (resolved router.bittorrent.com etc.) */
void pc_dht_add_bootstrap(PcDht* d, const PcNetAddr* addr);
int pc_dht_bootstrap_count(PcDht* d);

/* A packet from the socket; 1 if it was the DHT's (bencoded) */
int pc_dht_handle(PcDht* d, const PcNetAddr* from, const uint8_t* data, int len);

/* Sends what is due; call every frame */
void pc_dht_tick(PcDht* d, uint32_t now_ms);

/* Starts a lookup of key in slot (0..PC_DHT_MAX_LOOKUPS-1), replacing
 * what was there. announce: once found, announce this socket under key. */
void pc_dht_lookup(PcDht* d, int slot, const uint8_t key[20], int announce);
/* After pc_dht_lookup: announce port (one the router forwards) instead of
 * the port the nodes see the packets come from */
void pc_dht_lookup_port(PcDht* d, int slot, uint16_t port);
/* 1 while running */
int pc_dht_lookup_running(PcDht* d, int slot);
/* The peers found so far; returns how many */
int pc_dht_lookup_peers(PcDht* d, int slot, PcNetAddr* out, int max);
/* How many nodes answered */
int pc_dht_lookup_responses(PcDht* d, int slot);
/* 1 if the last announce reached at least one node */
int pc_dht_lookup_announced(PcDht* d, int slot);

/* Our address as nodes saw it (BEP 42 "ip"), 0 if none told us */
uint32_t pc_dht_external_ip(PcDht* d);

/* A key from text (the first 20 bytes of its SHA-256) */
void pc_dht_key(const char* text, uint8_t key[20]);

/* --- bencode (exposed for the tests) --- */
/* The value of key in the dictionary at p (end = buffer end); NULL if none */
const uint8_t* pc_bdec_dict_get(const uint8_t* p, const uint8_t* end, const char* key);
/* A string value: its bytes and length, 1 if it is one */
int pc_bdec_str(const uint8_t* p, const uint8_t* end, const uint8_t** s, int* len);
/* Past one value, NULL if malformed */
const uint8_t* pc_bdec_skip(const uint8_t* p, const uint8_t* end);

/* --- STUN --- */
/* A Binding Request into buf (20 bytes); txid gets its id */
int pc_stun_request(uint8_t* buf, uint8_t txid[12]);
/* 1 if data is the response to txid; mapped gets the address it saw */
int pc_stun_parse(const uint8_t* data, int len, const uint8_t txid[12], PcNetAddr* mapped);

#ifdef __cplusplus
}
#endif

#endif /* PC_DHT_H */
