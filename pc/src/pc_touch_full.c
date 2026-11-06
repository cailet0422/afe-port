/* pc_touch_full.c - see pc_touch_full.h */
#include "pc_touch_full.h"
#include "pc_touch_internal.h"
#include "pc_touch.h"
#include "pc_settings.h"
#include "pc_pause_menu.h"
#include "pc_autosave.h"
#include "pc_minimap.h"
#include "pc_gui.h"

#include "game.h"
#include "m_common_data.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_msg.h"
#include "m_choice.h"
#include "m_submenu.h"
#include "m_field_info.h"
#include "m_collision_bg.h"
#include "m_name_table.h"
#include "m_private.h"
#include "m_camera2.h"
#include "m_event.h"
#include "m_actor.h"

#include <dolphin/pad.h>
#include <math.h>
#include <string.h>

extern u32 pc_frame_counter;

#define MAX_FINGERS 10
#define TAP_MS 300       /* a finger up this soon, */
#define TAP_SLOP 0.035f  /* having moved less than this (of the shorter side), is a tap */
#define HOLD_MS 280      /* a finger kept still this long runs to where it is */
#define DRAG_REACH 0.11f /* a drag this long (of the shorter side) is full tilt; the anchor follows past it */
#define STICK_FULL 72    /* past the game's full tilt (61, m_controller.h) */
#define TOUCH_STICK_MIN 14 /* just past the game's dead zone (9.9) */
#define WALK_SPEED 4.875f /* the game's speed at full tilt without a dash button */
#define GO_GIVE_UP 300   /* frames a walk may take */
#define PRESS_FRAMES 3

/* --- State ------------------------------------------------------------ */

enum {
    FK_NONE,
    FK_HUD,   /* landed on an icon: acts when lifted on it */
    FK_STRIP, /* the tool strip: a swipe or a tap on a side */
    FK_WORLD, /* tap, hold or drag in the world */
    FK_MENU,  /* the pockets or the map (pc_touch_menu.c) */
    FK_ROOM,  /* the furniture mode (pc_touch_room.c) */
};

typedef struct {
    int active;
    s64 id;
    float x, y;   /* pixels */
    float x0, y0; /* where it landed */
    float ax, ay; /* a drag's anchor (follows a finger dragged far) */
    Uint32 down_ms;
    int kind;
    int hud;      /* the icon it landed on (FK_HUD) */
    int moved;    /* went past the tap slop */
    int holding;  /* kept still past HOLD_MS: runs to where it is */
} TFinger;

static TFinger s_fingers[MAX_FINGERS];
static int s_pad_wanted = 0; /* the gamepad icon */

/* What the player was asked to do (worked out in pc_touch_full_game_frame,
 * pressed in pc_touch_full_read) */
enum {
    GO_NONE,
    GO_WALK, /* to a point */
    GO_TALK, /* up to a villager, then A */
    GO_USE,  /* up to a unit, face it, then a button */
};

static struct {
    int kind;
    float x, z;       /* where to (a unit's centre, a villager's feet) */
    ACTOR* actor;     /* GO_TALK: the villager (checked each frame) */
    float stop;       /* close enough */
    float reach;      /* GO_USE/TALK: if the walk stops short (a tree, a fence), still act within this */
    u16 button;       /* GO_USE: what to press there */
    float ux, uz;     /* GO_USE: the unit's centre, to face */
    u32 start;
    u32 still;
    float lx, lz;
} s_go;

static s8 s_stick_x, s_stick_y; /* for the next reads */
static int s_dash;              /* R held with the stick: the player runs */
static u16 s_press;             /* pressed from s_press_from for PRESS_FRAMES frames */
static u32 s_press_from;
static int s_face_pending;      /* turn the player to s_face_yaw at the next game frame, then press */
static s16 s_face_yaw;
static u16 s_face_button;

/* A tap waiting for the game frame (window fractions) */
static int s_tap_pending;
static float s_tap_x, s_tap_y;

/* The tool strip: the slot the tool in hand came from, doubled (+1 after a
 * step to the right, -1 after one to the left; -2 = the empty hand) */
static int s_tool_pos2 = 2 * mPr_POCKETS_SLOT_COUNT + 1;
static int s_tool_step; /* -1, +1: take the previous or next at the next game frame */

/* --- Small things ------------------------------------------------------- */

int pc_touch_full_on(void) {
    return g_pc_settings.touch_controls == 2;
}

static float shorter_side(void) {
    return (float)(g_pc_window_w < g_pc_window_h ? g_pc_window_w : g_pc_window_h);
}

GAME_PLAY* pc_touch_play(void) {
    GAME* game = gamePT;

    if (game == NULL || game->exec != play_main) {
        return NULL;
    }
    return (GAME_PLAY*)game;
}

PLAYER_ACTOR* pc_touch_player(GAME_PLAY* play) {
    return play != NULL ? GET_PLAYER_ACTOR(play) : NULL;
}

int pc_touch_msg_open(void) {
    mMsg_Window_c* msg = mMsg_Get_base_window_p();

    return msg != NULL && !mMsg_Check_main_hide(msg);
}

/* The player can be steered: walking or standing, no message, no menu */
int pc_touch_player_free(GAME_PLAY* play) {
    int index;

    if (play == NULL || pc_touch_player(play) == NULL || pc_touch_msg_open() ||
        play->submenu.process_status != mSM_PROCESS_WAIT || mEv_CheckTitleDemo() > 0) {
        return FALSE;
    }
    index = mPlib_get_player_actor_main_index((GAME*)play);
    return index >= mPlayer_INDEX_WAIT && index <= mPlayer_INDEX_DASH;
}

static int submenu_type(GAME_PLAY* play) {
    if (play == NULL || play->submenu.process_status == mSM_PROCESS_WAIT) {
        return mSM_OVL_NONE;
    }
    return play->submenu.menu_type;
}

