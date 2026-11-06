/* pc_input_script.c - see pc_input_script.h */
#include "pc_input_script.h"
#include "pc_autosave.h"
#include "pc_mp_game.h"
#include "pc_touch.h"
#include "pc_typing.h"
#include "pc_minimap.h"
#include "pc_gui.h"

#include "game.h"
#include "m_common_data.h"
#include "m_npc_schedule.h"
#include "m_player_lib.h"
#include "m_needlework.h"
#include "m_play.h"
#include "m_field_info.h"
#include "m_random_field_h.h"
#include "m_submenu.h"
#include "m_event.h"
#include "m_quest.h"
#include "m_npc.h"
#include "m_soncho.h"
#include "m_house.h"
#include "m_choice.h"
#include "m_msg.h"
#include "m_time.h"
#include "m_actor.h"
#include "ac_insect.h"
#include "ac_gyoei.h"
#include "m_player.h"
#include "m_scene.h"
#include "m_collision_bg.h"
#include "m_weather_type.h"
#include "m_item_name.h"
#include "m_melody.h"
#include "audio.h"
#include "pc_ko.h"
#include "pc_platform.h"
#include "charmap.h"
#include "audio.h"

#include <ctype.h>
#include <dolphin/pad.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#define SCRIPT_STICK 80
#define SCRIPT_DEFAULT_HOLD 6
#define SCRIPT_SEEK_FRAMES 600
#define SCRIPT_SEEK_NEAR 45.0f /* close enough to talk */

typedef enum {
    SCRIPT_INPUT,
    SCRIPT_SHOT,
    SCRIPT_QUIT,
    SCRIPT_TYPE,
    SCRIPT_EDIT,
    SCRIPT_TEXT,
    SCRIPT_TOUCH,
    SCRIPT_KEY,
    SCRIPT_PAD,
    SCRIPT_POS,
    SCRIPT_SEEK,
    SCRIPT_CHOOSE,
    SCRIPT_TALK,
    SCRIPT_WAITMENU,
    SCRIPT_TALKEND,
    SCRIPT_GIVE,
    SCRIPT_LEDIT,
    SCRIPT_RESETS,
    SCRIPT_CATCH,
    SCRIPT_MENU,
    SCRIPT_VOICELOG,
    SCRIPT_ITEM,
    SCRIPT_NAMES,
    SCRIPT_SOUND,
    SCRIPT_NOJOB,
    SCRIPT_JOBDONE,
    SCRIPT_BELLS,
    SCRIPT_DESIGN,
    SCRIPT_EQUIP,
    SCRIPT_HANIWA,
    SCRIPT_WARP,
    SCRIPT_WEATHER,
    SCRIPT_SPEVENT,
    SCRIPT_FG,
    SCRIPT_CLOCK,
    SCRIPT_BGM,
    SCRIPT_MAP,
    SCRIPT_BLOCK,
    SCRIPT_CLICK,
    SCRIPT_MP,
} ScriptKind;

typedef struct {
    u32 frame;
    u32 duration;
    ScriptKind kind;
    u16 buttons;
    s8 stick_x;
    s8 stick_y;
    float touch[4]; /* TOUCH: where the finger lands, then where it moves (0-1) */
    int touch_moves;
    u16 seek_id;     /* SEEK: villager to walk to, or 0 for seek_pos */
    float seek_pos[2];
    u32 value[3]; /* GIVE item; LEDIT type and villager; RESETS count; CATCH villager; MENU
                     submenu and its two arguments; VOICELOG on */
    char name[128];
    int done;    /* TALK, CHOOSE, WAITMENU: happened */
    u32 started; /* TALK, CHOOSE, WAITMENU: the real frame it began waiting */
} ScriptEvent;

static ScriptEvent* s_events = NULL;
static int s_count = 0;
static u32 s_frame = 0; /* script time: real frames less those TALK, CHOOSE and WAITMENU held */
static u32 s_real = 0;
static u32 s_held = 0;
static u32 s_last = 0xFFFFFFFF;
static int s_fast = 0;

void pc_input_script_set_fast(int fast) {
    s_fast = fast;
}

int pc_input_script_active(void) {
    return s_count > 0;
}

static int parse_inputs(char* text, ScriptEvent* ev) {
    static const struct {
        const char* name;
        u16 button;
        s8 x, y;
    } table[] = {
        { "A", PAD_BUTTON_A, 0, 0 },          { "B", PAD_BUTTON_B, 0, 0 },
        { "X", PAD_BUTTON_X, 0, 0 },          { "Y", PAD_BUTTON_Y, 0, 0 },
        { "START", PAD_BUTTON_START, 0, 0 },  { "Z", PAD_TRIGGER_Z, 0, 0 },
        { "L", PAD_TRIGGER_L, 0, 0 },         { "R", PAD_TRIGGER_R, 0, 0 },
        { "UP", PAD_BUTTON_UP, 0, 0 },        { "DOWN", PAD_BUTTON_DOWN, 0, 0 },
        { "LEFT", PAD_BUTTON_LEFT, 0, 0 },    { "RIGHT", PAD_BUTTON_RIGHT, 0, 0 },
        { "SU", 0, 0, SCRIPT_STICK },         { "SD", 0, 0, -SCRIPT_STICK },
        { "SL", 0, -SCRIPT_STICK, 0 },        { "SR", 0, SCRIPT_STICK, 0 },
    };
    char* tok;

    for (tok = strtok(text, "+"); tok != NULL; tok = strtok(NULL, "+")) {
        size_t i;

        for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
            if (strcmp(tok, table[i].name) == 0) {
                ev->buttons |= table[i].button;
                ev->stick_x += table[i].x;
                ev->stick_y += table[i].y;
                break;
            }
        }
        if (i == sizeof(table) / sizeof(table[0])) {
            return 0;
        }
    }
    return 1;
}

/* MENU: a submenu by its mSM_OVL_ name (without the prefix) or number */
static u32 menu_type(const char* name) {
    static const struct {
        const char* name;
        int type;
    } menus[] = {
        { "HBOARD", mSM_OVL_HBOARD },     { "TIMEIN", mSM_OVL_TIMEIN },
        { "NOTICE", mSM_OVL_NOTICE },     { "WARNING", mSM_OVL_WARNING },
        { "CALENDAR", mSM_OVL_CALENDAR }, { "PASSWORDMAKE", mSM_OVL_PASSWORDMAKE },
        { "PASSWORDCHK", mSM_OVL_PASSWORDCHK }, { "ALARMIN", mSM_OVL_ALARMIN },
        { "FILM", mSM_OVL_FILM },         { "DIARY", mSM_OVL_DIARY },
        { "BOARD", mSM_OVL_BOARD },       { "MAP", mSM_OVL_MAP },
    };
    int i;

    for (i = 0; i < (int)(sizeof(menus) / sizeof(menus[0])); i++) {
        if (strcmp(name, menus[i].name) == 0) {
            return (u32)menus[i].type;
        }
    }
    return (u32)strtoul(name, NULL, 10);
}

