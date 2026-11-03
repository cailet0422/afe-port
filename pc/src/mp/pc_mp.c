/* pc_mp.c - see pc_mp.h
 *
 * Packets: a 16-byte header (magic "AFM1", type, flags, sender's slot,
 * room id, sequence number, payload length) and a payload, little-endian.
 * Messages flagged reliable are numbered per peer, acknowledged, sent again
 * until acknowledged and handed over in order; the rest (positions, pings)
 * may be lost. A guest talks only to the host; the host keeps one peer per
 * guest and passes positions on. DHT packets (bencoded, starting with 'd')
 * and STUN answers share the socket. */
#include "pc_mp.h"
#include "pc_dht.h"
#include "ko_patch.h" /* SHA-256 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PC_PORT_VERSION
#define PC_PORT_VERSION "dev"
#endif

#define MAGIC0 'A'
#define MAGIC1 'F'
#define MAGIC2 'M'
#define MAGIC3 '1'
#define HDR 16
#define MAX_PAYLOAD 1180
#define MAX_PKT (HDR + MAX_PAYLOAD)
#define OUT_CAP 256        /* unacknowledged reliable messages per peer */
#define WINDOW 48          /* of those, sent at once */
#define REORDER 64
#define RESEND_MS 250
#define GIVE_UP_TRIES 60
#define PEER_TIMEOUT_MS 15000
#define JOIN_RETRY_MS 400
#define JOIN_TIMEOUT_MS 40000
#define PING_MS 1000
#define STATE_MS 66
#define LAN_BROADCAST_MS 2000
#define ROOM_FORGET_MS 9000
#define EVENTS 32
#define GAME_QUEUE 256
#define TOWN_CHUNK 1024

#define FLAG_RELIABLE 1

enum {
    T_DISCOVER = 1,
    T_ROOM_INFO,
    T_JOIN,
    T_JOIN_OK,
    T_JOIN_DENY,
    T_LEAVE,
    T_KICK,
    T_PLAYERS,
    T_STATE,
    T_GAME,
    T_TOWN_REQ,
    T_TOWN_DATA,
    T_CHAT,
    T_PING,
    T_PONG,
    T_ACK,
    T_PUNCH,
    T_LOOK, /* guest -> host: its look changed (clothes, tool) */
};

enum {
    DENY_FULL = 1,
    DENY_PASSWORD,
    DENY_VERSION,
    DENY_KICKED,
    DENY_NO_ROOM,
};

typedef struct {
    uint16_t seq;
    uint8_t type;
    uint16_t len;
    uint8_t* data;
    uint32_t last;
    int tries;
} Out;

typedef struct {
    int have;
    uint8_t type;
    uint16_t len;
    uint8_t* data;
} In;

typedef struct {
    int used;
    int slot;
    PcNetAddr addr;
    uint32_t last_recv;
    uint16_t out_seq;
    Out out[OUT_CAP];
    int nout;
    uint16_t in_next;
    In reorder[REORDER];
    uint16_t acks[48];
    int nacks;
    uint32_t last_ping;
    /* town transfer (host): the next chunk to queue, 0 = not asked */
    size_t town_next;
    int town_asked;
} Peer;

typedef struct {
    int from;
    uint16_t len;
    uint8_t data[PC_MP_MAX_GAME_MSG];
} GameMsg;

struct PcMp {
    PcNetSocket* sock;
    PcNetSocket* lan; /* the host's listener for broadcasts */
    PcDht* dht;
    PcMpMode mode;
    uint32_t now;

    char my_name[PC_MP_NAME_LEN];
    PcMpLook my_look;
    PcMpState my_state;
    int state_dirty;
    uint32_t state_sent;

    /* the room (hosted or joined) */
    PcMpRoomInfo room;
    PcMpHostConfig cfg;
    int my_slot;
    PcMpPlayer players[PC_MP_MAX_PLAYERS];
    Peer peers[PC_MP_MAX_PLAYERS]; /* host: by slot; guest: [0] = the host */
    PcNetAddr kicked[16];
    int nkicked;

    /* joining */
    PcNetAddr join_addr;
    uint32_t join_room;
    uint8_t join_pass[32];
    char join_password[32];
    uint32_t join_started, join_sent;

    /* browsing */
    int browsing;
    uint32_t browse_sent;
    uint32_t nonce;
    PcMpRoomInfo rooms[PC_MP_MAX_ROOMS];
    int nrooms;

    /* internet */
    PcNetAddr stun;
    uint8_t stun_tx[12];
    uint32_t stun_sent;
    PcNetAddr external;
    PcNetAddr mapped; /* the router's address and the port it forwards to us (UPnP) */
    int punch_alt;
    uint32_t dht_rooms_ms, dht_seek_ms, dht_punch_ms;
    int dht_rooms_was_running, dht_seek_was_running, dht_punch_was_running;

    PcMpEvent events[EVENTS];
    int ev_head, ev_tail;
    GameMsg* game;
    int game_head, game_tail;

    /* town */
    uint8_t* town;
    size_t town_len;
    uint8_t* town_in;
    size_t town_in_len, town_in_got;
    int town_asked;
    int town_ready;
};

/* --- Little helpers --- */

static void w16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void w32(uint8_t* p, uint32_t v) {
    w16(p, (uint16_t)v);
    w16(p + 2, (uint16_t)(v >> 16));
}
static uint16_t r16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t r32(const uint8_t* p) {
    return r16(p) | ((uint32_t)r16(p + 2) << 16);
}
static void wf(uint8_t* p, float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    w32(p, v);
}
static float rf(const uint8_t* p) {
    uint32_t v = r32(p);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static void wstr(uint8_t* p, const char* s, size_t n) {
    memset(p, 0, n);
    if (s != NULL) {
        size_t l = strlen(s);
        memcpy(p, s, l < n - 1 ? l : n - 1);
    }
}
static void rstr(char* out, const uint8_t* p, size_t n) {
    memcpy(out, p, n);
    out[n - 1] = '\0';
}

static void event(PcMp* mp, PcMpEventKind kind, int slot, const char* text) {
    PcMpEvent* e = &mp->events[mp->ev_tail];

    e->kind = kind;
    e->slot = slot;
    snprintf(e->text, sizeof(e->text), "%s", text ? text : "");
    mp->ev_tail = (mp->ev_tail + 1) % EVENTS;
    if (mp->ev_tail == mp->ev_head) mp->ev_head = (mp->ev_head + 1) % EVENTS;
}

static void pass_hash(uint32_t room_id, const char* password, uint8_t out[32]) {
    KoSha256 s;
    uint8_t id[4];

    w32(id, room_id);
    ko_sha256_init(&s);
    ko_sha256_update(&s, "afe-port room password", 22);
    ko_sha256_update(&s, id, 4);
    ko_sha256_update(&s, password, strlen(password));
    ko_sha256_final(&s, out);
}

/* --- Sending --- */

static int raw_send(PcMp* mp, const PcNetAddr* to, uint8_t type, uint8_t flags, uint16_t seq, const void* data,
                    size_t len) {
    uint8_t pkt[MAX_PKT];

    if (len > MAX_PAYLOAD) return 0;
    pkt[0] = MAGIC0;
    pkt[1] = MAGIC1;
    pkt[2] = MAGIC2;
    pkt[3] = MAGIC3;
    pkt[4] = type;
    pkt[5] = flags;
    pkt[6] = (uint8_t)mp->my_slot;
    pkt[7] = PC_MP_PROTOCOL;
    w32(pkt + 8, mp->room.room_id);
    w16(pkt + 12, seq);
    w16(pkt + 14, (uint16_t)len);
    if (len > 0) memcpy(pkt + HDR, data, len);
    return pc_net_send(mp->sock, to, pkt, HDR + len);
}

static int send_rel(PcMp* mp, Peer* p, uint8_t type, const void* data, size_t len) {
    Out* o;

    if (!p->used || len > MAX_PAYLOAD || p->nout >= OUT_CAP) return 0;
    o = &p->out[p->nout++];
    o->seq = p->out_seq++;
    o->type = type;
    o->len = (uint16_t)len;
    o->data = (uint8_t*)malloc(len ? len : 1);
    if (o->data == NULL) {
        p->nout--;
        return 0;
    }
    memcpy(o->data, data, len);
    o->tries = 0;
    o->last = 0;
    if (p->nout <= WINDOW) {
        raw_send(mp, &p->addr, type, FLAG_RELIABLE, o->seq, data, len);
        o->tries = 1;
        o->last = mp->now;
    }
    return 1;
}

static void peer_clear(Peer* p) {
    for (int i = 0; i < p->nout; i++) free(p->out[i].data);
    for (int i = 0; i < REORDER; i++) free(p->reorder[i].data);
    memset(p, 0, sizeof(*p));
}

static Peer* peer_by_addr(PcMp* mp, const PcNetAddr* a) {
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
        if (mp->peers[i].used && pc_net_addr_equal(&mp->peers[i].addr, a)) return &mp->peers[i];
    }
    return NULL;
}

