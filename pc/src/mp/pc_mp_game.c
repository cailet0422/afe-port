/* pc_mp_game.c - see pc_mp_game.h */
#include "pc_platform.h"
#include "pc_mp_game.h"
#include "pc_mp.h"
#include "pc_net.h"
#include "pc_upnp.h"
#include "pc_settings.h"
#include "pc_gui.h"
#include "pc_ko.h"
#include "pc_rom_dir.h"
#include "ko_charset.h"

#include "game.h"
#include "m_actor.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_name_table.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_private.h"
#include "m_scene_table.h"
#include "lb_rtc.h"

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

extern void mCD_PC_SnapshotSave(Save* out);
extern int mCD_PC_LandFileInfo(const char** name, int* file_size, int* save_ofs0, int* save_ofs1);
extern void pc_card_set_slot_b(const char* dir);
extern const char* pc_card_dir(s32 chan);

#define NET_DIR "save/card_b_net"
#define TAG_FRAMES 3

/* Messages of the game (first byte) */
enum {
    G_ITEM = 1,     /* guest -> host: town, bx, bz, ux, uz, old, new */
    G_ITEM_SET,     /* host -> guests: town, bx, bz, ux, uz, item */
    G_DEPOSIT,      /* guest -> host: town, bx, bz, ux, uz, on */
    G_DEPOSIT_SET,  /* host -> guests */
    G_FG_REQ,       /* guest -> host: the whole item layer, please */
    G_FG_DATA,      /* host -> guest: town, block, items[256], deposit[16] */
    G_TIME,         /* host -> guests: the host's clock */
};

static PcMp* s_mp = NULL;

/* lookups on a thread: DHT nodes, the STUN server, the router (UPnP) */
static SDL_mutex* s_lock = NULL;
static PcNetAddr s_boot[8];
static int s_nboot = 0;
static int s_boot_given = 0;
static PcNetAddr s_stun;
static int s_stun_given = 0;
static int s_net_started = 0;
static volatile int s_upnp = 0; /* 0 not tried, 1 trying, 2 open, 3 no */
static uint32_t s_upnp_ext = 0;
static uint16_t s_upnp_port = 0;

/* where we are */
static uint32_t s_my_town = 0;
static int s_my_scene = -1;
static int s_my_outside = 0;
static int s_foreigner = 0;

/* visiting */
static PcMpVisit s_visit = PC_MP_VISIT_NONE;
static uint32_t s_host_town = 0;
static int s_slot_b_net = 0;
static int s_visited = 0;   /* were in the host's town */
static int s_fg_asked = 0;  /* asked for the item layer on this arrival */
static int s_fg_blocks = 0;

/* the other players */
static void* s_puppet[PC_MP_MAX_PLAYERS];
static float s_tag_x[PC_MP_MAX_PLAYERS], s_tag_y[PC_MP_MAX_PLAYERS];
static u32 s_tag_frame[PC_MP_MAX_PLAYERS];
static u32 s_frame = 0;
static u32 s_time_sent = 0;

/* units the game is busy with (RSV_NO while an item drops or a hole opens):
 * what was there before, for the host's check */
typedef struct {
    u8 bx, bz, ux, uz;
    u16 orig;
} Pending;
static Pending s_rsv[32];
static int s_nrsv = 0;
static int s_applying = 0;

/* the last chat line of each player, shown over its head for a while */
static char s_said[PC_MP_MAX_PLAYERS][160];
static u32 s_said_ms[PC_MP_MAX_PLAYERS];
#define SAID_MS 6000

#define CHAT_LINES 8
static char s_chat[CHAT_LINES][192];
static int s_nchat = 0;

/* --- Little helpers --- */

static void w16(u8* p, u16 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}
static void w32(u8* p, u32 v) {
    w16(p, (u16)v);
    w16(p + 2, (u16)(v >> 16));
}
static u16 r16(const u8* p) {
    return (u16)(p[0] | (p[1] << 8));
}
static u32 r32(const u8* p) {
    return r16(p) | ((u32)r16(p + 2) << 16);
}

static void utf8_put(char** o, char* end, unsigned u) {
    char* p = *o;

    if (u < 0x80 && p + 1 < end) {
        *p++ = (char)u;
    } else if (u < 0x800 && p + 2 < end) {
        *p++ = (char)(0xC0 | (u >> 6));
        *p++ = (char)(0x80 | (u & 63));
    } else if (p + 3 < end) {
        *p++ = (char)(0xE0 | (u >> 12));
        *p++ = (char)(0x80 | ((u >> 6) & 63));
        *p++ = (char)(0x80 | (u & 63));
    }
    *o = p;
}

/* A name in the game's characters as UTF-8: Hangul (Korean patch), letters,
 * digits; others are left out */
