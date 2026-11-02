/* pc_dht.c - see pc_dht.h */
#include "pc_dht.h"
#include "ko_patch.h" /* SHA-256 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define K 8               /* the closest nodes a lookup settles on */
#define ALPHA 4           /* queries in flight per lookup */
#define MAX_CAND 96
#define MAX_PEERS 128
#define QUERY_TIMEOUT 1500
#define LOOKUP_TIMEOUT 25000

enum {
    C_NEW,
    C_SENT,
    C_REPLIED,
    C_FAILED,
};

typedef struct {
    uint8_t id[20];
    int have_id;
    PcNetAddr addr;
    int state;
    uint32_t sent_ms;
    uint8_t token[40];
    int token_len;
} Cand;

typedef struct {
    int active;
    int running;
    int announce;
    int announced;
    uint8_t key[20];
    uint8_t nonce;
    uint32_t started;
    Cand cand[MAX_CAND];
    int ncand;
    PcNetAddr peers[MAX_PEERS];
    int npeers;
    int responses;
} Lookup;

struct PcDht {
    PcNetSocket* sock;
    uint8_t id[20];
    PcNetAddr boot[8];
    int nboot;
    Lookup look[PC_DHT_MAX_LOOKUPS];
    uint32_t ext_ip;
};

void pc_dht_key(const char* text, uint8_t key[20]) {
    KoSha256 s;
    uint8_t d[32];

    ko_sha256_init(&s);
    ko_sha256_update(&s, text, strlen(text));
    ko_sha256_final(&s, d);
    memcpy(key, d, 20);
}

PcDht* pc_dht_new(PcNetSocket* sock) {
    PcDht* d = (PcDht*)calloc(1, sizeof(PcDht));
    char seed[64];

    if (d == NULL) {
        return NULL;
    }
    d->sock = sock;
    snprintf(seed, sizeof(seed), "afe-node %u %u %p", pc_net_ms(), (unsigned)rand(), (void*)d);
    pc_dht_key(seed, d->id);
    return d;
}

void pc_dht_free(PcDht* d) {
    free(d);
}

void pc_dht_add_bootstrap(PcDht* d, const PcNetAddr* addr) {
    for (int i = 0; i < d->nboot; i++) {
        if (pc_net_addr_equal(&d->boot[i], addr)) return;
    }
    if (d->nboot < 8) d->boot[d->nboot++] = *addr;
}

int pc_dht_bootstrap_count(PcDht* d) {
    return d->nboot;
}

uint32_t pc_dht_external_ip(PcDht* d) {
    return d->ext_ip;
}

/* --- bencode --- */

const uint8_t* pc_bdec_skip(const uint8_t* p, const uint8_t* end) {
    if (p >= end) return NULL;
    if (*p == 'i') {
        p++;
        while (p < end && *p != 'e') p++;
        return p < end ? p + 1 : NULL;
    }
    if (*p == 'l' || *p == 'd') {
        p++;
        while (p < end && *p != 'e') {
            p = pc_bdec_skip(p, end);
            if (p == NULL) return NULL;
        }
        return p < end ? p + 1 : NULL;
    }
    if (*p >= '0' && *p <= '9') {
        long n = 0;

        while (p < end && *p >= '0' && *p <= '9') {
            n = n * 10 + (*p - '0');
            if (n > 1 << 20) return NULL;
            p++;
        }
        if (p >= end || *p != ':') return NULL;
        p++;
        return end - p >= n ? p + n : NULL;
    }
    return NULL;
}

int pc_bdec_str(const uint8_t* p, const uint8_t* end, const uint8_t** s, int* len) {
    long n = 0;

    if (p == NULL || p >= end || *p < '0' || *p > '9') return 0;
    while (p < end && *p >= '0' && *p <= '9') {
        n = n * 10 + (*p - '0');
        if (n > 1 << 20) return 0;
        p++;
    }
    if (p >= end || *p != ':') return 0;
    p++;
    if (end - p < n) return 0;
    *s = p;
    *len = (int)n;
    return 1;
}

