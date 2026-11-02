/* test_mp.c - the multiplayer's rooms over loopback: codes, DHT/STUN
 * parsing, join with a password, players, positions, messages, the town,
 * kicking; with lost packets */
#include "pc_mp.h"
#include "pc_dht.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int pc_net_test_drop_percent;

static int s_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
            s_fail++;                                                    \
        }                                                                \
    } while (0)

static void run(PcMp** list, int n, int ms) {
    uint32_t start = pc_net_ms();

    while (pc_net_ms() - start < (uint32_t)ms) {
        for (int i = 0; i < n; i++) pc_mp_tick(list[i]);
        pc_net_sleep(2);
    }
}

static int wait_event(PcMp** list, int n, PcMp* who, PcMpEventKind kind, PcMpEvent* out, int ms) {
    uint32_t start = pc_net_ms();
    PcMpEvent ev;

    while (pc_net_ms() - start < (uint32_t)ms) {
        for (int i = 0; i < n; i++) pc_mp_tick(list[i]);
        while (pc_mp_poll_event(who, &ev)) {
            if (ev.kind == kind) {
                if (out) *out = ev;
                return 1;
            }
        }
        pc_net_sleep(2);
    }
    return 0;
}

static void test_codes(void) {
    PcNetAddr a = { 0xC0A80117u, 47070 }, b;
    uint32_t room;
    char code[32];

    pc_mp_code_make(&a, 0x1234ABCDu, code, sizeof(code));
    CHECK(strlen(code) == 15);
    CHECK(pc_mp_code_parse(code, &b, &room));
    CHECK(b.ip == a.ip && b.port == a.port && room == 0xABCD);
    /* lower case, O for 0, no dashes */
    for (char* p = code; *p; p++) {
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p + 32);
        if (*p == '0') *p = 'o';
    }
    CHECK(pc_mp_code_parse(code, &b, &room) && b.ip == a.ip);
    CHECK(!pc_mp_code_parse("ABCD-EFGH", &b, &room));
}

static void test_bencode_stun(void) {
    static const char msg[] = "d1:rd2:id20:abcdefghij01234567895:token4:tok16:valuesl6:\x7f\x00\x00\x01\xb7\xde"
                              "ee1:t3:xyz1:y1:re";
    const uint8_t* m = (const uint8_t*)msg;
    const uint8_t* end = m + sizeof(msg) - 1;
    const uint8_t *r, *s;
    int len;

    CHECK(pc_bdec_skip(m, end) == end);
    r = pc_bdec_dict_get(m, end, "r");
    CHECK(r != NULL);
    CHECK(pc_bdec_str(pc_bdec_dict_get(r, end, "token"), end, &s, &len) && len == 4 && memcmp(s, "tok1", 4) == 0);
    CHECK(pc_bdec_dict_get(m, end, "nope") == NULL);

    uint8_t req[20], tx[12], resp[32];
    PcNetAddr mapped;
    pc_stun_request(req, tx);
    memset(resp, 0, sizeof(resp));
    resp[0] = 0x01; resp[1] = 0x01; resp[3] = 12;
    resp[4] = 0x21; resp[5] = 0x12; resp[6] = 0xA4; resp[7] = 0x42;
    memcpy(resp + 8, tx, 12);
    resp[20] = 0x00; resp[21] = 0x20; resp[23] = 8; resp[25] = 0x01;
    resp[26] = (uint8_t)((47070 ^ 0x2112) >> 8); resp[27] = (uint8_t)((47070 ^ 0x2112) & 255);
    uint32_t ip = 0xCB007107u ^ 0x2112A442u; /* 203.0.113.7 */
    resp[28] = (uint8_t)(ip >> 24); resp[29] = (uint8_t)(ip >> 16); resp[30] = (uint8_t)(ip >> 8); resp[31] = (uint8_t)ip;
    CHECK(pc_stun_parse(resp, 32, tx, &mapped) && mapped.ip == 0xCB007107u && mapped.port == 47070);
}

/* A fake DHT node: answers get_peers with a token and what was announced */
static PcNetSocket* s_node;
static PcNetAddr s_announced[16];
static int s_nannounced;