/* Widescreen: the game's 3D picture is widened about the centre (hor+,
 * pc_gx.c), its 2D drawing kept in the 4:3 middle. (g_pc_widescreen_stretch
 * only says how emu64 draws at that moment; the world is always hor+.) */
static float aspect_3d(void) {
    float w = (float)g_pc_window_w, h = (float)g_pc_window_h;
    float gc = (float)PC_GC_WIDTH / (float)PC_GC_HEIGHT;

    if (h <= 0.0f || w / h <= gc + 0.01f) {
        return 1.0f;
    }
    return gc / (w / h);
}

int pc_touch_world_to_window(GAME_PLAY* play, const xyz_t* p, float* fx, float* fy) {
    const MtxF* m = &play->projection_matrix;
    float cx = m->xx * p->x + m->xy * p->y + m->xz * p->z + m->xw;
    float cy = m->yx * p->x + m->yy * p->y + m->yz * p->z + m->yw;
    float w = m->wx * p->x + m->wy * p->y + m->wz * p->z + m->ww;

    if (w < 1.0f) {
        return FALSE; /* behind the camera */
    }
    *fx = (cx / w * aspect_3d() + 1.0f) * 0.5f;
    *fy = (1.0f - cy / w) * 0.5f;
    return isfinite(*fx) && isfinite(*fy);
}

/* Where a window point meets the plane y = h */
static int window_to_plane(GAME_PLAY* play, float fx, float fy, float h, float* x, float* z) {
    const MtxF* m = &play->projection_matrix;
    float nx = (fx * 2.0f - 1.0f) / aspect_3d();
    float ny = 1.0f - fy * 2.0f;
    /* (row - n * w_row) . (x, h, z, 1) = 0 for the x and y rows */
    float a1 = m->xx - nx * m->wx, a3 = m->xz - nx * m->wz;
    float c1 = (m->xy - nx * m->wy) * h + (m->xw - nx * m->ww);
    float b1 = m->yx - ny * m->wx, b3 = m->yz - ny * m->wz;
    float c2 = (m->yy - ny * m->wy) * h + (m->yw - ny * m->ww);
    float det = a1 * b3 - a3 * b1;

    if (fabsf(det) < 1e-9f) {
        return FALSE;
    }
    *x = (-c1 * b3 + a3 * c2) / det;
    *z = (-a1 * c2 + c1 * b1) / det;
    return isfinite(*x) && isfinite(*z);
}

/* The ground under a window point: the plane at the player's feet, then the
 * ground's height there a few times (slopes, the cliffs' steps) */
int pc_touch_window_to_ground(GAME_PLAY* play, float fx, float fy, xyz_t* out) {
    PLAYER_ACTOR* player = pc_touch_player(play);
    float h = player != NULL ? player->actor_class.world.position.y : 0.0f;
    int i;

    for (i = 0; i < 3; i++) {
        xyz_t p;

        if (!window_to_plane(play, fx, fy, h, &p.x, &p.z)) {
            return FALSE;
        }
        p.y = h;
        *out = p;
        h = mCoBG_GetBgY_AngleS_FromWpos(NULL, p, 0.0f);
        if (!isfinite(h)) {
            return FALSE;
        }
    }
    out->y = h;
    return TRUE;
}

static s16 yaw_to(float dx, float dz) {
    return (s16)(int)lrintf(atan2f(dx, dz) * (32768.0f / (float)M_PI));
}

/* The stick that walks the player toward world yaw `yaw`: stick up is the
 * camera's forward (m_player_common.c_inc Player_actor_Get_ControllerAngle) */
static void stick_for_yaw(GAME_PLAY* play, s16 yaw, float mag, s8* sx, s8* sy) {
    s16 move = (s16)(yaw - getCamera2AngleY(play) + 0x4000);
    float a = (float)move * ((float)M_PI / 32768.0f);

    *sx = (s8)lrintf(cosf(a) * mag);
    *sy = (s8)lrintf(sinf(a) * mag);
}

static void press(u16 buttons, u32 delay) {
    s_press = buttons;
    s_press_from = pc_frame_counter + delay;
}

/* --- What is under a finger ------------------------------------------- */

/* The box an actor stands in on the window: feet to about its height */
static int actor_box(GAME_PLAY* play, ACTOR* a, float height, float* cx, float* cy, float* hw, float* hh) {
    xyz_t feet = a->world.position;
    xyz_t head = feet;
    float fx, fy, hx, hy;

    head.y += height;
    if (!pc_touch_world_to_window(play, &feet, &fx, &fy) || !pc_touch_world_to_window(play, &head, &hx, &hy)) {
        return FALSE;
    }
    *cx = (fx + hx) * 0.5f * g_pc_window_w;
    *cy = (fy + hy) * 0.5f * g_pc_window_h;
    *hh = fabsf(fy - hy) * 0.5f * g_pc_window_h;
    if (*hh < 0.03f * shorter_side()) {
        *hh = 0.03f * shorter_side();
    }
    *hw = *hh * 0.75f;
    *hh *= 1.25f;
    return TRUE;
}

static int hit_actor(GAME_PLAY* play, ACTOR* a, float height, float px, float py, float* score) {
    float cx, cy, hw, hh, nx, ny, d;

    if (!actor_box(play, a, height, &cx, &cy, &hw, &hh)) {
        return FALSE;
    }
    nx = (px - cx) / hw;
    ny = (py - cy) / hh;
    d = nx * nx + ny * ny;
    if (d > 1.0f) {
        return FALSE;
    }
    *score = d;
    return TRUE;
}

/* The villager (or other character) under a point of the window, pixels */
static ACTOR* npc_at(GAME_PLAY* play, float px, float py) {
    ACTOR* best = NULL;
    float best_score = 2.0f;
    ACTOR* a;

    for (a = play->actor_info.list[ACTOR_PART_NPC].actor; a != NULL; a = a->next_actor) {
        float score;

        if (hit_actor(play, a, 38.0f, px, py, &score) && score < best_score) {
            best = a;
            best_score = score;
        }
    }
    return best;
}