int pc_input_script_load(const char* path) {
    char line[256];
    int line_no = 0;
    int capacity = 0;
    FILE* f = fopen(path, "r");

    if (f == NULL) {
        fprintf(stderr, "[INPUT] cannot open script %s\n", path);
        return 0;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        ScriptEvent ev;
        char word[64] = "";
        char arg[128] = "";
        char arg2[32] = "";
        char arg3[32] = "";
        char arg4[32] = "";
        char* hash = strchr(line, '#');
        unsigned long frame;
        int fields;

        line_no++;
        if (hash != NULL) {
            *hash = '\0';
        }
        fields = sscanf(line, "%lu %63s %127s %31s %31s %31s", &frame, word, arg, arg2, arg3, arg4);
        if (fields <= 0) {
            continue; /* blank or comment */
        }
        if (fields < 2) {
            fprintf(stderr, "[INPUT] %s:%d: expected '<frame> <inputs>'\n", path, line_no);
            fclose(f);
            return 0;
        }

        memset(&ev, 0, sizeof(ev));
        ev.frame = (u32)frame;
        for (char* p = word; *p != '\0'; p++) {
            *p = (char)toupper((unsigned char)*p);
        }
        if (strcmp(word, "SHOT") == 0) {
            ev.kind = SCRIPT_SHOT;
            snprintf(ev.name, sizeof(ev.name), "%s", fields >= 3 ? arg : "shot");
        } else if (strcmp(word, "QUIT") == 0) {
            ev.kind = SCRIPT_QUIT;
        } else if (strcmp(word, "POS") == 0) {
            ev.kind = SCRIPT_POS;
        } else if (strcmp(word, "SEEK") == 0 && fields >= 3) {
            ev.kind = SCRIPT_SEEK;
            if (sscanf(arg, "%f,%f", &ev.seek_pos[0], &ev.seek_pos[1]) != 2) {
                ev.seek_id = (u16)strtoul(arg, NULL, 16);
            }
            ev.duration = fields >= 4 ? (u32)strtoul(arg2, NULL, 10) : SCRIPT_SEEK_FRAMES;
        } else if (strcmp(word, "TALKEND") == 0) {
            ev.kind = SCRIPT_TALKEND;
            ev.duration = fields >= 3 ? (u32)strtoul(arg, NULL, 10) : SCRIPT_SEEK_FRAMES;
            ev.value[0] = fields >= 4 ? (u32)strtoul(arg2, NULL, 10) : 30;
        } else if (strcmp(word, "WAITMENU") == 0) {
            ev.kind = SCRIPT_WAITMENU;
            ev.duration = fields >= 3 ? (u32)strtoul(arg, NULL, 10) : SCRIPT_SEEK_FRAMES;
        } else if (strcmp(word, "TALK") == 0) {
            ev.kind = SCRIPT_TALK;
            ev.duration = fields >= 3 ? (u32)strtoul(arg, NULL, 10) : SCRIPT_SEEK_FRAMES;
        } else if (strcmp(word, "CHOOSE") == 0 && fields >= 3) {
            ev.kind = SCRIPT_CHOOSE;
            ev.value[0] = (u32)strtoul(arg, NULL, 10);
            ev.duration = fields >= 4 ? (u32)strtoul(arg2, NULL, 10) : SCRIPT_SEEK_FRAMES;
        } else if (strcmp(word, "GIVE") == 0 && fields >= 3) {
            ev.kind = SCRIPT_GIVE;
            ev.value[0] = (u32)strtoul(arg, NULL, 16);
        } else if (strcmp(word, "LEDIT") == 0 && fields >= 3) {
            ev.kind = SCRIPT_LEDIT;
            ev.value[0] = (u32)strtoul(arg, NULL, 10);
            ev.value[1] = fields >= 4 ? (u32)strtoul(arg2, NULL, 16) : 0;
        } else if (strcmp(word, "RESETS") == 0 && fields >= 3) {
            ev.kind = SCRIPT_RESETS;
            ev.value[0] = (u32)strtoul(arg, NULL, 10);
        } else if (strcmp(word, "CATCH") == 0 && fields >= 3) {
            ev.kind = SCRIPT_CATCH;
            ev.value[0] = (u32)strtoul(arg, NULL, 16);
        } else if (strcmp(word, "MENU") == 0 && fields >= 3) {
            unsigned a0 = 0, a1 = 0;

            ev.kind = SCRIPT_MENU;
            ev.value[0] = menu_type(arg);
            if (fields >= 4) {
                sscanf(arg2, "%u", &a0);
            }
            if (fields >= 5) {
                sscanf(arg3, "%u", &a1);
            }
            ev.value[1] = a0;
            ev.value[2] = a1;
        } else if (strcmp(word, "VOICELOG") == 0) {
            ev.kind = SCRIPT_VOICELOG;
            ev.value[0] = fields >= 3 ? (u32)strtoul(arg, NULL, 10) : 1;
        } else if (strcmp(word, "ITEM") == 0) {
            ev.kind = SCRIPT_ITEM;
        } else if (strcmp(word, "SOUND") == 0 && fields >= 5) {
            /* SOUND kind first count [frames]: play sounds one after another
             * (BGM, SE, HANIWA, INST; hex first, decimal count and frames) */
            ev.kind = SCRIPT_SOUND;
            ev.value[0] = strcmp(arg, "BGM") == 0 ? 0 : strcmp(arg, "SE") == 0 ? 1 : strcmp(arg, "HANIWA") == 0 ? 2 : 3;
            ev.value[1] = (u32)strtoul(arg2, NULL, 16);
            ev.value[2] = (u32)strtoul(arg3, NULL, 10);
            ev.duration = 120;
            if (fields >= 6) {
                ev.duration = (u32)strtoul(arg4, NULL, 10);
            }
        } else if (strcmp(word, "NAMES") == 0 && fields >= 3) {
            /* NAMES first [count]: the names of items (hex ids) */
            ev.kind = SCRIPT_NAMES;
            ev.value[0] = (u32)strtoul(arg, NULL, 16);
            ev.value[1] = fields >= 4 ? (u32)strtoul(arg2, NULL, 10) : 1;
        } else if (strcmp(word, "NOJOB") == 0) {
            ev.kind = SCRIPT_NOJOB;
        } else if (strcmp(word, "JOBDONE") == 0) {
            ev.kind = SCRIPT_JOBDONE;
        } else if (strcmp(word, "WARP") == 0 && fields >= 3 &&
                   sscanf(arg, "%f,%f", &ev.seek_pos[0], &ev.seek_pos[1]) == 2) {
            ev.kind = SCRIPT_WARP;
            ev.value[0] = fields >= 4 ? (u32)strtoul(arg2, NULL, 10) : mSc_DIRECT_NORTH;
        } else if (strcmp(word, "CLOCK") == 0 && fields >= 3) {
            unsigned h = 0, m = 0, sec = 0;

            sscanf(arg, "%u:%u:%u", &h, &m, &sec);
            ev.kind = SCRIPT_CLOCK;
            ev.value[0] = (h % 24) * 3600 + (m % 60) * 60 + sec % 60;
        } else if (strcmp(word, "FG") == 0 && fields >= 4 &&
                   sscanf(arg, "%f,%f", &ev.seek_pos[0], &ev.seek_pos[1]) == 2) {
            ev.kind = SCRIPT_FG;
            ev.value[0] = (u32)strtoul(arg2, NULL, 16);
        } else if (strcmp(word, "SPEVENT") == 0 && fields >= 3) {
            unsigned pbx, pbz, pux, puz;

            ev.kind = SCRIPT_SPEVENT;
            ev.value[0] = (u32)strtoul(arg, NULL, 10);
            if (fields >= 4 && sscanf(arg2, "%u,%u,%u,%u", &pbx, &pbz, &pux, &puz) == 4 && pbx >= 1 &&
                pbx <= FG_BLOCK_X_NUM && pbz >= 1 && pbz <= FG_BLOCK_Z_NUM && pux < UT_X_NUM && puz < UT_Z_NUM) {
                ev.value[1] = (pbx << 24) | (pbz << 16) | (pux << 8) | puz;
            }
        } else if (strcmp(word, "WEATHER") == 0 && fields >= 3) {
            ev.kind = SCRIPT_WEATHER;
            ev.value[0] = (u32)strtoul(arg, NULL, 10);
            ev.value[1] = fields >= 4 ? (u32)strtoul(arg2, NULL, 10) : mEnv_WEATHER_INTENSITY_NORMAL;
        } else if (strcmp(word, "DESIGN") == 0 && fields >= 4) {
            /* DESIGN slot pattern: a test design (stripes, the pattern a
             * number 0-255) in the player's slot (0-7), and wear it */
            ev.kind = SCRIPT_DESIGN;
            ev.value[0] = (u32)strtoul(arg, NULL, 10) & 7;
            ev.value[1] = (u32)strtoul(arg2, NULL, 10) & 0xFF;
        } else if (strcmp(word, "HANIWA") == 0 && fields >= 5) {
            /* HANIWA slot item price: the gyroid at the player's door sells that item (hex) */
            ev.kind = SCRIPT_HANIWA;
            ev.value[0] = (u32)strtoul(arg, NULL, 10) % HANIWA_ITEM_HOLD_NUM;
            ev.value[1] = (u32)strtoul(arg2, NULL, 16);
            ev.value[2] = (u32)strtoul(arg3, NULL, 10);
        } else if (strcmp(word, "EQUIP") == 0 && fields >= 3) {
            /* EQUIP item: the tool in hand (hex, 0 = none) */
            ev.kind = SCRIPT_EQUIP;
            ev.value[0] = (u32)strtoul(arg, NULL, 16);
        } else if (strcmp(word, "BELLS") == 0 && fields >= 3) {
            ev.kind = SCRIPT_BELLS;
            ev.value[0] = (u32)strtoul(arg, NULL, 10);
        } else if (strcmp(word, "BGM") == 0 && fields >= 3) {
            ev.kind = SCRIPT_BGM;
            ev.value[0] = (u32)strtoul(arg, NULL, 16);
        } else if (strncmp(word, "MP", 2) == 0 && strcmp(word, "MENU") != 0 &&
                   (strcmp(word, "MPHOST") == 0 || strcmp(word, "MPJOIN") == 0 || strcmp(word, "MPLEAVE") == 0 ||
                    strcmp(word, "MPKICK") == 0 || strcmp(word, "MPCHAT") == 0)) {
            /* the multiplayer (pc_mp_game.h): MPHOST [private], MPJOIN code|ip:port, MPLEAVE, MPKICK slot,
             * MPCHAT text ('_' for spaces) */
            ev.kind = SCRIPT_MP;
            snprintf(ev.name, sizeof(ev.name), "%s %s", word, fields >= 3 ? arg : "");
            for (char* p = ev.name + strlen(word) + 1; *p != '\0'; p++) {
                if (*p == '_') *p = ' ';
            }
        } else if (strcmp(word, "BLOCK") == 0) {
            ev.kind = SCRIPT_BLOCK;
        } else if (strcmp(word, "MAP") == 0) {
            ev.kind = SCRIPT_MAP;
        } else if (strcmp(word, "TYPE") == 0 && fields >= 3) {
            ev.kind = SCRIPT_TYPE;
            snprintf(ev.name, sizeof(ev.name), "%s", arg);
        } else if (strcmp(word, "TOUCH") == 0 && fields >= 3) {
            float* t = ev.touch;
            int n = sscanf(arg, "%f,%f:%f,%f", &t[0], &t[1], &t[2], &t[3]);

            if (n != 2 && n != 4) {
                fprintf(stderr, "[INPUT] %s:%d: expected 'TOUCH x,y[:x2,y2] [frames]'\n", path, line_no);
                fclose(f);
                return 0;
            }
            ev.kind = SCRIPT_TOUCH;
            ev.touch_moves = n == 4;
            for (int i = 0; i < 4; i++) {
                t[i] /= 100.0f; /* percent of the window */
            }
            ev.duration = fields >= 4 ? (u32)strtoul(arg2, NULL, 10) : SCRIPT_DEFAULT_HOLD;
        } else if (strcmp(word, "CLICK") == 0 && fields >= 3) {
            /* a left click of the mouse (percent of the window): the menus */
            if (sscanf(arg, "%f,%f", &ev.touch[0], &ev.touch[1]) != 2) {
                fprintf(stderr, "[INPUT] %s:%d: expected 'CLICK x,y'\n", path, line_no);
                fclose(f);
                return 0;
            }
            ev.kind = SCRIPT_CLICK;
            ev.touch[0] /= 100.0f;
            ev.touch[1] /= 100.0f;
        } else if ((strcmp(word, "KEY") == 0 || strcmp(word, "PAD") == 0) && fields >= 3) {
            /* a key press (SDL key name, '_' for spaces) or a gamepad button (SDL name) as an event */
            ev.kind = word[0] == 'K' ? SCRIPT_KEY : SCRIPT_PAD;
            snprintf(ev.name, sizeof(ev.name), "%s", arg);
            for (char* p = ev.name; *p != '\0'; p++) {
                if (*p == '_') *p = ' ';
            }
        } else if ((strcmp(word, "EDIT") == 0 || strcmp(word, "TEXT") == 0) && fields >= 2) {
            /* the system input method's composition ("EDIT" alone ends it) or committed text */
            ev.kind = word[0] == 'E' ? SCRIPT_EDIT : SCRIPT_TEXT;
            snprintf(ev.name, sizeof(ev.name), "%s", fields >= 3 ? arg : "");
        } else {
            ev.kind = SCRIPT_INPUT;
            ev.duration = fields >= 3 ? (u32)strtoul(arg, NULL, 10) : SCRIPT_DEFAULT_HOLD;
            if (!parse_inputs(word, &ev)) {
                fprintf(stderr, "[INPUT] %s:%d: unknown input in '%s'\n", path, line_no, word);
                fclose(f);
                return 0;
            }
        }

        if (s_count == capacity) {
            capacity = capacity ? capacity * 2 : 64;
            s_events = (ScriptEvent*)realloc(s_events, capacity * sizeof(ScriptEvent));
        }
        s_events[s_count++] = ev;
    }

    fclose(f);
    printf("[INPUT] loaded %d events from %s\n", s_count, path);
    return 1;
}