static void game_to_utf8(const u8* s, int n, char* out, size_t cap) {
    char* o = out;
    char* end = out + cap;

    for (int i = 0; i < n;) {
        int g = (g_pc_ko_mode && i + 1 < n) ? ko_charset_decode(s + i) : -1;
        u8 c = s[i];

        if (g >= 0) {
            utf8_put(&o, end, ko_charset_unicode(g));
            i += 2;
            continue;
        }
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ' || c == '.' ||
            c == '-' || c == '!' || c == '?') {
            utf8_put(&o, end, c);
        }
        i++;
    }
    while (o > out && o[-1] == ' ') o--;
    *o = '\0';
}

static u32 town_id(const mLd_land_info_c* land) {
    u32 h = 2166136261u;

    for (int i = 0; i < LAND_NAME_SIZE; i++) h = (h ^ land->name[i]) * 16777619u;
    return ((h & 0xFFFF) << 16) | land->id;
}

static GAME_PLAY* play_of(GAME* game) {
    if (game == NULL || game->exec != play_main) {
        return NULL;
    }
    return (GAME_PLAY*)game;
}

static int outside_in_town(void) {
    return Save_Get(scene_no) == SCENE_FG && mFI_GET_TYPE(mFI_GetFieldId()) == mFI_FIELD_FG &&
           Common_Get(field_type) == mFI_FIELDTYPE2_FG && !mFI_CheckInIsland();
}

static int in_room(void) {
    return s_mp != NULL && (pc_mp_mode(s_mp) == PC_MP_HOSTING || pc_mp_mode(s_mp) == PC_MP_JOINED);
}

static int is_host(void) {
    return s_mp != NULL && pc_mp_mode(s_mp) == PC_MP_HOSTING;
}

/* The town the room shares: the host's own */
static u32 room_town(void) {
    return is_host() ? s_my_town : s_host_town;
}

static int in_shared_town(void) {
    return in_room() && s_my_town != 0 && s_my_town == room_town();
}

void pc_mp_game_my_names(char* name, size_t n, char* town, size_t tn) {
    if (g_pc_settings.mp_name[0] != '\0') {
        snprintf(name, n, "%s", g_pc_settings.mp_name);
    } else if (Common_Get(now_private) != NULL) {
        game_to_utf8(Now_Private->player_ID.player_name, PLAYER_NAME_LEN, name, n);
    } else {
        name[0] = '\0';
    }
    if (name[0] == '\0') snprintf(name, n, "%s", pc_gui_korean() ? "플레이어" : "Player");
    game_to_utf8(Save_Get(land_info).name, LAND_NAME_SIZE, town, tn);
}

/* --- Lookups on a thread --- */

static int net_thread(void* unused) {
    static const struct {
        const char* host;
        uint16_t port;
    } boots[] = {
        { "router.bittorrent.com", 6881 },
        { "dht.transmissionbt.com", 6881 },
        { "router.utorrent.com", 6881 },
        { "dht.libtorrent.org", 25401 },
    };
    PcNetAddr a;

    (void)unused;
    for (size_t i = 0; i < sizeof(boots) / sizeof(boots[0]); i++) {
        if (pc_net_resolve(boots[i].host, boots[i].port, &a)) {
            SDL_LockMutex(s_lock);
            if (s_nboot < 8) s_boot[s_nboot++] = a;
            SDL_UnlockMutex(s_lock);
        }
    }
    if (pc_net_resolve("stun.l.google.com", 19302, &a) || pc_net_resolve("stun.cloudflare.com", 3478, &a)) {
        SDL_LockMutex(s_lock);
        s_stun = a;
        SDL_UnlockMutex(s_lock);
    }
    return 0;
}

static int upnp_thread(void* unused) {
    uint32_t ext = 0;
    int ok;

    (void)unused;
    ok = pc_upnp_map(s_upnp_port, &ext);
    s_upnp_ext = ext;
    s_upnp = ok ? 2 : 3;
    printf("[MP] UPnP: %s\n", ok ? "port forwarded" : "no router answered");
    return 0;
}

static int unmap_thread(void* arg) {
    pc_upnp_unmap((uint16_t)(uintptr_t)arg);
    return 0;
}

void pc_mp_game_open(void) {
    if (s_mp == NULL) {
        uint16_t port = (uint16_t)(g_pc_settings.mp_port > 0 ? g_pc_settings.mp_port : PC_MP_DEFAULT_PORT);

        s_mp = pc_mp_new(port);
        if (s_mp == NULL) {
            printf("[MP] cannot open a UDP socket\n");
            return;
        }
        printf("[MP] UDP port %u\n", pc_mp_port(s_mp));
    }
    if (!s_net_started && !g_pc_deterministic) {
        s_net_started = 1;
        if (s_lock == NULL) s_lock = SDL_CreateMutex();
        SDL_DetachThread(SDL_CreateThread(net_thread, "mpnet", NULL));
    }
}

