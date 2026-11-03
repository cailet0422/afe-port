/* pc_mp_game.c - see pc_mp_game.h */
#include "pc_platform.h"
#include "pc_mp_game.h"
#include "pc_mp.h"
#include "pc_net.h"
#include "pc_upnp.h"
#include "pc_settings.h"
#include "pc_gui.h"
#include "pc_autosave.h"
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
#include "m_home_h.h"
#include "m_house.h"
#include "m_npc.h"
#include "m_npc_schedule.h"
#include "m_needlework.h"
#include "ac_npc.h"
#include "ac_my_indoor.h"
#include "ac_weather.h"
#include "m_kankyo.h"
#include "lb_rtc.h"

#include <stddef.h>
#include <stdlib.h>
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
    G_HOME,         /* host -> guests: town, house, floor, offset, length, bytes of mHm_flr_c */
    G_HOME_REQ,     /* guest -> host: all the houses, please */
    G_NPC,          /* host -> guests: town, count, villagers (unreliable, ~10 a second) */
    G_DESIGN,       /* a player's own design: origin slot, pno, index, palette, name, id, pixels */
    G_NPC_ROOM,     /* host -> guests: town, house owner, count, villagers in that house (unreliable) */
    G_REGION,       /* host -> guests (or a guest's change -> host): town, region, offset, length, bytes */
    G_REGION_REQ,   /* guest -> host: all of the shared town data, please */
};

static void home_frame(void);
static void home_message(int from, const u8* m, int len);
static void home_reset(void);
static void npc_frame(GAME_PLAY* play);
static void npc_message(int from, const u8* m, int len);
static void npc_reset(void);
static void design_frame(void);
static void npc_room_message(int from, const u8* m, int len);
static void npc_room_frame(void);
static void region_message(int from, const u8* m, int len);
static void region_frame(void);
static void region_reset(void);
static void design_message(int from, const u8* m, int len);
static void design_reset(void);
static void design_forget(int slot);

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
static volatile uint32_t s_upnp_ext = 0;
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
static int s_was_out = 0;   /* a guest outdoors in the host's town last frame */
static int s_fg_shadow_ok = 0; /* host: the item layer as sent (fg_diff) */
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
static unsigned s_chat_serial = 0; /* lines added so far */

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