/* To every guest (host) or the host (guest) */
static void send_rel_all(PcMp* mp, uint8_t type, const void* data, size_t len, int except_slot) {
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
        if (mp->peers[i].used && mp->peers[i].slot != except_slot) send_rel(mp, &mp->peers[i], type, data, len);
    }
}

static void send_unrel_all(PcMp* mp, uint8_t type, const void* data, size_t len, int except_slot) {
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
        if (mp->peers[i].used && mp->peers[i].slot != except_slot) {
            raw_send(mp, &mp->peers[i].addr, type, 0, 0, data, len);
        }
    }
}

/* --- Room info, players --- */

static int mapped_usable(PcMp* mp);

static size_t pack_room(PcMp* mp, uint8_t* p) {
    PcMpRoomInfo* r = &mp->room;
    int n = 0;

    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) n += mp->players[i].used;
    w32(p, r->room_id);
    wstr(p + 4, r->name, 48);
    wstr(p + 52, mp->my_name, PC_MP_NAME_LEN);
    wstr(p + 84, r->town, 32);
    p[116] = (uint8_t)n;
    p[117] = r->max_players;
    p[118] = (uint8_t)((r->is_public ? 1 : 0) | (r->has_password ? 2 : 0) | (mp->my_look.korean ? 4 : 0) |
                       (mapped_usable(mp) ? 8 : 0));
    p[119] = 0;
    w16(p + 120, PC_MP_PROTOCOL);
    wstr(p + 122, PC_PORT_VERSION, 16);
    return 138;
}

static void unpack_room(const uint8_t* p, PcMpRoomInfo* r) {
    memset(r, 0, sizeof(*r));
    r->room_id = r32(p);
    rstr(r->name, p + 4, 48);
    rstr(r->host_name, p + 52, PC_MP_NAME_LEN);
    rstr(r->town, p + 84, 32);
    r->players = p[116];
    r->max_players = p[117];
    r->is_public = (p[118] & 1) != 0;
    r->has_password = (p[118] & 2) != 0;
    r->korean = (p[118] & 4) != 0;
    r->protocol = r16(p + 120);
    rstr(r->version, p + 122, 16);
}

static void pack_look(uint8_t* p, const PcMpLook* l) {
    p[0] = l->gender;
    p[1] = l->face;
    p[2] = l->tan;
    p[3] = l->korean;
    w16(p + 4, l->cloth);
    w16(p + 6, l->held);
}

static void unpack_look(const uint8_t* p, PcMpLook* l) {
    l->gender = p[0];
    l->face = p[1];
    l->tan = p[2];
    l->korean = p[3];
    l->cloth = r16(p + 4);
    l->held = r16(p + 6);
}

static void send_players(PcMp* mp) {
    uint8_t buf[1 + PC_MP_MAX_PLAYERS * (1 + PC_MP_NAME_LEN + 8)];
    size_t n = 1;

    buf[0] = 0;
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
        PcMpPlayer* pl = &mp->players[i];

        if (!pl->used) continue;
        buf[0]++;
        buf[n++] = (uint8_t)pl->slot;
        wstr(buf + n, pl->name, PC_MP_NAME_LEN);
        n += PC_MP_NAME_LEN;
        pack_look(buf + n, &pl->look);
        n += 8;
    }
    send_rel_all(mp, T_PLAYERS, buf, n, -1);
}

static void pack_state(uint8_t* p, int slot, const PcMpState* s) {
    p[0] = (uint8_t)slot;
    w32(p + 1, s->town);
    w16(p + 5, (uint16_t)s->scene);
    p[7] = s->outside;
    p[8] = s->anim;
    wf(p + 9, s->x);
    wf(p + 13, s->y);
    wf(p + 17, s->z);
    w16(p + 21, (uint16_t)s->angle);
    w16(p + 23, s->flags);
    w16(p + 25, s->place);
    w16(p + 27, s->owner);
    w16(p + 29, s->anim0);
    p[31] = s->part;
    p[32] = s->item;
    wf(p + 33, s->frame);
}

static void unpack_state(const uint8_t* p, PcMpState* s) {
    s->town = r32(p + 1);
    s->scene = (int16_t)r16(p + 5);
    s->outside = p[7];
    s->anim = p[8];
    s->x = rf(p + 9);
    s->y = rf(p + 13);
    s->z = rf(p + 17);
    s->angle = (int16_t)r16(p + 21);
    s->flags = r16(p + 23);
    s->place = r16(p + 25);
    s->owner = r16(p + 27);
    s->anim0 = r16(p + 29);
    s->part = p[31];
    s->item = p[32];
    s->frame = rf(p + 33);
}
#define STATE_SIZE 37

/* --- Public: life cycle --- */

PcMp* pc_mp_new(uint16_t port) {
    PcMp* mp = (PcMp*)calloc(1, sizeof(PcMp));

    if (mp == NULL) return NULL;
    mp->sock = pc_net_open(port);
    if (mp->sock == NULL && port != 0) mp->sock = pc_net_open(0); /* taken: any port */
    mp->game = (GameMsg*)calloc(GAME_QUEUE, sizeof(GameMsg));
    if (mp->sock == NULL || mp->game == NULL) {
        pc_net_close(mp->sock);
        free(mp->game);
        free(mp);
        return NULL;
    }
    mp->dht = pc_dht_new(mp->sock);
    mp->nonce = pc_net_ms() * 2654435761u;
    snprintf(mp->my_name, sizeof(mp->my_name), "Player");
    mp->now = pc_net_ms();
    return mp;
}

static void reset_room(PcMp* mp) {
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) peer_clear(&mp->peers[i]);
    memset(mp->players, 0, sizeof(mp->players));
    memset(&mp->room, 0, sizeof(mp->room));
    mp->nkicked = 0;
    mp->my_slot = 0;
    mp->mode = PC_MP_IDLE;
    memset(&mp->mapped, 0, sizeof(mp->mapped));
    mp->punch_alt = 0;
    free(mp->town_in);
    mp->town_in = NULL;
    mp->town_in_len = mp->town_in_got = 0;
    mp->town_asked = 0;
    mp->town_ready = 0;
    if (mp->lan != NULL) {
        pc_net_close(mp->lan);
        mp->lan = NULL;
    }
}