static int npc_alive(GAME_PLAY* play, ACTOR* actor) {
    ACTOR* a;

    for (a = play->actor_info.list[ACTOR_PART_NPC].actor; a != NULL; a = a->next_actor) {
        if (a == actor) {
            return TRUE;
        }
    }
    return FALSE;
}

/* What a tap on this item does when the player stands before it: B picks
 * up (items, flowers, weeds; outside, a dropped piece of furniture too),
 * A uses (trees, rocks, signs, holes, the furniture in a room) */
static u16 item_button(mActor_name_t item, int in_room) {
    int type = ITEM_NAME_GET_TYPE(item);

    if (item == EMPTY_NO) {
        return 0;
    }
    if (IS_ITEM_FLOWER(item) || IS_ITEM_GRASS(item)) {
        return PAD_BUTTON_B;
    }
    if (type == NAME_TYPE_ITEM1 || type == NAME_TYPE_ITEM2) {
        return PAD_BUTTON_B;
    }
    if ((type == NAME_TYPE_FTR0 || type == NAME_TYPE_FTR1) && !in_room) {
        return PAD_BUTTON_B;
    }
    return PAD_BUTTON_A;
}

/* --- The choice window ------------------------------------------------ */

/* Which choice a window point is on, -1 if none (m_choice_main.c_inc
 * mChoice_Set_DisplayScaleAndDisplayPos, m_choice_draw.c_inc
 * mChoice_DrawFont: rows of 16 from the text's top left, in 320x240) */
int pc_touch_choice_at(float fx, float fy) {
    mChoice_c* choice = mChoice_Get_base_window_p();
    float ux, uy, left, top, width, dotw;
    int row;

    if (choice == NULL || choice->main_index != mChoice_MAIN_NORMAL || choice->data.choice_num <= 0) {
        return -1;
    }
    pc_gx_window_to_ui(fx, fy, &ux, &uy);
    ux *= 0.5f; /* 640x480 to the game's 320x240 */
    uy *= 0.5f;
    dotw = (choice->scale_x - 1.0f / 2.4f) / 0.5833333730697632f;
    width = dotw * 96.0f + 24.0f;
    left = choice->center_x + choice->text_x - 160.0f;
    top = choice->center_y + choice->text_y - 120.0f;
    if (ux < left - 24.0f || ux > left + width + 12.0f || uy < top - 4.0f) {
        return -1;
    }
    row = (int)floorf((uy - top + 2.0f) / 16.0f);
    if (row < 0 || row >= choice->data.choice_num) {
        return -1;
    }
    return row;
}

/* --- Tools ------------------------------------------------------------ */

int pc_touch_tool_glyph(mActor_name_t item, int* gold) {
    s8 kind = item != EMPTY_NO ? mPlib_Get_ItemNoToItemKind(item) : -1;

    *gold = IS_ITEM_GOLDEN_TOOL(item);
    if (item == EMPTY_NO) return PC_GLYPH_NONE;
    if (kind < 0) return PC_GLYPH_OTHER;
    if (mPlayer_ITEM_IS_NET(kind)) return PC_GLYPH_NET;
    if (mPlayer_ITEM_IS_AXE(kind)) return PC_GLYPH_AXE;
    if (mPlayer_ITEM_IS_SHOVEL(kind)) return PC_GLYPH_SHOVEL;
    if (mPlayer_ITEM_IS_ROD(kind)) return PC_GLYPH_ROD;
    if (mPlayer_ITEM_IS_UMBRELLA(kind)) return PC_GLYPH_UMBRELLA;
    if (mPlayer_ITEM_IS_BALLOON(kind)) return PC_GLYPH_BALLOON;
    if (mPlayer_ITEM_IS_WINDMILL(kind)) return PC_GLYPH_PINWHEEL;
    if (mPlayer_ITEM_IS_FAN(kind)) return PC_GLYPH_FAN;
    if (mPlayer_ITEM_IS_FLOWER(kind)) return PC_GLYPH_FLOWER;
    if (mPlayer_ITEM_IS_CRACKER(kind)) return PC_GLYPH_POPPER;
    return PC_GLYPH_OTHER;
}

/* A pocket's item the player can take in hand */
static int pocket_tool(int slot) {
    mActor_name_t item = Now_Private->inventory.pockets[slot];

    return item != EMPTY_NO && mPr_CHK_ITEM_COND(Now_Private->inventory.item_conditions, slot) == mPr_ITEM_COND_NORMAL &&
           mPlib_Get_ItemNoToItemKind(item) >= 0;
}

static int free_pocket(void) {
    int slot;

    for (slot = 0; slot < mPr_POCKETS_SLOT_COUNT; slot++) {
        if (Now_Private->inventory.pockets[slot] == EMPTY_NO) {
            return slot;
        }
    }
    return -1;
}

/* A hand flower or a My Design umbrella is not kept in the pockets (the
 * pockets' own menu throws it away the same way, m_hand_ovl.c) */
static int tool_thrown_away(mActor_name_t item) {
    return (item >= ITM_MY_ORG_UMBRELLA0 && item <= ITM_MY_ORG_UMBRELLA7) || ITEM_IS_TOOL_FLOWER(item);
}

/* The next (dir 1) or previous (-1) tool from the one in hand: a pocket
 * slot, -1 for the empty hand, -2 for none. The places go round: the empty
 * hand, then the pockets in order; the one in hand sits between two of them
 * (s_tool_pos2), so a step back after a step on takes back the same one. */
