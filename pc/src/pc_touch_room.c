/* pc_touch_room.c - the furniture mode of the full touch mode
 * (pc_touch_full.h)
 *
 * In one's own house (the player's room of the town one lives in) the
 * chair icon turns the mode on: the player stands, the floor shows its
 * units, and
 * - a piece of furniture is picked with a tap and moved with a drag (it
 *   snaps to the units; green where it fits, red where it does not);
 * - the turn icons turn the picked piece a quarter to the left or right;
 * - the satchel icon puts it in the pockets;
 * - the furniture in the pockets is in a row at the bottom: dragged onto the
 *   floor, a piece is put there, facing the room.
 * What a piece holds (a dresser's things, what stands on a table) goes with
 * it, as do a lamp's switch and a gyroid's step; a piece with things in or on
 * it is not put in the pockets. A piece fits where the game would let the
 * player push one: on the floor inside the walls, on no other piece, item or
 * door, not on the player, and no more pieces than the room builds.
 *
 * An edit is made on the save's floor when the room is quiet, by the room
 * itself (ac_my_room.c aMR_PC_SyncRoom): the furniture is taken down as the
 * room's destructor does, the edit is made here (pc_touch_room_apply_edits),
 * and the furniture is built again from the save as the constructor does. So
 * the house is as if the player had moved the piece, saved with the town. */
#include "pc_touch_internal.h"
#include "pc_settings.h"

#include "m_common_data.h"
#include "m_field_info.h"
#include "m_home_h.h"
#include "m_name_table.h"
#include "m_room_type.h"
#include "m_private.h"
#include "m_scene_table.h"
#include "m_house.h"
#include "m_mark_room.h"
#include "m_collision_bg.h"
#include "ac_my_room.h"
#include "m_ftr_def.h"
#include "m_item_name.h"
#include "m_font.h"
#include "charmap.h"

#include <math.h>
#include <string.h>

extern u32 pc_frame_counter;
extern int aMR_PC_FurnitureMax(void); /* ac_my_room.c */

#define UNITS 16
#define SLOT_MAX mPr_POCKETS_SLOT_COUNT

enum {
    OP_NONE,
    OP_MOVE,   /* the piece at (ax, az) to (nx, nz) */
    OP_TURN,   /* the piece at (ax, az) a quarter: dir -1 right, +1 left */
    OP_STORE,  /* the piece at (ax, az) into the pockets */
    OP_PLACE,  /* the pocket slot's piece to (nx, nz) */
};

static struct {
    int kind;
    int house, floor;
    int ax, az;
    int nx, nz;
    int dir;
    int slot;
    mActor_name_t item; /* OP_PLACE: the pocket's item, checked again */
} s_op;

static int s_on;
static int s_sel_x = -1, s_sel_z = -1; /* the picked piece's anchor unit */
static int s_msg;                       /* the last edit's answer, for the hint (0 none) */
static u32 s_msg_until;
static mActor_name_t s_name_item;       /* the piece whose name shows (a tap on it), until s_name_until */
static u32 s_name_until;

static void show_name(mActor_name_t item) {
    s_name_item = item;
    s_name_until = pc_frame_counter + 180;
}

/* A drag: a piece of the room, or one from the pockets */
static struct {
    int active;
    int moved;
    s64 id;
    int slot;            /* -1: a piece of the room */
    mActor_name_t ftr;   /* what is dragged (with its direction) */
    int ax, az;          /* the piece's anchor before the drag */
    int gx, gz;          /* the finger's unit when it landed, minus the anchor */
    int nx, nz;          /* where it would go */
    int ok;
    float fx0, fy0;
} s_drag;

/* The pockets' furniture as shown at the bottom (slot numbers) */
static int s_slots[SLOT_MAX];
static int s_slot_count;

enum {
    MSG_NONE,
    MSG_NO_ROOM,   /* it does not fit there */
    MSG_FULL,      /* the pockets are full */
    MSG_HOLDS,     /* there are things in or on it */
    MSG_TOO_MANY,  /* the room has all the pieces it can */
};

/* --- The room's data ---------------------------------------------------- */

static int room_ids(int* house, int* floor) {
    int field_id = mFI_GetFieldId();

    if (mFI_GET_TYPE(field_id) != mFI_FIELD_PLAYER0_ROOM) {
        return FALSE;
    }
    *house = mFI_GET_PLAYER_ROOM_NO(field_id);
    *floor = mFI_GetNowPlayerHouseFloorNo();
    return *floor >= 0 && *floor < 3;
}