const uint8_t* pc_bdec_dict_get(const uint8_t* p, const uint8_t* end, const char* key) {
    size_t klen = strlen(key);

    if (p == NULL || p >= end || *p != 'd') return NULL;
    p++;
    while (p < end && *p != 'e') {
        const uint8_t* k;
        int kl;
        const uint8_t* v;

        if (!pc_bdec_str(p, end, &k, &kl)) return NULL;
        v = k + kl;
        if ((size_t)kl == klen && memcmp(k, key, klen) == 0) return v;
        p = pc_bdec_skip(v, end);
        if (p == NULL) return NULL;
    }
    return NULL;
}

typedef struct {
    uint8_t* p;
    size_t len, cap;
} Buf;

static void b_raw(Buf* b, const void* data, size_t n) {
    if (b->len + n <= b->cap) memcpy(b->p + b->len, data, n);
    b->len += n;
}

static void b_str(Buf* b, const void* data, size_t n) {
    char num[16];
    int k = snprintf(num, sizeof(num), "%u:", (unsigned)n);

    b_raw(b, num, (size_t)k);
    b_raw(b, data, n);
}

static void b_cstr(Buf* b, const char* s) {
    b_str(b, s, strlen(s));
}

static void b_int(Buf* b, long v) {
    char num[24];
    int k = snprintf(num, sizeof(num), "i%lde", v);

    b_raw(b, num, (size_t)k);
}

/* --- Sending --- */

static void make_tid(uint8_t tid[3], int slot, int cand, uint8_t nonce) {
    tid[0] = (uint8_t)slot;
    tid[1] = (uint8_t)cand;
    tid[2] = nonce;
}

static void send_get_peers(PcDht* d, int slot, int ci) {
    Lookup* L = &d->look[slot];
    Cand* c = &L->cand[ci];
    uint8_t out[256];
    Buf b = { out, 0, sizeof(out) };
    uint8_t tid[3];

    make_tid(tid, slot, ci, L->nonce);
    /* keys in order: a, q, ro, t, y */
    b_raw(&b, "d", 1);
    b_cstr(&b, "a");
    b_raw(&b, "d", 1);
    b_cstr(&b, "id");
    b_str(&b, d->id, 20);
    b_cstr(&b, "info_hash");
    b_str(&b, L->key, 20);
    b_raw(&b, "e", 1);
    b_cstr(&b, "q");
    b_cstr(&b, "get_peers");
    b_cstr(&b, "ro");
    b_int(&b, 1);
    b_cstr(&b, "t");
    b_str(&b, tid, 3);
    b_cstr(&b, "y");
    b_cstr(&b, "q");
    b_raw(&b, "e", 1);
    if (b.len <= b.cap) pc_net_send(d->sock, &c->addr, out, b.len);
    c->state = C_SENT;
    c->sent_ms = pc_net_ms();
}

static void send_announce(PcDht* d, int slot, int ci) {
    Lookup* L = &d->look[slot];
    Cand* c = &L->cand[ci];
    uint8_t out[320];
    Buf b = { out, 0, sizeof(out) };
    uint8_t tid[3];

    make_tid(tid, slot, 0xFF, L->nonce);
    b_raw(&b, "d", 1);
    b_cstr(&b, "a");
    b_raw(&b, "d", 1);
    b_cstr(&b, "id");
    b_str(&b, d->id, 20);
    b_cstr(&b, "implied_port");
    b_int(&b, 1);
    b_cstr(&b, "info_hash");
    b_str(&b, L->key, 20);
    b_cstr(&b, "port");
    b_int(&b, pc_net_port(d->sock));
    b_cstr(&b, "token");
    b_str(&b, c->token, (size_t)c->token_len);
    b_raw(&b, "e", 1);
    b_cstr(&b, "q");
    b_cstr(&b, "announce_peer");
    b_cstr(&b, "ro");
    b_int(&b, 1);
    b_cstr(&b, "t");
    b_str(&b, tid, 3);
    b_cstr(&b, "y");
    b_cstr(&b, "q");
    b_raw(&b, "e", 1);
    if (b.len <= b.cap) pc_net_send(d->sock, &c->addr, out, b.len);
}

/* --- Lookups --- */