static void node_tick(void) {
    uint8_t buf[2048];
    PcNetAddr from;
    int n;

    while ((n = pc_net_recv(s_node, &from, buf, sizeof(buf))) > 0) {
        const uint8_t* end = buf + n;
        const uint8_t *q = pc_bdec_dict_get(buf, end, "q"), *t = pc_bdec_dict_get(buf, end, "t");
        const uint8_t *qs, *ts;
        int ql, tl;
        char out[1024];
        int o = 0;

        if (!q || !t || !pc_bdec_str(q, end, &qs, &ql) || !pc_bdec_str(t, end, &ts, &tl)) continue;
        if (ql == 13 && memcmp(qs, "announce_peer", 13) == 0) {
            const uint8_t* a = pc_bdec_dict_get(buf, end, "a");
            const uint8_t* implied = a ? pc_bdec_dict_get(a, end, "implied_port") : NULL;
            const uint8_t* port = a ? pc_bdec_dict_get(a, end, "port") : NULL;
            PcNetAddr who = from;

            /* as a real node: the given port unless implied_port is 1 */
            if (implied != NULL && implied[0] == 'i' && implied[1] == '0' && port != NULL && port[0] == 'i') {
                who.port = (uint16_t)atoi((const char*)port + 1);
            }
            if (s_nannounced < 16) s_announced[s_nannounced++] = who;
            o = sprintf(out, "d1:rd2:id20:NODEnodeNODEnodeNODEe1:t%d:", tl);
            memcpy(out + o, ts, (size_t)tl);
            o += tl;
            o += sprintf(out + o, "1:y1:re");
        } else if (ql == 9 && memcmp(qs, "get_peers", 9) == 0) {
            o = sprintf(out, "d2:ip6:");
            out[o++] = 1; out[o++] = 2; out[o++] = 3; out[o++] = 4; out[o++] = 0; out[o++] = 80;
            o += sprintf(out + o, "1:rd2:id20:NODEnodeNODEnodeNODE5:token4:tok1");
            if (s_nannounced > 0) {
                o += sprintf(out + o, "6:valuesl");
                for (int i = 0; i < s_nannounced; i++) {
                    uint32_t ip = s_announced[i].ip;
                    o += sprintf(out + o, "6:");
                    out[o++] = (char)(ip >> 24); out[o++] = (char)(ip >> 16); out[o++] = (char)(ip >> 8); out[o++] = (char)ip;
                    out[o++] = (char)(s_announced[i].port >> 8); out[o++] = (char)s_announced[i].port;
                }
                out[o++] = 'e';
            }
            o += sprintf(out + o, "e1:t%d:", tl);
            memcpy(out + o, ts, (size_t)tl);
            o += tl;
            o += sprintf(out + o, "1:y1:re");
        } else {
            continue;
        }
        pc_net_send(s_node, &from, out, (size_t)o);
    }
}