static int tool_neighbour(int dir) {
    int count = mPr_POCKETS_SLOT_COUNT + 1;
    mActor_name_t held = Now_Private->equipment;
    int hand_ok = held != EMPTY_NO && (tool_thrown_away(held) || free_pocket() >= 0);
    int first = -1;
    int k, i;

    /* place k: 0 the hand (doubled position -2), k >= 1 pocket k - 1 (2 (k - 1)) */
#define TOOL_PLACE(k) ((k) == 0 ? -2 : 2 * ((k) - 1))
    if (dir > 0) {
        for (k = 0; k < count && TOOL_PLACE(k) <= s_tool_pos2; k++) {
        }
        first = k % count;
    } else {
        for (k = count - 1; k >= 0 && TOOL_PLACE(k) >= s_tool_pos2; k--) {
        }
        first = (k + count) % count;
    }
    for (i = 0; i < count; i++) {
        int kk = ((first + dir * i) % count + count) % count;

        if (TOOL_PLACE(kk) == s_tool_pos2) {
            continue; /* where the one in hand is */
        }
        if (kk == 0) {
            if (hand_ok) {
                return -1;
            }
        } else if (pocket_tool(kk - 1)) {
            return kk - 1;
        }
    }
#undef TOOL_PLACE
    return -2;
}

static int tool_can_change(GAME_PLAY* play) {
    return pc_touch_player_free(play) && mPlib_able_submenu_type1((GAME*)play) && !mEv_CheckTitleDemo();
}

static void tool_take(int slot, int dir) {
    mActor_name_t old = Now_Private->equipment;

    if (slot == -1) {
        int free_slot = free_pocket();

        if (old != EMPTY_NO && !tool_thrown_away(old)) {
            if (free_slot < 0) {
                return;
            }
            mPr_SetPossessionItem(Now_Private, free_slot, old, mPr_ITEM_COND_NORMAL);
        }
        Now_Private->equipment = EMPTY_NO;
        s_tool_pos2 = -2;
        printf("[TOUCH] tool put away\n");
        return;
    }
    {
        mActor_name_t item = Now_Private->inventory.pockets[slot];

        if (old != EMPTY_NO && !tool_thrown_away(old)) {
            mPr_SetPossessionItem(Now_Private, slot, old, mPr_ITEM_COND_NORMAL);
        } else {
            mPr_SetPossessionItem(Now_Private, slot, EMPTY_NO, mPr_ITEM_COND_NORMAL);
        }
        /* the player takes it out at its next wait, walk or run (m_player.c
         * Player_actor_request_change_item), as after the pockets' menu */
        Now_Private->equipment = item;
        s_tool_pos2 = 2 * slot + dir;
        printf("[TOUCH] tool %04X in hand (from pocket %d)\n", (unsigned)item, slot);
    }
}

/* --- The icons -------------------------------------------------------- */

static int furniture_icon_wanted(GAME_PLAY* play) {
    return pc_touch_room_available(play);
}

/* 0 none, 1 playing, 2 the pockets or the map, 3 the pad up, 4 the furniture mode */
static int hud_mode(GAME_PLAY* play) {
    int index;
    int menu;

    if (!pc_touch_full_on() || g_pc_paused || g_pc_nes_active || pc_gui_chat_active()) {
        return 0;
    }
    if (pc_touch_room_on()) {
        return 4;
    }
    if (pc_touch_full_pad_shown()) {
        return s_pad_wanted ? 3 : 0;
    }
    if (play == NULL || pc_touch_player(play) == NULL || Common_Get(reset_flag)) {
        return 0;
    }
    menu = submenu_type(play);
    if (menu == mSM_OVL_INVENTORY || menu == mSM_OVL_MAP) {
        return play->submenu.process_status == mSM_PROCESS_PLAY ? 2 : 0;
    }
    if (menu != mSM_OVL_NONE || pc_touch_msg_open() || mEv_CheckTitleDemo() > 0) {
        return 0;
    }
    index = mPlib_get_player_actor_main_index((GAME*)play);
    if (index < mPlayer_INDEX_WAIT || index == mPlayer_INDEX_TALK || index >= mPlayer_INDEX_DEMO_WAIT ||
        index == mPlayer_INDEX_DOOR || index == mPlayer_INDEX_OUTDOOR || index == mPlayer_INDEX_INVADE) {
        return 0;
    }
    return 1;
}

static int add_icon(PCTouchHudIcon* out, int n, int max, int kind, float px, float py, float pr) {
    PCTouchHudIcon* ic;

    if (n >= max) {
        return n;
    }
    ic = &out[n];
    memset(ic, 0, sizeof(*ic));
    ic->kind = kind;
    ic->x = px / (float)g_pc_window_w;
    ic->y = py / (float)g_pc_window_h;
    ic->r = pr / shorter_side();
    return n + 1;
}

static int finger_on_icon(int kind) {
    int i;

    for (i = 0; i < MAX_FINGERS; i++) {
        if (s_fingers[i].active && s_fingers[i].kind == FK_HUD && s_fingers[i].hud == kind) {
            return TRUE;
        }
    }
    return FALSE;
}

/* The icons' places: the pause menu, a save and the gamepad top left, the
 * pockets and the map top right (the furniture mode before them in one's own
 * house), the tools in the middle. Sizes follow the shorter side, as the
 * pad's. With the pad up, only its own icon, under START. */