static mHm_flr_c* room_floor(void) {
    int house, floor;

    if (!room_ids(&house, &floor)) {
        return NULL;
    }
    return &Save_Get(homes[house]).floors[floor];
}

static mHm_lyr_c* layer_of(mHm_flr_c* fl, int layer) {
    switch (layer) {
        case 0: return &fl->layer_main;
        case 1: return &fl->layer_secondary;
        case 2: return &fl->layer_storage1;
        default: return &fl->layer_storage2;
    }
}

/* The inside of the walls: units 1 to n, n by the room's size (the
 * templates of m_home.c and the main floor's rooms: 4 for the first room, 6
 * for the second and the upper floor, 8 for the larger ones and the
 * basement) */
static int room_bounds(mHm_flr_c* fl, int* x0, int* z0, int* x1, int* z1) {
    int scene = Save_Get(scene_no);
    int n;

    (void)fl;
    if (scene == SCENE_MY_ROOM_S) {
        n = 4;
    } else if (scene == SCENE_MY_ROOM_M || scene == SCENE_MY_ROOM_LL2) {
        n = 6;
    } else if (mSc_IS_SCENE_PLAYER_HOUSE_ROOM(scene)) {
        n = 8;
    } else {
        return FALSE;
    }
    *x0 = *z0 = 1;
    *x1 = *z1 = n;
    return TRUE;
}

/* The units a piece takes with its anchor there (n of them in out, the
 * anchor first: what a dresser holds is kept at its anchor) */
static int footprint(mActor_name_t ftr, int ax, int az, int out[mRmTp_FTR_UNIT_MAX][2]) {
    mRmTp_FtrPlaceInfoOne_t info[mRmTp_FTR_UNIT_MAX];
    int n = 1;
    int i;

    if (!ITEM_IS_FTR(ftr) || mRmTp_GetFurnitureData(ftr, ax, az, info) < 0) {
        return 0;
    }
    out[0][0] = ax;
    out[0][1] = az;
    for (i = 0; i < mRmTp_FTR_UNIT_MAX; i++) {
        if (info[i].exists && !(info[i].ut_x == ax && info[i].ut_z == az)) {
            out[n][0] = info[i].ut_x;
            out[n][1] = info[i].ut_z;
            n++;
        }
    }
    return n;
}