PcMp* pc_mp_game_session(void) {
    return s_mp;
}

/* --- Rooms --- */

static void update_me(void) {
    char name[PC_MP_NAME_LEN], town[32];
    PcMpLook look;

    if (s_mp == NULL) return;
    memset(&look, 0, sizeof(look));
    if (Common_Get(now_private) != NULL) {
        look.gender = Now_Private->gender;
        look.face = Now_Private->face;
        look.tan = (u8)(Now_Private->sunburn.rank > 0 ? Now_Private->sunburn.rank : 0);
        look.cloth = Now_Private->cloth.item;
        look.held = Now_Private->equipment;
    }
    look.korean = (u8)(g_pc_ko_mode != 0);
    pc_mp_game_my_names(name, sizeof(name), town, sizeof(town));
    pc_mp_set_me(s_mp, name, &look);
}

int pc_mp_game_host(const PcMpHostConfig* cfg) {
    pc_mp_game_open();
    if (s_mp == NULL) return 0;
    update_me();
    if (!pc_mp_host(s_mp, cfg)) return 0;
    s_visit = PC_MP_VISIT_NONE;
    if (!g_pc_deterministic && s_upnp == 0) {
        s_upnp = 1;
        s_upnp_port = pc_mp_port(s_mp);
        SDL_DetachThread(SDL_CreateThread(upnp_thread, "upnp", NULL));
    }
    pc_mp_set_port_open(s_mp, s_upnp == 2);
    return 1;
}

int pc_mp_game_join_text(const char* text, const char* password) {
    PcNetAddr addr;
    uint32_t room = 0;

    pc_mp_game_open();
    if (s_mp == NULL) return 0;
    if (!pc_mp_code_parse(text, &addr, &room) && !pc_net_addr_parse(text, PC_MP_DEFAULT_PORT, &addr)) {
        return 0;
    }
    update_me();
    return pc_mp_join(s_mp, &addr, room, password);
}

int pc_mp_game_join_room(const PcMpRoomInfo* room, const char* password) {
    pc_mp_game_open();
    if (s_mp == NULL) return 0;
    update_me();
    return pc_mp_join(s_mp, &room->addr, room->room_id, password);
}

static void slot_b_restore(void) {
    char path[600];
    const char* name;
    int size, o0, o1;

    if (!s_slot_b_net) return;
    pc_card_set_slot_b(NULL);
    s_slot_b_net = 0;
    mCD_PC_LandFileInfo(&name, &size, &o0, &o1);
    snprintf(path, sizeof(path), "%s/%s", NET_DIR, name);
    remove(path);
    printf("[MP] slot B is the real card again\n");
}

void pc_mp_game_leave(void) {
    if (s_mp == NULL) return;
    if (is_host() && s_upnp == 2) {
        SDL_DetachThread(SDL_CreateThread(unmap_thread, "upnp", (void*)(uintptr_t)s_upnp_port));
        s_upnp = 0;
    }
    pc_mp_leave(s_mp);
    /* a guest still in the host's town keeps slot B until it is home:
     * the trip back writes that town there, never to a real card B */
    if (s_visit != PC_MP_VISIT_THERE && !s_foreigner) {
        slot_b_restore();
        s_visit = PC_MP_VISIT_NONE;
    }
    memset(s_puppet, 0, sizeof(s_puppet));
}

void pc_mp_game_browse(int on) {
    pc_mp_game_open();
    if (s_mp != NULL) pc_mp_browse(s_mp, on);
}

void pc_mp_game_kick(int slot) {
    if (is_host()) pc_mp_kick(s_mp, slot);
}

static void chat_add(const char* who, const char* text) {
    if (s_nchat == CHAT_LINES) {
        memmove(s_chat[0], s_chat[1], sizeof(s_chat[0]) * (CHAT_LINES - 1));
        s_nchat--;
    }
    snprintf(s_chat[s_nchat++], sizeof(s_chat[0]), "%s: %s", who, text);
}

void pc_mp_game_chat(const char* text) {
    char name[PC_MP_NAME_LEN], town[32];

    if (!in_room() || text[0] == '\0') return;
    pc_mp_chat(s_mp, text);
    pc_mp_game_my_names(name, sizeof(name), town, sizeof(town));
    chat_add(name, text);
}

int pc_mp_game_chat_lines(char lines[][192], int max) {
    int n = s_nchat < max ? s_nchat : max;

    for (int i = 0; i < n; i++) memcpy(lines[i], s_chat[s_nchat - n + i], 192);
    return n;
}