void pc_mp_free(PcMp* mp) {
    if (mp == NULL) return;
    pc_mp_leave(mp);
    reset_room(mp);
    pc_dht_free(mp->dht);
    pc_net_close(mp->sock);
    free(mp->town);
    free(mp->game);
    free(mp);
}

uint16_t pc_mp_port(PcMp* mp) {
    return pc_net_port(mp->sock);
}

void pc_mp_set_me(PcMp* mp, const char* name, const PcMpLook* look) {
    if (name != NULL && name[0] != '\0') snprintf(mp->my_name, sizeof(mp->my_name), "%s", name);
    if (look != NULL) {
        int changed = memcmp(look, &mp->my_look, sizeof(*look)) != 0;

        mp->my_look = *look;
        if (changed && mp->mode == PC_MP_HOSTING) {
            mp->players[0].look = *look;
            send_players(mp);
        } else if (changed && mp->mode == PC_MP_JOINED && mp->peers[0].used) {
            uint8_t p[8];

            pack_look(p, look);
            send_rel(mp, &mp->peers[0], T_LOOK, p, sizeof(p));
        }
    }
    if (mp->mode == PC_MP_HOSTING) snprintf(mp->players[0].name, sizeof(mp->players[0].name), "%s", mp->my_name);
}

void pc_mp_set_state(PcMp* mp, const PcMpState* state) {
    if (memcmp(state, &mp->my_state, sizeof(*state)) != 0) {
        mp->my_state = *state;
        mp->state_dirty = 1;
    }
    if (mp->mode == PC_MP_HOSTING || mp->mode == PC_MP_JOINED) {
        PcMpPlayer* me = NULL;

        for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
            if (mp->players[i].used && mp->players[i].slot == mp->my_slot) me = &mp->players[i];
        }
        if (me != NULL) {
            me->state = *state;
            me->state_ms = mp->now;
        }
    }
}

void pc_mp_add_bootstrap(PcMp* mp, const PcNetAddr* node) {
    pc_dht_add_bootstrap(mp->dht, node);
}

void pc_mp_set_stun(PcMp* mp, const PcNetAddr* server) {
    mp->stun = *server;
    mp->stun_sent = 0;
}

/* What the internet sees of our socket: STUN's answer, else the DHT's */
static int seen_addr(PcMp* mp, PcNetAddr* out) {
    if (mp->external.ip != 0) {
        *out = mp->external;
        return 1;
    }
    if (pc_dht_external_ip(mp->dht) != 0) {
        out->ip = pc_dht_external_ip(mp->dht);
        out->port = pc_mp_port(mp); /* a guess: routers often keep it */
        return 1;
    }
    return 0;
}

int pc_mp_ip_is_public(uint32_t ip) {
    uint32_t a = ip >> 24, b = (ip >> 16) & 255;

    if (a == 0 || a == 10 || a == 127 || a >= 224) return 0;
    if (a == 100 && (b & 0xC0) == 64) return 0; /* 100.64/10: the carrier's NAT */
    if (a == 169 && b == 254) return 0;
    if (a == 172 && (b & 0xF0) == 16) return 0;
    if (a == 192 && b == 168) return 0;
    return 1;
}

/* The port the router forwards is reached from the internet only if the
 * router's own outside address is the one the internet sees: not a router
 * behind another router or behind the carrier's NAT */
static int mapped_usable(PcMp* mp) {
    PcNetAddr seen;

    if (mp->mapped.ip == 0 || mp->mapped.port == 0) return 0;
    if (seen_addr(mp, &seen)) return seen.ip == mp->mapped.ip;
    return pc_mp_ip_is_public(mp->mapped.ip);
}

int pc_mp_external_addr(PcMp* mp, PcNetAddr* out) {
    /* the forwarded port, not the one the router happened to pick for STUN
     * (some routers change the port even when one is forwarded) */
    if (mapped_usable(mp)) {
        *out = mp->mapped;
        return 1;
    }
    return seen_addr(mp, out);
}

void pc_mp_set_port_mapping(PcMp* mp, uint32_t router_ip, uint16_t port) {
    if (mp->mapped.ip == router_ip && mp->mapped.port == port) return;
    mp->mapped.ip = router_ip;
    mp->mapped.port = port;
    /* a public room tells the DHT the forwarded port now, not in 10 minutes */
    if (mp->mode == PC_MP_HOSTING && mp->room.is_public) mp->dht_rooms_ms = 0;
}

int pc_mp_port_mapped(PcMp* mp) {
    return mapped_usable(mp);
}

int pc_mp_internet_ready(PcMp* mp) {
    for (int s = 0; s < 3; s++) {
        if (pc_dht_lookup_responses(mp->dht, s) > 0) return 1;
    }
    return 0;
}

/* --- Room codes: ip (32) + port (16) + room (16) bits, Crockford base32 --- */

static const char B32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

void pc_mp_code_make(const PcNetAddr* addr, uint32_t room_id, char* buf, size_t n) {
    uint64_t v = ((uint64_t)addr->ip << 32) | ((uint64_t)addr->port << 16) | (room_id & 0xFFFF);
    char digits[14];
    int i;

    for (i = 12; i >= 0; i--) {
        digits[i] = B32[v & 31];
        v >>= 5;
    }
    digits[13] = '\0';
    snprintf(buf, n, "%.4s-%.4s-%.5s", digits, digits + 4, digits + 8);
}

int pc_mp_code_parse(const char* code, PcNetAddr* addr, uint32_t* room_low16) {
    uint64_t v = 0;
    int count = 0;

    for (const char* p = code; *p; p++) {
        char c = *p;
        const char* hit;

        if (c == '-' || c == ' ') continue;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c == 'O') c = '0';
        if (c == 'I' || c == 'L') c = '1';
        hit = strchr(B32, c);
        if (hit == NULL || c == '\0') return 0;
        v = (v << 5) | (uint64_t)(hit - B32);
        count++;
    }
    if (count != 13) return 0;
    addr->ip = (uint32_t)(v >> 32);
    addr->port = (uint16_t)(v >> 16);
    *room_low16 = (uint32_t)(v & 0xFFFF);
    return addr->ip != 0 && addr->port != 0;
}

void pc_mp_room_code(PcMp* mp, char* buf, size_t n) {
    PcNetAddr a;

    if (!pc_mp_external_addr(mp, &a)) {
        a.ip = pc_net_local_ip();
        a.port = pc_mp_port(mp);
    }
    pc_mp_code_make(&a, mp->room.room_id, buf, n);
}

static void punch_key(const PcNetAddr* host_ext, uint32_t room_id, uint8_t key[20]) {
    char text[96];

    snprintf(text, sizeof(text), "afe-port/v1/punch/%08x/%u/%04x", host_ext->ip, host_ext->port, room_id & 0xFFFF);
    pc_dht_key(text, key);
}

/* --- Hosting --- */