static int build_hud(PCTouchHudIcon* out, int max) {
    GAME_PLAY* play = pc_touch_play();
    int mode = hud_mode(play);
    float w = (float)g_pc_window_w;
    float u = shorter_side();
    float r = 0.05f * u;
    float y = 0.085f * u;
    int n = 0;
    int i;

    if (mode == 0 || u <= 0.0f) {
        return 0;
    }
    if (mode == 3) {
        n = add_icon(out, n, max, PC_HUD_PAD, w * 0.5f, 0.19f * u, 0.045f * u);
        out[0].active = 1;
    } else if (mode == 2) {
        n = add_icon(out, n, max, PC_HUD_PAD, 0.08f * u, y, r);
        n = add_icon(out, n, max, PC_HUD_CLOSE, w - 0.08f * u, y, r);
    } else if (mode == 4) {
        n = pc_touch_room_hud(out, n, max);
    } else {
        n = add_icon(out, n, max, PC_HUD_MENU, 0.08f * u, y, r);
        n = add_icon(out, n, max, PC_HUD_SAVE, 0.20f * u, y, r);
        n = add_icon(out, n, max, PC_HUD_PAD, 0.32f * u, y, r);
        n = add_icon(out, n, max, PC_HUD_BAG, w - 0.08f * u, y, r);
        n = add_icon(out, n, max, PC_HUD_MAP, w - 0.20f * u, y, r);
        if (play != NULL && furniture_icon_wanted(play)) {
            n = add_icon(out, n, max, PC_HUD_FURNITURE, w - 0.32f * u, y, r);
        }
        /* the tools: the one in hand, and the ones a step left and right */
        {
            int prev = tool_neighbour(-1);
            int next = tool_neighbour(1);
            mActor_name_t cur = Now_Private->equipment;
            int k = n;

            n = add_icon(out, n, max, PC_HUD_TOOL, w * 0.5f, y, 0.058f * u);
            if (k < n) {
                out[k].glyph = pc_touch_tool_glyph(cur, &out[k].gold);
                out[k].item = cur;
            }
            if (prev != -2) {
                k = n;
                n = add_icon(out, n, max, PC_HUD_TOOL_PREV, w * 0.5f - 0.125f * u, y, 0.04f * u);
                if (k < n) {
                    mActor_name_t it = prev >= 0 ? Now_Private->inventory.pockets[prev] : EMPTY_NO;

                    out[k].glyph = pc_touch_tool_glyph(it, &out[k].gold);
                    out[k].item = it;
                    out[k].index = prev;
                }
            }
            if (next != -2 && next != prev) {
                k = n;
                n = add_icon(out, n, max, PC_HUD_TOOL_NEXT, w * 0.5f + 0.125f * u, y, 0.04f * u);
                if (k < n) {
                    mActor_name_t it = next >= 0 ? Now_Private->inventory.pockets[next] : EMPTY_NO;

                    out[k].glyph = pc_touch_tool_glyph(it, &out[k].gold);
                    out[k].item = it;
                    out[k].index = next;
                }
            }
        }
    }
    for (i = 0; i < n; i++) {
        out[i].pressed = finger_on_icon(out[i].kind);
        if (out[i].kind == PC_HUD_PAD) {
            out[i].active = s_pad_wanted;
        }
    }
    return n;
}

int pc_touch_full_hud(PCTouchHudIcon* out, int max) {
    return build_hud(out, max);
}

/* The icon under a point (pixels), PC_HUD_KIND_COUNT if none; the tool
 * strip counts as one (its middle) */
static int icon_at(float px, float py, int* index) {
    PCTouchHudIcon icons[48];
    int n = build_hud(icons, 48);
    float u = shorter_side();
    int i;

    for (i = 0; i < n; i++) {
        float dx = px - icons[i].x * g_pc_window_w;
        float dy = py - icons[i].y * g_pc_window_h;
        float r = icons[i].r * u * 1.3f; /* a finger is not a pointer */

        if (dx * dx + dy * dy <= r * r) {
            *index = icons[i].index;
            return icons[i].kind;
        }
    }
    return PC_HUD_KIND_COUNT;
}

/* --- The pad ---------------------------------------------------------- */

int pc_touch_full_pad_shown(void) {
    GAME_PLAY* play;
    int menu;

    if (!pc_touch_full_on()) {
        return FALSE;
    }
    if (s_pad_wanted || g_pc_nes_active) {
        return TRUE;
    }
    if (pc_touch_room_on()) {
        return FALSE;
    }
    play = pc_touch_play();
    if (play == NULL) {
        return TRUE; /* the title, the save screens: START and the buttons */
    }
    menu = submenu_type(play);
    /* the menus other than the pockets and the map want buttons */
    return menu != mSM_OVL_NONE && menu != mSM_OVL_INVENTORY && menu != mSM_OVL_MAP;
}

/* --- Fingers ---------------------------------------------------------- */

static TFinger* find(s64 id) {
    int i;

    for (i = 0; i < MAX_FINGERS; i++) {
        if (s_fingers[i].active && s_fingers[i].id == id) {
            return &s_fingers[i];
        }
    }
    return NULL;
}

static TFinger* world_finger(void) {
    int i;

    for (i = 0; i < MAX_FINGERS; i++) {
        if (s_fingers[i].active && s_fingers[i].kind == FK_WORLD) {
            return &s_fingers[i];
        }
    }
    return NULL;
}

int pc_touch_full_finger_down(s64 id, float x, float y) {
    GAME_PLAY* play = pc_touch_play();
    TFinger* f = find(id);
    float px = x * g_pc_window_w, py = y * g_pc_window_h;
    int index = -1;
    int icon;
    int i;

    if (!pc_touch_full_on()) {
        return FALSE;
    }
    icon = icon_at(px, py, &index);
    if (pc_touch_full_pad_shown() && icon != PC_HUD_PAD) {
        return FALSE; /* the GameCube pad's */
    }
    for (i = 0; f == NULL && i < MAX_FINGERS; i++) {
        if (!s_fingers[i].active) {
            f = &s_fingers[i];
        }
    }
    if (f == NULL) {
        return TRUE;
    }
    memset(f, 0, sizeof(*f));
    f->active = 1;
    f->id = id;
    f->x = f->x0 = f->ax = px;
    f->y = f->y0 = f->ay = py;
    f->down_ms = SDL_GetTicks();

    if (icon == PC_HUD_FURN_SLOT) {
        f->kind = FK_ROOM; /* a piece in the pockets, to drag onto the floor */
        pc_touch_room_finger(0, f->id, x, y);
    } else if (icon == PC_HUD_TOOL || icon == PC_HUD_TOOL_PREV || icon == PC_HUD_TOOL_NEXT) {
        f->kind = FK_STRIP;
        f->hud = icon;
    } else if (icon != PC_HUD_KIND_COUNT) {
        f->kind = FK_HUD;
        f->hud = icon;
    } else if (pc_touch_room_on()) {
        f->kind = FK_ROOM;
        pc_touch_room_finger(0, f->id, x, y);
    } else if (play != NULL && (submenu_type(play) == mSM_OVL_INVENTORY || submenu_type(play) == mSM_OVL_MAP)) {
        f->kind = FK_MENU;
        pc_touch_menu_finger(0, f->id, x, y);
    } else if (pc_minimap_at(x, y) && pc_touch_player_free(play)) {
        f->kind = FK_HUD; /* the minimap: the map, as its icon */
        f->hud = PC_HUD_MAP;
    } else {
        /* one finger steers at a time: a new one takes over */
        for (i = 0; i < MAX_FINGERS; i++) {
            if (&s_fingers[i] != f && s_fingers[i].active && s_fingers[i].kind == FK_WORLD) {
                s_fingers[i].kind = FK_NONE;
            }
        }
        f->kind = FK_WORLD;
        s_go.kind = GO_NONE;
    }
    return TRUE;
}