PcMpVisit pc_mp_game_visit(int* progress) {
    if (progress != NULL) *progress = s_mp != NULL ? pc_mp_town_progress(s_mp) : 0;
    return s_visit;
}

int pc_mp_game_in_host_town(void) {
    return in_shared_town();
}

void pc_mp_game_net_status(char* buf, size_t n) {
    PcNetAddr ext;
    char a[32];
    int ko = pc_gui_korean();

    if (s_mp == NULL) {
        snprintf(buf, n, "-");
        return;
    }
    if (pc_mp_external_addr(s_mp, &ext)) {
        if (s_upnp_ext != 0) ext.ip = s_upnp_ext;
        pc_net_addr_str(&ext, a, sizeof(a));
        snprintf(buf, n, ko ? "인터넷 주소 %s%s%s" : "Internet address %s%s%s", a,
                 s_upnp == 2 ? (ko ? " (공유기 포트 열림)" : " (router port open)") : "",
                 pc_mp_internet_ready(s_mp) ? (ko ? ", DHT 연결됨" : ", DHT connected") : "");
    } else if (s_nboot == 0 && s_net_started) {
        snprintf(buf, n, ko ? "인터넷 확인 중... (같은 네트워크는 바로 됩니다)" : "Looking for the internet... (LAN works now)");
    } else {
        PcNetAddr local = { pc_net_local_ip(), pc_mp_port(s_mp) };

        pc_net_addr_str(&local, a, sizeof(a));
        snprintf(buf, n, ko ? "같은 네트워크만 (%s)" : "LAN only (%s)", a);
    }
}

/* --- Town file (host) and slot B (guest) --- */

static void mkdirs(const char* path) {
#ifdef _WIN32
    _mkdir("save");
    _mkdir(path);
#else
    mkdir("save", 0755);
    mkdir(path, 0755);
#endif
}

static void host_give_town(void) {
    const char* name;
    int size, o0, o1, save_size = mCD_PC_LandFileInfo(&name, &size, &o0, &o1);
    char path[600];
    u8* file;
    FILE* f;

    snprintf(path, sizeof(path), "%s/%s", pc_card_dir(0), name);
    file = (u8*)malloc((size_t)size);
    if (file == NULL) return;
    memset(file, 0, (size_t)size);
    f = pc_fopen_utf8(path, "rb");
    if (f == NULL) {
        free(file);
        pc_gui_notify(pc_gui_korean() ? "마을을 보내려면 먼저 한 번 저장하세요" : "Save once first to share your town");
        return;
    }
    fread(file, 1, (size_t)size, f);
    fclose(f);
    /* the town as it is now in both copies of the save part */
    mCD_PC_SnapshotSave((Save*)(file + o0));
    memcpy(file + o1, file + o0, (size_t)save_size);
    pc_mp_set_town_blob(s_mp, file, (size_t)size);
    free(file);
    printf("[MP] gave the town (%d bytes)\n", size);
}

static void guest_got_town(void) {
    const char* name;
    int size, o0, o1, save_size = mCD_PC_LandFileInfo(&name, &size, &o0, &o1);
    size_t len = 0;
    const u8* blob = pc_mp_town_blob(s_mp, &len);
    char path[600];
    FILE* f;

    if (blob == NULL || len != (size_t)size || o0 + save_size > size) {
        s_visit = PC_MP_VISIT_FAILED;
        return;
    }
    mkdirs(NET_DIR);
    snprintf(path, sizeof(path), "%s/%s", NET_DIR, name);
    f = pc_fopen_utf8(path, "wb");
    if (f == NULL || fwrite(blob, 1, len, f) != len) {
        if (f) fclose(f);
        s_visit = PC_MP_VISIT_FAILED;
        return;
    }
    fclose(f);
    s_host_town = town_id(&((const Save*)(blob + o0))->save.land_info);
    pc_card_set_slot_b(NET_DIR);
    s_slot_b_net = 1;
    s_visit = PC_MP_VISIT_READY;
    s_fg_asked = 0;
    pc_gui_notify(pc_gui_korean() ? "호스트 마을이 준비됐어요. 기차역 역무원에게 여행을 부탁하세요!"
                                  : "The host's town is ready: ask the station master for a trip!");
}

/* --- Items --- */

static u16* save_unit(int bx, int bz, int ux, int uz) {
    if (bx < 1 || bx > FG_BLOCK_X_NUM || bz < 1 || bz > FG_BLOCK_Z_NUM || ux < 0 || ux >= UT_X_NUM || uz < 0 ||
        uz >= UT_Z_NUM) {
        return NULL;
    }
    return &Save_Get(fg[bz - 1][bx - 1]).items[uz][ux];
}