/* World position of a villager's house (one block is 16 units of 40) */
static float home_x(const Anmhome_c* home) {
    return home->bx * 640.0f + home->ux * 40.0f + 20.0f;
}

static float home_z(const Anmhome_c* home) {
    return home->bz * 640.0f + home->uz * 40.0f + 20.0f;
}

static ACTOR* find_actor(int part, u16 npc_id) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    ACTOR* actor;

    if (play == NULL || play->game.exec != play_main) {
        return NULL;
    }
    for (actor = play->actor_info.list[part].actor; actor != NULL; actor = actor->next_actor) {
        if (part == ACTOR_PART_PLAYER || actor->npc_id == npc_id) {
            return actor;
        }
    }
    return NULL;
}

/* Steers the stick toward a villager (wherever this run put them) or a
 * point and lets go once close; the player is left facing it. When the
 * player stops moving (a tree, a fence) it sidesteps for a while, to the
 * other side each time. */
static void seek(const ScriptEvent* ev, s8* stick_x, s8* stick_y) {
    static const ScriptEvent* s_ev = NULL;
    static float s_last[2];
    static int s_still, s_detour, s_side;
    ACTOR* player = find_actor(ACTOR_PART_PLAYER, 0);
    ACTOR* target = ev->seek_id != 0 ? find_actor(ACTOR_PART_NPC, ev->seek_id) : NULL;
    float tx, tz, dx, dz, dist, moved;

    if (player == NULL) {
        return;
    }
    tx = ev->seek_pos[0];
    tz = ev->seek_pos[1];
    if (target != NULL) {
        tx = target->world.position.x;
        tz = target->world.position.z;
    } else if (ev->seek_id != 0) {
        /* not loaded yet: head for their house */
        int i;

        for (i = 0; i < ANIMAL_NUM_MAX && Save_Get(animals[i]).id.npc_id != ev->seek_id; i++) {
        }
        if (i == ANIMAL_NUM_MAX) {
            return;
        }
        tx = home_x(&Save_Get(animals[i]).home_info);
        tz = home_z(&Save_Get(animals[i]).home_info);
    }
    dx = tx - player->world.position.x;
    dz = tz - player->world.position.z;
    dist = sqrtf(dx * dx + dz * dz);
    if (dist < (target != NULL ? SCRIPT_SEEK_NEAR : 4.0f)) {
        return;
    }
    if (s_ev != ev) {
        s_ev = ev;
        s_still = s_detour = 0;
        s_side = 1;
    } else {
        moved = fabsf(player->world.position.x - s_last[0]) + fabsf(player->world.position.z - s_last[1]);
        s_still = moved < 0.5f ? s_still + 1 : 0;
    }
    s_last[0] = player->world.position.x;
    s_last[1] = player->world.position.z;
    if (s_still > 8) {
        s_still = 0;
        s_detour = 40;
        s_side = -s_side;
    }
    if (s_detour > 0) {
        float t = dx;

        s_detour--;
        dx = -dz * s_side;
        dz = t * s_side;
    }
    /* stick up walks toward -z */
    *stick_x = (s8)(SCRIPT_STICK * dx / dist);
    *stick_y = (s8)(-SCRIPT_STICK * dz / dist);
}

/* CHOOSE: once the choice window takes input, the n-th choice and A until
 * the window takes it (the frame the window opens varies with the text
 * before it, and a press right as it opens can be missed) */
static int choose(ScriptEvent* ev, u16* buttons) {
    mChoice_c* choice = mChoice_Get_base_window_p();
    int normal = choice != NULL && choice->main_index == mChoice_MAIN_NORMAL;

    if (!normal) {
        return ev->value[2] != 0; /* answered once it was open and is not any more */
    }
    if (ev->value[2] == 0) {
        printf("[INPUT] frame %u: choice %u of %d\n", (unsigned)s_frame, (unsigned)ev->value[0],
               choice->data.choice_num);
    }
    ev->value[2] = 1;
    /* past the last: the last (often 아무것도, nothing) */
    choice->selected_choice_idx =
        (int)ev->value[0] < choice->data.choice_num ? (int)ev->value[0] : choice->data.choice_num - 1;
    if (ev->value[1]++ % 20 < 4) {
        *buttons |= PAD_BUTTON_A;
    }
    return 0;
}

static int is_wait(const ScriptEvent* ev) {
    return ev->kind == SCRIPT_TALK || ev->kind == SCRIPT_CHOOSE || ev->kind == SCRIPT_WAITMENU ||
           ev->kind == SCRIPT_TALKEND;
}

/* The first TALK, CHOOSE or WAITMENU that is due and has not happened: it
 * holds the script time (the events after it move later) for up to its
 * frames of real time */
static ScriptEvent* first_waiting(void) {
    int i;

    for (i = 0; i < s_count; i++) {
        ScriptEvent* ev = &s_events[i];

        if (is_wait(ev) && !ev->done && ev->frame <= s_frame) {
            if (ev->started == 0) {
                ev->started = s_real + 1;
            }
            if (s_real + 1 - ev->started < ev->duration) {
                return ev;
            }
            if (!ev->done) {
                ev->done = 1;
                printf("[INPUT] frame %u: gave up waiting (line %d, started %u, now %u)\n", (unsigned)s_frame, i, ev->started, s_real);
            }
        }
    }
    return NULL;
}

static int waiting(const ScriptEvent* ev) {
    return ev == first_waiting();
}