void pc_touch_full_finger_move(s64 id, float x, float y) {
    TFinger* f = find(id);
    float u = shorter_side();

    if (f == NULL) {
        return;
    }
    f->x = x * g_pc_window_w;
    f->y = y * g_pc_window_h;
    if (!f->moved && (fabsf(f->x - f->x0) > TAP_SLOP * u || fabsf(f->y - f->y0) > TAP_SLOP * u)) {
        f->moved = 1;
    }
    if (f->kind == FK_MENU) {
        pc_touch_menu_finger(1, f->id, x, y);
    } else if (f->kind == FK_ROOM) {
        pc_touch_room_finger(1, f->id, x, y);
    } else if (f->kind == FK_WORLD && f->moved && !f->holding) {
        /* the anchor follows a finger dragged past the full tilt, so a turn
         * back takes effect at once */
        float dx = f->x - f->ax, dy = f->y - f->ay;
        float len = sqrtf(dx * dx + dy * dy);
        float reach = DRAG_REACH * u;

        if (len > reach) {
            f->ax = f->x - dx / len * reach;
            f->ay = f->y - dy / len * reach;
        }
    }
}

static void icon_tapped(int icon) {
    GAME_PLAY* play = pc_touch_play();

    switch (icon) {
        case PC_HUD_MENU:
            if (!g_pc_paused) {
                pc_pause_menu_toggle();
            }
            break;
        case PC_HUD_SAVE:
            pc_autosave_request_save();
            break;
        case PC_HUD_PAD:
            s_pad_wanted = !s_pad_wanted;
            break;
        case PC_HUD_BAG:
            /* START opens the pockets, under the game's own conditions */
            if (pc_touch_player_free(play)) {
                press(PAD_BUTTON_START, 0);
            }
            break;
        case PC_HUD_MAP:
            pc_minimap_request_toggle();
            break;
        case PC_HUD_CLOSE:
            pc_touch_menu_close();
            break;
        case PC_HUD_FURNITURE:
            pc_touch_room_toggle();
            break;
        case PC_HUD_FURN_DONE:
        case PC_HUD_FURN_ROT_L:
        case PC_HUD_FURN_ROT_R:
        case PC_HUD_FURN_STORE:
            pc_touch_room_icon(icon, -1);
            break;
        default:
            break;
    }
}

int pc_touch_full_finger_up(s64 id) {
    TFinger* f = find(id);
    Uint32 held;

    if (f == NULL) {
        return FALSE;
    }
    held = SDL_GetTicks() - f->down_ms;
    if (f->kind == FK_HUD) {
        int index = -1;

        if (icon_at(f->x, f->y, &index) == f->hud || (f->hud == PC_HUD_MAP && !f->moved)) {
            icon_tapped(f->hud);
        }
    } else if (f->kind == FK_STRIP) {
        float dx = f->x - f->x0;
        float u = shorter_side();

        if (fabsf(dx) > 0.05f * u) {
            s_tool_step = dx < 0.0f ? 1 : -1; /* a swipe left brings the next one in from the right */
        } else if (f->hud == PC_HUD_TOOL_PREV) {
            s_tool_step = -1;
        } else if (f->hud == PC_HUD_TOOL_NEXT) {
            s_tool_step = 1;
        }
    } else if (f->kind == FK_MENU) {
        pc_touch_menu_finger(2, f->id, f->x / g_pc_window_w, f->y / g_pc_window_h);
    } else if (f->kind == FK_ROOM) {
        pc_touch_room_finger(2, f->id, f->x / g_pc_window_w, f->y / g_pc_window_h);
    } else if (f->kind == FK_WORLD) {
        if (!f->moved && !f->holding && held <= TAP_MS) {
            s_tap_pending = 1;
            s_tap_x = f->x / g_pc_window_w;
            s_tap_y = f->y / g_pc_window_h;
        }
        s_stick_x = s_stick_y = 0;
        s_dash = 0;
    }
    f->active = 0;
    return TRUE;
}

/* --- Input for the game ------------------------------------------------- */

void pc_touch_full_read(u16* buttons, s8* stick_x, s8* stick_y) {
    if (!pc_touch_full_on() || g_pc_paused) {
        return;
    }
    if (s_stick_x != 0 || s_stick_y != 0) {
        *stick_x = s_stick_x;
        *stick_y = s_stick_y;
        if (s_dash) {
            *buttons |= PAD_TRIGGER_R; /* (R only runs: B would pick up, m_player_lib.c) */
        }
    }
    if (s_press != 0 && (s32)(pc_frame_counter - s_press_from) >= 0 &&
        (s32)(pc_frame_counter - s_press_from) < PRESS_FRAMES) {
        *buttons |= s_press;
    }
    pc_touch_menu_read(buttons, stick_x, stick_y);
}

/* --- The game frame ----------------------------------------------------- */