/* The anchor of the piece of layer 0 that takes this unit */
static int piece_at(mHm_flr_c* fl, int ux, int uz, int* ax, int* az) {
    int x, z;

    if (ux < 0 || ux >= UNITS || uz < 0 || uz >= UNITS) {
        return FALSE;
    }
    if (ITEM_IS_FTR(fl->layer_main.items[uz][ux])) {
        *ax = ux;
        *az = uz;
        return TRUE;
    }
    if (fl->layer_main.items[uz][ux] != RSV_FE1F) {
        return FALSE;
    }
    /* a piece's other units: the anchor is within one unit */
    for (z = uz - 1; z <= uz + 1; z++) {
        for (x = ux - 1; x <= ux + 1; x++) {
            int units[mRmTp_FTR_UNIT_MAX][2];
            int n, i;

            if (x < 0 || x >= UNITS || z < 0 || z >= UNITS || !ITEM_IS_FTR(fl->layer_main.items[z][x])) {
                continue;
            }
            n = footprint(fl->layer_main.items[z][x], x, z, units);
            for (i = 0; i < n; i++) {
                if (units[i][0] == ux && units[i][1] == uz) {
                    *ax = x;
                    *az = z;
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

static void player_unit(int* ux, int* uz) {
    PLAYER_ACTOR* player = pc_touch_player(pc_touch_play());

    *ux = *uz = -1;
    if (player != NULL) {
        *ux = (int)floorf(player->actor_class.world.position.x / 40.0f);
        *uz = (int)floorf(player->actor_class.world.position.z / 40.0f);
    }
}

/* Whether ftr fits with its anchor at (ax, az); the units in own (a piece
 * being moved) count as free */
static int fits(mHm_flr_c* fl, mActor_name_t ftr, int ax, int az, int own[][2], int own_n) {
    int units[mRmTp_FTR_UNIT_MAX][2];
    int n = footprint(ftr, ax, az, units);
    int x0, z0, x1, z1, px, pz;
    int i, k;

    if (n == 0 || !room_bounds(fl, &x0, &z0, &x1, &z1)) {
        return FALSE;
    }
    player_unit(&px, &pz);
    for (i = 0; i < n; i++) {
        int x = units[i][0], z = units[i][1];
        int mine = FALSE;

        if (x < x0 || x > x1 || z < z0 || z > z1) {
            return FALSE;
        }
        for (k = 0; k < own_n; k++) {
            mine = mine || (own[k][0] == x && own[k][1] == z);
        }
        if (!mine && fl->layer_main.items[z][x] != EMPTY_NO) {
            return FALSE;
        }
        if (x == px && z == pz) {
            return FALSE; /* not on the player */
        }
    }
    return TRUE;
}

static int piece_count(mHm_flr_c* fl) {
    int count = 0;
    int x, z;

    for (z = 0; z < UNITS; z++) {
        for (x = 0; x < UNITS; x++) {
            count += ITEM_IS_FTR(fl->layer_main.items[z][x]) ? 1 : 0;
            count += ITEM_IS_FTR(fl->layer_secondary.items[z][x]) ? 1 : 0;
        }
    }
    return count;
}

static int switch_bit(int x, int z) {
    return ((x - 1) + (z - 1) * 8) & 0x3F;
}

/* A gyroid's step (4 bits) and a switch (1 bit) of a unit, read and cleared */
static u32 take_state(mHm_lyr_c* lyr, int x, int z, int* sw) {
    u32 step = 0;

    *sw = 0;
    if (x >= 1 && x <= 8 && z >= 1 && z <= 8) {
        int shift = (x - 1) * 4;

        *sw = (lyr->ftr_switch >> switch_bit(x, z)) & 1;
        lyr->ftr_switch &= ~(1ull << switch_bit(x, z));
        step = (lyr->haniwa_step[z - 1] >> shift) & 0xF;
        lyr->haniwa_step[z - 1] &= ~(0xFu << shift);
    }
    return step;
}

static void put_state(mHm_lyr_c* lyr, int x, int z, int sw, u32 step) {
    if (x >= 1 && x <= 8 && z >= 1 && z <= 8) {
        int shift = (x - 1) * 4;

        lyr->ftr_switch &= ~(1ull << switch_bit(x, z));
        if (sw) {
            lyr->ftr_switch |= 1ull << switch_bit(x, z);
        }
        /* (the game leaves a step where a gyroid stood: cleared first) */
        lyr->haniwa_step[z - 1] &= ~(0xFu << shift);
        lyr->haniwa_step[z - 1] |= (step & 0xF) << shift;
    }
}

/* A pocket's item as the piece the game would put down facing the room
 * (ac_my_room_move.c_inc aMR_JudgeBreedNewFurniture: the stego skull and the
 * balloons a step more, the frog two), EMPTY_NO if it cannot be put down */
static mActor_name_t pocket_piece(mActor_name_t item) {
    mActor_name_t ftr = mRmTp_Item1ItemNo2FtrItemNo_AtPlayerRoom(item, TRUE);
    int idx, dir = 0;

    if (!ITEM_IS_FTR(ftr)) {
        return EMPTY_NO;
    }
    idx = mNT_ftr_item_no_to_ftr_idx(ftr);
    if (!aMR_check_valid_furniture(mNT_ftr_idx_to_ftr_item_no(idx, 0))) {
        return EMPTY_NO;
    }
    if (idx == FTR_DIN_STEGO_HEAD || (idx >= FTR_NOG_BALLOON_COMMON0 && idx <= FTR_NOG_BALLOON_COMMON7)) {
        dir = 1;
    } else if (idx == FTR_NOG_KAERU) {
        dir = 2;
    }
    return mNT_ftr_idx_to_ftr_item_no(idx, dir & 3);
}

/* --- Edits (made by the room, ac_my_room.c aMR_PC_SyncRoom) -------------- */

int pc_touch_room_edits_pending(int house, int floor) {
    return s_op.kind != OP_NONE && s_op.house == house && s_op.floor == floor;
}

static void say(int msg) {
    s_msg = msg;
    s_msg_until = pc_frame_counter + 150;
}

/* The piece at (ax, az) as new_ftr at (nx, nz), with what it holds and what
 * stands on it */
static int move_piece(mHm_flr_c* fl, int ax, int az, mActor_name_t new_ftr, int nx, int nz) {
    mActor_name_t ftr = fl->layer_main.items[az][ax];
    int old_units[mRmTp_FTR_UNIT_MAX][2];
    int new_units[mRmTp_FTR_UNIT_MAX][2];
    int old_n = footprint(ftr, ax, az, old_units);
    int new_n = footprint(new_ftr, nx, nz, new_units);
    mActor_name_t above[3][mRmTp_FTR_UNIT_MAX];
    int sw[4][mRmTp_FTR_UNIT_MAX];
    u32 step[4][mRmTp_FTR_UNIT_MAX];
    int i, l;

    if (old_n == 0 || new_n != old_n || !fits(fl, new_ftr, nx, nz, old_units, old_n)) {
        return FALSE;
    }
    /* take it up: the piece, what is above each of its units, the states */
    for (i = 0; i < old_n; i++) {
        int x = old_units[i][0], z = old_units[i][1];

        fl->layer_main.items[z][x] = EMPTY_NO;
        for (l = 1; l < 4; l++) {
            above[l - 1][i] = layer_of(fl, l)->items[z][x];
            layer_of(fl, l)->items[z][x] = EMPTY_NO;
        }
        for (l = 0; l < 2; l++) {
            step[l][i] = take_state(layer_of(fl, l), x, z, &sw[l][i]);
        }
    }
    /* and down again, unit for unit in the same order */
    for (i = 0; i < new_n; i++) {
        int x = new_units[i][0], z = new_units[i][1];

        fl->layer_main.items[z][x] = (x == nx && z == nz) ? new_ftr : RSV_FE1F;
        for (l = 1; l < 4; l++) {
            layer_of(fl, l)->items[z][x] = above[l - 1][i];
        }
        for (l = 0; l < 2; l++) {
            put_state(layer_of(fl, l), x, z, sw[l][i], step[l][i]);
        }
    }
    return TRUE;
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

void pc_touch_room_apply_edits(mHm_flr_c* fl) {
    int kind = s_op.kind;
    int ok = FALSE;

    s_op.kind = OP_NONE;
    if (fl == NULL) {
        return;
    }
    switch (kind) {
        case OP_MOVE:
        case OP_TURN: {
            mActor_name_t ftr = fl->layer_main.items[s_op.az][s_op.ax];
            mActor_name_t new_ftr = ftr;
            int nx = s_op.ax, nz = s_op.az;

            if (!ITEM_IS_FTR(ftr)) {
                break;
            }
            if (kind == OP_TURN) {
                new_ftr = (mActor_name_t)((ftr & ~3) | ((FTR_GET_ROTATION(ftr) + s_op.dir) & 3));
            } else {
                nx = s_op.nx;
                nz = s_op.nz;
            }
            ok = move_piece(fl, s_op.ax, s_op.az, new_ftr, nx, nz);
            if (ok) {
                s_sel_x = nx;
                s_sel_z = nz;
            } else {
                say(MSG_NO_ROOM);
            }
            break;
        }
        case OP_STORE: {
            mActor_name_t ftr = fl->layer_main.items[s_op.az][s_op.ax];
            int units[mRmTp_FTR_UNIT_MAX][2];
            int n = footprint(ftr, s_op.ax, s_op.az, units);
            int slot = free_pocket();
            int i, l, holds = FALSE;

            if (n == 0) {
                break;
            }
            for (i = 0; i < n; i++) {
                for (l = 1; l < 4; l++) {
                    holds = holds || layer_of(fl, l)->items[units[i][1]][units[i][0]] != EMPTY_NO;
                }
            }
            if (holds) {
                say(MSG_HOLDS);
                break;
            }
            if (slot < 0) {
                say(MSG_FULL);
                break;
            }
            for (i = 0; i < n; i++) {
                int sw;

                fl->layer_main.items[units[i][1]][units[i][0]] = EMPTY_NO;
                take_state(&fl->layer_main, units[i][0], units[i][1], &sw);
            }
            mPr_SetPossessionItem(Now_Private, slot, mRmTp_FtrItemNo2Item1ItemNo((mActor_name_t)(ftr & ~3), TRUE),
                                  mPr_ITEM_COND_NORMAL);
            s_sel_x = s_sel_z = -1;
            ok = TRUE;
            break;
        }
        case OP_PLACE: {
            mActor_name_t item = Now_Private->inventory.pockets[s_op.slot];
            mActor_name_t ftr = pocket_piece(item);
            int units[mRmTp_FTR_UNIT_MAX][2];
            int n, i;

            if (item != s_op.item ||
                mPr_CHK_ITEM_COND(Now_Private->inventory.item_conditions, s_op.slot) != mPr_ITEM_COND_NORMAL ||
                !ITEM_IS_FTR(ftr)) {
                break;
            }
            if (piece_count(fl) >= aMR_PC_FurnitureMax()) {
                say(MSG_TOO_MANY);
                break;
            }
            if (!fits(fl, ftr, s_op.nx, s_op.nz, NULL, 0)) {
                say(MSG_NO_ROOM);
                break;
            }
            n = footprint(ftr, s_op.nx, s_op.nz, units);
            for (i = 0; i < n; i++) {
                fl->layer_main.items[units[i][1]][units[i][0]] = RSV_FE1F;
            }
            fl->layer_main.items[s_op.nz][s_op.nx] = ftr;
            mPr_SetPossessionItem(Now_Private, s_op.slot, EMPTY_NO, mPr_ITEM_COND_NORMAL);
            s_sel_x = s_op.nx;
            s_sel_z = s_op.nz;
            ok = TRUE;
            break;
        }
        default:
            break;
    }
    if (ok) {
        mMkRm_ReportChangePlayerRoom(); /* (the room's rating looks again) */
        s_msg = MSG_NONE;
    }
    printf("[TOUCH] furniture: edit %d %s\n", kind, ok ? "made" : "refused");
}

static void queue(int kind) {
    int house, floor;

    if (!room_ids(&house, &floor)) {
        return;
    }
    s_op.kind = kind;
    s_op.house = house;
    s_op.floor = floor;
}

/* --- The mode ---------------------------------------------------------- */

int pc_touch_room_available(GAME_PLAY* play) {
    int house, floor;

    if (!pc_touch_full_on() || play == NULL || !mSc_IS_SCENE_PLAYER_HOUSE_ROOM(Save_Get(scene_no)) ||
        Common_Get(field_type) != mFI_FIELDTYPE2_PLAYER_ROOM || !room_ids(&house, &floor)) {
        return FALSE;
    }
    /* (the player's own house in the town they live in: a guest is a
     * foreigner in the host's town, so never there, m_field_make.c
     * set_field_type) */
    return TRUE;
}

int pc_touch_room_on(void) {
    return s_on;
}

int pc_touch_full_furniture_mode(void) {
    return s_on;
}

void pc_touch_room_toggle(void) {
    GAME_PLAY* play = pc_touch_play();

    if (s_on) {
        s_on = 0;
        s_op.kind = OP_NONE;
    } else if (pc_touch_room_available(play) && pc_touch_player_free(play)) {
        s_on = 1;
        s_sel_x = s_sel_z = -1;
        memset(&s_drag, 0, sizeof(s_drag));
        s_msg = MSG_NONE;
    }
    printf("[TOUCH] furniture mode %s\n", s_on ? "on" : "off");
}

void pc_touch_room_frame(GAME_PLAY* play) {
    mHm_flr_c* fl;
    int slot;

    if (!s_on) {
        s_op.kind = OP_NONE;
        return;
    }
    if (!pc_touch_room_available(play)) {
        s_on = 0; /* left the room */
        s_op.kind = OP_NONE;
        return;
    }
    fl = room_floor();
    /* the picked piece is still there */
    if (fl != NULL && s_sel_x >= 0 && !ITEM_IS_FTR(fl->layer_main.items[s_sel_z][s_sel_x])) {
        s_sel_x = s_sel_z = -1;
    }
    /* the pockets' furniture */
    s_slot_count = 0;
    for (slot = 0; slot < mPr_POCKETS_SLOT_COUNT; slot++) {
        mActor_name_t item = Now_Private->inventory.pockets[slot];

        if (item != EMPTY_NO &&
            mPr_CHK_ITEM_COND(Now_Private->inventory.item_conditions, slot) == mPr_ITEM_COND_NORMAL &&
            pocket_piece(item) != EMPTY_NO) {
            s_slots[s_slot_count++] = slot;
        }
    }
}

/* --- Geometry ---------------------------------------------------------- */

static float shorter_side(void) {
    return (float)(g_pc_window_w < g_pc_window_h ? g_pc_window_w : g_pc_window_h);
}

static void slot_place(int i, float* x, float* y, float* r) {
    float u = shorter_side();
    float gap = 0.125f * u;
    float w = gap * (s_slot_count - 1);

    *r = 0.05f * u;
    *x = g_pc_window_w * 0.5f - w * 0.5f + gap * i;
    *y = g_pc_window_h - 0.09f * u;
}

static int slot_at(float px, float py) {
    int i;

    for (i = 0; i < s_slot_count; i++) {
        float x, y, r;

        slot_place(i, &x, &y, &r);
        if (fabsf(px - x) <= r * 1.2f && fabsf(py - y) <= r * 1.2f) {
            return i;
        }
    }
    return -1;
}

/* The unit under a window point (fractions) */
static int unit_at(float fx, float fy, int* ux, int* uz) {
    GAME_PLAY* play = pc_touch_play();
    xyz_t p;

    if (play == NULL || !pc_touch_window_to_ground(play, fx, fy, &p)) {
        return FALSE;
    }
    *ux = (int)floorf(p.x / 40.0f);
    *uz = (int)floorf(p.z / 40.0f);
    return *ux >= 0 && *ux < UNITS && *uz >= 0 && *uz < UNITS;
}

static float floor_y(void) {
    PLAYER_ACTOR* player = pc_touch_player(pc_touch_play());

    return player != NULL ? player->actor_class.world.position.y : 0.0f;
}

/* Four corners of the units x0..x1, z0..z1 on the window */
static int rect_corners(int x0, int z0, int x1, int z1, float out[8]) {
    GAME_PLAY* play = pc_touch_play();
    xyz_t c[4];
    int i;

    if (play == NULL) {
        return FALSE;
    }
    c[0] = (xyz_t){ x0 * 40.0f, floor_y(), z0 * 40.0f };
    c[1] = (xyz_t){ (x1 + 1) * 40.0f, floor_y(), z0 * 40.0f };
    c[2] = (xyz_t){ (x1 + 1) * 40.0f, floor_y(), (z1 + 1) * 40.0f };
    c[3] = (xyz_t){ x0 * 40.0f, floor_y(), (z1 + 1) * 40.0f };
    for (i = 0; i < 4; i++) {
        if (!pc_touch_world_to_window(play, &c[i], &out[i * 2], &out[i * 2 + 1])) {
            return FALSE;
        }
    }
    return TRUE;
}

static int piece_rect(mActor_name_t ftr, int ax, int az, float out[8]) {
    int units[mRmTp_FTR_UNIT_MAX][2];
    int n = footprint(ftr, ax, az, units);
    int x0 = UNITS, z0 = UNITS, x1 = -1, z1 = -1;
    int i;

    for (i = 0; i < n; i++) {
        if (units[i][0] < x0) x0 = units[i][0];
        if (units[i][0] > x1) x1 = units[i][0];
        if (units[i][1] < z0) z0 = units[i][1];
        if (units[i][1] > z1) z1 = units[i][1];
    }
    return n > 0 && rect_corners(x0, z0, x1, z1, out);
}

int pc_touch_full_furniture_outline(float corners[8], int* ok) {
    mHm_flr_c* fl = room_floor();

    if (!s_on || fl == NULL) {
        return FALSE;
    }
    if (s_drag.active && s_drag.moved) {
        *ok = s_drag.ok;
        return piece_rect(s_drag.ftr, s_drag.nx, s_drag.nz, corners);
    }
    if (s_sel_x >= 0) {
        *ok = TRUE;
        return piece_rect(fl->layer_main.items[s_sel_z][s_sel_x], s_sel_x, s_sel_z, corners);
    }
    return FALSE;
}

int pc_touch_full_furniture_floor(float corners[8], int* units_x, int* units_z) {
    mHm_flr_c* fl = room_floor();
    int x0, z0, x1, z1;

    if (!s_on || fl == NULL || !room_bounds(fl, &x0, &z0, &x1, &z1)) {
        return FALSE;
    }
    *units_x = x1 - x0 + 1;
    *units_z = z1 - z0 + 1;
    return rect_corners(x0, z0, x1, z1, corners);
}

const char* pc_touch_full_furniture_hint(int korean) {
    if (!s_on) {
        return "";
    }
    if (s_msg != MSG_NONE && (s32)(s_msg_until - pc_frame_counter) > 0) {
        switch (s_msg) {
            case MSG_NO_ROOM: return korean ? "거기에는 놓을 수 없어요" : "It does not fit there";
            case MSG_FULL: return korean ? "주머니가 가득 찼어요" : "The pockets are full";
            case MSG_HOLDS: return korean ? "안이나 위에 물건이 있어요" : "There are things in or on it";
            case MSG_TOO_MANY: return korean ? "이 방에는 더 놓을 수 없어요" : "The room has all the furniture it can hold";
            default: break;
        }
    }
    if (s_sel_x >= 0) {
        return korean ? "끌어서 옮기기 · 돌리기 · 주머니에 넣기" : "Drag to move it, turn it, or put it in the pockets";
    }
    if (s_slot_count > 0) {
        return korean ? "가구를 눌러 고르기 · 아래 주머니의 가구를 바닥으로 끌어 놓기"
                      : "Tap a piece to pick it; drag one from the pockets below onto the floor";
    }
    return korean ? "가구를 눌러 고르기" : "Tap a piece of furniture to pick it";
}

/* --- The icons and fingers ----------------------------------------------- */

static int add(PCTouchHudIcon* out, int n, int max, int kind, float px, float py, float pr) {
    if (n >= max) {
        return n;
    }
    memset(&out[n], 0, sizeof(out[n]));
    out[n].kind = kind;
    out[n].x = px / (float)g_pc_window_w;
    out[n].y = py / (float)g_pc_window_h;
    out[n].r = pr / shorter_side();
    return n + 1;
}

int pc_touch_room_hud(PCTouchHudIcon* out, int n, int max) {
    float u = shorter_side();
    float w = (float)g_pc_window_w;
    float y = 0.085f * u;
    float r = 0.05f * u;
    int i;

    n = add(out, n, max, PC_HUD_FURN_DONE, w - 0.08f * u, y, r);
    if (s_sel_x >= 0) {
        n = add(out, n, max, PC_HUD_FURN_ROT_L, 0.08f * u, y, r);
        n = add(out, n, max, PC_HUD_FURN_ROT_R, 0.20f * u, y, r);
        n = add(out, n, max, PC_HUD_FURN_STORE, 0.32f * u, y, r);
    }
    for (i = 0; i < s_slot_count; i++) {
        float x, sy, sr;
        int k = n;

        slot_place(i, &x, &sy, &sr);
        n = add(out, n, max, PC_HUD_FURN_SLOT, x, sy, sr);
        if (k < n) {
            out[k].index = s_slots[i];
            out[k].item = Now_Private->inventory.pockets[s_slots[i]];
            out[k].glyph = PC_GLYPH_FURNITURE;
            out[k].active = s_drag.active && s_drag.slot == s_slots[i];
        }
    }
    return n;
}

void pc_touch_room_icon(int icon, int index) {
    (void)index;
    if (icon == PC_HUD_FURN_DONE) {
        s_on = 0;
        s_op.kind = OP_NONE; /* (an edit still waiting for a quiet room is dropped with the mode) */
        printf("[TOUCH] furniture mode off\n");
        return;
    }
    if (s_sel_x < 0 || s_op.kind != OP_NONE) {
        return;
    }
    s_op.ax = s_sel_x;
    s_op.az = s_sel_z;
    if (icon == PC_HUD_FURN_ROT_L || icon == PC_HUD_FURN_ROT_R) {
        s_op.dir = icon == PC_HUD_FURN_ROT_L ? 1 : -1;
        queue(OP_TURN);
    } else if (icon == PC_HUD_FURN_STORE) {
        queue(OP_STORE);
    }
}

static void drag_update(float fx, float fy) {
    mHm_flr_c* fl = room_floor();
    int ux, uz;

    if (fl == NULL || !unit_at(fx, fy, &ux, &uz)) {
        s_drag.ok = FALSE;
        return;
    }
    s_drag.nx = ux - s_drag.gx;
    s_drag.nz = uz - s_drag.gz;
    if (s_drag.slot >= 0) {
        s_drag.ok = fits(fl, s_drag.ftr, s_drag.nx, s_drag.nz, NULL, 0) && piece_count(fl) < aMR_PC_FurnitureMax();
    } else {
        int own[mRmTp_FTR_UNIT_MAX][2];
        int own_n = footprint(s_drag.ftr, s_drag.ax, s_drag.az, own);

        s_drag.ok = fits(fl, s_drag.ftr, s_drag.nx, s_drag.nz, own, own_n);
    }
}

void pc_touch_room_finger(int kind, s64 id, float x, float y) {
    mHm_flr_c* fl = room_floor();
    float px = x * g_pc_window_w, py = y * g_pc_window_h;

    if (!s_on || fl == NULL) {
        return;
    }
    if (kind == 0) {
        int i = slot_at(px, py);
        int ux, uz, ax, az;

        memset(&s_drag, 0, sizeof(s_drag));
        s_drag.id = id;
        s_drag.fx0 = x;
        s_drag.fy0 = y;
        if (i >= 0) {
            /* from the pockets: it hangs under the finger, facing the room */
            s_drag.active = 1;
            s_drag.slot = s_slots[i];
            s_drag.ftr = pocket_piece(Now_Private->inventory.pockets[s_drag.slot]);
            show_name(Now_Private->inventory.pockets[s_drag.slot]);
            return;
        }
        if (unit_at(x, y, &ux, &uz) && piece_at(fl, ux, uz, &ax, &az)) {
            s_sel_x = ax;
            s_sel_z = az;
            s_drag.active = 1;
            s_drag.slot = -1;
            s_drag.ftr = fl->layer_main.items[az][ax];
            show_name((mActor_name_t)(s_drag.ftr & ~3));
            s_drag.ax = ax;
            s_drag.az = az;
            s_drag.gx = ux - ax;
            s_drag.gz = uz - az;
            s_drag.nx = ax;
            s_drag.nz = az;
            s_drag.ok = TRUE;
        } else {
            s_sel_x = s_sel_z = -1; /* a tap on the floor: nothing picked */
        }
        return;
    }
    if (!s_drag.active || id != s_drag.id) {
        return;
    }
    if (kind == 1) {
        float u = shorter_side();

        if (!s_drag.moved && (fabsf(px - s_drag.fx0 * g_pc_window_w) > 0.03f * u ||
                              fabsf(py - s_drag.fy0 * g_pc_window_h) > 0.03f * u)) {
            s_drag.moved = 1;
        }
        if (s_drag.moved) {
            drag_update(x, y);
        }
        if (s_name_item != EMPTY_NO) {
            s_name_until = pc_frame_counter + 120; /* (shown while it is carried) */
        }
        return;
    }
    /* up */
    if (s_drag.moved) {
        drag_update(x, y);
        if (!s_drag.ok) {
            say(MSG_NO_ROOM);
        } else if (s_op.kind == OP_NONE) {
            if (s_drag.slot >= 0) {
                s_op.slot = s_drag.slot;
                s_op.item = Now_Private->inventory.pockets[s_drag.slot];
                s_op.nx = s_drag.nx;
                s_op.nz = s_drag.nz;
                queue(OP_PLACE);
            } else if (s_drag.nx != s_drag.ax || s_drag.nz != s_drag.az) {
                s_op.ax = s_drag.ax;
                s_op.az = s_drag.az;
                s_op.nx = s_drag.nx;
                s_op.nz = s_drag.nz;
                queue(OP_MOVE);
            }
        }
    }
    s_drag.active = 0;
}

/* The picked piece's name over the pockets' row, in the game's own font (so
 * as the game writes it, Korean patch or not); called by the room's draw
 * (ac_my_room_draw.c_inc) */
void pc_touch_room_draw(GAME* game) {
    u8 name[mIN_ITEM_NAME_LEN];
    int len = mIN_ITEM_NAME_LEN;
    GRAPH* graph;
    f32 scale = 1.0f;
    f32 width, x, y;
    static const int ofs[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    int i;

    if (!s_on || game == NULL || s_name_item == EMPTY_NO || (s32)(s_name_until - pc_frame_counter) <= 0) {
        return;
    }
    mIN_copy_name_str(name, s_name_item);
    while (len > 0 && name[len - 1] == CHAR_SPACE) {
        len--;
    }
    if (len == 0) {
        return;
    }
    graph = game->graph;
    width = mFont_GetStringWidth(name, len, TRUE) * scale;
    x = 160.0f - width * 0.5f;
    y = 182.0f; /* just above the pockets' row (pc_touch_room_hud) */
    mFont_SetMatrix(graph, mFont_MODE_FONT);
    for (i = 0; i < 4; i++) {
        mFont_SetLineStrings(game, name, len, x + ofs[i][0], y + ofs[i][1], 40, 30, 20, 255, FALSE, FALSE, scale, scale,
                             mFont_MODE_FONT);
    }
    mFont_SetLineStrings(game, name, len, x, y, 255, 250, 230, 255, FALSE, FALSE, scale, scale, mFont_MODE_FONT);
    mFont_UnSetMatrix(graph, mFont_MODE_FONT);
}