int pc_mp_host(PcMp* mp, const PcMpHostConfig* cfg) {
    pc_mp_leave(mp);
    reset_room(mp);
    mp->cfg = *cfg;
    if (mp->cfg.max_players < 2) mp->cfg.max_players = 2;
    if (mp->cfg.max_players > PC_MP_MAX_PLAYERS) mp->cfg.max_players = PC_MP_MAX_PLAYERS;
    mp->mode = PC_MP_HOSTING;
    mp->my_slot = 0;
    do {
        mp->room.room_id = (uint32_t)rand() ^ (pc_net_ms() << 12) ^ (uint32_t)(uintptr_t)mp;
    } while ((mp->room.room_id & 0xFFFF) == 0);
    snprintf(mp->room.name, sizeof(mp->room.name), "%s", cfg->room_name);
    snprintf(mp->room.town, sizeof(mp->room.town), "%s", cfg->town);
    snprintf(mp->room.host_name, sizeof(mp->room.host_name), "%s", mp->my_name);
    mp->room.max_players = (uint8_t)mp->cfg.max_players;
    mp->room.is_public = (uint8_t)(cfg->is_public != 0);
    mp->room.has_password = cfg->password[0] != '\0';
    mp->players[0].used = 1;
    mp->players[0].slot = 0;
    mp->players[0].is_you = 1;
    mp->players[0].look = mp->my_look;
    mp->players[0].state = mp->my_state;
    snprintf(mp->players[0].name, sizeof(mp->players[0].name), "%s", mp->my_name);
    mp->lan = pc_net_open_shared(PC_MP_LAN_PORT);
    mp->dht_rooms_ms = mp->dht_seek_ms = mp->dht_punch_ms = 0;
    mp->stun_sent = 0;
    return 1;
}

const PcMpHostConfig* pc_mp_host_config(PcMp* mp) {
    return &mp->cfg;
}

static int find_slot(PcMp* mp, int slot) {
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
        if (mp->players[i].used && mp->players[i].slot == slot) return i;
    }
    return -1;
}

static void drop_guest(PcMp* mp, int slot, const char* why) {
    int pi = find_slot(mp, slot);
    char text[160];

    if (slot <= 0 || slot >= PC_MP_MAX_PLAYERS) return;
    snprintf(text, sizeof(text), "%s", pi >= 0 ? mp->players[pi].name : "?");
    if (pi >= 0) memset(&mp->players[pi], 0, sizeof(PcMpPlayer));
    peer_clear(&mp->peers[slot]);
    send_players(mp);
    event(mp, PC_MP_EV_PLAYER_OUT, slot, text);
    (void)why;
}

void pc_mp_kick(PcMp* mp, int slot) {
    Peer* p;
    uint8_t reason = 1;

    if (mp->mode != PC_MP_HOSTING || slot <= 0 || slot >= PC_MP_MAX_PLAYERS) return;
    p = &mp->peers[slot];
    if (!p->used) return;
    for (int i = 0; i < 3; i++) raw_send(mp, &p->addr, T_KICK, 0, 0, &reason, 1);
    if (mp->nkicked < 16) mp->kicked[mp->nkicked++] = p->addr;
    drop_guest(mp, slot, "kicked");
}

static void host_join(PcMp* mp, const PcNetAddr* from, const uint8_t* p, size_t len) {
    uint8_t deny;
    uint8_t want[32];
    int slot = -1, count = 0;

    if (len < 2 + PC_MP_NAME_LEN + 8 + 32 + 4) return;
    if (peer_by_addr(mp, from) != NULL) return; /* JOIN_OK is on its way */
    for (int i = 0; i < mp->nkicked; i++) {
        if (pc_net_addr_equal(&mp->kicked[i], from)) {
            deny = DENY_KICKED;
            raw_send(mp, from, T_JOIN_DENY, 0, 0, &deny, 1);
            return;
        }
    }
    if (r16(p) != PC_MP_PROTOCOL) {
        deny = DENY_VERSION;
        raw_send(mp, from, T_JOIN_DENY, 0, 0, &deny, 1);
        return;
    }
    uint32_t want_room = r32(p + 2 + PC_MP_NAME_LEN + 8 + 32);
    if (want_room != 0 && (want_room & 0xFFFF) != (mp->room.room_id & 0xFFFF)) {
        deny = DENY_NO_ROOM;
        raw_send(mp, from, T_JOIN_DENY, 0, 0, &deny, 1);
        return;
    }
    pass_hash(mp->room.room_id, mp->cfg.password, want);
    if (mp->cfg.password[0] != '\0' && memcmp(want, p + 2 + PC_MP_NAME_LEN + 8, 32) != 0) {
        deny = DENY_PASSWORD;
        raw_send(mp, from, T_JOIN_DENY, 0, 0, &deny, 1);
        return;
    }
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) count += mp->players[i].used;
    for (int s = 1; s < PC_MP_MAX_PLAYERS && slot < 0; s++) {
        if (!mp->peers[s].used) slot = s;
    }
    if (count >= mp->cfg.max_players || slot < 0) {
        deny = DENY_FULL;
        raw_send(mp, from, T_JOIN_DENY, 0, 0, &deny, 1);
        return;
    }
    Peer* peer = &mp->peers[slot];
    memset(peer, 0, sizeof(*peer));
    peer->used = 1;
    peer->slot = slot;
    peer->addr = *from;
    peer->last_recv = mp->now;
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
        PcMpPlayer* pl = &mp->players[i];

        if (pl->used) continue;
        memset(pl, 0, sizeof(*pl));
        pl->used = 1;
        pl->slot = slot;
        rstr(pl->name, p + 2, PC_MP_NAME_LEN);
        unpack_look(p + 2 + PC_MP_NAME_LEN, &pl->look);
        break;
    }
    {
        uint8_t ok[2 + 138];

        ok[0] = (uint8_t)slot;
        ok[1] = (uint8_t)mp->cfg.max_players;
        pack_room(mp, ok + 2);
        send_rel(mp, peer, T_JOIN_OK, ok, sizeof(ok));
    }
    send_players(mp);
    {
        int pi = find_slot(mp, slot);
        event(mp, PC_MP_EV_PLAYER_IN, slot, pi >= 0 ? mp->players[pi].name : "?");
    }
}

/* --- Joining --- */

static void send_join(PcMp* mp) {
    uint8_t p[2 + PC_MP_NAME_LEN + 8 + 32 + 4];

    w16(p, PC_MP_PROTOCOL);
    wstr(p + 2, mp->my_name, PC_MP_NAME_LEN);
    pack_look(p + 2 + PC_MP_NAME_LEN, &mp->my_look);
    memcpy(p + 2 + PC_MP_NAME_LEN + 8, mp->join_pass, 32);
    w32(p + 2 + PC_MP_NAME_LEN + 8 + 32, mp->join_room);
    raw_send(mp, &mp->join_addr, T_JOIN, 0, 0, p, sizeof(p));
    mp->join_sent = mp->now;
}