static void go_to(int kind, float x, float z, float stop, float reach, u16 button, ACTOR* actor) {
    s_go.kind = kind;
    s_go.x = x;
    s_go.z = z;
    s_go.stop = stop;
    s_go.reach = reach;
    s_go.button = button;
    s_go.actor = actor;
    s_go.start = pc_frame_counter;
    s_go.still = 0;
    s_go.lx = s_go.lz = 1e9f;
}

/* Turns the player to (x, z) at once and presses a button the frame after */
static void face_and_press(PLAYER_ACTOR* player, float x, float z, u16 button) {
    float dx = x - player->actor_class.world.position.x;
    float dz = z - player->actor_class.world.position.z;

    s_face_pending = 1;
    s_face_yaw = (dx * dx + dz * dz) > 1.0f ? yaw_to(dx, dz) : player->actor_class.shape_info.rotation.y;
    s_face_button = button;
}

static void tap(GAME_PLAY* play, float fx, float fy) {
    PLAYER_ACTOR* player = pc_touch_player(play);
    float px = fx * g_pc_window_w, py = fy * g_pc_window_h;
    ACTOR* npc;
    xyz_t ground;
    s8 kind;
    float dx, dz, dist;
    int index;
    float score;

    if (play == NULL || player == NULL) {
        return;
    }
    /* a message: on; a choice: that one */
    if (pc_touch_msg_open()) {
        mChoice_c* choice = mChoice_Get_base_window_p();
        int row = pc_touch_choice_at(fx, fy);

        if (row >= 0) {
            choice->selected_choice_idx = row;
            press(PAD_BUTTON_A, 1);
            printf("[TOUCH] choice %d\n", row);
        } else if (choice == NULL || choice->main_index == mChoice_MAIN_HIDE) {
            press(PAD_BUTTON_A, 0);
        }
        return;
    }
    if (play->submenu.process_status != mSM_PROCESS_WAIT) {
        return;
    }
    index = mPlib_get_player_actor_main_index((GAME*)play);
    if (index < mPlayer_INDEX_WAIT || index > mPlayer_INDEX_DASH) {
        /* busy (a rod in the water, a net held, a scene): A, as the game asks */
        press(PAD_BUTTON_A, 0);
        return;
    }
    /* the player: A (what is in front) */
    if (hit_actor(play, &player->actor_class, 34.0f, px, py, &score)) {
        press(PAD_BUTTON_A, 0);
        printf("[TOUCH] A\n");
        return;
    }
    /* a villager: up to them and talk */
    npc = npc_at(play, px, py);
    if (npc != NULL) {
        float stop = npc->talk_distance > 30.0f ? npc->talk_distance - 14.0f : 30.0f;

        if (stop > 42.0f) {
            stop = 42.0f;
        }
        go_to(GO_TALK, npc->world.position.x, npc->world.position.z, stop, npc->talk_distance - 4.0f, PAD_BUTTON_A,
              npc);
        printf("[TOUCH] talk to %04X\n", (unsigned)npc->npc_id);
        return;
    }
    if (!pc_touch_window_to_ground(play, fx, fy, &ground)) {
        return;
    }
    dx = ground.x - player->actor_class.world.position.x;
    dz = ground.z - player->actor_class.world.position.z;
    dist = sqrtf(dx * dx + dz * dz);
    kind = Now_Private->equipment != EMPTY_NO ? mPlib_Get_ItemNoToItemKind(Now_Private->equipment) : -1;

    /* the net swings and the rod casts where it is tapped, near enough */
    if ((mPlayer_ITEM_IS_NET(kind) && dist < 90.0f) || (mPlayer_ITEM_IS_ROD(kind) && dist < 170.0f)) {
        face_and_press(player, ground.x, ground.z, PAD_BUTTON_A);
        printf("[TOUCH] %s at %.0f,%.0f\n", mPlayer_ITEM_IS_NET(kind) ? "swing" : "cast", ground.x, ground.z);
        return;
    }
    {
        mActor_name_t* fg = mFI_GetUnitFG(ground);
        mActor_name_t item = fg != NULL ? *fg : EMPTY_NO;
        int in_room = mFI_IS_ROOM(mFI_GetFieldId());
        float ux = floorf(ground.x / 40.0f) * 40.0f + 20.0f;
        float uz = floorf(ground.z / 40.0f) * 40.0f + 20.0f;
        u16 button = item_button(item, in_room);

        if (button == 0 && mPlayer_ITEM_IS_SHOVEL(kind) && !in_room) {
            button = PAD_BUTTON_A; /* dig here */
        }
        if (button != 0) {
            /* stand a unit off it, facing it: B looks 10 to 35 ahead
             * (Search_putin_item), A the next unit (trees, the shovel) */
            float sx = player->actor_class.world.position.x - ux;
            float sz = player->actor_class.world.position.z - uz;
            float len = sqrtf(sx * sx + sz * sz);
            float off = button == PAD_BUTTON_B ? 22.0f : 34.0f;

            if (len < 1.0f) {
                sx = 0.0f;
                sz = 1.0f; /* on it: step back south */
                len = 1.0f;
            }
            go_to(GO_USE, ux + sx / len * off, uz + sz / len * off, 5.0f, 60.0f, button, NULL);
            s_go.ux = ux;
            s_go.uz = uz;
            printf("[TOUCH] %s %04X at %.0f,%.0f\n", button == PAD_BUTTON_B ? "pick up" : "use", (unsigned)item, ux,
                   uz);
            return;
        }
    }
    go_to(GO_WALK, ground.x, ground.z, 6.0f, 0.0f, 0, NULL);
    printf("[TOUCH] walk to %.0f,%.0f\n", ground.x, ground.z);
}

/* Steers toward s_go's point; at it (or stopped short within reach) faces
 * the target and presses */
