/* pc_mp_game.h - the multiplayer (pc_mp.h) in the game
 *
 * Visiting works the way the game already lets you visit another town:
 * after joining a room the guest receives the host's town as it is at that
 * moment (a town file, as the station reads from a memory card in slot B,
 * kept apart in save/card_b_net/ so a real card B is never touched), walks
 * to the train station and travels there with the station master; coming
 * back is the game's own trip home, with what was found on the way.
 *
 * In the host's town every player sees the others (pc_mp_puppet.c, with
 * their names over their heads) and the town's items stay the same for
 * all: every change to an outdoor unit (dropping, picking up, placing,
 * digging, burying, planting, shaking fruit down, chopping, weeds ...) goes
 * to the host, which keeps it if the unit still held what the guest saw,
 * and passes it on to everyone; a guest arriving gets the whole item layer
 * first. Guests follow the host's clock while there. */
#ifndef PC_MP_GAME_H
#define PC_MP_GAME_H

#include "pc_mp.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* After game_main every frame (pc_pause_menu_draw) */
void pc_mp_game_frame(struct game_s* game);

/* --- For the menus (pc_gui_mp.cpp) --- */
PcMp* pc_mp_game_session(void); /* NULL until the page was opened */
void pc_mp_game_open(void);     /* the page opened: socket, internet lookups */
int pc_mp_game_host(const PcMpHostConfig* cfg);
/* A room code ("ABCD-EFGH-JKMNP") or an address ("1.2.3.4:47070") */
int pc_mp_game_join_text(const char* text, const char* password);
int pc_mp_game_join_room(const PcMpRoomInfo* room, const char* password);
void pc_mp_game_leave(void);
void pc_mp_game_browse(int on);
void pc_mp_game_kick(int slot);
void pc_mp_game_chat(const char* text);

typedef enum {
    PC_MP_VISIT_NONE,
    PC_MP_VISIT_DOWNLOADING, /* the host's town is coming */
    PC_MP_VISIT_READY,       /* go to the station: it is in slot B */
    PC_MP_VISIT_THERE,       /* in the host's town */
    PC_MP_VISIT_FAILED,
} PcMpVisit;
PcMpVisit pc_mp_game_visit(int* progress_permille);
/* The host's town is your own (the room is yours or you are home) */
int pc_mp_game_in_host_town(void);

/* What the internet side does: "LAN only", "internet: 203.0.113.7:47070 (port open)" */
void pc_mp_game_net_status(char* buf, size_t n);
/* The town and the name as the game has them (UTF-8) */
/* A name in the game's characters (n bytes) as UTF-8 */
void pc_mp_game_name_utf8(const unsigned char* s, int n, char* out, size_t cap);
void pc_mp_game_my_names(char* name, size_t n, char* town, size_t tn);

/* A look's cloth (PcMpLook.cloth) that is a design of the player's own:
 * PC_MP_LOOK_DESIGN | its my_org slot (0-7) */
#define PC_MP_LOOK_DESIGN 0xFD00
/* A player's design (its pixels, 32x32 CI4, and palette index) as it came;
 * gen grows with every design of that player that comes. 1 if known. */
int pc_mp_game_design(int slot, int index, unsigned char* palette, const unsigned char** tex, unsigned int* gen);

/* The last chat lines, oldest first */
int pc_mp_game_chat_lines(char lines[][192], int max);
/* Grows by one with every chat line (sent or received) */
unsigned pc_mp_game_chat_serial(void);

/* Names over the other players' heads, in window pixels */
typedef struct {
    char name[PC_MP_NAME_LEN];
    char said[160]; /* what they said in the chat lately, "" if nothing */
    float x, y;
} PcMpTag;
int pc_mp_game_tags(PcMpTag* out, int max);

/* --- For the puppets (pc_mp_puppet.c) --- */
/* 1 while slot is in the same place as us; fills where it is and its look */
int pc_mp_game_puppet_target(int slot, PcMpState* st, PcMpLook* look);
void pc_mp_game_puppet_gone(int slot, void* actor);
void pc_mp_game_puppet_screen(int slot, float sx, float sy, float sz);

/* --- From the game (afe-decomp m_field_info.c, TARGET_PC) --- */
void pc_mp_fg_written(int bx, int bz, int ut_x, int ut_z, unsigned short old_item, unsigned short new_item);
void pc_mp_deposit_written(int bx, int bz, int ut_x, int ut_z, int on);

#ifdef __cplusplus
}
#endif

#endif /* PC_MP_GAME_H */