static void wait_step(ScriptEvent* ev, u16* buttons) {
    u32 t = s_real + 1 - ev->started;

    if (ev->kind == SCRIPT_CHOOSE) {
        ev->done = choose(ev, buttons);
    } else if (ev->kind == SCRIPT_TALKEND) {
        /* A every 20 frames until the message window has closed and stays
         * closed for the quiet frames (a talk can go on in a new window, the
         * player can think aloud after a scene change) */
        if (mMsg_Check_main_hide(mMsg_Get_base_window_p())) {
            if (++ev->value[1] >= ev->value[0]) {
                ev->done = 1;
                printf("[INPUT] frame %u: talk over\n", (unsigned)s_frame);
            }
        } else {
            ev->value[1] = 0;
            if (t % 20 < 4) {
                *buttons |= PAD_BUTTON_A;
            }
        }
    } else if (ev->kind == SCRIPT_WAITMENU) {
        /* A every 20 frames until a submenu opens (the text before it) */
        GAME_PLAY* play = (GAME_PLAY*)gamePT;

        if (play != NULL && play->game.exec == play_main && play->submenu.open_flag) {
            ev->done = 1;
            printf("[INPUT] frame %u: menu open\n", (unsigned)s_frame);
        } else if (t % 20 < 4) {
            *buttons |= PAD_BUTTON_A;
        }
    } else {
        /* A every 20 frames until a choice window opens */
        mChoice_c* choice = mChoice_Get_base_window_p();
        int open =
            choice != NULL && (choice->main_index == mChoice_MAIN_APPEAR || choice->main_index == mChoice_MAIN_NORMAL);

        if (!open) {
            ev->value[2] = 1; /* the last choice window (a CHOOSE just before) has closed */
        }
        if (open && ev->value[2]) {
            ev->done = 1;
            printf("[INPUT] frame %u: talk to a choice\n", (unsigned)s_frame);
        } else if (t % 20 < 4) {
            *buttons |= PAD_BUTTON_A;
        }
    }
}

void pc_input_script_apply(u16* buttons, s8* stick_x, s8* stick_y) {
    int i;

    for (i = 0; i < s_count; i++) {
        ScriptEvent* ev = &s_events[i];

        if (is_wait(ev)) {
            if (waiting(ev)) {
                wait_step(ev, buttons);
            }
            continue;
        }
        if (ev->kind == SCRIPT_SEEK && s_frame >= ev->frame && s_frame < ev->frame + ev->duration) {
            seek(ev, stick_x, stick_y);
        }
        if (ev->kind == SCRIPT_INPUT && s_frame >= ev->frame && s_frame < ev->frame + ev->duration) {
            *buttons |= ev->buttons;
            if (ev->stick_x != 0) {
                *stick_x = ev->stick_x;
            }
            if (ev->stick_y != 0) {
                *stick_y = ev->stick_y;
            }
        }
    }
}

/* Types keys into the text editor: plain characters are QWERTY keys, and
 * {SP} {BS} {HAN} {ENTER} {END} {LEFT} {RIGHT} {UP} {DOWN} name the others. */
static void type_keys(const char* text) {
    static const struct {
        const char* name;
        int key;
    } names[] = {
        { "{SP}", ' ' },
        { "{BS}", PC_TYPING_CMD_BACKSPACE },
        { "{HAN}", -1 },
        { "{ENTER}", PC_TYPING_CMD_ENTER },
        { "{END}", PC_TYPING_CMD_END },
        { "{LEFT}", PC_TYPING_CMD_LEFT },
        { "{RIGHT}", PC_TYPING_CMD_RIGHT },
        { "{UP}", PC_TYPING_CMD_UP },
        { "{DOWN}", PC_TYPING_CMD_DOWN },
    };

    while (*text != '\0') {
        size_t i;
        int key = (unsigned char)*text;
        size_t len = 1;

        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (strncmp(text, names[i].name, strlen(names[i].name)) == 0) {
                key = names[i].key;
                len = strlen(names[i].name);
                break;
            }
        }
        if (key < 0) {
            pc_typing_toggle_korean();
        } else if (!pc_typing_key(key)) {
            printf("[INPUT] frame %u: no text editor to type into\n", s_frame);
            return;
        }
        text += len;
    }
}

/* Writes the back buffer (what is about to be presented) as a binary PPM. */
static void save_shot(const char* name) {
    char path[96];
    int w = g_pc_window_w;
    int h = g_pc_window_h;
    u8* pixels = (u8*)malloc((size_t)w * h * 4);
    FILE* f;
    int y;

    if (pixels == NULL) {
        return;
    }
#ifdef _WIN32
    _mkdir("shots");
#else
    mkdir("shots", 0755);
#endif
    snprintf(path, sizeof(path), "shots/%s.ppm", name);

    pc_gfx_read_pixels(0, 0, w, h, 0, pixels);

    f = fopen(path, "wb");
    if (f != NULL) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (y = h - 1; y >= 0; y--) { /* GL rows are bottom-up */
            fwrite(pixels + (size_t)y * w * 3, 1, (size_t)w * 3, f);
        }
        fclose(f);
        printf("[INPUT] frame %u: saved %s\n", s_frame, path);
    }
    free(pixels);
}

/* Queues the event for the next frame's pc_platform_poll_events */
static void push_key_or_pad(const ScriptEvent* ev) {
    SDL_Event e;

    memset(&e, 0, sizeof(e));
    if (ev->kind == SCRIPT_KEY) {
        e.type = SDL_KEYDOWN;
        e.key.state = SDL_PRESSED;
        e.key.windowID = SDL_GetWindowID(g_pc_window); /* the menus take only their window's */
        e.key.keysym.sym = SDL_GetKeyFromName(ev->name);
        e.key.keysym.scancode = SDL_GetScancodeFromName(ev->name);
        if (e.key.keysym.sym == SDLK_UNKNOWN) {
            printf("[INPUT] unknown key '%s'\n", ev->name);
            return;
        }
    } else {
        SDL_GameControllerButton button = SDL_GameControllerGetButtonFromString(ev->name);
        SDL_GameControllerAxis axis = SDL_GameControllerGetAxisFromString(ev->name);

        if (button != SDL_CONTROLLER_BUTTON_INVALID) {
            e.type = SDL_CONTROLLERBUTTONDOWN;
            e.cbutton.button = (Uint8)button;
            e.cbutton.state = SDL_PRESSED;
        } else if (axis != SDL_CONTROLLER_AXIS_INVALID) {
            e.type = SDL_CONTROLLERAXISMOTION;
            e.caxis.axis = (Uint8)axis;
            e.caxis.value = 32767;
        } else {
            printf("[INPUT] unknown gamepad input '%s'\n", ev->name);
            return;
        }
    }
    SDL_PushEvent(&e);
    /* and let go (the menus keep keys held until then) */
    if (e.type == SDL_KEYDOWN) {
        e.type = SDL_KEYUP;
        e.key.state = SDL_RELEASED;
        SDL_PushEvent(&e);
    } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        e.type = SDL_CONTROLLERBUTTONUP;
        e.cbutton.state = SDL_RELEASED;
        SDL_PushEvent(&e);
    }
}

/* MPHOST [private] | MPJOIN code|ip:port | MPLEAVE | MPKICK slot | MPCHAT text */
static void mp_command(const char* cmd) {
    const char* arg = strchr(cmd, ' ');

    arg = arg != NULL ? arg + 1 : "";
    printf("[INPUT] frame %u: %s\n", s_frame, cmd);
    if (strncmp(cmd, "MPHOST", 6) == 0) {
        PcMpHostConfig cfg;
        char name[PC_MP_NAME_LEN], town[32];

        memset(&cfg, 0, sizeof(cfg));
        pc_mp_game_my_names(name, sizeof(name), town, sizeof(town));
        snprintf(cfg.room_name, sizeof(cfg.room_name), "%s", town);
        snprintf(cfg.town, sizeof(cfg.town), "%s", town);
        cfg.is_public = strstr(arg, "private") == NULL;
        cfg.max_players = 4;
        pc_mp_game_host(&cfg);
        if (pc_mp_game_session() != NULL) {
            char code[32];

            pc_mp_room_code(pc_mp_game_session(), code, sizeof(code));
            printf("[MP] hosting on port %u, code %s\n", pc_mp_port(pc_mp_game_session()), code);
        }
    } else if (strncmp(cmd, "MPJOIN", 6) == 0) {
        printf("[MP] join %s: %d\n", arg, pc_mp_game_join_text(arg, ""));
    } else if (strncmp(cmd, "MPLEAVE", 7) == 0) {
        pc_mp_game_leave();
    } else if (strncmp(cmd, "MPKICK", 6) == 0) {
        pc_mp_game_kick(atoi(arg));
    } else if (strncmp(cmd, "MPCHAT", 6) == 0) {
        pc_mp_game_chat(arg);
    }
}

static int s_click_pending = 0;

/* A left click at x, y (0-1 of the window) */
/* Text for a field of the menus or the chat box: as the system's input
 * method sends it (SDL_TEXTINPUT, at most 31 bytes, whole UTF-8 letters) */