static u16* save_deposit(int bx, int bz, int uz) {
    if (bx < 1 || bx > FG_BLOCK_X_NUM || bz < 1 || bz > FG_BLOCK_Z_NUM || uz < 0 || uz >= UT_Z_NUM) return NULL;
    return &Save_Get(deposit[(bx - 1) + (bz - 1) * FG_BLOCK_X_NUM][uz]);
}

static void refresh_field(void) {
    if (outside_in_town()) mFI_SetFGUpData();
}

static void put_unit(int bx, int bz, int ux, int uz, u16 item) {
    u16* p = save_unit(bx, bz, ux, uz);

    if (p == NULL || *p == item) return;
    s_applying = 1;
    *p = item;
    s_applying = 0;
    refresh_field();
}

static void put_deposit(int bx, int bz, int ux, int uz, int on) {
    u16* d = save_deposit(bx, bz, uz);

    if (d == NULL || ux < 0 || ux >= UT_X_NUM) return;
    if (on) *d |= (u16)(1 << ux);
    else *d &= (u16)~(1 << ux);
}

static void send_item_set(int to, int bx, int bz, int ux, int uz, u16 item) {
    u8 m[11];

    m[0] = G_ITEM_SET;
    w32(m + 1, room_town());
    m[5] = (u8)bx;
    m[6] = (u8)bz;
    m[7] = (u8)ux;
    m[8] = (u8)uz;
    w16(m + 9, item);
    pc_mp_send_game(s_mp, to, 1, m, sizeof(m));
}

void pc_mp_fg_written(int bx, int bz, int ux, int uz, unsigned short old_item, unsigned short new_item) {
    if (s_applying || !in_shared_town() || !outside_in_town() || save_unit(bx, bz, ux, uz) == NULL) {
        return;
    }
    if (new_item == RSV_NO) {
        /* the game is busy with it: keep what was there */
        if (old_item != RSV_NO && s_nrsv < (int)(sizeof(s_rsv) / sizeof(s_rsv[0]))) {
            Pending p = { (u8)bx, (u8)bz, (u8)ux, (u8)uz, old_item };
            s_rsv[s_nrsv++] = p;
        }
        return;
    }
    if (old_item == RSV_NO) {
        old_item = 0xFFFE; /* not known */
        for (int i = 0; i < s_nrsv; i++) {
            if (s_rsv[i].bx == bx && s_rsv[i].bz == bz && s_rsv[i].ux == ux && s_rsv[i].uz == uz) {
                old_item = s_rsv[i].orig;
                s_rsv[i] = s_rsv[--s_nrsv];
                break;
            }
        }
    }
    if (old_item == new_item) return;
    if (is_host()) {
        send_item_set(-1, bx, bz, ux, uz, new_item);
    } else {
        u8 m[13];

        m[0] = G_ITEM;
        w32(m + 1, room_town());
        m[5] = (u8)bx;
        m[6] = (u8)bz;
        m[7] = (u8)ux;
        m[8] = (u8)uz;
        w16(m + 9, old_item);
        w16(m + 11, new_item);
        pc_mp_send_game(s_mp, 0, 1, m, sizeof(m));
    }
}

void pc_mp_deposit_written(int bx, int bz, int ux, int uz, int on) {
    u8 m[10];

    if (s_applying || !in_shared_town() || !outside_in_town() || save_deposit(bx, bz, uz) == NULL) {
        return;
    }
    m[0] = is_host() ? G_DEPOSIT_SET : G_DEPOSIT;
    w32(m + 1, room_town());
    m[5] = (u8)bx;
    m[6] = (u8)bz;
    m[7] = (u8)ux;
    m[8] = (u8)uz;
    m[9] = (u8)(on != 0);
    pc_mp_send_game(s_mp, is_host() ? -1 : 0, 1, m, sizeof(m));
}

static void send_fg(int to) {
    u8 m[1 + 4 + 1 + UT_X_NUM * UT_Z_NUM * 2 + UT_Z_NUM * 2];

    for (int bz = 1; bz <= FG_BLOCK_Z_NUM; bz++) {
        for (int bx = 1; bx <= FG_BLOCK_X_NUM; bx++) {
            size_t n = 0;

            m[n++] = G_FG_DATA;
            w32(m + n, room_town());
            n += 4;
            m[n++] = (u8)((bz - 1) * FG_BLOCK_X_NUM + (bx - 1));
            for (int uz = 0; uz < UT_Z_NUM; uz++) {
                for (int ux = 0; ux < UT_X_NUM; ux++) {
                    w16(m + n, *save_unit(bx, bz, ux, uz));
                    n += 2;
                }
            }
            for (int uz = 0; uz < UT_Z_NUM; uz++) {
                w16(m + n, *save_deposit(bx, bz, uz));
                n += 2;
            }
            pc_mp_send_game(s_mp, to, 1, m, n);
        }
    }
}