static int closer(const uint8_t* key, const uint8_t* a, const uint8_t* b) {
    for (int i = 0; i < 20; i++) {
        uint8_t x = a[i] ^ key[i], y = b[i] ^ key[i];

        if (x != y) return x < y;
    }
    return 0;
}

/* Keeps candidates ordered: unknown ids (bootstrap) first, then by distance */
static void add_cand(Lookup* L, const uint8_t* id, const PcNetAddr* addr) {
    int at;

    if (addr->ip == 0 || addr->port == 0) return;
    for (int i = 0; i < L->ncand; i++) {
        if (pc_net_addr_equal(&L->cand[i].addr, addr)) return;
    }
    at = L->ncand;
    if (id != NULL) {
        for (at = 0; at < L->ncand; at++) {
            if (L->cand[at].have_id && closer(L->key, id, L->cand[at].id)) break;
        }
    }
    if (at >= MAX_CAND) return;
    if (L->ncand == MAX_CAND) L->ncand--; /* the farthest goes */
    memmove(&L->cand[at + 1], &L->cand[at], sizeof(Cand) * (size_t)(L->ncand - at));
    memset(&L->cand[at], 0, sizeof(Cand));
    if (id != NULL) {
        memcpy(L->cand[at].id, id, 20);
        L->cand[at].have_id = 1;
    }
    L->cand[at].addr = *addr;
    L->ncand++;
}

void pc_dht_lookup(PcDht* d, int slot, const uint8_t key[20], int announce) {
    Lookup* L;

    if (slot < 0 || slot >= PC_DHT_MAX_LOOKUPS) return;
    L = &d->look[slot];
    uint8_t nonce = (uint8_t)(L->nonce + 1);
    memset(L, 0, sizeof(*L));
    L->nonce = nonce;
    L->active = 1;
    L->running = 1;
    L->announce = announce;
    L->started = pc_net_ms();
    memcpy(L->key, key, 20);
    for (int i = 0; i < d->nboot; i++) add_cand(L, NULL, &d->boot[i]);
}

int pc_dht_lookup_running(PcDht* d, int slot) {
    return slot >= 0 && slot < PC_DHT_MAX_LOOKUPS && d->look[slot].running;
}

int pc_dht_lookup_peers(PcDht* d, int slot, PcNetAddr* out, int max) {
    Lookup* L;
    int n;

    if (slot < 0 || slot >= PC_DHT_MAX_LOOKUPS) return 0;
    L = &d->look[slot];
    n = L->npeers < max ? L->npeers : max;
    memcpy(out, L->peers, sizeof(PcNetAddr) * (size_t)n);
    return n;
}

int pc_dht_lookup_responses(PcDht* d, int slot) {
    return slot >= 0 && slot < PC_DHT_MAX_LOOKUPS ? d->look[slot].responses : 0;
}

int pc_dht_lookup_announced(PcDht* d, int slot) {
    return slot >= 0 && slot < PC_DHT_MAX_LOOKUPS ? d->look[slot].announced : 0;
}

static void finish(PcDht* d, int slot) {
    Lookup* L = &d->look[slot];
    int sent = 0;

    L->running = 0;
    if (!L->announce) return;
    for (int i = 0; i < L->ncand && sent < K; i++) {
        if (L->cand[i].state == C_REPLIED && L->cand[i].token_len > 0) {
            send_announce(d, slot, i);
            sent++;
        }
    }
    L->announced = sent > 0;
}

void pc_dht_tick(PcDht* d, uint32_t now) {
    for (int s = 0; s < PC_DHT_MAX_LOOKUPS; s++) {
        Lookup* L = &d->look[s];
        int inflight = 0, replied_closest = 0, pending_closer = 0;

        if (!L->running) continue;
        for (int i = 0; i < L->ncand; i++) {
            Cand* c = &L->cand[i];

            if (c->state == C_SENT && now - c->sent_ms > QUERY_TIMEOUT) c->state = C_FAILED;
            if (c->state == C_SENT) inflight++;
        }
        /* query the closest not asked yet, until K of the closest answered */
        for (int i = 0; i < L->ncand && replied_closest < K; i++) {
            Cand* c = &L->cand[i];

            if (c->state == C_REPLIED) {
                replied_closest++;
            } else if (c->state == C_NEW) {
                pending_closer++;
                if (inflight < ALPHA) {
                    send_get_peers(d, s, i);
                    inflight++;
                }
            }
        }
        if ((inflight == 0 && (pending_closer == 0 || replied_closest >= K)) || now - L->started > LOOKUP_TIMEOUT) {
            finish(d, s);
        }
    }
}