static void go_step(GAME_PLAY* play, PLAYER_ACTOR* player) {
    float px = player->actor_class.world.position.x;
    float pz = player->actor_class.world.position.z;
    float tx = s_go.x, tz = s_go.z;
    float dx, dz, dist, moved, speed, mag;

    if (s_go.kind == GO_TALK) {
        if (s_go.actor == NULL || !npc_alive(play, s_go.actor)) {
            s_go.kind = GO_NONE;
            return;
        }
        tx = s_go.actor->world.position.x;
        tz = s_go.actor->world.position.z;
    }
    dx = tx - px;
    dz = tz - pz;
    dist = sqrtf(dx * dx + dz * dz);
    moved = fabsf(px - s_go.lx) + fabsf(pz - s_go.lz);
    s_go.still = moved < 0.4f ? s_go.still + 1 : 0;
    s_go.lx = px;
    s_go.lz = pz;

    if (dist <= s_go.stop || (s_go.still > 12 && s_go.kind != GO_WALK) ||
        (s_go.still > 20 && s_go.kind == GO_WALK) || pc_frame_counter - s_go.start > GO_GIVE_UP) {
        int kind = s_go.kind;

        s_go.kind = GO_NONE;
        s_stick_x = s_stick_y = 0;
        if (kind == GO_TALK) {
            if (dist <= s_go.reach) {
                face_and_press(player, tx, tz, PAD_BUTTON_A);
            }
        } else if (kind == GO_USE) {
            float ux = s_go.ux, uz = s_go.uz;
            float ex = ux - px, ez = uz - pz;

            if (sqrtf(ex * ex + ez * ez) <= s_go.reach) {
                face_and_press(player, ux, uz, s_go.button);
            }
        }
        return;
    }
    /* slow down near the end: the player slides on for a while */
    speed = dist * 0.3f;
    if (speed > WALK_SPEED) speed = WALK_SPEED;
    mag = 61.0f * speed / WALK_SPEED;
    if (dist > 160.0f) mag = STICK_FULL;
    if (mag < TOUCH_STICK_MIN) mag = TOUCH_STICK_MIN;
    stick_for_yaw(play, yaw_to(dx, dz), mag, &s_stick_x, &s_stick_y);
}

/* A finger held or dragged in the world */
static void world_finger_step(GAME_PLAY* play, PLAYER_ACTOR* player, TFinger* f) {
    float u = shorter_side();

    if (!f->moved && !f->holding && SDL_GetTicks() - f->down_ms >= HOLD_MS) {
        f->holding = 1;
    }
    if (f->holding) {
        /* runs to where the finger is held */
        xyz_t ground;
        float dx, dz, dist;

        if (!pc_touch_window_to_ground(play, f->x / g_pc_window_w, f->y / g_pc_window_h, &ground)) {
            s_stick_x = s_stick_y = 0;
            return;
        }
        dx = ground.x - player->actor_class.world.position.x;
        dz = ground.z - player->actor_class.world.position.z;
        dist = sqrtf(dx * dx + dz * dz);
        if (dist < 14.0f) {
            s_stick_x = s_stick_y = 0;
            s_dash = 0;
            return;
        }
        stick_for_yaw(play, yaw_to(dx, dz), STICK_FULL, &s_stick_x, &s_stick_y);
        s_dash = dist > 40.0f;
    } else if (f->moved) {
        /* runs the way the finger is dragged: screen up is the camera's
         * forward, as the stick's up */
        float dx = f->x - f->ax, dy = f->y - f->ay;
        float len = sqrtf(dx * dx + dy * dy);
        float mag;

        if (len < 0.01f * u) {
            s_stick_x = s_stick_y = 0;
            return;
        }
        mag = len / (DRAG_REACH * u);
        if (mag > 1.0f) mag = 1.0f;
        mag = TOUCH_STICK_MIN + (STICK_FULL - TOUCH_STICK_MIN) * mag;
        s_stick_x = (s8)lrintf(dx / len * mag);
        s_stick_y = (s8)lrintf(-dy / len * mag);
        s_dash = len > 0.5f * DRAG_REACH * u;
    }
}

void pc_touch_full_game_frame(GAME* game) {
    GAME_PLAY* play = game != NULL && game->exec == play_main ? (GAME_PLAY*)game : NULL;
    PLAYER_ACTOR* player = pc_touch_player(play);
    TFinger* f = world_finger();

    if (!pc_touch_full_on()) {
        s_pad_wanted = 0;
        return;
    }
    pc_touch_room_frame(play);
    pc_touch_menu_frame(play);
    if (play == NULL || player == NULL || g_pc_paused) {
        s_go.kind = GO_NONE;
        s_stick_x = s_stick_y = 0;
        s_dash = 0;
        s_tap_pending = 0;
        s_face_pending = 0;
        return;
    }

    /* a turn asked for last frame: now the press */
    if (s_face_pending) {
        s_xyz angle = { 0, s_face_yaw, 0 };

        s_face_pending = 0;
        if (pc_touch_player_free(play)) {
            player->Set_force_position_angle_proc((GAME*)play, NULL, &angle, mPlayer_FORCE_POSITION_ANGLE_ROTY);
            press(s_face_button, 1);
        }
    }

    if (s_tap_pending) {
        s_tap_pending = 0;
        tap(play, s_tap_x, s_tap_y);
    }

    if (s_tool_step != 0) {
        int dir = s_tool_step;

        s_tool_step = 0;
        if (tool_can_change(play)) {
            int slot = tool_neighbour(dir);

            if (slot != -2) {
                tool_take(slot, dir);
            }
        }
    }

    if (!pc_touch_player_free(play)) {
        if (s_go.kind != GO_NONE && (pc_touch_msg_open() || play->submenu.process_status != mSM_PROCESS_WAIT)) {
            s_go.kind = GO_NONE;
        }
        s_stick_x = s_stick_y = 0;
        s_dash = 0;
        return;
    }
    s_stick_x = s_stick_y = 0;
    s_dash = 0;
    if (f != NULL && !pc_touch_full_pad_shown()) {
        world_finger_step(play, player, f);
    } else if (s_go.kind != GO_NONE) {
        go_step(play, player);
    }
}