static void push_text(const char* text) {
    while (*text != '\0') {
        SDL_Event e;
        size_t n = strlen(text);

        if (n > sizeof(e.text.text) - 1) {
            n = sizeof(e.text.text) - 1;
            while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
        }
        memset(&e, 0, sizeof(e));
        e.type = SDL_TEXTINPUT;
        e.text.windowID = SDL_GetWindowID(g_pc_window);
        memcpy(e.text.text, text, n);
        SDL_PushEvent(&e);
        text += n;
    }
}

static void push_click(float fx, float fy) {
    SDL_Event e;
    int w = 0, h = 0;

    SDL_GetWindowSize(g_pc_window, &w, &h);
    /* the menus also read the system's pointer */
    SDL_WarpMouseInWindow(g_pc_window, (int)(fx * w), (int)(fy * h));
    memset(&e, 0, sizeof(e));
    e.type = SDL_MOUSEMOTION;
    e.motion.x = (int)(fx * w);
    e.motion.y = (int)(fy * h);
    e.motion.windowID = SDL_GetWindowID(g_pc_window);
    SDL_PushEvent(&e);
    s_click_pending = 1; /* pressed on the next frame, as a real pointer moves before it clicks */
}

static void push_click_button(void) {
    SDL_Event e;
    int x = 0, y = 0;

    s_click_pending = 0;
    SDL_GetMouseState(&x, &y);
    memset(&e, 0, sizeof(e));
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.state = SDL_PRESSED;
    e.button.clicks = 1;
    e.button.x = x;
    e.button.y = y;
    e.button.windowID = SDL_GetWindowID(g_pc_window);
    SDL_PushEvent(&e);
    e.type = SDL_MOUSEBUTTONUP;
    e.button.state = SDL_RELEASED;
    SDL_PushEvent(&e);
}

/* BLOCK: the block the game counts the player in, the player's own block and
 * where the block change stands (mFI_WADE_*), for seamless block crossing */
static void log_block(u32 frame) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    ACTOR* player = find_actor(ACTOR_PART_PLAYER, 0);
    int bx = -1;
    int bz = -1;

    if (play == NULL || play->game.exec != play_main || player == NULL) {
        return;
    }
    mFI_Wpos2BlockNum(&bx, &bz, player->world.position);
    printf("[INPUT] frame %u: block %d,%d player %d,%d wade %d x %.0f z %.0f\n", (unsigned)frame,
           (int)play->block_table.block_x, (int)play->block_table.block_z, bx, bz, mFI_GetPlayerWade(),
           player->world.position.x, player->world.position.z);

    /* the blocks around the player's with insects (I) and fish (F) out, and the
     * villager and other NPC actors (seamless crossing's area) */
    {
        char area[160];
        int len = 0;
        int x;
        int z;

        for (z = bz - 1; z <= bz + 1; z++) {
            for (x = bx - 1; x <= bx + 1; x++) {
                int insect = CLIP(insect_clip) != NULL && CLIP(insect_clip)->chk_live_insect_proc != NULL &&
                             CLIP(insect_clip)->chk_live_insect_proc(x, z, gamePT);
                int fish = CLIP(gyo_clip) != NULL && CLIP(gyo_clip)->chk_live_gyoei_proc != NULL &&
                           CLIP(gyo_clip)->chk_live_gyoei_proc(x, z, gamePT);

                len += snprintf(area + len, sizeof(area) - len, " %d,%d%s%s", x, z, insect ? "I" : "",
                                fish ? "F" : "");
            }
        }
        printf("[INPUT] frame %u: area%s npcs %d\n", (unsigned)frame, area,
               (int)play->actor_info.list[ACTOR_PART_NPC].num_actors);
    }
}

/* Where the player and the villagers are (outside, x/z in world units; one
 * block is 640), for writing routes */
static void log_positions(u32 frame) {
    static const int parts[] = { ACTOR_PART_PLAYER, ACTOR_PART_NPC };
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    int i;

    if (play == NULL || play->game.exec != play_main) {
        return;
    }
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        const Anmhome_c* home = &Save_Get(animals[i]).home_info;

        if (Save_Get(animals[i]).id.npc_id != EMPTY_NO) {
            mNPS_schedule_c* sched = mNPS_get_schedule_area(&Save_Get(animals[i]).id);

            printf("[POS] frame %u home %04X block %u,%u x %.0f z %.0f at home %d schedule %d looks %d\n", (unsigned)frame,
                   Save_Get(animals[i]).id.npc_id, home->bx, home->bz, home_x(home), home_z(home),
                   Save_Get(animals[i]).is_home, sched != NULL ? (int)sched->current_type : -1, Save_Get(animals[i]).id.looks);
        }
    }
    printf("[POS] frame %u scene %d\n", (unsigned)frame, (int)Save_Get(scene_no));
    {
        lbRTC_time_c* t = Common_GetPointer(time.rtc_time);

        printf("[POS] frame %u time %04d-%02d-%02d %02d:%02d:%02d weekday %d\n", (unsigned)frame, t->year, t->month,
               t->day, t->hour, t->min, t->sec, t->weekday);
        /* the visitor of the week (mEv_EVENT_*: the broker's sale, the
         * artist ...), its day (month << 8 | day) and state (3 today) */
        printf("[POS] frame %u special event %d day %04X state %d weekly %d\n", (unsigned)frame,
               (int)(s16)mEv_get_special_event_type(), (unsigned)mEv_get_special_event_day(),
               mEv_get_special_event_state(), (int)Save_Get(event_save_common).weekly_event.type);
        {
            /* where the week's visitor sets up (the broker's tent ...) */
            int ev_type = (s16)mEv_get_special_event_type();
            int bx, bz;

            if (mEv_get_event_place(ev_type, &bx, &bz)) {
                mEv_place_data_c* place = mEv_get_common_place(ev_type, 81); /* as mEv_get_event_place */

                printf("[POS] frame %u special event place block %d,%d unit %d,%d actor %04X\n", (unsigned)frame, bx,
                       bz, place->unit.x, place->unit.z, (unsigned)place->actor_name);
            }
        }
        /* today's events (mEv_EVENT_*, by number: the harvest festival, the
         * countdown, the visitor ...) and those running at this hour (*) */
        printf("[POS] frame %u events today", (unsigned)frame);
        for (int e = 0; e < mEv_EVENT_NUM; e++) {
            if (mEv_check_run_today(e)) {
                int bx, bz;

                printf(" %d%s", e, mEv_check_schedule(e) ? "*" : "");
                if (mEv_get_event_place(e, &bx, &bz)) {
                    printf("@%d,%d", bx, bz); /* the block it is held in */
                }
            }
        }
        printf("\n");
    }
    for (int h = 0; h < PLAYER_NUM; h++) {
        const Haniwa_c* haniwa = &Save_Get(homes[h]).haniwa;

        printf("[POS] frame %u gyroid %d bells %u items %04X %04X %04X %04X\n", (unsigned)frame, h,
               (unsigned)haniwa->bells, haniwa->items[0].item, haniwa->items[1].item, haniwa->items[2].item,
               haniwa->items[3].item);
    }
    if (Common_Get(player_no) < PLAYER_NUM) {
        /* the player's house and debt: a house is enlarged a day after its loan is paid */
        const mHm_hs_c* house = &Save_Get(homes[mHS_get_arrange_idx(Common_Get(player_no))]);

        printf("[POS] frame %u house size %d next %d renew %d basement %d island %d loan %u bells %u\n",
               (unsigned)frame, house->size_info.size, house->size_info.next_size, house->size_info.renew,
               house->size_info.basement_ordered, house->island.flags.unlocked, (unsigned)Now_Private->inventory.loan,
               (unsigned)Now_Private->inventory.wallet);
        printf("[POS] frame %u pockets", (unsigned)frame);
        for (i = 0; i < mPr_POCKETS_SLOT_COUNT; i++) {
            printf(" %04X", (unsigned)Now_Private->inventory.pockets[i]);
        }
        printf("\n");
        if (mEv_CheckFirstJob()) {
            /* the part-time job: kind - mQst_ERRAND_FIRSTJOB_CHANGE_CLOTH is Nook's job number */
            mQst_errand_c* job = mQst_GetFirstJobData();

            printf("[POS] frame %u first job %d progress %d to %04X item %04X friends %d of %d tortimer %u\n",
                   (unsigned)frame, (int)job->base.quest_kind - mQst_ERRAND_FIRSTJOB_CHANGE_CLOTH,
                   (int)job->base.progress, (unsigned)job->recipient.npc_id, (unsigned)job->item,
                   mNpc_GetFriendAnimalNum(&Now_Private->player_ID), mNpc_GetAnimalNum(),
                   (unsigned)mSC_check_ArbeitPlayer());
        }
    }
    printf("[POS] frame %u camera %d center %.0f,%.0f,%.0f eye %.0f,%.0f,%.0f dir %d,%d dist %.0f\n", (unsigned)frame,
           play->camera.now_main_index, play->camera.lookat.center.x, play->camera.lookat.center.y,
           play->camera.lookat.center.z, play->camera.lookat.eye.x, play->camera.lookat.eye.y, play->camera.lookat.eye.z,
           play->camera.direction.x, play->camera.direction.y, play->camera.focus_distance);
    for (i = 0; i < (int)(sizeof(parts) / sizeof(parts[0])); i++) {
        ACTOR* actor;

        for (actor = play->actor_info.list[parts[i]].actor; actor != NULL; actor = actor->next_actor) {
            if (parts[i] == ACTOR_PART_PLAYER) {
                /* and what is in front of the player (a house's door is a dummy item) */
                printf("[POS] frame %u player %04X x %.0f z %.0f facing %04X in front %04X\n", (unsigned)frame,
                       actor->npc_id, actor->world.position.x, actor->world.position.z,
                       (u16)actor->shape_info.rotation.y, ((PLAYER_ACTOR*)actor)->item_in_front);
                continue;
            }
            printf("[POS] frame %u npc %04X x %.0f z %.0f\n", (unsigned)frame, actor->npc_id,
                   actor->world.position.x, actor->world.position.z);
        }
    }
    /* buildings: the blocks they are in, and the ones loaded nearby by actor profile */
    {
        static const struct {
            u32 kind;
            const char* name;
        } buildings[] = {
            { mRF_BLOCKKIND_SHOP, "shop" },       { mRF_BLOCKKIND_MUSEUM, "museum" },
            { mRF_BLOCKKIND_POSTOFFICE, "post" }, { mRF_BLOCKKIND_TAILORS, "tailors" },
            { mRF_BLOCKKIND_POLICE, "police" },   { mRF_BLOCKKIND_SHRINE, "shrine" },
            { mRF_BLOCKKIND_DOCK, "dock" },       { mRF_BLOCKKIND_ISLAND, "island" },
            { mRF_BLOCKKIND_POOL, "pool" }, /* the pond: the countdown, the fireworks */
        };
        ACTOR* actor;
        int bx, bz;

        for (i = 0; i < (int)(sizeof(buildings) / sizeof(buildings[0])); i++) {
            if (mFI_BlockKind2BkNum(&bx, &bz, buildings[i].kind)) {
                printf("[POS] frame %u %s block %d,%d\n", (unsigned)frame, buildings[i].name, bx, bz);
            }
        }

        /* boards in the player's block (the message board for Nook's notice job) */
        {
            static const struct {
                mActor_name_t item;
                const char* name;
            } boards[] = { { MESSAGE_BOARD0, "message board" }, { MESSAGE_BOARD1, "message board" },
                           { MAP_BOARD0, "map board" },         { MAP_BOARD1, "map board" } };
            int ux, uz, k;

            for (k = 0; k < (int)(sizeof(boards) / sizeof(boards[0])); k++) {
                if (mFI_SearchFGInBlock(&ux, &uz, boards[k].item, play->block_table.block_x,
                                        play->block_table.block_z)) {
                    printf("[POS] frame %u %s at %d,%d\n", (unsigned)frame, boards[k].name,
                           play->block_table.block_x * 640 + ux * 40 + 20, play->block_table.block_z * 640 + uz * 40 + 20);
                }
            }
        }
        for (actor = play->actor_info.list[ACTOR_PART_ITEM].actor; actor != NULL; actor = actor->next_actor) {
            printf("[POS] frame %u structure %04X x %.0f z %.0f\n", (unsigned)frame, (u16)actor->id,
                   actor->world.position.x, actor->world.position.z);
        }
    }
}