int pc_mp_join(PcMp* mp, const PcNetAddr* addr, uint32_t room_id, const char* password) {
    pc_mp_leave(mp);
    reset_room(mp);
    mp->mode = PC_MP_JOINING;
    mp->join_addr = *addr;
    mp->join_room = room_id;
    mp->join_started = mp->now;
    snprintf(mp->join_password, sizeof(mp->join_password), "%s", password ? password : "");
    memset(mp->join_pass, 0, sizeof(mp->join_pass));
    /* the hash needs the room's full id: known from its info, if we have it */
    for (int i = 0; i < mp->nrooms; i++) {
        if (pc_net_addr_equal(&mp->rooms[i].addr, addr) &&
            (room_id == 0 || (mp->rooms[i].room_id & 0xFFFF) == (room_id & 0xFFFF))) {
            mp->join_room = mp->rooms[i].room_id;
            break;
        }
    }
    if (password != NULL && password[0] != '\0') {
        if ((mp->join_room >> 16) == 0) {
            /* only the code's 16 bits: ask the room for the rest first */
            pc_mp_probe(mp, addr);
        }
        pass_hash(mp->join_room, password, mp->join_pass);
    }
    /* punching: tell the room (through the DHT) that we want in */
    if (pc_dht_bootstrap_count(mp->dht) > 0) {
        uint8_t key[20];

        punch_key(addr, mp->join_room, key);
        pc_dht_lookup(mp->dht, 2, key, 1);
    }
    mp->peers[0].used = 1;
    mp->peers[0].slot = 0;
    mp->peers[0].addr = *addr;
    mp->peers[0].last_recv = mp->now;
    send_join(mp);
    return 1;
}

void pc_mp_leave(PcMp* mp) {
    if (mp->mode == PC_MP_HOSTING) {
        for (int i = 1; i < PC_MP_MAX_PLAYERS; i++) {
            if (mp->peers[i].used) {
                for (int k = 0; k < 2; k++) raw_send(mp, &mp->peers[i].addr, T_LEAVE, 0, 0, NULL, 0);
            }
        }
    } else if (mp->mode == PC_MP_JOINED || mp->mode == PC_MP_JOINING) {
        for (int k = 0; k < 2; k++) raw_send(mp, &mp->peers[0].addr, T_LEAVE, 0, 0, NULL, 0);
    }
    reset_room(mp);
}

/* --- Browsing --- */

void pc_mp_browse(PcMp* mp, int on) {
    mp->browsing = on;
    if (on) {
        mp->browse_sent = 0;
        mp->dht_rooms_ms = mp->dht_seek_ms = 0;
    }
}

int pc_mp_rooms(PcMp* mp, PcMpRoomInfo* out, int max) {
    int n = mp->nrooms < max ? mp->nrooms : max;

    memcpy(out, mp->rooms, sizeof(PcMpRoomInfo) * (size_t)n);
    return n;
}

static void send_discover(PcMp* mp, const PcNetAddr* to, uint16_t room_low16) {
    uint8_t p[10];

    w32(p, mp->nonce);
    w16(p + 4, PC_MP_PROTOCOL);
    w16(p + 6, room_low16);
    w16(p + 8, 0);
    raw_send(mp, to, T_DISCOVER, 0, 0, p, sizeof(p));
}

void pc_mp_probe(PcMp* mp, const PcNetAddr* addr) {
    send_discover(mp, addr, 0);
}

static void add_room(PcMp* mp, const PcNetAddr* from, const uint8_t* p, size_t len, int lan) {
    PcMpRoomInfo r;
    int i;

    if (len < 138) return;
    unpack_room(p, &r);
    r.addr = *from;
    r.lan = (uint8_t)lan;
    r.seen_ms = mp->now;
    for (i = 0; i < mp->nrooms; i++) {
        if (mp->rooms[i].room_id == r.room_id) break;
    }
    if (i == mp->nrooms) {
        if (mp->nrooms == PC_MP_MAX_ROOMS) return;
        mp->nrooms++;
    } else if (!lan && mp->rooms[i].lan) {
        r.lan = 1; /* found on the LAN as well: keep the LAN address */
        r.addr = mp->rooms[i].addr;
    }
    mp->rooms[i] = r;
}

/* --- Receiving --- */

static void deliver(PcMp* mp, Peer* peer, uint8_t type, const uint8_t* p, size_t len);

static void game_push(PcMp* mp, int from, const uint8_t* data, size_t len) {
    int next = (mp->game_tail + 1) % GAME_QUEUE;
    GameMsg* g;

    if (len > PC_MP_MAX_GAME_MSG || next == mp->game_head) return;
    g = &mp->game[mp->game_tail];
    g->from = from;
    g->len = (uint16_t)len;
    memcpy(g->data, data, len);
    mp->game_tail = next;
}

static void got_reliable(PcMp* mp, Peer* peer, uint8_t type, uint16_t seq, const uint8_t* p, size_t len) {
    int16_t ahead = (int16_t)(seq - peer->in_next);

    if (ahead >= REORDER) return; /* too far ahead: not kept, so not acknowledged (sent again) */
    if (peer->nacks < (int)(sizeof(peer->acks) / sizeof(peer->acks[0]))) peer->acks[peer->nacks++] = seq;
    if (ahead < 0) return; /* had it */
    if (ahead > 0) {
        In* slot = &peer->reorder[seq % REORDER];

        if (!slot->have) {
            slot->data = (uint8_t*)malloc(len ? len : 1);
            if (slot->data == NULL) return;
            memcpy(slot->data, p, len);
            slot->len = (uint16_t)len;
            slot->type = type;
            slot->have = 1;
        }
        return;
    }
    deliver(mp, peer, type, p, len);
    peer->in_next++;
    for (;;) {
        In* slot = &peer->reorder[peer->in_next % REORDER];

        if (!slot->have || !peer->used) break;
        slot->have = 0;
        deliver(mp, peer, slot->type, slot->data, slot->len);
        free(slot->data);
        slot->data = NULL;
        peer->in_next++;
    }
}

static void got_ack(Peer* peer, const uint8_t* p, size_t len) {
    if (len < 1) return;
    for (int i = 0; i < p[0] && 1 + i * 2 + 2 <= (int)len; i++) {
        uint16_t seq = r16(p + 1 + i * 2);

        for (int k = 0; k < peer->nout; k++) {
            if (peer->out[k].seq == seq) {
                free(peer->out[k].data);
                memmove(&peer->out[k], &peer->out[k + 1], sizeof(Out) * (size_t)(peer->nout - k - 1));
                peer->nout--;
                break;
            }
        }
    }
}

static void town_got(PcMp* mp, const uint8_t* p, size_t len) {
    uint32_t off, total;

    if (len < 8) return;
    off = r32(p);
    total = r32(p + 4);
    if (total == 0 || total > (8u << 20)) return;
    if (mp->town_in == NULL || mp->town_in_len != total) {
        free(mp->town_in);
        mp->town_in = (uint8_t*)malloc(total);
        mp->town_in_len = total;
        mp->town_in_got = 0;
        if (mp->town_in == NULL) return;
    }
    if (off + (len - 8) > total) return;
    memcpy(mp->town_in + off, p + 8, len - 8);
    mp->town_in_got += len - 8;
    if (mp->town_in_got >= total && !mp->town_ready) {
        mp->town_ready = 1;
        event(mp, PC_MP_EV_TOWN, 0, "");
    }
}