static void game_message(int from, const u8* m, int len) {
    if (len < 5) return;
    u32 town = r32(m + 1);

    switch (m[0]) {
        case G_ITEM:
            if (is_host() && len >= 13 && town == s_my_town) {
                int bx = m[5], bz = m[6], ux = m[7], uz = m[8];
                u16 old = r16(m + 9), item = r16(m + 11);
                u16* cur = save_unit(bx, bz, ux, uz);

                if (cur == NULL) return;
                if (*cur == old || old == 0xFFFE || *cur == item) {
                    put_unit(bx, bz, ux, uz, item);
                    send_item_set(-1, bx, bz, ux, uz, item);
                } else {
                    /* someone was first: the guest gets what is there */
                    send_item_set(from, bx, bz, ux, uz, *cur);
                }
            }
            break;
        case G_ITEM_SET:
            if (!is_host() && len >= 11 && in_shared_town() && town == s_my_town) {
                put_unit(m[5], m[6], m[7], m[8], r16(m + 9));
            }
            break;
        case G_DEPOSIT:
            if (is_host() && len >= 10 && town == s_my_town) {
                u8 out[10];

                put_deposit(m[5], m[6], m[7], m[8], m[9]);
                memcpy(out, m, 10);
                out[0] = G_DEPOSIT_SET;
                pc_mp_send_game(s_mp, -1, 1, out, 10);
            }
            break;
        case G_DEPOSIT_SET:
            if (!is_host() && len >= 10 && in_shared_town() && town == s_my_town) {
                put_deposit(m[5], m[6], m[7], m[8], m[9]);
            }
            break;
        case G_FG_REQ:
            if (is_host() && town == s_my_town) send_fg(from);
            break;
        case G_FG_DATA:
            if (!is_host() && len >= 6 + UT_X_NUM * UT_Z_NUM * 2 + UT_Z_NUM * 2 && in_shared_town() &&
                town == s_my_town) {
                int block = m[5];
                int bx = block % FG_BLOCK_X_NUM + 1, bz = block / FG_BLOCK_X_NUM + 1;
                const u8* p = m + 6;

                if (bz > FG_BLOCK_Z_NUM) return;
                s_applying = 1;
                for (int uz = 0; uz < UT_Z_NUM; uz++) {
                    for (int ux = 0; ux < UT_X_NUM; ux++) {
                        u16* u = save_unit(bx, bz, ux, uz);
                        u16 item = r16(p);

                        /* (a unit the game is busy with stays as it is) */
                        if (*u != RSV_NO) *u = item;
                        p += 2;
                    }
                }
                for (int uz = 0; uz < UT_Z_NUM; uz++) {
                    *save_deposit(bx, bz, uz) = r16(p);
                    p += 2;
                }
                s_applying = 0;
                refresh_field();
                s_fg_blocks++;
            }
            break;
        case G_TIME:
            if (!is_host() && len >= 5 + (int)sizeof(lbRTC_time_c) && in_shared_town() && town == s_my_town) {
                lbRTC_time_c host_time, now;

                memcpy(&host_time, m + 5, sizeof(host_time));
                now = Common_Get(time.rtc_time);
                if (now.year != host_time.year || now.month != host_time.month || now.day != host_time.day ||
                    now.hour != host_time.hour || now.min != host_time.min) {
                    lbRTC_SetTime(&host_time);
                    printf("[MP] clock set to the host's: %02u:%02u\n", host_time.hour, host_time.min);
                }
            }
            break;
        default:
            break;
    }
}

/* --- Puppets --- */

static const PcMpPlayer* player_of(int slot, PcMpPlayer* buf) {
    PcMpPlayer list[PC_MP_MAX_PLAYERS];
    int n = s_mp != NULL ? pc_mp_players(s_mp, list, PC_MP_MAX_PLAYERS) : 0;

    for (int i = 0; i < n; i++) {
        if (list[i].slot == slot) {
            *buf = list[i];
            return buf;
        }
    }
    return NULL;
}

static int wanted_here(const PcMpPlayer* p) {
    return p != NULL && !p->is_you && in_room() && p->state.town != 0 && p->state.town == s_my_town &&
           p->state.scene == s_my_scene && pc_net_ms() - p->state_ms < 5000;
}

int pc_mp_game_puppet_target(int slot, PcMpState* st, PcMpLook* look) {
    PcMpPlayer buf;
    const PcMpPlayer* p = player_of(slot, &buf);

    if (!wanted_here(p)) return 0;
    *st = p->state;
    *look = p->look;
    return 1;
}

void pc_mp_game_puppet_gone(int slot, void* actor) {
    if (slot >= 0 && slot < PC_MP_MAX_PLAYERS && s_puppet[slot] == actor) s_puppet[slot] = NULL;
}