static Animal_c* find_animal(u16 npc_id) {
    int i;

    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        if (Save_Get(animals[i]).id.npc_id == npc_id) {
            return &Save_Get(animals[i]);
        }
    }
    return NULL;
}

/* Debug commands for testing what the game only does now and then (a
 * villager asking for a new catchphrase, a stationery item in the pockets,
 * Resetti's later visits) */
/* SOUND: one sound after another, each for some frames */
static struct {
    int active;
    int kind; /* 0 BGM, 1 SE (system trigger), 2 HANIWA (a gyroid's rhythm), 3 INST (furniture instrument) */
    u32 next, left, frames, timer, cur;
} s_sweep;
#define SWEEP_ID 0x7E570000u /* the rhythm and instrument owner id */

static void sound_sweep_frame(void) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    PLAYER_ACTOR* pl;
    xyz_t pos = { 0.0f, 0.0f, 0.0f };

    if (!s_sweep.active) return; /* (no sweep: nothing to touch) */
    if (play == NULL || play->game.exec != play_main) return;
    pl = GET_PLAYER_ACTOR(play);
    if (pl != NULL) pos = ((ACTOR*)pl)->world.position;
    if (s_sweep.timer == 0) {
        /* the one before ends */
        if (s_sweep.cur != 0xFFFFFFFF) {
            if (s_sweep.kind == 0) sAdo_BgmStop((u16)s_sweep.cur);
            if (s_sweep.kind == 2) sAdo_RhythmStop(SWEEP_ID);
        }
        if (s_sweep.left == 0) {
            printf("[INPUT] frame %u: sound sweep done\n", (unsigned)s_frame);
            s_sweep.cur = 0xFFFFFFFF;
            s_sweep.active = 0;
            return;
        }
        s_sweep.cur = s_sweep.next++;
        s_sweep.left--;
        s_sweep.timer = s_sweep.frames;
        printf("[INPUT] frame %u: sound %d %X\n", (unsigned)s_frame, s_sweep.kind, (unsigned)s_sweep.cur);
        fflush(stdout);
        switch (s_sweep.kind) {
            case 0: sAdo_BgmStart((u16)s_sweep.cur); break;
            case 1: sAdo_SysTrgStart((u16)s_sweep.cur); break;
            case 2: sAdo_RhythmStart(SWEEP_ID, 1, 0); break;
            default: break;
        }
    }
    if (s_sweep.kind == 2) {
        sAdo_RhythmPos(SWEEP_ID, (u8)s_sweep.cur, &pos);
    } else if (s_sweep.kind == 3 && s_sweep.timer % 30 == 0) {
        static u8 melody[16];

        mMld_TransformMelodyData_u64_2_u8(melody, Save_Get(melody));
        sAdo_FurnitureInst(SWEEP_ID + s_sweep.timer, (u8)s_sweep.cur, melody, &pos);
    }
    s_sweep.timer--;
}