/* the router's forwarded port, for the room code and the listings */
static void set_port_mapping(void) {
    if (s_upnp == 2) pc_mp_set_port_mapping(s_mp, s_upnp_ext, s_upnp_port);
    else pc_mp_set_port_mapping(s_mp, 0, 0);
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
        if (Now_Private->cloth.item == RSV_CLOTH) {
            /* a design of their own (my_org): its picture travels as G_DESIGN */
            look.cloth = (u16)(PC_MP_LOOK_DESIGN | ((Now_Private->cloth.idx - 0x100) & 7));
        }
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
    home_reset();
    npc_reset();
    design_reset();
    region_reset();
    s_fg_shadow_ok = 0;
    if (!g_pc_deterministic && s_upnp == 0) {
        s_upnp = 1;
        s_upnp_port = pc_mp_port(s_mp);
        SDL_DetachThread(SDL_CreateThread(upnp_thread, "upnp", NULL));
    }
    set_port_mapping();
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
    home_reset();
    npc_reset();
    design_reset();
    region_reset();
    s_fg_shadow_ok = 0;
    for (int i = 0; i < PC_MP_MAX_PLAYERS; i++) design_forget(i);
    if (is_host() && s_upnp == 2) {
        SDL_DetachThread(SDL_CreateThread(unmap_thread, "upnp", (void*)(uintptr_t)s_upnp_port));
        s_upnp = 0;
    } else if (s_upnp == 3) {
        s_upnp = 0; /* no router answered: ask again for the next room */
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
    s_chat_serial++;
}

unsigned pc_mp_game_chat_serial(void) {
    return s_chat_serial;
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
        const char* port = "";

        if (pc_mp_port_mapped(s_mp)) {
            port = ko ? " (공유기 포트 열림)" : " (router port open)";
        } else if (s_upnp == 2 && s_upnp_ext != 0) {
            /* the router answered with an address the internet does not see */
            port = ko ? " (공유기가 다른 공유기 뒤에 있어 연 포트가 쓰이지 않음)"
                      : " (the router is behind another router: its open port is not used)";
        } else if (s_upnp == 2) {
            port = ko ? " (공유기 포트 열림, 공유기 주소 모름)" : " (router port open, router address unknown)";
        }
        pc_net_addr_str(&ext, a, sizeof(a));
        snprintf(buf, n, ko ? "인터넷 주소 %s%s%s" : "Internet address %s%s%s", a, port,
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

static u16* save_unit(int bx, int bz, int ux, int uz);

/* While an actor made from a unit exists (a player's house, a villager's
 * house, a mailbox, the shop...) the game keeps a placeholder there
 * (DUMMY_HOUSE0...) and puts the name back when the actor goes (m_actor.c
 * restore_fgdata). The placeholders are each game's own: never sent, and a
 * town given or sent whole has the names in their place. */
#define IS_PLACEHOLDER(it) ((it) >= DUMMY_START && (it) <= DUMMY_MONUMENT_FOUNTAIN)

typedef struct {
    u16* unit;
    u16 name, placeholder;
} RealName;

static GAME_PLAY* s_play = NULL; /* this frame's */

static int real_names(RealName* out, int max) {
    static const int parts[] = { ACTOR_PART_FG, ACTOR_PART_ITEM };
    int n = 0;

    if (s_play == NULL) return 0;
    for (int i = 0; i < 2; i++) {
        for (ACTOR* a = s_play->actor_info.list[parts[i]].actor; a != NULL && n < max; a = a->next_actor) {
            int bx, bz, ux, uz;
            u16* u;

            if (a->npc_id == EMPTY_NO || a->move_actor_list_idx != -1 || a->block_x < 0 || a->block_z < 0 ||
                ITEM_NAME_GET_TYPE(a->npc_id) == NAME_TYPE_ITEM2 ||
                !mFI_Wpos2BkandUtNuminBlock(&bx, &bz, &ux, &uz, a->home.position)) {
                continue;
            }
            u = save_unit(bx, bz, ux, uz);
            if (u != NULL && IS_PLACEHOLDER(*u)) {
                out[n].unit = u;
                out[n].name = a->npc_id;
                out[n].placeholder = *u;
                n++;
            }
        }
    }
    return n;
}

static u16 shared_item(const u16* unit, const RealName* names, int n) {
    if (IS_PLACEHOLDER(*unit)) {
        for (int i = 0; i < n; i++) {
            if (names[i].unit == unit) return names[i].name;
        }
    }
    return *unit;
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
    /* the town as it is now in both copies of the save part (with the names
     * of the buildings out as actors, not their placeholders) */
    {
        RealName names[96];
        int n = real_names(names, 96);

        for (int i = 0; i < n; i++) *names[i].unit = names[i].name;
        mCD_PC_SnapshotSave((Save*)(file + o0));
        for (int i = 0; i < n; i++) *names[i].unit = names[i].placeholder;
    }
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

    if (p == NULL || *p == item || IS_PLACEHOLDER(*p) || IS_PLACEHOLDER(item)) return;
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
    if (!s_applying) pc_autosave_unit_written(old_item, new_item);
    if (s_applying || !in_shared_town() || !outside_in_town() || save_unit(bx, bz, ux, uz) == NULL ||
        IS_PLACEHOLDER(old_item) || IS_PLACEHOLDER(new_item)) {
        return; /* (an actor made from the unit, or gone: this game's own) */
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

static void send_fg_block(int to, int bx, int bz, const RealName* names, int nnames) {
    u8 m[1 + 4 + 1 + UT_X_NUM * UT_Z_NUM * 2 + UT_Z_NUM * 2];
    size_t n = 0;

    m[n++] = G_FG_DATA;
    w32(m + n, room_town());
    n += 4;
    m[n++] = (u8)((bz - 1) * FG_BLOCK_X_NUM + (bx - 1));
    for (int uz = 0; uz < UT_Z_NUM; uz++) {
        for (int ux = 0; ux < UT_X_NUM; ux++) {
            w16(m + n, shared_item(save_unit(bx, bz, ux, uz), names, nnames));
            n += 2;
        }
    }
    for (int uz = 0; uz < UT_Z_NUM; uz++) {
        w16(m + n, *save_deposit(bx, bz, uz));
        n += 2;
    }
    pc_mp_send_game(s_mp, to, 1, m, n);
}

static void send_fg(int to) {
    RealName names[96];
    int nnames = real_names(names, 96);

    for (int bz = 1; bz <= FG_BLOCK_Z_NUM; bz++) {
        for (int bx = 1; bx <= FG_BLOCK_X_NUM; bx++) send_fg_block(to, bx, bz, names, nnames);
    }
}

/* The host: the game also changes the town's items without the functions
 * the hook is in (the day's regrowth of trees, fruit, weeds and flowers,
 * shells on the beach, mushrooms, buried treasure): compare the item and
 * buried layers with what was sent twice a second and send the blocks that
 * changed (placeholders and units the game is busy with left as they were) */
static u16 s_fg_shadow[FG_BLOCK_Z_NUM][FG_BLOCK_X_NUM][UT_Z_NUM][UT_X_NUM];
static u16 s_dep_shadow[FG_BLOCK_Z_NUM][FG_BLOCK_X_NUM][UT_Z_NUM];

static void fg_diff(void) {
    RealName names[96];
    int nnames = -1;

    for (int bz = 1; bz <= FG_BLOCK_Z_NUM; bz++) {
        for (int bx = 1; bx <= FG_BLOCK_X_NUM; bx++) {
            int changed = 0;

            for (int uz = 0; uz < UT_Z_NUM; uz++) {
                u16 dep = *save_deposit(bx, bz, uz);

                if (dep != s_dep_shadow[bz - 1][bx - 1][uz]) {
                    s_dep_shadow[bz - 1][bx - 1][uz] = dep;
                    changed = 1;
                }
                for (int ux = 0; ux < UT_X_NUM; ux++) {
                    u16 it = *save_unit(bx, bz, ux, uz);
                    u16* sh = &s_fg_shadow[bz - 1][bx - 1][uz][ux];

                    if (it != *sh && it != RSV_NO && !IS_PLACEHOLDER(it)) {
                        *sh = it;
                        changed = 1;
                    }
                }
            }
            if (changed) {
                if (nnames < 0) nnames = real_names(names, 96);
                send_fg_block(-1, bx, bz, names, nnames);
            }
        }
    }
}

static void fg_shadow_take(void) {
    for (int bz = 1; bz <= FG_BLOCK_Z_NUM; bz++) {
        for (int bx = 1; bx <= FG_BLOCK_X_NUM; bx++) {
            for (int uz = 0; uz < UT_Z_NUM; uz++) {
                s_dep_shadow[bz - 1][bx - 1][uz] = *save_deposit(bx, bz, uz);
                for (int ux = 0; ux < UT_X_NUM; ux++) s_fg_shadow[bz - 1][bx - 1][uz][ux] = *save_unit(bx, bz, ux, uz);
            }
        }
    }
    s_fg_shadow_ok = 1;
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

                        /* (a unit the game is busy with, or one with an actor of ours made
                         * from it, stays as it is) */
                        if (*u != RSV_NO && !IS_PLACEHOLDER(*u) && !IS_PLACEHOLDER(item)) *u = item;
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
        case G_HOME:
        case G_HOME_REQ:
            home_message(from, m, len);
            break;
        case G_NPC:
            npc_message(from, m, len);
            break;
        case G_DESIGN:
            design_message(from, m, len);
            break;
        case G_NPC_ROOM:
            npc_room_message(from, m, len);
            break;
        case G_REGION:
        case G_REGION_REQ:
            region_message(from, m, len);
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

/* in a player's or a villager's house (one scene for many houses: the
 * field and house_owner_name tell which) */
static int in_a_house(void) {
    int type = mFI_GET_TYPE(mFI_GetFieldId());

    return type == mFI_FIELD_PLAYER0_ROOM || type == mFI_FIELD_NPCROOM0;
}

static int wanted_here(const PcMpPlayer* p) {
    return p != NULL && !p->is_you && in_room() && p->state.town != 0 && p->state.town == s_my_town &&
           p->state.scene == s_my_scene && p->state.place == (u16)mFI_GetFieldId() &&
           p->state.owner == (in_a_house() ? (u16)Common_Get(house_owner_name) : 0) &&
           pc_net_ms() - p->state_ms < 5000;
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
        st.place = (u16)mFI_GetFieldId();
        st.owner = in_a_house() ? (u16)Common_Get(house_owner_name) : 0;
        /* what the player is doing, as the game animates it (puppets play the same) */
        st.anim0 = (u16)pl->animation0_idx;
        st.part = (u8)pl->part_table_idx;
        st.item = (u8)pl->item_kind;
        st.frame = pl->keyframe0.frame_control.current_frame;
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
                design_reset(); /* ours go to the room */
                break;
            case PC_MP_EV_PLAYER_IN:
                snprintf(text, sizeof(text), ko ? "%s 님이 들어왔습니다" : "%s joined", ev.text);
                pc_gui_notify(text);
                if (is_host()) design_reset(); /* everyone's designs for the newcomer */
                break;
            case PC_MP_EV_PLAYER_OUT:
                snprintf(text, sizeof(text), ko ? "%s 님이 나갔습니다" : "%s left", ev.text);
                pc_gui_notify(text);
                design_forget(ev.slot);
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
                /* (the menus show it: the chat lines at the top, pc_gui_mp.cpp) */
                printf("[MP] chat %s: %s\n", p ? p->name : "?", ev.text);
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
    s_play = play;
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
    if (is_host()) set_port_mapping();

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
                m[0] = G_HOME_REQ;
                pc_mp_send_game(s_mp, 0, 1, m, 5);
                m[0] = G_REGION_REQ;
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
        home_frame();
        npc_frame(play);
        design_frame();
        npc_room_frame();
        region_frame();
        if (is_host() && in_shared_town()) {
            if (!s_fg_shadow_ok) fg_shadow_take();
            else if (s_frame % 30 == 0) fg_diff();
        } else if (!is_host()) {
            /* a guest stepping outside in the host's town: the whole item layer
             * again (what its own game regrew while loading is the host's) */
            int out = s_visit == PC_MP_VISIT_THERE && outside_in_town() && play != NULL &&
                      play->fb_fade_type == FADE_TYPE_NONE;

            if (out && !s_was_out && s_fg_asked) {
                u8 m[5];

                m[0] = G_FG_REQ;
                w32(m + 1, s_host_town);
                pc_mp_send_game(s_mp, 0, 1, m, 5);
            }
            s_was_out = out;
        }
        if (s_frame % 30 == 0) update_me(); /* changed clothes, a tool in hand */
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

/* --- Houses: the host's furniture ---------------------------------------
 *
 * In a house the save's homes[] is the room itself (the furniture actors are
 * built from it when the room loads). The host compares the parts of every
 * floor that guests can see (the furniture and items of layers 0 and 1, the
 * wallpaper and carpet) with what it sent and sends what changed; a guest in
 * the host's town writes it into its copy at once, or, if it stands in that
 * room, keeps it until the room is quiet (afe-decomp ac_my_room.c,
 * aMR_PC_SyncRoom) and the furniture is built again. Guests cannot change a
 * house that is not theirs (the game's own rule), so nothing goes back. */

#define HOME_FLOORS mHm_ROOM_NUM
#define FLOOR_SIZE ((int)sizeof(mHm_flr_c))

typedef struct {
    u16 off, len;
    u8 items; /* u16 units: RSV_NO (busy) is not sent */
    u8 look;  /* wallpaper / carpet */
} HomeRegion;

static const HomeRegion s_home_regions[] = {
    { offsetof(mHm_flr_c, layer_main.items), sizeof(((mHm_flr_c*)0)->layer_main.items), 1, 0 },
    { offsetof(mHm_flr_c, layer_secondary.items), sizeof(((mHm_flr_c*)0)->layer_secondary.items), 1, 0 },
    { offsetof(mHm_flr_c, wall_floor), sizeof(mHm_wf_c), 0, 1 },
    { offsetof(mHm_flr_c, floor_bit_info), sizeof(mHm_fllot_bit_c), 0, 1 },
};
#define HOME_NREG ((int)(sizeof(s_home_regions) / sizeof(s_home_regions[0])))

static u8 s_home_shadow[PLAYER_NUM][HOME_FLOORS][sizeof(mHm_flr_c)]; /* host: as sent */
static u8 s_door_shadow[PLAYER_NUM]; /* host: the door decoration (a design) as sent */
#define HOME_DOOR 0xFF /* G_HOME "floor": the house's door decoration (mHm_hs_c door_original) */
static int s_home_shadow_ok = 0;
static u8 s_home_target[PLAYER_NUM][HOME_FLOORS][sizeof(mHm_flr_c)]; /* guest: as received */
static u8 s_home_mask[PLAYER_NUM][HOME_FLOORS][(sizeof(mHm_flr_c) + 7) / 8];
static u8 s_home_dirty[PLAYER_NUM][HOME_FLOORS];
static u32 s_home_frame = 0;

static u8* floor_bytes(int house, int floor) {
    return (u8*)&Save_Get(homes[house]).floors[floor];
}

static void home_reset(void) {
    s_home_shadow_ok = 0;
    memset(s_home_mask, 0, sizeof(s_home_mask));
    memset(s_home_dirty, 0, sizeof(s_home_dirty));
}

/* the guest stands in this room */
static int in_house_room(int house, int floor) {
    int id = mFI_GetFieldId();

    return mFI_GET_TYPE(id) == mFI_FIELD_PLAYER0_ROOM && mFI_GET_PLAYER_ROOM_NO(id) == house &&
           mFI_GetNowPlayerHouseFloorNo() == floor;
}

static void home_send(int to, int house, int floor, int off, int len) {
    while (len > 0) {
        u8 m[11 + 1024];
        int n = len > 1024 ? 1024 : len;

        m[0] = G_HOME;
        w32(m + 1, s_my_town);
        m[5] = (u8)house;
        m[6] = (u8)floor;
        w16(m + 7, (u16)off);
        w16(m + 9, (u16)n);
        memcpy(m + 11, floor_bytes(house, floor) + off, (size_t)n);
        pc_mp_send_game(s_mp, to, 1, m, (size_t)(11 + n));
        off += n;
        len -= n;
    }
}

static void door_send(int to, int house) {
    u8 m[12];

    m[0] = G_HOME;
    w32(m + 1, s_my_town);
    m[5] = (u8)house;
    m[6] = HOME_DOOR;
    w16(m + 7, 0);
    w16(m + 9, 1);
    m[11] = Save_Get(homes[house]).door_original;
    pc_mp_send_game(s_mp, to, 1, m, sizeof(m));
}

/* host: what changed since it was sent, as runs of a region */
static void home_diff(void) {
    for (int h = 0; h < PLAYER_NUM; h++) {
        if (Save_Get(homes[h]).door_original != s_door_shadow[h]) {
            s_door_shadow[h] = Save_Get(homes[h]).door_original;
            door_send(-1, h);
        }
    }
    for (int h = 0; h < PLAYER_NUM; h++) {
        for (int f = 0; f < HOME_FLOORS; f++) {
            u8* cur = floor_bytes(h, f);
            u8* old = s_home_shadow[h][f];

            for (int r = 0; r < HOME_NREG; r++) {
                const HomeRegion* rg = &s_home_regions[r];
                int step = rg->items ? 2 : 1;
                int run = -1, last = -1;

                for (int o = rg->off; o <= rg->off + rg->len; o += step) {
                    int changed = 0;

                    if (o < rg->off + rg->len && memcmp(cur + o, old + o, (size_t)step) != 0) {
                        changed = 1;
                        if (rg->items) {
                            u16 v;

                            memcpy(&v, cur + o, 2);
                            changed = v != RSV_NO; /* (an item in the air, a hole opening) */
                        }
                    }
                    if (changed) {
                        if (run < 0) run = o;
                        last = o + step;
                    } else if (run >= 0 && (o >= rg->off + rg->len || o - last >= 16)) {
                        home_send(-1, h, f, run, last - run);
                        memcpy(old + run, cur + run, (size_t)(last - run));
                        run = -1;
                    }
                }
            }
        }
    }
}

/* guest: what came in goes into the save (1 if the wallpaper or carpet changed) */
static int home_apply(int house, int floor) {
    u8* dst = floor_bytes(house, floor);
    u8* src = s_home_target[house][floor];
    u8* mask = s_home_mask[house][floor];
    int look = 0;

    for (int r = 0; r < HOME_NREG; r++) {
        const HomeRegion* rg = &s_home_regions[r];

        for (int o = rg->off; o < rg->off + rg->len; o++) {
            if (mask[o >> 3] & (1 << (o & 7))) {
                if (dst[o] != src[o]) {
                    dst[o] = src[o];
                    look |= rg->look;
                }
            }
        }
    }
    memset(mask, 0, sizeof(s_home_mask[0][0]));
    s_home_dirty[house][floor] = 0;
    return look;
}

int pc_mp_room_changes_pending(int house, int floor) {
    if (house < 0 || house >= PLAYER_NUM || floor < 0 || floor >= HOME_FLOORS) return 0;
    return s_home_dirty[house][floor] && in_shared_town() && !is_host();
}

static int s_design_room_dirty;

int pc_mp_room_apply_changes(int house, int floor) {
    int look;

    if (house < 0 || house >= PLAYER_NUM || floor < 0 || floor >= HOME_FLOORS) return 0;
    if (g_pc_verbose) printf("[MP] house %d floor %d: the host's furniture, built again\n", house, floor);
    look = home_apply(house, floor) | s_design_room_dirty;
    s_design_room_dirty = 0;
    return look;
}

static void home_message(int from, const u8* m, int len) {
    u32 town = r32(m + 1);

    if (m[0] == G_HOME_REQ) {
        if (is_host() && town == s_my_town) {
            for (int h = 0; h < PLAYER_NUM; h++) {
                for (int f = 0; f < HOME_FLOORS; f++) {
                    for (int r = 0; r < HOME_NREG; r++) {
                        home_send(from, h, f, s_home_regions[r].off, s_home_regions[r].len);
                    }
                }
                door_send(from, h);
            }
        }
        return;
    }
    if (is_host() || from != 0 || len < 11 || !in_shared_town() || town != s_my_town) return;
    {
        int h = m[5], f = m[6], off = r16(m + 7), n = r16(m + 9), ok = 0;

        if (h >= PLAYER_NUM || n <= 0 || 11 + n > len) return;
        if (f == HOME_DOOR) {
            Save_Get(homes[h]).door_original = m[11]; /* (drawn as it is, every frame) */
            return;
        }
        if (f >= HOME_FLOORS) return;
        for (int r = 0; r < HOME_NREG; r++) {
            ok |= off >= s_home_regions[r].off && off + n <= s_home_regions[r].off + s_home_regions[r].len;
        }
        if (!ok) return; /* only the parts that are sent */
        memcpy(s_home_target[h][f] + off, m + 11, (size_t)n);
        for (int o = off; o < off + n; o++) s_home_mask[h][f][o >> 3] |= (u8)(1 << (o & 7));
        s_home_dirty[h][f] = 1;
        if (!in_house_room(h, f)) home_apply(h, f);
    }
}

static void home_frame(void) {
    s_home_frame++;
    if (is_host()) {
        if (!in_shared_town()) return;
        if (!s_home_shadow_ok) {
            for (int h = 0; h < PLAYER_NUM; h++) {
                for (int f = 0; f < HOME_FLOORS; f++) memcpy(s_home_shadow[h][f], floor_bytes(h, f), FLOOR_SIZE);
                s_door_shadow[h] = Save_Get(homes[h]).door_original;
            }
            s_home_shadow_ok = 1;
        }
        if (s_home_frame % 10 == 0) home_diff();
        return;
    }
    /* guest: a room left before it was quiet */
    if (!in_shared_town()) return;
    for (int h = 0; h < PLAYER_NUM; h++) {
        for (int f = 0; f < HOME_FLOORS; f++) {
            if (s_home_dirty[h][f] && !in_house_room(h, f)) home_apply(h, f);
        }
    }
}

/* --- Villagers: the host's lead ------------------------------------------
 *
 * The host reports every resident villager it has as an actor (afe-decomp
 * ac_npc_pc_mp.c_inc), and sends them all ~10 times a second: those with an
 * actor exactly, the others where the game keeps them (npclist). A guest in
 * the host's town makes its actors walk where the host's are (while they
 * wander), keeps its villagers that are not out as actors where the host's
 * are (so the same ones are in the same acres), and does not let them travel
 * on its own (afe-decomp ac_set_npc_manager.c). */

#define NPC_HIDDEN (1 << 0)
#define NPC_AT_HOME (1 << 1)
#define NPC_FREE (1 << 2)
#define NPC_EXACT (1 << 7) /* the host has it as an actor */
#define NPC_ENTRY 14
#define NPC_NO_SCHEDULE 0xFF

typedef struct {
    u16 id;
    u8 act, flags, schedule;
    f32 x, y, z;
    s16 angle;
    u32 frame; /* host: s_frame of the report */
    u32 ms;    /* guest: when it came */
} NpcState;

static NpcState s_npc[ANIMAL_NUM_MAX];
static u32 s_npc_frame = 0;

static void npc_reset(void) {
    memset(s_npc, 0, sizeof(s_npc));
}

static int npc_valid(int k) {
    return k >= 0 && k < ANIMAL_NUM_MAX && !mNpc_CheckFreeAnimalPersonalID(&Save_Get(animals[k]).id);
}

void pc_mp_npc_report(int idx, mActor_name_t npc_id, f32 x, f32 y, f32 z, s16 angle, int act, int flags) {
    NpcState* n;

    if (idx < 0 || idx >= ANIMAL_NUM_MAX || !is_host() || !in_shared_town()) return;
    n = &s_npc[idx];
    n->id = npc_id;
    n->x = x;
    n->y = y;
    n->z = z;
    n->angle = angle;
    n->act = (u8)act;
    n->flags = (u8)flags;
    n->frame = s_frame;
}

int pc_mp_npc_guest(void) {
    return in_room() && !is_host() && in_shared_town();
}

int pc_mp_npc_target(int idx, mActor_name_t npc_id, f32* x, f32* y, f32* z, s16* angle, int* act, int* flags) {
    const NpcState* n;

    if (idx < 0 || idx >= ANIMAL_NUM_MAX || !pc_mp_npc_guest() || !outside_in_town()) return 0;
    n = &s_npc[idx];
    if (n->id != npc_id || !(n->flags & NPC_EXACT) || pc_net_ms() - n->ms > 1500) return 0;
    *x = n->x;
    *y = n->y;
    *z = n->z;
    *angle = n->angle;
    *act = n->act;
    *flags = n->flags;
    return 1;
}

static void npc_message(int from, const u8* m, int len) {
    u32 town = r32(m + 1);
    int count;

    if (is_host() || from != 0 || len < 6 || !in_shared_town() || town != s_my_town) return;
    count = m[5];
    if (6 + count * NPC_ENTRY > len) return;
    for (int i = 0; i < count; i++) {
        const u8* e = m + 6 + i * NPC_ENTRY;
        int k = e[0];
        NpcState* n;

        if (k >= ANIMAL_NUM_MAX) continue;
        n = &s_npc[k];
        n->id = r16(e + 1);
        n->flags = e[3];
        n->act = e[4];
        n->x = (f32)(s16)r16(e + 5);
        n->y = (f32)(s16)r16(e + 7);
        n->z = (f32)(s16)r16(e + 9);
        n->angle = (s16)r16(e + 11);
        n->schedule = e[13];
        n->ms = pc_net_ms();
        /* the same schedule as the host's (going home, out, asleep at the
         * same moment): the game's own override, renewed with every message */
        if (n->schedule < mNPS_SCHED_TYPE_NUM && pc_mp_npc_guest() &&
            n->id == Save_Get(animals[k]).id.npc_id) {
            mNPS_schedule_c* sched = mNPS_get_schedule_area(&Save_Get(animals[k]).id);

            if (sched != NULL) {
                sched->forced_type = n->schedule;
                sched->forced_timer = 60;
            }
        }
    }
}

/* the acre of a position */
static int npc_block(f32 x, f32 z, int* bx, int* bz) {
    xyz_t p;

    p.x = x;
    p.y = 0.0f;
    p.z = z;
    return mFI_Wpos2BlockNum(bx, bz, p);
}

static void npc_frame(GAME_PLAY* play) {
    s_npc_frame++;
    if (is_host()) {
        u8 m[6 + ANIMAL_NUM_MAX * NPC_ENTRY];
        int count = 0;

        if (!in_shared_town() || s_npc_frame % 6 != 0) return;
        m[0] = G_NPC;
        w32(m + 1, s_my_town);
        for (int k = 0; k < ANIMAL_NUM_MAX; k++) {
            u8* e = m + 6 + count * NPC_ENTRY;
            const NpcState* n = &s_npc[k];
            Animal_c* a = &Save_Get(animals[k]);
            f32 x, y, z;
            s16 angle = 0;
            u8 flags, act = 0;

            if (!npc_valid(k)) continue;
            if (n->id == a->id.npc_id && s_frame - n->frame <= 2) {
                x = n->x;
                y = n->y;
                z = n->z;
                angle = n->angle;
                act = n->act;
                flags = (u8)(n->flags | NPC_EXACT);
            } else {
                /* not out as an actor here: where the game keeps it */
                x = Common_Get(npclist[k]).position.x;
                y = Common_Get(npclist[k]).position.y;
                z = Common_Get(npclist[k]).position.z;
                flags = a->is_home ? (NPC_AT_HOME | NPC_HIDDEN) : 0;
            }
            e[0] = (u8)k;
            w16(e + 1, a->id.npc_id);
            e[3] = flags;
            e[4] = act;
            w16(e + 5, (u16)(s16)x);
            w16(e + 7, (u16)(s16)y);
            w16(e + 9, (u16)(s16)z);
            w16(e + 11, (u16)angle);
            {
                /* what the villager is up to by the clock (out, home, asleep...) */
                mNPS_schedule_c* sched = mNPS_get_schedule_area(&a->id);

                e[13] = sched != NULL ? sched->current_type : NPC_NO_SCHEDULE;
            }
            count++;
        }
        m[5] = (u8)count;
        if (count > 0) pc_mp_send_game(s_mp, -1, 0, m, (size_t)(6 + count * NPC_ENTRY));
        if (g_pc_verbose && s_npc_frame % 300 == 0) {
            for (int k = 0; k < ANIMAL_NUM_MAX; k++) {
                if (npc_valid(k) && s_frame - s_npc[k].frame <= 2) {
                    printf("[MP] villager %d at the host %.0f,%.0f act %d flags %x\n", k, s_npc[k].x, s_npc[k].z,
                           s_npc[k].act, s_npc[k].flags);
                }
            }
        }
        return;
    }

    /* guest: the villagers that are not out here stay where the host's are;
     * one the host has in this acre and we do not is brought out, one of
     * ours the host has elsewhere goes when out of sight */
    if (!pc_mp_npc_guest() || !outside_in_town() || play == NULL || s_npc_frame % 30 != 0 ||
        play->fb_fade_type != FADE_TYPE_NONE || GET_PLAYER_ACTOR(play) == NULL) {
        return;
    }
    {
        int pbx, pbz;
        PLAYER_ACTOR* pl = GET_PLAYER_ACTOR(play);

        if (!mFI_Wpos2BlockNum(&pbx, &pbz, pl->actor_class.world.position)) return;
        for (int k = 0; k < ANIMAL_NUM_MAX; k++) {
            NpcState* n = &s_npc[k];
            Animal_c* a = &Save_Get(animals[k]);
            mNpc_NpcList_c* list = &Common_Get(npclist[k]);
            int bx, bz;

            if (!npc_valid(k) || n->id != a->id.npc_id || pc_net_ms() - n->ms > 3000) continue;
            if (!npc_block(n->x, n->z, &bx, &bz)) continue;
            if (list->appear_flag) {
                list->position.x = n->x;
                list->position.y = n->y;
                list->position.z = n->z;
                a->is_home = (n->flags & NPC_AT_HOME) ? TRUE : FALSE;
                if ((n->flags & NPC_EXACT) && !(n->flags & NPC_HIDDEN) && bx == pbx && bz == pbz &&
                    CLIP(npc_clip) != NULL && CLIP(npc_clip)->setupActor_proc != NULL) {
                    xyz_t p = { n->x, n->y, n->z };
                    int ux, uz;

                    if (mFI_Wpos2BkandUtNuminBlock(&bx, &bz, &ux, &uz, p) &&
                        CLIP(npc_clip)->setupActor_proc(play, a->id.npc_id, (s8)k, k, -1, bx, bz, ux, uz)) {
                        list->appear_flag = FALSE;
                        if (g_pc_verbose) printf("[MP] villager %d out here as the host's\n", k);
                    }
                }
            } else {
                ACTOR* actor = Actor_info_fgName_search(&play->actor_info, a->id.npc_id, ACTOR_PART_NPC);
                NPC_ACTOR* na = (NPC_ACTOR*)actor;
                int out_of_sight;

                if (actor == NULL || actor->id != mAc_PROFILE_NORMAL_NPC ||
                    na->condition_info.talk_condition != aNPC_TALK_TYPE_NONE) {
                    continue;
                }
                out_of_sight = (actor->state_bitfield & ACTOR_STATE_NO_CULL) == 0;
                if (bx != pbx || bz != pbz) {
                    if (out_of_sight) {
                        Actor_delete(actor);
                        if (g_pc_verbose) printf("[MP] villager %d is in another acre at the host's\n", k);
                    }
                } else if ((n->flags & (NPC_EXACT | NPC_HIDDEN | NPC_FREE)) == (NPC_EXACT | NPC_FREE) &&
                           na->condition_info.hide_flg) {
                    /* in its house here, out walking at the host's: out again, there */
                    a->is_home = FALSE;
                    Actor_delete(actor);
                    if (g_pc_verbose) printf("[MP] villager %d is out at the host's\n", k);
                } else if ((n->flags & NPC_HIDDEN) && !na->condition_info.hide_flg && out_of_sight) {
                    a->is_home = TRUE;
                    Actor_delete(actor);
                    if (g_pc_verbose) printf("[MP] villager %d is in its house at the host's\n", k);
                }
            }
        }
    }
    if (g_pc_verbose && s_npc_frame % 300 == 0) {
        for (int k = 0; k < ANIMAL_NUM_MAX; k++) {
            ACTOR* actor;

            if (!npc_valid(k)) continue;
            actor = Actor_info_fgName_search(&play->actor_info, Save_Get(animals[k]).id.npc_id, ACTOR_PART_NPC);
            if (actor != NULL) {
                NPC_ACTOR* na = (NPC_ACTOR*)actor;

                printf("[MP] villager %d here %.0f,%.0f host %.0f,%.0f%s (hide %d schedule %d think %d act %d/%d)\n", k,
                       actor->world.position.x, actor->world.position.z, s_npc[k].x, s_npc[k].z,
                       (s_npc[k].flags & NPC_EXACT) ? "" : " (coarse)", na->condition_info.hide_flg, na->schedule.type,
                       na->think.idx, na->action.idx, na->action.step);
            }
        }
    }
}

/* --- Designs: each player's own -----------------------------------------
 *
 * What a player wears, holds or hangs that is a design of their own refers
 * to a slot of their my_org (Private_c, 8 designs of a palette index and
 * 32x32 CI4 pixels); the pixels are only in their own game. Every player
 * sends its 8 (and again any that changes); the host passes the guests' on
 * and gives everyone's to a newcomer. A puppet wears the design its look
 * names (PC_MP_LOOK_DESIGN); a guest in the host's town also writes the
 * host's into its copy of the town, where its door, signboards, wallpaper
 * and carpet read them. */

#define DESIGN_COUNT mPr_ORIGINAL_DESIGN_COUNT
#define DESIGN_TEX mNW_DESIGN_TEX_SIZE
#define DESIGN_MSG (9 + 10 + (int)sizeof(PersonalID_c) + DESIGN_TEX)

typedef struct {
    u8 have[DESIGN_COUNT];
    u8 palette[DESIGN_COUNT];
    u8 name[DESIGN_COUNT][10];
    u8 pno;
    PersonalID_c pid;
    u32 gen; /* grows with every design that comes */
    u8 tex[DESIGN_COUNT][DESIGN_TEX];
} Designs;

static Designs s_designs[PC_MP_MAX_PLAYERS];
static u8 s_design_sent[DESIGN_COUNT][1 + 10 + DESIGN_TEX];
static int s_design_sent_ok = 0;
static u32 s_design_frame = 0;
/* (s_design_room_dirty: a host design changed while we stand in its room) */

static void design_reset(void) {
    s_design_sent_ok = 0; /* all ours again (and, as host, everyone's) */
}

static void design_forget(int slot) {
    if (slot >= 0 && slot < PC_MP_MAX_PLAYERS) memset(&s_designs[slot], 0, sizeof(s_designs[0]));
}

int pc_mp_game_design(int slot, int index, unsigned char* palette, const unsigned char** tex, unsigned int* gen) {
    const Designs* d;

    if (slot < 0 || slot >= PC_MP_MAX_PLAYERS || index < 0 || index >= DESIGN_COUNT) return 0;
    d = &s_designs[slot];
    if (!d->have[index]) return 0;
    *palette = d->palette[index];
    *tex = d->tex[index];
    *gen = d->gen;
    return 1;
}

static void design_send(int to, int origin, int pno, const PersonalID_c* pid, int j, u8 palette, const u8* name,
                        const u8* tex) {
    u8 m[DESIGN_MSG];

    m[0] = G_DESIGN;
    w32(m + 1, s_my_town);
    m[5] = (u8)origin;
    m[6] = (u8)pno;
    m[7] = (u8)j;
    m[8] = palette;
    memcpy(m + 9, name, 10);
    memcpy(m + 19, pid, sizeof(PersonalID_c));
    memcpy(m + 19 + sizeof(PersonalID_c), tex, DESIGN_TEX);
    pc_mp_send_game(s_mp, to, 1, m, sizeof(m));
}

/* a host design changed in the guest's copy of the host's town: the room we
 * stand in shows it again if it uses it */
static void design_room_uses(int pno, int j) {
    int id = mFI_GetFieldId(), house;

    if (mFI_GET_TYPE(id) != mFI_FIELD_PLAYER0_ROOM) return;
    house = mFI_GET_PLAYER_ROOM_NO(id);
    if (mHS_get_pl_no_detail(house) == pno) {
        int f = mFI_GetNowPlayerHouseFloorNo();

        if (f >= 0 && f < HOME_FLOORS) {
            (void)j;
            s_home_dirty[house][f] = 1; /* built again: wallpaper, carpet, design furniture */
            s_design_room_dirty = 1;
        }
    }
}

static void design_message(int from, const u8* m, int len) {
    int origin, pno, j;
    Designs* d;
    PersonalID_c pid;

    if (len < DESIGN_MSG) return;
    origin = is_host() ? from : m[5];
    pno = m[6];
    j = m[7];
    if (origin < 0 || origin >= PC_MP_MAX_PLAYERS || j >= DESIGN_COUNT) return;
    if (!is_host() && origin == pc_mp_my_slot(s_mp)) return;
    memcpy(&pid, m + 19, sizeof(pid));
    d = &s_designs[origin];
    d->have[j] = 1;
    d->palette[j] = m[8] & 15;
    memcpy(d->name[j], m + 9, 10);
    memcpy(d->tex[j], m + 19 + sizeof(PersonalID_c), DESIGN_TEX);
    d->pno = (u8)pno;
    d->pid = pid;
    d->gen++;
    if (is_host()) {
        /* to the others (the sender ignores its own) */
        design_send(-1, origin, pno, &pid, j, d->palette[j], d->name[j], d->tex[j]);
        return;
    }
    /* the host's own designs into our copy of its town */
    if (origin == 0 && pno < PLAYER_NUM && in_shared_town() &&
        mPr_CheckCmpPersonalID(&Save_Get(private_data[pno]).player_ID, &pid)) {
        mNW_original_design_c* o = &Save_Get(private_data[pno]).my_org[j];

        if (o->palette != d->palette[j] || memcmp(o->design.data, d->tex[j], DESIGN_TEX) != 0 ||
            memcmp(o->name, d->name[j], 10) != 0) {
            o->palette = d->palette[j];
            memcpy(o->name, d->name[j], 10);
            mNW_OverWriteOriginalTexture(o, d->tex[j]);
            design_room_uses(pno, j);
        }
    }
}

static void design_frame(void) {
    Private_c* me = Common_Get(now_private);

    s_design_frame++;
    if (me == NULL || (s_design_sent_ok && s_design_frame % 60 != 0)) return;
    for (int j = 0; j < DESIGN_COUNT; j++) {
        const mNW_original_design_c* o = &me->my_org[j];
        u8* sent = s_design_sent[j];

        if (s_design_sent_ok && sent[0] == o->palette && memcmp(sent + 1, o->name, 10) == 0 &&
            memcmp(sent + 11, o->design.data, DESIGN_TEX) == 0) {
            continue;
        }
        sent[0] = o->palette;
        memcpy(sent + 1, o->name, 10);
        memcpy(sent + 11, o->design.data, DESIGN_TEX);
        {
            int pno = (int)Common_Get(player_no);
            int my = pc_mp_my_slot(s_mp);

            /* (ours for our puppet on the others; the host keeps its own too) */
            if (my >= 0 && my < PC_MP_MAX_PLAYERS) {
                Designs* d = &s_designs[my];

                d->have[j] = 1;
                d->palette[j] = o->palette & 15;
                memcpy(d->name[j], o->name, 10);
                memcpy(d->tex[j], o->design.data, DESIGN_TEX);
                d->pno = (u8)pno;
                d->pid = me->player_ID;
                d->gen++;
            }
            design_send(-1, my < 0 ? 0 : my, pno, &me->player_ID, j, o->palette, o->name, o->design.data);
        }
    }
    if (!s_design_sent_ok && is_host()) {
        /* a newcomer: the guests' designs we have */
        for (int slot = 1; slot < PC_MP_MAX_PLAYERS; slot++) {
            Designs* d = &s_designs[slot];

            for (int j = 0; j < DESIGN_COUNT; j++) {
                if (d->have[j]) design_send(-1, slot, d->pno, &d->pid, j, d->palette[j], d->name[j], d->tex[j]);
            }
        }
    }
    s_design_sent_ok = 1;
}

/* --- Villagers in their houses ------------------------------------------
 *
 * In its house a villager is run by the indoor controller (afe-decomp
 * ac_npc2.c) in room coordinates: the host reports it (the same hook, as
 * pc_mp_npc_room_report) and sends it with the house's owner; a guest in
 * the same house (the field and house_owner_name) makes its copy walk where
 * the host's is while it walks about the room. */

typedef struct {
    u16 id, owner;
    u8 act, flags;
    f32 x, y, z;
    s16 angle;
    u32 frame, ms;
} NpcRoom;

static NpcRoom s_npc_room[ANIMAL_NUM_MAX];
static u32 s_npc_room_frame = 0;

void pc_mp_npc_room_report(int idx, mActor_name_t npc_id, f32 x, f32 y, f32 z, s16 angle, int act, int flags) {
    NpcRoom* n;

    if (idx < 0 || idx >= ANIMAL_NUM_MAX || !is_host() || !in_shared_town()) return;
    n = &s_npc_room[idx];
    n->id = npc_id;
    n->owner = (u16)Common_Get(house_owner_name);
    n->x = x;
    n->y = y;
    n->z = z;
    n->angle = angle;
    n->act = (u8)act;
    n->flags = (u8)flags;
    n->frame = s_frame;
}

int pc_mp_npc_room_target(int idx, mActor_name_t npc_id, f32* x, f32* y, f32* z, s16* angle, int* act, int* flags) {
    const NpcRoom* n;

    if (idx < 0 || idx >= ANIMAL_NUM_MAX || !pc_mp_npc_guest()) return 0;
    n = &s_npc_room[idx];
    if (n->id != npc_id || pc_net_ms() - n->ms > 1500 || mFI_GET_TYPE(mFI_GetFieldId()) != mFI_FIELD_NPCROOM0 ||
        n->owner != (u16)Common_Get(house_owner_name)) {
        return 0;
    }
    if (g_pc_verbose && s_frame % 120 == 0) {
        printf("[MP] room villager %d at the host %.0f,%.0f act %d flags %x\n", idx, n->x, n->z, n->act, n->flags);
    }
    *x = n->x;
    *y = n->y;
    *z = n->z;
    *angle = n->angle;
    *act = n->act;
    *flags = n->flags;
    return 1;
}

static void npc_room_message(int from, const u8* m, int len) {
    int count;
    u16 owner;

    if (is_host() || from != 0 || len < 8 || !in_shared_town() || r32(m + 1) != s_my_town) return;
    owner = r16(m + 5);
    count = m[7];
    if (8 + count * NPC_ENTRY > len) return;
    for (int i = 0; i < count; i++) {
        const u8* e = m + 8 + i * NPC_ENTRY;
        int k = e[0];
        NpcRoom* n;

        if (k >= ANIMAL_NUM_MAX) continue;
        n = &s_npc_room[k];
        n->id = r16(e + 1);
        n->owner = owner;
        n->flags = e[3];
        n->act = e[4];
        n->x = (f32)(s16)r16(e + 5);
        n->y = (f32)(s16)r16(e + 7);
        n->z = (f32)(s16)r16(e + 9);
        n->angle = (s16)r16(e + 11);
        n->ms = pc_net_ms();
    }
}

static void npc_room_frame(void) {
    u8 m[8 + ANIMAL_NUM_MAX * NPC_ENTRY];
    int count = 0;

    s_npc_room_frame++;
    if (!is_host() || !in_shared_town() || s_npc_room_frame % 6 != 0 ||
        mFI_GET_TYPE(mFI_GetFieldId()) != mFI_FIELD_NPCROOM0) {
        return;
    }
    m[0] = G_NPC_ROOM;
    w32(m + 1, s_my_town);
    w16(m + 5, (u16)Common_Get(house_owner_name));
    for (int k = 0; k < ANIMAL_NUM_MAX; k++) {
        const NpcRoom* n = &s_npc_room[k];
        u8* e = m + 8 + count * NPC_ENTRY;

        if (s_frame - n->frame > 2 || n->owner != (u16)Common_Get(house_owner_name)) continue;
        e[0] = (u8)k;
        w16(e + 1, n->id);
        e[3] = n->flags;
        e[4] = n->act;
        w16(e + 5, (u16)(s16)n->x);
        w16(e + 7, (u16)(s16)n->y);
        w16(e + 9, (u16)(s16)n->z);
        w16(e + 11, (u16)n->angle);
        e[13] = NPC_NO_SCHEDULE;
        count++;
    }
    m[7] = (u8)count;
    if (count > 0) pc_mp_send_game(s_mp, -1, 0, m, (size_t)(8 + count * NPC_ENTRY));
}

/* --- The rest of the town's data -----------------------------------------
 *
 * Parts of the host's Save that every player should see the same: the
 * weather, the town tune, the museum, the noticeboard, the lost and found,
 * Nook's shop, the Able Sisters' display, the post office (letters waiting
 * to go), the turnip prices, the visitors' events, the snowmen. The host
 * compares them with what it sent every second and sends what changed; a
 * guest arriving gets all of them. What a visitor can change there (a post
 * on the board, the town tune, a lost item taken, things bought or sold, a
 * design put on display, a letter sent) goes from the guest to the host
 * while the guest is at that place, and from the host to everyone. */

#define SAVE_FIELD(f) offsetof(Save_t, f), sizeof(((Save_t*)0)->f)

typedef struct {
    u32 off, len;
    s16 guest_scene; /* a guest's change goes to the host while it is here: -1 never, -2 outdoors, -3 anywhere */
    s16 guest_scene2;
} Region;

static const Region s_regions[] = {
    { SAVE_FIELD(weather), -1, -1 },
    { SAVE_FIELD(melody), -3, -1 },
    { SAVE_FIELD(museum_display), -1, -1 },
    { SAVE_FIELD(noticeboard), -2, -1 },
    { SAVE_FIELD(police_box), SCENE_POLICE_BOX, -1 },
    { SAVE_FIELD(shop), SCENE_SHOP0, SCENE_CONVENI },
    { SAVE_FIELD(needlework), SCENE_NEEDLEWORK, -1 },
    { SAVE_FIELD(post_office), SCENE_POST_OFFICE, -1 },
    { SAVE_FIELD(kabu_price_schedule), -1, -1 },
    { SAVE_FIELD(event_save_data), -1, -1 },
    { SAVE_FIELD(snowmen), -1, -1 },
};
#define NREGIONS ((int)(sizeof(s_regions) / sizeof(s_regions[0])))
#define REGION_WEATHER 0
#define REGION_CHUNK 1000

static u8* s_region_shadow[NREGIONS]; /* host: as sent; guest: as last taken from the host */
static int s_region_ok = 0;
static u32 s_region_frame = 0;
static u32 s_region_applied_ms = 0;

static u8* save_base(void) {
    return (u8*)&Common_Get(save).save;
}

static void region_reset(void) {
    s_region_ok = 0;
}

static void region_send(int to, int r, u32 off, u32 n) {
    const u8* src = save_base() + s_regions[r].off;

    while (n > 0) {
        u8 m[12 + REGION_CHUNK];
        u32 k = n > REGION_CHUNK ? REGION_CHUNK : n;

        m[0] = G_REGION;
        w32(m + 1, s_my_town);
        m[5] = (u8)r;
        w32(m + 6, off);
        w16(m + 10, (u16)k);
        memcpy(m + 12, src + off, k);
        pc_mp_send_game(s_mp, to, 1, m, 12 + k);
        off += k;
        n -= k;
    }
}

static int region_shadows(void) {
    for (int r = 0; r < NREGIONS; r++) {
        if (s_region_shadow[r] == NULL) {
            s_region_shadow[r] = (u8*)malloc(s_regions[r].len);
            if (s_region_shadow[r] == NULL) return 0;
        }
    }
    return 1;
}

/* what changed in region r since the shadow: sent to `to`, the shadow updated */
static void region_diff(int r, int to) {
    const u8* cur = save_base() + s_regions[r].off;
    u8* old = s_region_shadow[r];
    u32 len = s_regions[r].len, run = 0, last = 0;
    int in_run = 0;

    for (u32 o = 0; o <= len; o++) {
        int changed = o < len && cur[o] != old[o];

        if (changed) {
            if (!in_run) run = o;
            in_run = 1;
            last = o + 1;
        } else if (in_run && (o >= len || o - last >= 16)) {
            region_send(to, r, run, last - run);
            memcpy(old + run, cur + run, last - run);
            in_run = 0;
        }
    }
}

static int guest_at(const Region* rg) {
    int scene = Save_Get(scene_no);

    if (rg->guest_scene == -3) return 1;
    if (rg->guest_scene == -2) return outside_in_town();
    return rg->guest_scene >= 0 && (scene == rg->guest_scene || scene == rg->guest_scene2 ||
                                    (rg->guest_scene == SCENE_SHOP0 && (scene == SCENE_SUPER || scene == SCENE_DEPART ||
                                                                         scene == SCENE_DEPART_2)));
}

static void weather_now(void) {
    u8 w = Save_Get(weather);
    aWeather_Clip_c* clip = CLIP(weather_clip);

    if (clip != NULL && clip->actor != NULL && clip->change_weather != NULL && outside_in_town()) {
        clip->change_weather(clip->actor, (s16)mEnv_SAVE_GET_WEATHER_TYPE(w), (s16)mEnv_SAVE_GET_WEATHER_INTENSITY(w));
    }
}

static void region_message(int from, const u8* m, int len) {
    int r;
    u32 off, n;

    if (len < 5 || r32(m + 1) != s_my_town || !in_shared_town() || !region_shadows()) return;
    if (m[0] == G_REGION_REQ) {
        if (is_host()) {
            for (r = 0; r < NREGIONS; r++) region_send(from, r, 0, s_regions[r].len);
        }
        return;
    }
    if (len < 12) return;
    r = m[5];
    off = r32(m + 6);
    n = r16(m + 10);
    if (r >= NREGIONS || n == 0 || 12 + (int)n > len || off + n > s_regions[r].len) return;
    if (is_host()) {
        /* a guest's change there: ours now, and to everyone by the next compare */
        if (s_regions[r].guest_scene == -1) return;
        memcpy(save_base() + s_regions[r].off + off, m + 12, n);
        if (g_pc_verbose) printf("[MP] town data %d changed by a guest (%u bytes)\n", r, (unsigned)n);
        return;
    }
    if (from != 0) return;
    memcpy(save_base() + s_regions[r].off + off, m + 12, n);
    memcpy(s_region_shadow[r] + off, m + 12, n);
    s_region_applied_ms = pc_net_ms();
    if (r == REGION_WEATHER) weather_now();
}

static void region_frame(void) {
    s_region_frame++;
    if (!in_shared_town() || s_region_frame % 60 != 0 || !region_shadows()) return;
    if (!s_region_ok) {
        for (int r = 0; r < NREGIONS; r++) memcpy(s_region_shadow[r], save_base() + s_regions[r].off, s_regions[r].len);
        s_region_ok = 1;
        return;
    }
    if (is_host()) {
        for (int r = 0; r < NREGIONS; r++) region_diff(r, -1);
        return;
    }
    /* a guest: what it changed itself at a place where it can (not just
     * after the host's data came) goes to the host */
    if (s_visit != PC_MP_VISIT_THERE || pc_net_ms() - s_region_applied_ms < 3000) return;
    for (int r = 0; r < NREGIONS; r++) {
        if (s_regions[r].guest_scene != -1 && guest_at(&s_regions[r])) {
            region_diff(r, 0);
        } else {
            memcpy(s_region_shadow[r], save_base() + s_regions[r].off, s_regions[r].len);
        }
    }
}
