/* pc_mp.h - multiplayer without a server: rooms, players and messages
 *
 * One player hosts a room and the others join it; the host's game is the one
 * that decides (its town, its clock, which item lies where) and passes every
 * message on, so no machine but the players' own is needed. Everything goes
 * over one UDP socket per player:
 *   - on the same network, rooms answer a broadcast (LAN);
 *   - over the internet a room is reached at the address in its room code
 *     (STUN tells the host what that is; UPnP opens the router's port when
 *     the router allows it, pc_mp_upnp.c), and public rooms are listed
 *     through the BitTorrent DHT (pc_dht.h), which also lets a room open its
 *     router to a guest that wants in (hole punching).
 * Private rooms are not listed; they need the room code, and a password if
 * the host set one. The host can send a player out (kick); that player
 * cannot come back into the same room.
 *
 * This file has no game code (the unit tests run two of these in one
 * process); pc_mp_game.c connects it to the game. */
#ifndef PC_MP_H
#define PC_MP_H

#include "pc_net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_MP_PROTOCOL 1
#define PC_MP_MAX_PLAYERS 8
#define PC_MP_DEFAULT_PORT 47070
#define PC_MP_LAN_PORT 47071 /* where rooms on the network listen for the broadcast */
#define PC_MP_NAME_LEN 32
#define PC_MP_MAX_ROOMS 32
#define PC_MP_MAX_GAME_MSG 1100

typedef struct PcMp PcMp;

typedef enum {
    PC_MP_IDLE,       /* not in a room (rooms can be browsed) */
    PC_MP_HOSTING,
    PC_MP_JOINING,
    PC_MP_JOINED,
} PcMpMode;

/* How a player looks, from the game (pc_mp_game.c); travels with the list */
typedef struct {
    uint8_t gender;
    uint8_t face;
    uint8_t tan;
    uint8_t korean;  /* plays the Korean-patched disc */
    uint16_t cloth;  /* clothing item */
    uint16_t held;   /* tool in hand */
} PcMpLook;

/* Where a player is and what it does, sent a few times a second */
typedef struct {
    uint32_t town;   /* which town (land id), 0 = none */
    int16_t scene;
    uint8_t outside; /* in the town's outdoors */
    uint8_t anim;    /* 0 still, 1 walk, 2 run, ... (pc_mp_game.c) */
    float x, y, z;
    int16_t angle;
    uint16_t flags;
} PcMpState;

typedef struct {
    int used;
    int slot;      /* 0 = the host */
    char name[PC_MP_NAME_LEN]; /* UTF-8 */
    PcMpLook look;
    PcMpState state;
    uint32_t state_ms;   /* when state came */
    int ping_ms;
    int is_you;
} PcMpPlayer;

typedef struct {
    uint32_t room_id;
    PcNetAddr addr;
    char name[48];        /* the room's name */
    char host_name[PC_MP_NAME_LEN];
    char town[32];        /* the host's town (UTF-8) */
    uint8_t players, max_players;
    uint8_t is_public, has_password, korean, lan;
    uint16_t protocol;
    char version[16];     /* the port's version */
    uint32_t seen_ms;
    int ping_ms;
} PcMpRoomInfo;

typedef struct {
    char room_name[48];
    char password[32];  /* "" = none */
    int is_public;      /* listed (LAN and the DHT) */
    int max_players;    /* 2..8 */
    char town[32];
} PcMpHostConfig;

typedef enum {
    PC_MP_EV_NONE,
    PC_MP_EV_JOINED,     /* we are in (guest) */
    PC_MP_EV_PLAYER_IN,  /* slot came in */
    PC_MP_EV_PLAYER_OUT, /* slot left or timed out */
    PC_MP_EV_KICKED,     /* we were sent out */
    PC_MP_EV_CLOSED,     /* the host closed the room / is gone */
    PC_MP_EV_DENIED,     /* could not join: text says why */
    PC_MP_EV_CHAT,
    PC_MP_EV_TOWN,       /* the host's town arrived (pc_mp_town_blob) */
    PC_MP_EV_TOWN_WANTED, /* host: a guest asked for the town: pc_mp_set_town_blob now */
} PcMpEventKind;

