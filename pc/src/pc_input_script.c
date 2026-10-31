/* pc_input_script.c - see pc_input_script.h */
#include "pc_input_script.h"
#include "pc_touch.h"
#include "pc_typing.h"

#include "game.h"
#include "m_common_data.h"
#include "m_play.h"
#include "m_field_info.h"
#include "m_random_field_h.h"
#include "m_submenu.h"
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
    SCRIPT_GIVE,
    SCRIPT_LEDIT,
    SCRIPT_RESETS,
    SCRIPT_CATCH,
    SCRIPT_ITEM,
    SCRIPT_BGM,
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
    u32 value[2]; /* GIVE item; LEDIT type and villager; RESETS count; CATCH villager */
    char name[128];
} ScriptEvent;

static ScriptEvent* s_events = NULL;
static int s_count = 0;
static u32 s_frame = 0;
static int s_fast = 0;

void pc_input_script_set_fast(int fast) {
    s_fast = fast;
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
        char* hash = strchr(line, '#');
        unsigned long frame;
        int fields;

        line_no++;
        if (hash != NULL) {
            *hash = '\0';
        }
        fields = sscanf(line, "%lu %63s %127s %31s", &frame, word, arg, arg2);
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
        } else if (strcmp(word, "ITEM") == 0) {
            ev.kind = SCRIPT_ITEM;
        } else if (strcmp(word, "BGM") == 0 && fields >= 3) {
            ev.kind = SCRIPT_BGM;
            ev.value[0] = (u32)strtoul(arg, NULL, 16);
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

void pc_input_script_apply(u16* buttons, s8* stick_x, s8* stick_y) {
    int i;

    for (i = 0; i < s_count; i++) {
        const ScriptEvent* ev = &s_events[i];

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

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
#ifdef PC_GLES
    { /* ES only guarantees RGBA reads */
        int i;

        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (i = 0; i < w * h; i++) {
            memmove(pixels + i * 3, pixels + i * 4, 3);
        }
    }
#else
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
#endif

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
            printf("[POS] frame %u home %04X block %u,%u x %.0f z %.0f\n", (unsigned)frame, Save_Get(animals[i]).id.npc_id,
                   home->bx, home->bz, home_x(home), home_z(home));
        }
    }
    printf("[POS] frame %u camera %d center %.0f,%.0f,%.0f eye %.0f,%.0f,%.0f dir %d,%d dist %.0f\n", (unsigned)frame,
           play->camera.now_main_index, play->camera.lookat.center.x, play->camera.lookat.center.y,
           play->camera.lookat.center.z, play->camera.lookat.eye.x, play->camera.lookat.eye.y, play->camera.lookat.eye.z,
           play->camera.direction.x, play->camera.direction.y, play->camera.focus_distance);
    for (i = 0; i < (int)(sizeof(parts) / sizeof(parts[0])); i++) {
        ACTOR* actor;

        for (actor = play->actor_info.list[parts[i]].actor; actor != NULL; actor = actor->next_actor) {
            printf("[POS] frame %u %s %04X x %.0f z %.0f\n", (unsigned)frame, parts[i] == ACTOR_PART_PLAYER ? "player" : "npc",
                   actor->npc_id, actor->world.position.x, actor->world.position.z);
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
        };
        ACTOR* actor;
        int bx, bz;

        for (i = 0; i < (int)(sizeof(buildings) / sizeof(buildings[0])); i++) {
            if (mFI_BlockKind2BkNum(&bx, &bz, buildings[i].kind)) {
                printf("[POS] frame %u %s block %d,%d\n", (unsigned)frame, buildings[i].name, bx, bz);
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
static void debug_command(const ScriptEvent* ev) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    Animal_c* animal;
    int i;

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
    } else if (ev->kind == SCRIPT_ITEM) {
        /* what a menu chose, e.g. the song a request to K.K. named */
        printf("[INPUT] frame %u: menu item %04X\n", (unsigned)s_frame,
               play->submenu.item_p != NULL ? (unsigned)play->submenu.item_p->item : 0u);
    } else if (ev->kind == SCRIPT_BGM) {
        /* a piece of music by number (audio_defs.h), as the game starts one */
        sAdo_BgmStart((u16)ev->value[0]);
        printf("[INPUT] frame %u: music %02X\n", (unsigned)s_frame, (unsigned)ev->value[0]);
    } else if (ev->kind == SCRIPT_CATCH && (animal = find_animal((u16)ev->value[0])) != NULL) {
        /* the field and the padding after it, where the Korean patch packs it */
        printf("[INPUT] frame %u: catchphrase of %04X:", (unsigned)s_frame, (unsigned)ev->value[0]);
        for (i = 0; i < 7; i++) {
            printf(" %02X", animal->catchphrase[i]);
        }
        printf("\n");
    }
}

int pc_input_script_frame(u32 frame) {
    int keep_running = 1;
    int i;

    s_frame = frame;
    for (i = 0; i < s_count; i++) {
        const ScriptEvent* ev = &s_events[i];

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
            pc_typing_system_text(ev->name);
        } else if (ev->kind == SCRIPT_KEY || ev->kind == SCRIPT_PAD) {
            push_key_or_pad(ev);
        } else if (ev->kind == SCRIPT_POS) {
            log_positions(frame);
        } else if (ev->kind >= SCRIPT_GIVE && ev->kind <= SCRIPT_BGM) {
            debug_command(ev);
        } else if (ev->kind == SCRIPT_QUIT) {
            printf("[INPUT] frame %u: quit\n", frame);
            keep_running = 0;
        }
    }
    if (s_fast) {
        /* Draw the next frame only if it is captured. Menus that keep a copy
         * of the scene behind them show black. */
        g_pc_skip_draw = 1;
        for (i = 0; i < s_count; i++) {
            if (s_events[i].kind == SCRIPT_SHOT && s_events[i].frame == frame + 1) {
                g_pc_skip_draw = 0;
                break;
            }
        }
    }
    return keep_running;
}