static void test_rooms(void) {
    PcMp* host = pc_mp_new(0);
    PcMp* guest = pc_mp_new(0);
    PcMp* both[2] = { host, guest };
    PcMpHostConfig cfg;
    PcMpLook look = { 1, 3, 0, 1, 0x2400, 0 };
    PcMpEvent ev;
    PcMpRoomInfo rooms[8];
    PcMpPlayer players[8];
    PcNetAddr addr = { 0x7F000001u, 0 };
    int n;

    CHECK(host != NULL && guest != NULL);
    if (host == NULL || guest == NULL) return;
    pc_mp_set_me(host, "Hana", &look);
    look.gender = 0;
    pc_mp_set_me(guest, "민수", &look);

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.room_name, sizeof(cfg.room_name), "Test room");
    snprintf(cfg.password, sizeof(cfg.password), "abc");
    snprintf(cfg.town, sizeof(cfg.town), "Leaf");
    cfg.is_public = 1;
    cfg.max_players = 4;
    CHECK(pc_mp_host(host, &cfg));
    CHECK(pc_mp_mode(host) == PC_MP_HOSTING);

    /* found by broadcast (loopback) */
    pc_mp_browse(guest, 1);
    run(both, 2, 300);
    n = pc_mp_rooms(guest, rooms, 8);
    CHECK(n >= 1);
    if (n >= 1) {
        CHECK(strcmp(rooms[0].name, "Test room") == 0 && rooms[0].has_password && rooms[0].is_public);
        CHECK(strcmp(rooms[0].host_name, "Hana") == 0 && rooms[0].players == 1 && rooms[0].max_players == 4);
    }
    pc_mp_browse(guest, 0);

    addr.port = pc_mp_port(host);
    /* wrong password */
    pc_mp_join(guest, &addr, 0, "nope");
    CHECK(wait_event(both, 2, guest, PC_MP_EV_DENIED, &ev, 3000));
    CHECK(pc_mp_mode(guest) == PC_MP_IDLE);

    /* with 15% of all packets lost from here on */
    pc_net_test_drop_percent = 15;
    pc_mp_join(guest, &addr, 0, "abc");
    CHECK(wait_event(both, 2, guest, PC_MP_EV_JOINED, &ev, 8000));
    CHECK(pc_mp_mode(guest) == PC_MP_JOINED);
    run(both, 2, 1500);
    n = pc_mp_players(guest, players, 8);
    CHECK(n == 2);
    n = pc_mp_players(host, players, 8);
    CHECK(n == 2);
    for (int i = 0; i < n; i++) {
        if (!players[i].is_you) CHECK(strcmp(players[i].name, "민수") == 0 && players[i].look.cloth == 0x2400);
    }

    /* positions */
    PcMpState st = { 0x1234, 7, 1, 1, 100.0f, 0.0f, 200.0f, 0x4000, 0 };
    pc_mp_set_state(guest, &st);
    run(both, 2, 800);
    n = pc_mp_players(host, players, 8);
    for (int i = 0; i < n; i++) {
        if (!players[i].is_you) CHECK(players[i].state.town == 0x1234 && players[i].state.x == 100.0f);
    }

    /* game messages, in order */
    for (int i = 0; i < 50; i++) {
        uint8_t m[3] = { 'G', (uint8_t)i, 0 };
        pc_mp_send_game(guest, 0, 1, m, 3);
    }
    {
        int got = 0, in_order = 1, from;
        uint8_t buf[64];
        uint32_t start = pc_net_ms();

        while (got < 50 && pc_net_ms() - start < 8000) {
            run(both, 2, 10);
            while (pc_mp_poll_game(host, &from, buf, sizeof(buf)) > 0) {
                if (buf[1] != got) in_order = 0;
                got++;
            }
        }
        CHECK(got == 50 && in_order);
    }

    /* chat */
    pc_mp_chat(host, "hello");
    CHECK(wait_event(both, 2, guest, PC_MP_EV_CHAT, &ev, 5000) && strcmp(ev.text, "hello") == 0);

    /* the town: 200 KB */
    {
        size_t len = 200000, got_len = 0;
        uint8_t* town = (uint8_t*)malloc(len);
        const uint8_t* got;

        for (size_t i = 0; i < len; i++) town[i] = (uint8_t)(i * 7 + (i >> 9));
        pc_mp_request_town(guest);
        CHECK(wait_event(both, 2, host, PC_MP_EV_TOWN_WANTED, &ev, 5000));
        pc_mp_set_town_blob(host, town, len);

        CHECK(wait_event(both, 2, guest, PC_MP_EV_TOWN, &ev, 20000));
        got = pc_mp_town_blob(guest, &got_len);
        CHECK(got != NULL && got_len == len && memcmp(got, town, len) == 0);
        free(town);
    }

    /* kicked, and not let back in */
    pc_net_test_drop_percent = 0;
    pc_mp_kick(host, pc_mp_my_slot(guest));
    CHECK(wait_event(both, 2, guest, PC_MP_EV_KICKED, &ev, 3000));
    CHECK(pc_mp_mode(guest) == PC_MP_IDLE);
    n = pc_mp_players(host, players, 8);
    CHECK(n == 1);
    pc_mp_join(guest, &addr, 0, "abc");
    CHECK(wait_event(both, 2, guest, PC_MP_EV_DENIED, &ev, 3000) && strstr(ev.text, "sent out") != NULL);

    pc_mp_free(guest);
    pc_mp_free(host);
}