/* --- Receiving --- */

static void add_peer(Lookup* L, const uint8_t* p) {
    PcNetAddr a;

    a.ip = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
    a.port = (uint16_t)((p[4] << 8) | p[5]);
    if (a.ip == 0 || a.port == 0) return;
    for (int i = 0; i < L->npeers; i++) {
        if (pc_net_addr_equal(&L->peers[i], &a)) return;
    }
    if (L->npeers < MAX_PEERS) L->peers[L->npeers++] = a;
}

static void reply_ping(PcDht* d, const PcNetAddr* from, const uint8_t* tid, int tid_len) {
    uint8_t out[128];
    Buf b = { out, 0, sizeof(out) };

    b_raw(&b, "d", 1);
    b_cstr(&b, "r");
    b_raw(&b, "d", 1);
    b_cstr(&b, "id");
    b_str(&b, d->id, 20);
    b_raw(&b, "e", 1);
    b_cstr(&b, "t");
    b_str(&b, tid, (size_t)tid_len);
    b_cstr(&b, "y");
    b_cstr(&b, "r");
    b_raw(&b, "e", 1);
    if (b.len <= b.cap) pc_net_send(d->sock, from, out, b.len);
}

int pc_dht_handle(PcDht* d, const PcNetAddr* from, const uint8_t* data, int len) {
    const uint8_t* end = data + len;
    const uint8_t *y, *t, *r, *ip;
    const uint8_t* s;
    int slen, tlen;

    if (len < 10 || data[0] != 'd' || pc_bdec_skip(data, end) == NULL) return 0;
    y = pc_bdec_dict_get(data, end, "y");
    t = pc_bdec_dict_get(data, end, "t");
    if (y == NULL || t == NULL || !pc_bdec_str(t, end, &t, &tlen) || !pc_bdec_str(y, end, &s, &slen) || slen != 1) {
        return 1;
    }
    char ytype = (char)s[0];
    if (ytype == 'q') {
        const uint8_t* q = pc_bdec_dict_get(data, end, "q");

        if (q != NULL && pc_bdec_str(q, end, &s, &slen) && slen == 4 && memcmp(s, "ping", 4) == 0) {
            reply_ping(d, from, t, tlen);
        }
        return 1; /* read-only: nothing else to answer */
    }
    ip = pc_bdec_dict_get(data, end, "ip");
    if (ip != NULL && pc_bdec_str(ip, end, &s, &slen) && slen == 6) {
        d->ext_ip = ((uint32_t)s[0] << 24) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 8) | s[3];
    }
    if (tlen != 3 || t[0] >= PC_DHT_MAX_LOOKUPS) return 1;
    Lookup* L = &d->look[t[0]];
    if (!L->active || t[2] != L->nonce) return 1;
    r = pc_bdec_dict_get(data, end, "r");
    if (r == NULL) {
        if (t[1] < L->ncand && pc_net_addr_equal(&L->cand[t[1]].addr, from)) L->cand[t[1]].state = C_FAILED;
        return 1;
    }
    if (t[1] == 0xFF) return 1; /* an announce's answer */
    int ci = t[1];
    if (ci >= L->ncand || !pc_net_addr_equal(&L->cand[ci].addr, from) || L->cand[ci].state != C_SENT) {
        /* the list moved: find it by address */
        ci = -1;
        for (int i = 0; i < L->ncand; i++) {
            if (pc_net_addr_equal(&L->cand[i].addr, from) && L->cand[i].state == C_SENT) {
                ci = i;
                break;
            }
        }
        if (ci < 0) return 1;
    }
    Cand reply = L->cand[ci];
    const uint8_t* id = pc_bdec_dict_get(r, end, "id");
    if (id != NULL && pc_bdec_str(id, end, &s, &slen) && slen == 20) {
        memcpy(reply.id, s, 20);
        reply.have_id = 1;
    }
    const uint8_t* tok = pc_bdec_dict_get(r, end, "token");
    if (tok != NULL && pc_bdec_str(tok, end, &s, &slen) && slen > 0 && slen <= (int)sizeof(reply.token)) {
        memcpy(reply.token, s, (size_t)slen);
        reply.token_len = slen;
    }
    reply.state = C_REPLIED;
    L->responses++;
    /* re-place it by its id, now known */
    memmove(&L->cand[ci], &L->cand[ci + 1], sizeof(Cand) * (size_t)(L->ncand - ci - 1));
    L->ncand--;
    {
        int at;

        for (at = 0; at < L->ncand; at++) {
            if (L->cand[at].have_id && closer(L->key, reply.id, L->cand[at].id)) break;
        }
        memmove(&L->cand[at + 1], &L->cand[at], sizeof(Cand) * (size_t)(L->ncand - at));
        L->cand[at] = reply;
        L->ncand++;
    }
    const uint8_t* values = pc_bdec_dict_get(r, end, "values");
    if (values != NULL && *values == 'l') {
        const uint8_t* p = values + 1;

        while (p < end && *p != 'e') {
            if (pc_bdec_str(p, end, &s, &slen) && slen == 6) add_peer(L, s);
            p = pc_bdec_skip(p, end);
            if (p == NULL) break;
        }
    }
    const uint8_t* nodes = pc_bdec_dict_get(r, end, "nodes");
    if (nodes != NULL && pc_bdec_str(nodes, end, &s, &slen)) {
        for (int i = 0; i + 26 <= slen; i += 26) {
            PcNetAddr a;
            const uint8_t* n = s + i;

            a.ip = ((uint32_t)n[20] << 24) | ((uint32_t)n[21] << 16) | ((uint32_t)n[22] << 8) | n[23];
            a.port = (uint16_t)((n[24] << 8) | n[25]);
            if (!pc_net_ip_private(a.ip) || pc_net_ip_private(from->ip)) add_cand(L, n, &a);
        }
    }
    return 1;
}