static void deliver(PcMp* mp, Peer* peer, uint8_t type, const uint8_t* p, size_t len) {
    switch (type) {
        case T_JOIN_OK:
            if (mp->mode != PC_MP_JOINING || len < 2 + 138) return;
            mp->my_slot = p[0];
            unpack_room(p + 2, &mp->room);
            mp->room.addr = peer->addr;
            mp->room.max_players = p[1];
            mp->mode = PC_MP_JOINED;
            event(mp, PC_MP_EV_JOINED, mp->my_slot, mp->room.name);
            break;
        case T_PLAYERS: {
            PcMpPlayer old[PC_MP_MAX_PLAYERS];
            size_t n = 1;

            if (mp->mode != PC_MP_JOINED || len < 1) return;
            memcpy(old, mp->players, sizeof(old));
            memset(mp->players, 0, sizeof(mp->players));
            for (int i = 0; i < p[0] && i < PC_MP_MAX_PLAYERS && n + 1 + PC_MP_NAME_LEN + 8 <= len; i++) {
                PcMpPlayer* pl = &mp->players[i];

                pl->used = 1;
                pl->slot = p[n++];
                rstr(pl->name, p + n, PC_MP_NAME_LEN);
                n += PC_MP_NAME_LEN;
                unpack_look(p + n, &pl->look);
                n += 8;
                pl->is_you = pl->slot == mp->my_slot;
                for (int k = 0; k < PC_MP_MAX_PLAYERS; k++) {
                    if (old[k].used && old[k].slot == pl->slot) {
                        pl->state = old[k].state;
                        pl->state_ms = old[k].state_ms;
                        pl->ping_ms = old[k].ping_ms;
                    }
                }
                if (pl->is_you) pl->state = mp->my_state;
            }
            for (int k = 0; k < PC_MP_MAX_PLAYERS; k++) {
                int still = 0;

                if (!old[k].used || old[k].is_you) continue;
                for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
                    if (mp->players[i].used && mp->players[i].slot == old[k].slot) still = 1;
                }
                if (!still) event(mp, PC_MP_EV_PLAYER_OUT, old[k].slot, old[k].name);
            }
            for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
                int was = 0;

                if (!mp->players[i].used || mp->players[i].is_you) continue;
                for (int k = 0; k < PC_MP_MAX_PLAYERS; k++) {
                    if (old[k].used && old[k].slot == mp->players[i].slot) was = 1;
                }
                if (!was) event(mp, PC_MP_EV_PLAYER_IN, mp->players[i].slot, mp->players[i].name);
            }
            break;
        }
        case T_GAME:
            game_push(mp, peer->slot, p, len);
            break;
        case T_CHAT: {
            char text[160];
            int from = peer->slot;
            size_t n;

            if (len < 2) return;
            if (mp->mode == PC_MP_JOINED) from = p[0];
            n = len - 1 < sizeof(text) - 1 ? len - 1 : sizeof(text) - 1;
            memcpy(text, p + 1, n);
            text[n] = '\0';
            if (mp->mode == PC_MP_HOSTING) {
                uint8_t out[1 + 160];

                out[0] = (uint8_t)peer->slot;
                memcpy(out + 1, text, n);
                send_rel_all(mp, T_CHAT, out, 1 + n, peer->slot);
            }
            event(mp, PC_MP_EV_CHAT, from, text);
            break;
        }
        case T_LOOK:
            if (mp->mode == PC_MP_HOSTING && len >= 8 && peer->slot > 0 && peer->slot < PC_MP_MAX_PLAYERS &&
                mp->players[peer->slot].used) {
                unpack_look(p, &mp->players[peer->slot].look);
                send_players(mp);
            }
            break;
        case T_TOWN_REQ:
            if (mp->mode == PC_MP_HOSTING) {
                peer->town_asked = 2; /* once the game gives it (pc_mp_set_town_blob) */
                peer->town_next = 0;
                event(mp, PC_MP_EV_TOWN_WANTED, peer->slot, "");
            }
            break;
        case T_TOWN_DATA:
            if (mp->mode == PC_MP_JOINED) town_got(mp, p, len);
            break;
        default:
            break;
    }
}

static void handle(PcMp* mp, const PcNetAddr* from, const uint8_t* pkt, int n, int on_lan_socket) {
    uint8_t type, flags;
    uint16_t seq, len;
    const uint8_t* p;
    Peer* peer;

    if (n >= 20 && pkt[0] == 0x01 && pkt[1] == 0x01) {
        PcNetAddr mapped;

        if (pc_stun_parse(pkt, n, mp->stun_tx, &mapped)) mp->external = mapped;
        return;
    }
    if (pkt[0] == 'd') {
        pc_dht_handle(mp->dht, from, pkt, n);
        return;
    }
    if (n < HDR || pkt[0] != MAGIC0 || pkt[1] != MAGIC1 || pkt[2] != MAGIC2 || pkt[3] != MAGIC3) return;
    type = pkt[4];
    flags = pkt[5];
    seq = r16(pkt + 12);
    len = r16(pkt + 14);
    if (HDR + len > n) return;
    p = pkt + HDR;

    /* without a connection */
    switch (type) {
        case T_DISCOVER:
            if (mp->mode == PC_MP_HOSTING && len >= 8 && r16(p + 4) == PC_MP_PROTOCOL) {
                uint16_t want = r16(p + 6);
                uint8_t info[138];

                /* private rooms only answer who has their code */
                if ((want == 0 && mp->room.is_public) || want == (mp->room.room_id & 0xFFFF)) {
                    pack_room(mp, info);
                    raw_send(mp, from, T_ROOM_INFO, 0, 0, info, sizeof(info));
                }
            }
            return;
        case T_ROOM_INFO:
            add_room(mp, from, p, len, pc_net_ip_private(from->ip));
            /* a guest waiting for a private room's full id */
            if (mp->mode == PC_MP_JOINING && pc_net_addr_equal(from, &mp->join_addr) && len >= 4) {
                uint32_t id = r32(p);

                if ((mp->join_room & 0xFFFF) == (id & 0xFFFF) || mp->join_room == 0) {
                    mp->join_room = id;
                    if (mp->join_password[0] != '\0') pass_hash(id, mp->join_password, mp->join_pass);
                    send_join(mp);
                }
            }
            return;
        case T_JOIN:
            if (mp->mode == PC_MP_HOSTING && !on_lan_socket) host_join(mp, from, p, len);
            return;
        case T_JOIN_DENY:
            if (mp->mode == PC_MP_JOINING && pc_net_addr_equal(from, &mp->join_addr) && len >= 1) {
                static const char* const why[] = { "?", "The room is full", "Wrong password",
                                                   "Another version of the game", "You were sent out of this room",
                                                   "No such room" };
                event(mp, PC_MP_EV_DENIED, p[0], why[p[0] < 6 ? p[0] : 0]);
                reset_room(mp);
            }
            return;
        case T_PUNCH:
            return; /* it opened the way: the next JOIN goes through */
        default:
            break;
    }

    peer = peer_by_addr(mp, from);
    if (peer == NULL) return;
    if (r32(pkt + 8) != mp->room.room_id && mp->mode != PC_MP_JOINING) return;
    peer->last_recv = mp->now;

    if (flags & FLAG_RELIABLE) {
        got_reliable(mp, peer, type, seq, p, len);
        return;
    }
    switch (type) {
        case T_ACK:
            got_ack(peer, p, len);
            break;
        case T_PING: {
            uint8_t back[4];

            if (len >= 4) {
                memcpy(back, p, 4);
                raw_send(mp, from, T_PONG, 0, 0, back, 4);
            }
            break;
        }
        case T_PONG:
            if (len >= 4) {
                int ping = (int)(mp->now - r32(p));
                int pi = find_slot(mp, peer->slot);

                if (pi >= 0) mp->players[pi].ping_ms = ping;
            }
            break;
        case T_STATE:
            if (len >= STATE_SIZE) {
                int slot = mp->mode == PC_MP_HOSTING ? peer->slot : p[0];
                int pi = find_slot(mp, slot);

                if (pi >= 0 && slot != mp->my_slot) {
                    unpack_state(p, &mp->players[pi].state);
                    mp->players[pi].state_ms = mp->now;
                }
                if (mp->mode == PC_MP_HOSTING) {
                    uint8_t out[STATE_SIZE];

                    memcpy(out, p, STATE_SIZE);
                    out[0] = (uint8_t)peer->slot;
                    send_unrel_all(mp, T_STATE, out, STATE_SIZE, peer->slot);
                }
            }
            break;
        case T_GAME:
            game_push(mp, peer->slot, p, len);
            break;
        case T_LEAVE:
            if (mp->mode == PC_MP_HOSTING) {
                drop_guest(mp, peer->slot, "left");
            } else {
                event(mp, PC_MP_EV_CLOSED, 0, "The host closed the room");
                reset_room(mp);
            }
            break;
        case T_KICK:
            if (mp->mode != PC_MP_HOSTING) {
                event(mp, PC_MP_EV_KICKED, 0, "The host sent you out of the room");
                reset_room(mp);
            }
            break;
        default:
            break;
    }
}