static void debug_command(const ScriptEvent* ev) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    Animal_c* animal;
    int i;

    if (ev->kind == SCRIPT_VOICELOG) {
        g_pc_ko_voice_log = (int)ev->value[0];
        return;
    }
    if (ev->kind == SCRIPT_RESETS) {
        /* as when a town that was not saved again is loaded; repeated over
         * the loading, which sets both */
        if (Now_Private->reset_count != (u8)ev->value[0] || !Common_Get(reset_flag)) {
            Now_Private->reset_count = (u8)ev->value[0];
            Common_Set(reset_flag, TRUE);
            printf("[INPUT] frame %u: reset count %u\n", (unsigned)s_frame, (unsigned)ev->value[0]);
        }
        return;
    }
    if (play == NULL || play->game.exec != play_main) {
        printf("[INPUT] frame %u: not playing\n", (unsigned)s_frame);
        return;
    }
    if (ev->kind == SCRIPT_GIVE) {
        for (i = 0; i < mPr_POCKETS_SLOT_COUNT && Now_Private->inventory.pockets[i] != EMPTY_NO; i++) {
        }
        if (i < mPr_POCKETS_SLOT_COUNT) {
            Now_Private->inventory.pockets[i] = (mActor_name_t)ev->value[0];
        }
        printf("[INPUT] frame %u: item %04X in pocket %d\n", (unsigned)s_frame, (unsigned)ev->value[0], i);
    } else if (ev->kind == SCRIPT_LEDIT) {
        /* the name editor; a catchphrase is the villager's (mLE_TYPE_EPHRASE
         * = 2), anything else is typed into a scratch buffer */
        static u8 scratch[32];

        animal = ev->value[1] != 0 ? find_animal((u16)ev->value[1]) : NULL;
        if (!play->submenu.open_flag) {
            mSM_open_submenu_new(&play->submenu, mSM_OVL_LEDIT, (int)ev->value[0], 0,
                                 animal != NULL ? (void*)animal->catchphrase : (void*)scratch);
        }
    } else if (ev->kind == SCRIPT_NOJOB) {
        /* as when Nook lets the player go: the shops, the post office and
         * the villagers' houses act as on any later day */
        mEv_EventOFF(mEv_SAVED_FIRSTJOB_PLR0 + Common_Get(player_no));
        mEv_EventOFF(mEv_SAVED_HRAWAIT_PLR0 + Common_Get(player_no));
        mEv_EventOFF(mEv_SAVED_HRATALK_PLR0 + Common_Get(player_no));
        printf("[INPUT] frame %u: part-time job over\n", (unsigned)s_frame);
    } else if (ev->kind == SCRIPT_JOBDONE) {
        /* as aID_first_job does once Nook has let the player go
         * (ac_intro_demo_move.c_inc): the first job's errand cleared, the
         * job's event off and the HRA one on, the clock running again */
        ACTOR* intro_demo = Actor_info_name_search(&play->actor_info, mAc_PROFILE_INTRO_DEMO, ACTOR_PART_CONTROL);

        if (mEv_CheckFirstJob()) {
            mQst_ClearErrand(mQst_GetFirstJobData(), 1);
            mEv_UnSetFirstJob();
            mTM_off_renew_time(1);
            Common_Set(complete_payment_type, mPlayer_COMPLETE_PAYMENT_TYPE_ARBEIT);
            play->event.day = 0;
            if (intro_demo != NULL) {
                Actor_delete(intro_demo);
            }
        }
        printf("[INPUT] frame %u: part-time job finished (job %d, HRA wait %d)\n", (unsigned)s_frame,
               mEv_CheckFirstJob(), mEv_CheckEvent(mEv_SAVED_HRAWAIT_PLR0 + Common_Get(player_no)));
    } else if (ev->kind == SCRIPT_WARP) {
        /* the scene again with the player at x,z, as when leaving a building
         * (a fade out and in): a route starts by the door it needs instead
         * of finding a way around rivers and cliffs; outdoors only (in
         * another scene, the player select say, it would crash) */
        Door_data_c door;

        if (mFI_GET_TYPE(mFI_GetFieldId()) != mFI_FIELDTYPE_FG) {
            printf("[INPUT] frame %u: warp: not outdoors (scene %d)\n", (unsigned)s_frame, (int)Save_Get(scene_no));
            return;
        }
        memset(&door, 0, sizeof(door));
        door.next_scene_id = Save_Get(scene_no);
        door.exit_orientation = (u8)ev->value[0];
        door.exit_position.x = (s16)ev->seek_pos[0];
        door.exit_position.y = (s16)mCoBG_GetBgY_OnlyCenter_FromWpos2(
            (xyz_t){ ev->seek_pos[0], 0.0f, ev->seek_pos[1] }, 0.0f);
        door.exit_position.z = (s16)ev->seek_pos[1];
        door.door_actor_name = EMPTY_NO;
        door.wipe_type = WIPE_TYPE_FADE_BLACK;
        printf("[INPUT] frame %u: warp to %d,%d: %d\n", (unsigned)s_frame, door.exit_position.x, door.exit_position.z,
               goto_other_scene(play, &door, FALSE));
    } else if (ev->kind == SCRIPT_CLOCK) {
        /* forward to the next H:M:S (later today, else tomorrow): the game
         * reads the clock every frame (mTM_time), so it crosses midnight
         * as when playing on into the night */
        lbRTC_time_c* t = Common_GetPointer(time.rtc_time);
        long long now = t->hour * 3600 + t->min * 60 + t->sec;
        long long step = (long long)ev->value[0] - now;

        if (step <= 0) {
            step += 24 * 3600;
        }
        pc_os_advance_clock(step);
        printf("[INPUT] frame %u: clock %04d-%02d-%02d %02d:%02d:%02d + %lld s\n", (unsigned)s_frame, t->year,
               t->month, t->day, t->hour, t->min, t->sec, step);
    } else if (ev->kind == SCRIPT_FG) {
        /* an item on the ground (the unit at x,z), as the game puts one:
         * Resetti's hole, a fence, a buried thing ... */
        xyz_t wpos = { ev->seek_pos[0], 0.0f, ev->seek_pos[1] };

        printf("[INPUT] frame %u: item %04X at %d,%d: %d\n", (unsigned)s_frame, (unsigned)ev->value[0],
               (int)wpos.x, (int)wpos.z, mFI_SetFG_common((mActor_name_t)ev->value[0], wpos, TRUE));
    } else if (ev->kind == SCRIPT_SPEVENT) {
        /* the week's visitor (a mEv_EVENT_* number: the artist, the broker,
         * Gracie ...) set for today and tomorrow and made new, as the game
         * starts one (its save data cleared, then set up by the event
         * manager), and today's events made again; the town has it from the
         * next scene. 68 is Gulliver, the weekday's visitor. */
        lbRTC_time_c* t = Common_GetPointer(time.rtc_time);
        u16 today = (u16)((t->month << 8) | t->day);
        u16 tomorrow;
        GAME_PLAY* play = (GAME_PLAY*)gamePT;
        int type = (int)ev->value[0];

        if (t->day < lbRTC_GetDaysByMonth(t->year, t->month)) {
            tomorrow = today + 1;
        } else {
            tomorrow = (u16)(((t->month % 12 + 1) << 8) | 1);
        }
        if (type == mEv_EVENT_DOZAEMON) {
            Save_Get(event_save_common).weekly_event.type = mEv_EVENT_DOZAEMON;
            Save_Get(event_save_common).dates[mEv_SAVE_DATE_WEEKLY] = today;
            Save_Get(event_save_common).dozaemon_completed = FALSE;
            if (t->weekday == lbRTC_SATURDAY || t->weekday == lbRTC_SUNDAY) {
                printf("[INPUT] frame %u: Gulliver comes on weekdays only\n", (unsigned)s_frame);
            }
        } else {
            mEv_ClearSpecialEvent(&Save_Get(event_save_data).special);
            Save_Get(event_save_common).special_event.type = (s16)type;
            Save_Get(event_save_common).special_event.flags = 1; /* (the event manager sets it up) */
            Save_Get(event_save_common).dates[mEv_SAVE_DATE_SPECIAL0] = today;
            Save_Get(event_save_common).dates[mEv_SAVE_DATE_SPECIAL1] = today;
            Save_Get(event_save_common).dates[mEv_SAVE_DATE_SPECIAL2] = tomorrow;
            Save_Get(event_save_common).dates[mEv_SAVE_DATE_SPECIAL3] = 6;
        }
        Save_Set(event_year, t->year);
        if (play != NULL && play->game.exec == play_main) {
            mEv_init_force(&play->event);
        }
        printf("[INPUT] frame %u: special event %d today\n", (unsigned)s_frame, type);
        /* the town's free lots (where a visitor's tent or car is put) */
        for (int bz = 1; bz < FG_BLOCK_Z_NUM + 1; bz++) {
            for (int bx = 1; bx < FG_BLOCK_X_NUM + 1; bx++) {
                for (int u = 0; u < UT_TOTAL_NUM; u++) {
                    mActor_name_t item = EMPTY_NO;

                    if (mFI_GetBlockUtNum2FG(&item, bx, bz, u % UT_X_NUM, u / UT_X_NUM) && mNT_IS_RESERVE(item)) {
                        printf("[INPUT] frame %u: lot block %d,%d unit %d,%d\n", (unsigned)s_frame, bx, bz,
                               u % UT_X_NUM, u / UT_X_NUM);
                    }
                }
            }
        }
        if (ev->value[1] != 0) {
            /* SPEVENT type bx,bz,ux,uz: the tent or car of a visitor who
             * comes to a lot (the broker, Gracie, the fortune teller), put
             * on the lot there instead of one the game picks; only before
             * the game has placed it (it moves no tent or car already set) */
            static const struct {
                int type;
                mActor_name_t structure;
            } lot_visitor[] = {
                { mEv_EVENT_BROKER_SALE, BROKER_TENT },
                { mEv_EVENT_DESIGNER, DESIGNER_CAR },
                { mEv_EVENT_GYPSY, FORTUNE_TENT },
            };
            mActor_name_t structure = EMPTY_NO;

            for (int i = 0; i < (int)ARRAY_COUNT(lot_visitor); i++) {
                if (lot_visitor[i].type == type) {
                    structure = lot_visitor[i].structure;
                }
            }
            if (structure == EMPTY_NO) {
                printf("[INPUT] frame %u: visitor %d does not come to a lot\n", (unsigned)s_frame, type);
            } else if (mEv_get_common_place(type, 0x51) != NULL) {
                printf("[INPUT] frame %u: visitor %d is placed already\n", (unsigned)s_frame, type);
            } else {
                mEv_place_data_c* place = mEv_reserve_common_place(type, 0x51);

                if (place != NULL) {
                    place->actor_name = structure;
                    place->flag = 0;
                    place->block.x = (u8)(ev->value[1] >> 24);
                    place->block.z = (u8)(ev->value[1] >> 16);
                    place->unit.x = (u8)(ev->value[1] >> 8);
                    place->unit.z = (u8)ev->value[1];
                }
            }
        }
    } else if (ev->kind == SCRIPT_WEATHER) {
        /* today's weather, as the game decides it at the start of a day (the
         * town shows it from the next scene on, e.g. after a WARP); while
         * the part-time job lasts the game never lets it rain */
        Common_Set(weather, (s16)ev->value[0]);
        Common_Set(weather_intensity, (s16)ev->value[1]);
        Save_Set(weather, (u8)(ev->value[1] | (ev->value[0] << 4)));
        printf("[INPUT] frame %u: weather %u, intensity %u\n", (unsigned)s_frame, (unsigned)ev->value[0],
               (unsigned)ev->value[1]);
    } else if (ev->kind == SCRIPT_DESIGN) {
        mNW_original_design_c* d = &Now_Private->my_org[ev->value[0]];
        u8 v = (u8)ev->value[1];

        for (int i = 0; i < (int)sizeof(d->design.data); i++) {
            /* bands of two colours of the palette, a row of 32 pixels in 16 bytes (CI4 tiles of 8x8: 32 bytes) */
            int row = (i / 4) % 8 + ((i / 32) / 4) * 8;

            d->design.data[i] = ((row / 4 + (v & 3)) & 1) ? (u8)(0x11 * (1 + (v >> 4) % 15)) : (u8)(0x11 * (v & 15));
        }
        d->palette = (u8)(v % 16);
        Now_Private->cloth.item = RSV_CLOTH;
        Now_Private->cloth.idx = (u16)(0x100 + ev->value[0]);
        mPlib_change_player_cloth((GAME*)play, Now_Private->cloth.idx);
        printf("[INPUT] frame %u: design %u (pattern %u) worn\n", (unsigned)s_frame, (unsigned)ev->value[0],
               (unsigned)v);
    } else if (ev->kind == SCRIPT_HANIWA) {
        if (Common_Get(player_no) < PLAYER_NUM) {
            Haniwa_c* haniwa = &Save_Get(homes[mHS_get_arrange_idx(Common_Get(player_no))]).haniwa;

            haniwa->items[ev->value[0]].item = (mActor_name_t)ev->value[1];
            haniwa->items[ev->value[0]].exchange_type = mHm_HANIWA_TRADE_2;
            haniwa->items[ev->value[0]].extra_data = ev->value[2];
            printf("[INPUT] frame %u: the gyroid sells %04X for %u\n", (unsigned)s_frame, (unsigned)ev->value[1],
                   (unsigned)ev->value[2]);
        }
    } else if (ev->kind == SCRIPT_EQUIP) {
        Now_Private->equipment = (mActor_name_t)ev->value[0];
        printf("[INPUT] frame %u: %04X in hand\n", (unsigned)s_frame, (unsigned)ev->value[0]);
    } else if (ev->kind == SCRIPT_BELLS) {
        Now_Private->inventory.wallet = ev->value[0];
        printf("[INPUT] frame %u: %u bells\n", (unsigned)s_frame, (unsigned)ev->value[0]);
    } else if (ev->kind == SCRIPT_MENU) {
        /* a submenu the game opens only now and then; the third argument
         * (a string some of them edit) is a scratch buffer */
        static u8 menu_scratch[64];

        if (!play->submenu.open_flag) {
            memset(menu_scratch, CHAR_SPACE, sizeof(menu_scratch));
            mSM_open_submenu_new(&play->submenu, (int)ev->value[0], (int)ev->value[1], (int)ev->value[2],
                                 menu_scratch);
            printf("[INPUT] frame %u: submenu %u (%u, %u)\n", (unsigned)s_frame, (unsigned)ev->value[0],
                   (unsigned)ev->value[1], (unsigned)ev->value[2]);
        }
    } else if (ev->kind == SCRIPT_SOUND) {
        if (s_sweep.active && s_sweep.cur != 0xFFFFFFFF) {
            /* a sweep still going: end its sound first */
            if (s_sweep.kind == 0) sAdo_BgmStop((u16)s_sweep.cur);
            if (s_sweep.kind == 2) sAdo_RhythmStop(SWEEP_ID);
        }
        s_sweep.active = 1;
        s_sweep.kind = (int)ev->value[0];
        s_sweep.next = ev->value[1];
        s_sweep.left = ev->value[2];
        s_sweep.frames = ev->duration > 0 ? ev->duration : 120;
        s_sweep.timer = 0;
        s_sweep.cur = 0xFFFFFFFF;
        printf("[INPUT] frame %u: sound sweep kind %d from %X, %u of them\n", (unsigned)s_frame, s_sweep.kind,
               (unsigned)s_sweep.next, (unsigned)s_sweep.left);
    } else if (ev->kind == SCRIPT_NAMES) {
        for (i = 0; i < (int)ev->value[1] && i < 4096; i++) {
            mActor_name_t item = (mActor_name_t)(ev->value[0] + (u32)i);
            u8 raw[mIN_ITEM_NAME_LEN];
            const u8* str = raw;
            char name[96];
            int len;

            mIN_copy_name_str(raw, item);
            len = pc_ko_expand(raw, mIN_ITEM_NAME_LEN, 5, &str);
            pc_mp_game_name_utf8(str, len, name, sizeof(name));
            printf("[INPUT] frame %u: name %04X %s\n", (unsigned)s_frame, (unsigned)item, name);
        }
    } else if (ev->kind == SCRIPT_ITEM) {
        /* what a menu chose, e.g. the song a request to K.K. named */
        printf("[INPUT] frame %u: menu item %04X\n", (unsigned)s_frame,
               play->submenu.item_p != NULL ? (unsigned)play->submenu.item_p->item : 0u);
    } else if (ev->kind == SCRIPT_BGM) {
        /* a piece of music by number (audio_defs.h), as the game starts one */
        sAdo_BgmStart((u16)ev->value[0]);
        printf("[INPUT] frame %u: music %02X\n", (unsigned)s_frame, (unsigned)ev->value[0]);
    } else if (ev->kind == SCRIPT_CATCH && (animal = find_animal((u16)ev->value[0])) != NULL) {
        /* the field and the padding after it, where the Korean patch packs it
         * (read as bytes of the struct: the field itself is 4 long) */
        const u8* bytes = (const u8*)animal + offsetof(Animal_c, catchphrase);

        printf("[INPUT] frame %u: catchphrase of %04X:", (unsigned)s_frame, (unsigned)ev->value[0]);
        for (i = 0; i < 7; i++) {
            printf(" %02X", bytes[i]);
        }
        printf("\n");
    }
}