typedef struct {
    PcMpEventKind kind;
    int slot;
    char text[160];
} PcMpEvent;

/* Opens the socket (port 0: any free one). NULL if it cannot. */
PcMp* pc_mp_new(uint16_t port);
void pc_mp_free(PcMp* mp);
uint16_t pc_mp_port(PcMp* mp);

/* Once a frame (or more often): sends, receives, times out */
void pc_mp_tick(PcMp* mp);

/* Who we are */
void pc_mp_set_me(PcMp* mp, const char* name, const PcMpLook* look);
void pc_mp_set_state(PcMp* mp, const PcMpState* state);

/* Internet: lets the DHT start from these (resolved on a thread by the
 * caller) and STUN ask this server; without them it is LAN only */
void pc_mp_add_bootstrap(PcMp* mp, const PcNetAddr* node);
void pc_mp_set_stun(PcMp* mp, const PcNetAddr* server);
/* The address outside the router, 0 until STUN (or the DHT) said */
int pc_mp_external_addr(PcMp* mp, PcNetAddr* out);
/* Tell the host's listing that the router's port is open (UPnP worked) */
void pc_mp_set_port_open(PcMp* mp, int open);
int pc_mp_internet_ready(PcMp* mp); /* 1 if the DHT answered */

/* --- Browsing --- */
void pc_mp_browse(PcMp* mp, int on);
int pc_mp_rooms(PcMp* mp, PcMpRoomInfo* out, int max);
/* Ask one address directly (a room code or ip:port) */
void pc_mp_probe(PcMp* mp, const PcNetAddr* addr);

/* --- Hosting --- */
int pc_mp_host(PcMp* mp, const PcMpHostConfig* cfg);
const PcMpHostConfig* pc_mp_host_config(PcMp* mp);
void pc_mp_kick(PcMp* mp, int slot);
/* The room's code: "ABCD-EFGH-JKMNP" (address, port and room) */
void pc_mp_room_code(PcMp* mp, char* buf, size_t n);
/* A code back into the address and the room's id; 1 if it is one */
int pc_mp_code_parse(const char* code, PcNetAddr* addr, uint32_t* room_low16);
void pc_mp_code_make(const PcNetAddr* addr, uint32_t room_id, char* buf, size_t n);

/* --- Joining --- */
/* room_id 0: whatever room is at addr */
int pc_mp_join(PcMp* mp, const PcNetAddr* addr, uint32_t room_id, const char* password);
/* Leave (guest) or close the room (host) */
void pc_mp_leave(PcMp* mp);

PcMpMode pc_mp_mode(PcMp* mp);
int pc_mp_my_slot(PcMp* mp);
int pc_mp_players(PcMp* mp, PcMpPlayer* out, int max);
const PcMpRoomInfo* pc_mp_room(PcMp* mp);
int pc_mp_poll_event(PcMp* mp, PcMpEvent* ev);

void pc_mp_chat(PcMp* mp, const char* text);

/* Messages of the game (pc_mp_game.c): to a slot or all (-1); the host
 * passes nothing on by itself */
int pc_mp_send_game(PcMp* mp, int to_slot, int reliable, const void* data, size_t len);
/* The next one: its length (0 if none), from_slot */
int pc_mp_poll_game(PcMp* mp, int* from_slot, void* buf, size_t cap);

/* The host's town (a save): a guest asks for it, the host gives it when
 * PC_MP_EV_TOWN_WANTED comes (it is sent to everyone who asked) */
void pc_mp_set_town_blob(PcMp* mp, const void* data, size_t len);
void pc_mp_request_town(PcMp* mp);
int pc_mp_town_progress(PcMp* mp); /* 0-1000, -1 if not asked */
const uint8_t* pc_mp_town_blob(PcMp* mp, size_t* len);

#ifdef __cplusplus
}
#endif

#endif /* PC_MP_H */