/* --- Tick --- */

static void send_acks(PcMp* mp, Peer* p) {
    uint8_t buf[1 + 48 * 2];

    if (p->nacks == 0) return;
    buf[0] = (uint8_t)p->nacks;
    for (int i = 0; i < p->nacks; i++) w16(buf + 1 + i * 2, p->acks[i]);
    raw_send(mp, &p->addr, T_ACK, 0, 0, buf, 1 + (size_t)p->nacks * 2);
    p->nacks = 0;
}

static int resend(PcMp* mp, Peer* p) {
    for (int i = 0; i < p->nout && i < WINDOW; i++) {
        Out* o = &p->out[i];
        uint32_t wait = RESEND_MS << (o->tries > 3 ? 2 : o->tries / 2);

        if (o->tries == 0 || mp->now - o->last >= wait) {
            if (o->tries >= GIVE_UP_TRIES) return 0;
            raw_send(mp, &p->addr, o->type, FLAG_RELIABLE, o->seq, o->data, o->len);
            o->tries++;
            o->last = mp->now;
        }
    }
    return 1;
}

static void host_town_send(PcMp* mp, Peer* p) {
    uint8_t buf[8 + TOWN_CHUNK];

    if (p->town_asked != 1 || mp->town == NULL) return;
    while (p->town_next < mp->town_len && p->nout < OUT_CAP - 8) {
        size_t n = mp->town_len - p->town_next;

        if (n > TOWN_CHUNK) n = TOWN_CHUNK;
        w32(buf, (uint32_t)p->town_next);
        w32(buf + 4, (uint32_t)mp->town_len);
        memcpy(buf + 8, mp->town + p->town_next, n);
        send_rel(mp, p, T_TOWN_DATA, buf, 8 + n);
        p->town_next += n;
    }
    if (p->town_next >= mp->town_len) p->town_asked = 0;
}

static void internet_tick(PcMp* mp) {
    uint8_t key[20];
    PcNetAddr peers[64];
    int n;

    /* STUN: our outside address */
    if (mp->stun.ip != 0 && mp->external.ip == 0 && (mp->stun_sent == 0 || mp->now - mp->stun_sent > 5000) &&
        (mp->mode == PC_MP_HOSTING || mp->browsing || mp->mode == PC_MP_JOINING)) {
        uint8_t req[20];

        pc_stun_request(req, mp->stun_tx);
        pc_net_send(mp->sock, &mp->stun, req, sizeof(req));
        mp->stun_sent = mp->now;
    }
    if (pc_dht_bootstrap_count(mp->dht) == 0) return;
    pc_dht_tick(mp->dht, mp->now);

    if (mp->mode == PC_MP_HOSTING) {
        PcNetAddr ext;

        /* public: in the list (announce now and then) */
        if (mp->room.is_public && (mp->dht_rooms_ms == 0 || mp->now - mp->dht_rooms_ms > 600000)) {
            pc_dht_key("afe-port/v1/rooms", key);
            pc_dht_lookup(mp->dht, 0, key, 1);
            if (mapped_usable(mp)) pc_dht_lookup_port(mp->dht, 0, mp->mapped.port);
            mp->dht_rooms_ms = mp->now;
        }
        /* public: guests looking for rooms get our info (opens our router to them) */
        if (mp->room.is_public && (mp->dht_seek_ms == 0 || mp->now - mp->dht_seek_ms > 15000)) {
            pc_dht_key("afe-port/v1/seek", key);
            pc_dht_lookup(mp->dht, 1, key, 0);
            mp->dht_seek_ms = mp->now;
            mp->dht_seek_was_running = 1;
        }
        if (mp->dht_seek_was_running && !pc_dht_lookup_running(mp->dht, 1)) {
            uint8_t info[138];

            mp->dht_seek_was_running = 0;
            n = pc_dht_lookup_peers(mp->dht, 1, peers, 64);
            pack_room(mp, info);
            for (int i = 0; i < n; i++) raw_send(mp, &peers[i], T_ROOM_INFO, 0, 0, info, sizeof(info));
        }
        /* guests that want in: punch */
        if (pc_mp_external_addr(mp, &ext) && (mp->dht_punch_ms == 0 || mp->now - mp->dht_punch_ms > 8000)) {
            PcNetAddr seen;

            /* a code made before the router's port was known holds the
             * address STUN saw: look for those guests too, every other time */
            if (mp->punch_alt && seen_addr(mp, &seen) && (seen.ip != ext.ip || seen.port != ext.port)) ext = seen;
            mp->punch_alt ^= 1;
            punch_key(&ext, mp->room.room_id, key);
            pc_dht_lookup(mp->dht, 2, key, 0);
            mp->dht_punch_ms = mp->now;
            mp->dht_punch_was_running = 1;
        }
        if (mp->dht_punch_was_running && !pc_dht_lookup_running(mp->dht, 2)) {
            mp->dht_punch_was_running = 0;
            n = pc_dht_lookup_peers(mp->dht, 2, peers, 64);
            for (int i = 0; i < n; i++) {
                if (peer_by_addr(mp, &peers[i]) == NULL) raw_send(mp, &peers[i], T_PUNCH, 0, 0, NULL, 0);
            }
        }
    } else if (mp->browsing && mp->mode == PC_MP_IDLE) {
        if (mp->dht_rooms_ms == 0 || mp->now - mp->dht_rooms_ms > 20000) {
            pc_dht_key("afe-port/v1/rooms", key);
            pc_dht_lookup(mp->dht, 0, key, 0);
            mp->dht_rooms_ms = mp->now;
            mp->dht_rooms_was_running = 1;
        }
        if (mp->dht_rooms_was_running) {
            /* ask each room as it is found */
            n = pc_dht_lookup_peers(mp->dht, 0, peers, 64);
            static int asked = 0;
            if (n < asked) asked = 0;
            for (int i = asked; i < n; i++) send_discover(mp, &peers[i], 0);
            asked = n;
            if (!pc_dht_lookup_running(mp->dht, 0)) {
                mp->dht_rooms_was_running = 0;
                asked = 0;
            }
        }
        if (mp->dht_seek_ms == 0 || mp->now - mp->dht_seek_ms > 300000) {
            pc_dht_key("afe-port/v1/seek", key);
            pc_dht_lookup(mp->dht, 1, key, 1);
            mp->dht_seek_ms = mp->now;
        }
    } else if (mp->mode == PC_MP_JOINING) {
        if (!pc_dht_lookup_running(mp->dht, 2) && mp->now - mp->dht_punch_ms > 30000) {
            punch_key(&mp->join_addr, mp->join_room, key);
            pc_dht_lookup(mp->dht, 2, key, 1);
            mp->dht_punch_ms = mp->now;
        }
    }
}