/* a SHOT captures the frame after script frame <frame> */
static int shot_next(u32 frame) {
    int i;

    for (i = 0; i < s_count; i++) {
        if (s_events[i].kind == SCRIPT_SHOT && s_events[i].frame == frame + 1) {
            return first_waiting() == NULL; /* (or the next frame is the same script time) */
        }
    }
    return 0;
}

int pc_input_script_frame(u32 real_frame) {
    int keep_running = 1;

    sound_sweep_frame();
    int i;
    u32 frame;
    int again;

    s_real = real_frame;
    s_frame = frame = real_frame - s_held;
    again = frame == s_last; /* held: the events of this time already happened */
    s_last = frame;
    if (first_waiting() != NULL) {
        s_held++; /* the next real frame is the same script time */
    }
    if (s_click_pending) {
        push_click_button();
    }
    for (i = 0; i < s_count; i++) {
        const ScriptEvent* ev = &s_events[i];

        if (again) {
            break;
        }

        if (ev->kind == SCRIPT_TOUCH) {
            s64 id = 0x5C000 + i;

            if (frame == ev->frame) {
                pc_touch_finger_down(id, ev->touch[0], ev->touch[1]);
            } else if (frame == ev->frame + 1 && ev->touch_moves) {
                pc_touch_finger_move(id, ev->touch[2], ev->touch[3]);
            }
            if (frame == ev->frame + ev->duration) {
                pc_touch_finger_up(id);
            }
            continue;
        }
        if (ev->frame != frame) {
            continue;
        }
        if (ev->kind == SCRIPT_SHOT) {
            save_shot(ev->name);
        } else if (ev->kind == SCRIPT_TYPE) {
            type_keys(ev->name);
        } else if (ev->kind == SCRIPT_EDIT) {
            pc_typing_system_editing(ev->name);
        } else if (ev->kind == SCRIPT_TEXT) {
            if (pc_gui_wants_keyboard()) {
                push_text(ev->name); /* a field of the menus or the chat box */
            } else {
                pc_typing_system_text(ev->name);
            }
        } else if (ev->kind == SCRIPT_KEY || ev->kind == SCRIPT_PAD) {
            push_key_or_pad(ev);
        } else if (ev->kind == SCRIPT_CLICK) {
            push_click(ev->touch[0], ev->touch[1]);
        } else if (ev->kind == SCRIPT_MP) {
            mp_command(ev->name);
        } else if (ev->kind == SCRIPT_POS) {
            log_positions(frame);
        } else if (ev->kind == SCRIPT_MAP) {
            pc_minimap_request_toggle();
        } else if (ev->kind == SCRIPT_BLOCK) {
            log_block(frame);
        } else if (ev->kind >= SCRIPT_GIVE && ev->kind <= SCRIPT_BGM) {
            debug_command(ev);
        } else if (ev->kind == SCRIPT_QUIT) {
            printf("[INPUT] frame %u: quit\n", frame);
            pc_autosave_quit(); /* as closing the window */
            keep_running = 0;
        }
    }
    if (s_fast) {
        /* Draw the next frame only if it is captured. Menus that keep a copy
         * of the scene behind them show black. */
        g_pc_skip_draw = !shot_next(frame);
    }
    return keep_running;
}

int pc_input_script_draws_next(void) {
    return s_count > 0 && shot_next(s_frame);
}