void pc_mp_game_puppet_screen(int slot, float sx, float sy, float sz) {
    (void)sz;
    if (slot < 0 || slot >= PC_MP_MAX_PLAYERS) return;
    s_tag_x[slot] = sx;
    s_tag_y[slot] = sy;
    s_tag_frame[slot] = s_frame;
    if (g_pc_verbose && s_frame % 300 == 0) {
        printf("[MP] player %d drawn at screen %.0f,%.0f\n", slot, sx, sy);
    }
}

int pc_mp_game_tags(PcMpTag* out, int max) {
    PcMpPlayer list[PC_MP_MAX_PLAYERS];
    int n = in_room() ? pc_mp_players(s_mp, list, PC_MP_MAX_PLAYERS) : 0, k = 0;
    float w = (float)g_pc_window_w, h = (float)g_pc_window_h;

    for (int i = 0; i < n && k < max; i++) {
        int slot = list[i].slot;

        if (list[i].is_you || s_puppet[slot] == NULL || s_frame - s_tag_frame[slot] > TAG_FRAMES) continue;
        snprintf(out[k].name, sizeof(out[k].name), "%s", list[i].name);
        out[k].said[0] = '\0';
        if (s_said_ms[slot] != 0 && pc_net_ms() - s_said_ms[slot] < SAID_MS) {
            snprintf(out[k].said, sizeof(out[k].said), "%s", s_said[slot]);
        }
        /* 320x240 of the 4:3 middle; wider windows show more at the sides */
        out[k].x = w * 0.5f + (s_tag_x[slot] - 160.0f) * (h / 240.0f);
        out[k].y = s_tag_y[slot] * (h / 240.0f);
        k++;
    }
    return k;
}

static void manage_puppets(GAME_PLAY* play) {
    PcMpPlayer list[PC_MP_MAX_PLAYERS];
    int n = pc_mp_players(s_mp, list, PC_MP_MAX_PLAYERS);

    if (GET_PLAYER_ACTOR(play) == NULL || play->fb_fade_type != FADE_TYPE_NONE) return;
    for (int i = 0; i < n; i++) {
        int slot = list[i].slot;

        if (slot < 0 || slot >= PC_MP_MAX_PLAYERS || s_puppet[slot] != NULL || !wanted_here(&list[i])) continue;
        s_puppet[slot] = Actor_info_make_actor(&play->actor_info, (GAME*)play, mAc_PROFILE_185, list[i].state.x,
                                               list[i].state.y, list[i].state.z, 0, list[i].state.angle, 0, -1, -1, -1,
                                               EMPTY_NO, (s16)slot, -1, -1);
        if (s_puppet[slot] != NULL) printf("[MP] %s is here\n", list[i].name);
    }
}

/* --- Every frame --- */

static void my_state(GAME_PLAY* play) {
    PcMpState st;
    PLAYER_ACTOR* pl = play != NULL ? GET_PLAYER_ACTOR(play) : NULL;

    memset(&st, 0, sizeof(st));
    if (play != NULL && pl != NULL && Common_Get(now_private) != NULL) {
        int idx = pl->now_main_index;

        st.town = s_my_town;
        st.scene = (s16)s_my_scene;
        st.outside = (u8)s_my_outside;
        st.anim = idx == mPlayer_INDEX_WALK ? 1 : idx == mPlayer_INDEX_RUN ? 2 : idx == mPlayer_INDEX_DASH ? 3 : 0;
        st.x = pl->actor_class.world.position.x;
        st.y = pl->actor_class.world.position.y;
        st.z = pl->actor_class.world.position.z;
        st.angle = pl->actor_class.shape_info.rotation.y;
    }
    pc_mp_set_state(s_mp, &st);
}