/* --- STUN --- */

int pc_stun_request(uint8_t* buf, uint8_t txid[12]) {
    for (int i = 0; i < 12; i++) txid[i] = (uint8_t)(rand() ^ (pc_net_ms() >> (i % 8)));
    buf[0] = 0x00; /* Binding Request */
    buf[1] = 0x01;
    buf[2] = 0;
    buf[3] = 0;
    buf[4] = 0x21; /* magic cookie */
    buf[5] = 0x12;
    buf[6] = 0xA4;
    buf[7] = 0x42;
    memcpy(buf + 8, txid, 12);
    return 20;
}

int pc_stun_parse(const uint8_t* data, int len, const uint8_t txid[12], PcNetAddr* mapped) {
    int pos = 20, total;

    if (len < 20 || data[0] != 0x01 || data[1] != 0x01 || data[4] != 0x21 || data[5] != 0x12 || data[6] != 0xA4 ||
        data[7] != 0x42 || memcmp(data + 8, txid, 12) != 0) {
        return 0;
    }
    total = 20 + ((data[2] << 8) | data[3]);
    if (total > len) total = len;
    while (pos + 4 <= total) {
        int type = (data[pos] << 8) | data[pos + 1];
        int alen = (data[pos + 2] << 8) | data[pos + 3];
        const uint8_t* v = data + pos + 4;

        if (pos + 4 + alen > total) break;
        if ((type == 0x0020 || type == 0x0001) && alen >= 8 && v[1] == 0x01) {
            uint16_t port = (uint16_t)((v[2] << 8) | v[3]);
            uint32_t ip = ((uint32_t)v[4] << 24) | ((uint32_t)v[5] << 16) | ((uint32_t)v[6] << 8) | v[7];

            if (type == 0x0020) { /* XOR-MAPPED-ADDRESS */
                port ^= 0x2112;
                ip ^= 0x2112A442u;
            }
            mapped->ip = ip;
            mapped->port = port;
            return 1;
        }
        pos += 4 + ((alen + 3) & ~3);
    }
    return 0;
}