void pc_mp_tick(PcMp* mp) {
    uint8_t pkt[2048];
    PcNetAddr from;
    int n;

    mp->now = pc_net_ms();
    for (int guard = 0; guard < 512 && (n = pc_net_recv(mp->sock, &from, pkt, sizeof(pkt))) > 0; guard++) {
        handle(mp, &from, pkt, n, 0);
    }
    if (mp->lan != NULL) {
        for (int guard = 0; guard < 64 && (n = pc_net_recv(mp->lan, &from, pkt, sizeof(pkt))) > 0; guard++) {
            handle(mp, &from, pkt, n, 1);
        }
    }

    /* browsing on the LAN */
    if (mp->browsing && (mp->browse_sent == 0 || mp->now - mp->browse_sent > LAN_BROADCAST_MS)) {
        uint32_t bc[16];
        int nb = pc_net_broadcast_addrs(bc, 16);

        for (int i = 0; i < nb; i++) {
            PcNetAddr a = { bc[i], PC_MP_LAN_PORT };
            send_discover(mp, &a, 0);
        }
        /* rooms on this machine (another copy of the game, tests) */
        {
            PcNetAddr self = { 0x7F000001u, PC_MP_LAN_PORT };
            send_discover(mp, &self, 0);
        }
        mp->browse_sent = mp->now;
    }
    for (int i = 0; i < mp->nrooms; i++) {
        if (mp->now - mp->rooms[i].seen_ms > ROOM_FORGET_MS + (mp->rooms[i].lan ? 0 : 30000)) {
            mp->rooms[i] = mp->rooms[--mp->nrooms];
            i--;
        }
    }

    internet_tick(mp);

    if (mp->mode == PC_MP_JOINING) {
        if (mp->now - mp->join_started > JOIN_TIMEOUT_MS) {
            event(mp, PC_MP_EV_DENIED, 0, "The room does not answer");
            reset_room(mp);
        } else if (mp->now - mp->join_sent > JOIN_RETRY_MS) {
            send_join(mp);
        }
    }

    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
        Peer* p = &mp->peers[i];

        if (!p->used) continue;
        if (mp->mode == PC_MP_JOINING) {
            send_acks(mp, p);
            continue;
        }
        if (mp->now - p->last_recv > PEER_TIMEOUT_MS || !resend(mp, p)) {
            if (mp->mode == PC_MP_HOSTING) {
                drop_guest(mp, p->slot, "timeout");
            } else {
                event(mp, PC_MP_EV_CLOSED, 0, "Lost the connection to the host");
                reset_room(mp);
                return;
            }
            continue;
        }
        if (mp->now - p->last_ping > PING_MS) {
            uint8_t t[4];

            w32(t, mp->now);
            raw_send(mp, &p->addr, T_PING, 0, 0, t, 4);
            p->last_ping = mp->now;
        }
        if (mp->mode == PC_MP_HOSTING) host_town_send(mp, p);
        send_acks(mp, p);
    }

    /* our position */
    if ((mp->mode == PC_MP_HOSTING || mp->mode == PC_MP_JOINED) &&
        (mp->state_dirty || mp->now - mp->state_sent > 500) && mp->now - mp->state_sent >= STATE_MS) {
        uint8_t st[STATE_SIZE];

        pack_state(st, mp->my_slot, &mp->my_state);
        send_unrel_all(mp, T_STATE, st, STATE_SIZE, -1);
        mp->state_sent = mp->now;
        mp->state_dirty = 0;
    }
}

/* --- The rest of the API --- */

PcMpMode pc_mp_mode(PcMp* mp) {
    return mp->mode;
}

int pc_mp_my_slot(PcMp* mp) {
    return mp->my_slot;
}

int pc_mp_players(PcMp* mp, PcMpPlayer* out, int max) {
    int n = 0;

    for (int i = 0; i < PC_MP_MAX_PLAYERS && n < max; i++) {
        if (mp->players[i].used) out[n++] = mp->players[i];
    }
    return n;
}

const PcMpRoomInfo* pc_mp_room(PcMp* mp) {
    return &mp->room;
}

int pc_mp_poll_event(PcMp* mp, PcMpEvent* ev) {
    if (mp->ev_head == mp->ev_tail) return 0;
    *ev = mp->events[mp->ev_head];
    mp->ev_head = (mp->ev_head + 1) % EVENTS;
    return 1;
}

void pc_mp_chat(PcMp* mp, const char* text) {
    uint8_t out[1 + 160];
    size_t n = strlen(text);

    if (n == 0) return;
    if (n > 159) n = 159;
    out[0] = (uint8_t)mp->my_slot;
    memcpy(out + 1, text, n);
    if (mp->mode == PC_MP_HOSTING || mp->mode == PC_MP_JOINED) send_rel_all(mp, T_CHAT, out, 1 + n, -1);
}

int pc_mp_send_game(PcMp* mp, int to_slot, int reliable, const void* data, size_t len) {
    int sent = 0;

    if (len > PC_MP_MAX_GAME_MSG || (mp->mode != PC_MP_HOSTING && mp->mode != PC_MP_JOINED)) return 0;
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
        Peer* p = &mp->peers[i];

        if (!p->used || (to_slot >= 0 && p->slot != to_slot)) continue;
        if (reliable) {
            sent += send_rel(mp, p, T_GAME, data, len);
        } else {
            sent += raw_send(mp, &p->addr, T_GAME, 0, 0, data, len);
        }
    }
    return sent;
}

int pc_mp_poll_game(PcMp* mp, int* from_slot, void* buf, size_t cap) {
    GameMsg* g;
    int len;

    if (mp->game_head == mp->game_tail) return 0;
    g = &mp->game[mp->game_head];
    mp->game_head = (mp->game_head + 1) % GAME_QUEUE;
    if (g->len > cap) return 0;
    memcpy(buf, g->data, g->len);
    *from_slot = g->from;
    len = g->len;
    return len > 0 ? len : 0;
}

void pc_mp_set_town_blob(PcMp* mp, const void* data, size_t len) {
    free(mp->town);
    mp->town = (uint8_t*)malloc(len ? len : 1);
    mp->town_len = mp->town != NULL ? len : 0;
    if (mp->town != NULL) memcpy(mp->town, data, len);
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) {
        if (mp->peers[i].used && mp->peers[i].town_asked == 2) {
            mp->peers[i].town_asked = 1;
            mp->peers[i].town_next = 0;
        }
    }
}

void pc_mp_request_town(PcMp* mp) {
    if (mp->mode != PC_MP_JOINED) return;
    free(mp->town_in);
    mp->town_in = NULL;
    mp->town_in_len = mp->town_in_got = 0;
    mp->town_ready = 0;
    mp->town_asked = 1;
    send_rel(mp, &mp->peers[0], T_TOWN_REQ, NULL, 0);
}

int pc_mp_town_progress(PcMp* mp) {
    if (!mp->town_asked) return -1;
    if (mp->town_ready) return 1000;
    if (mp->town_in_len == 0) return 0;
    return (int)(mp->town_in_got * 1000 / mp->town_in_len);
}

const uint8_t* pc_mp_town_blob(PcMp* mp, size_t* len) {
    if (!mp->town_ready) return NULL;
    *len = mp->town_in_len;
    return mp->town_in;
}