static void events(void) {
    PcMpEvent ev;
    char text[256];
    int ko = pc_gui_korean();

    while (pc_mp_poll_event(s_mp, &ev)) {
        switch (ev.kind) {
            case PC_MP_EV_JOINED:
                snprintf(text, sizeof(text), ko ? "'%s' 방에 들어왔습니다" : "Joined '%s'", ev.text);
                pc_gui_notify(text);
                s_visit = PC_MP_VISIT_DOWNLOADING;
                update_me();
                pc_mp_request_town(s_mp);
                break;
            case PC_MP_EV_PLAYER_IN:
                snprintf(text, sizeof(text), ko ? "%s 님이 들어왔습니다" : "%s joined", ev.text);
                pc_gui_notify(text);
                break;
            case PC_MP_EV_PLAYER_OUT:
                snprintf(text, sizeof(text), ko ? "%s 님이 나갔습니다" : "%s left", ev.text);
                pc_gui_notify(text);
                break;
            case PC_MP_EV_KICKED:
            case PC_MP_EV_CLOSED:
            case PC_MP_EV_DENIED:
                pc_gui_notify(ev.kind == PC_MP_EV_KICKED ? (ko ? "호스트가 방에서 내보냈습니다" : ev.text)
                              : ev.kind == PC_MP_EV_CLOSED ? (ko ? "방이 닫혔습니다" : ev.text)
                                                           : ev.text);
                if (s_visit != PC_MP_VISIT_THERE && !s_foreigner) {
                    slot_b_restore();
                    s_visit = PC_MP_VISIT_NONE;
                }
                break;
            case PC_MP_EV_CHAT: {
                PcMpPlayer buf;
                const PcMpPlayer* p = player_of(ev.slot, &buf);

                chat_add(p ? p->name : "?", ev.text);
                if (ev.slot >= 0 && ev.slot < PC_MP_MAX_PLAYERS) {
                    snprintf(s_said[ev.slot], sizeof(s_said[0]), "%s", ev.text);
                    s_said_ms[ev.slot] = pc_net_ms();
                }
                snprintf(text, sizeof(text), "%s: %s", p ? p->name : "?", ev.text);
                pc_gui_notify(text);
                break;
            }
            case PC_MP_EV_TOWN:
                guest_got_town();
                break;
            case PC_MP_EV_TOWN_WANTED:
                host_give_town();
                break;
            default:
                break;
        }
    }
}

void pc_mp_game_frame(struct game_s* game) {
    GAME_PLAY* play = play_of((GAME*)game);
    u8 buf[PC_MP_MAX_GAME_MSG];
    int from, len;

    s_frame++;
    if (s_mp == NULL) return;

    /* what the thread found */
    if (s_lock != NULL) {
        SDL_LockMutex(s_lock);
        for (; s_boot_given < s_nboot; s_boot_given++) pc_mp_add_bootstrap(s_mp, &s_boot[s_boot_given]);
        if (!s_stun_given && s_stun.ip != 0) {
            pc_mp_set_stun(s_mp, &s_stun);
            s_stun_given = 1;
        }
        SDL_UnlockMutex(s_lock);
    }
    if (is_host()) pc_mp_set_port_open(s_mp, s_upnp == 2);

    /* where we are */
    if (play != NULL && Common_Get(now_private) != NULL) {
        s_my_town = town_id(&Save_Get(land_info));
        s_my_scene = Save_Get(scene_no);
        s_my_outside = outside_in_town();
        s_foreigner = Common_Get(player_no) == mPr_FOREIGNER;
    }

    pc_mp_tick(s_mp);
    events();
    while ((len = pc_mp_poll_game(s_mp, &from, buf, sizeof(buf))) > 0) {
        game_message(from, buf, len);
    }

    /* the guest's trip */
    if (s_visit == PC_MP_VISIT_READY || s_visit == PC_MP_VISIT_THERE) {
        if (s_host_town != 0 && s_my_town == s_host_town) {
            if (s_visit != PC_MP_VISIT_THERE) {
                pc_gui_notify(pc_gui_korean() ? "호스트 마을에 도착했습니다" : "You are in the host's town");
            }
            s_visit = PC_MP_VISIT_THERE;
            s_visited = 1;
            if (!s_fg_asked && in_room() && outside_in_town()) {
                u8 m[5];

                m[0] = G_FG_REQ;
                w32(m + 1, s_host_town);
                pc_mp_send_game(s_mp, 0, 1, m, 5);
                s_fg_asked = 1;
                s_fg_blocks = 0;
            }
        } else if (s_visited && !s_foreigner && s_my_town != 0) {
            /* home again */
            slot_b_restore();
            s_visited = 0;
            s_visit = PC_MP_VISIT_NONE;
            if (in_room()) {
                s_visit = PC_MP_VISIT_DOWNLOADING; /* a fresh copy for the next trip */
                pc_mp_request_town(s_mp);
            }
        }
    }
    if (!in_room() && s_visit == PC_MP_VISIT_THERE && !s_foreigner && s_my_town != s_host_town) {
        slot_b_restore();
        s_visit = PC_MP_VISIT_NONE;
    }

    if (in_room()) {
        my_state(play);
        if (play != NULL) manage_puppets(play);
        /* the host's clock for the guests in its town */
        if (is_host() && play != NULL && pc_net_ms() - s_time_sent > 10000) {
            u8 m[5 + sizeof(lbRTC_time_c)];

            m[0] = G_TIME;
            w32(m + 1, s_my_town);
            memcpy(m + 5, Common_GetPointer(time.rtc_time), sizeof(lbRTC_time_c));
            pc_mp_send_game(s_mp, -1, 1, m, sizeof(m));
            s_time_sent = pc_net_ms();
        }
    }
}