/* A private room through its code, and a public one through the DHT */
static void test_internet(void) {
    PcMp* host = pc_mp_new(0);
    PcMp* guest = pc_mp_new(0);
    PcMp* both[2] = { host, guest };
    PcMpHostConfig cfg;
    PcNetAddr node, code_addr;
    PcMpEvent ev;
    PcMpRoomInfo rooms[8];
    char code[32];
    uint32_t room16;
    int n, found = 0;

    s_node = pc_net_open(0);
    node.ip = 0x7F000001u;
    node.port = pc_net_port(s_node);
    pc_mp_add_bootstrap(host, &node);
    pc_mp_add_bootstrap(guest, &node);

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.room_name, sizeof(cfg.room_name), "Public");
    cfg.is_public = 1;
    cfg.max_players = 8;
    pc_mp_host(host, &cfg);
    /* the host announces itself under the rooms key */
    for (int i = 0; i < 200 && s_nannounced == 0; i++) {
        pc_mp_tick(host);
        node_tick();
        pc_net_sleep(5);
    }
    CHECK(s_nannounced >= 1);
    CHECK(pc_mp_internet_ready(host));

    pc_mp_browse(guest, 1);
    for (int i = 0; i < 400 && !found; i++) {
        pc_mp_tick(host);
        pc_mp_tick(guest);
        node_tick();
        n = pc_mp_rooms(guest, rooms, 8);
        for (int k = 0; k < n; k++) found |= strcmp(rooms[k].name, "Public") == 0;
        pc_net_sleep(5);
    }
    CHECK(found);
    pc_mp_browse(guest, 0);

    /* the code holds the address; the guest tells the DHT it wants in */
    pc_mp_room_code(host, code, sizeof(code));
    CHECK(pc_mp_code_parse(code, &code_addr, &room16));
    code_addr.ip = 0x7F000001u; /* (the test's own machine) */
    code_addr.port = pc_mp_port(host);
    pc_mp_join(guest, &code_addr, room16, "");
    for (int i = 0; i < 400 && pc_mp_mode(guest) != PC_MP_JOINED; i++) {
        pc_mp_tick(host);
        pc_mp_tick(guest);
        node_tick();
        pc_net_sleep(5);
    }
    CHECK(pc_mp_mode(guest) == PC_MP_JOINED);

    /* the router forwards a port (UPnP) and its address is the one the DHT
     * sees (1.2.3.4): the code and the public listing carry that port */
    CHECK(pc_mp_ip_is_public(0x01020304u) && !pc_mp_ip_is_public(0xC0A80001u) && !pc_mp_ip_is_public(0x64400001u) &&
          !pc_mp_ip_is_public(0xAC1F0001u) && pc_mp_ip_is_public(0xAC200001u));
    pc_mp_set_port_mapping(host, 0x01020304u, 47070);
    CHECK(pc_mp_port_mapped(host));
    pc_mp_room_code(host, code, sizeof(code));
    CHECK(pc_mp_code_parse(code, &code_addr, &room16) && code_addr.ip == 0x01020304u && code_addr.port == 47070);
    /* (the guest's own join announce may come in too: look for the room's) */
    s_nannounced = 0;
    found = 0;
    for (int i = 0; i < 600 && !found; i++) {
        pc_mp_tick(host);
        pc_mp_tick(guest);
        node_tick();
        for (int k = 0; k < s_nannounced; k++) found |= s_announced[k].port == 47070;
        pc_net_sleep(5);
    }
    CHECK(found);
    for (int k = 0; k < s_nannounced; k++) CHECK(s_announced[k].port != pc_mp_port(host));
    /* a router behind another router (its address is not the one seen):
     * the code keeps the address and port the internet sees */
    pc_mp_set_port_mapping(host, 0xC0A80101u, 47070);
    CHECK(!pc_mp_port_mapped(host));
    pc_mp_room_code(host, code, sizeof(code));
    CHECK(pc_mp_code_parse(code, &code_addr, &room16) && code_addr.ip == 0x01020304u &&
          code_addr.port == pc_mp_port(host));
    pc_mp_set_port_mapping(host, 0x05060708u, 47070);
    CHECK(!pc_mp_port_mapped(host));
    (void)ev;
    (void)both;
    pc_mp_free(guest);
    pc_mp_free(host);
    pc_net_close(s_node);
}

int main(void) {
    srand(1234);
    pc_net_init();
    test_codes();
    test_bencode_stun();
    test_rooms();
    test_internet();
    if (s_fail == 0) printf("test_mp: all passed\n");
    return s_fail != 0;
}
