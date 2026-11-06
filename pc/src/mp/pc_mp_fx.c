/* pc_mp_fx.c - the multiplayer: what a player's actions look and sound like,
 * shown to the others where the player's puppet stands.
 *
 * The game's player actor makes its effects as it moves: dust and dirt
 * (the effect clip), a tree shaking (the effect BG), a hole growing or being
 * filled, an item flying out of the ground, falling from a tree or from the
 * hand (the bg_item clip), and its sounds (sAdo_OngenTrgStart). While the
 * local player's move runs (afe-decomp m_player.c pc_mp_fx_player) the calls
 * it makes through those clips are recorded here (the clips' procs are
 * wrapped) and sent to the room (pc_mp_game.c G_FX); a game that has the
 * sender's puppet in the same place makes the same calls with the same
 * arguments, the local player standing for the sender for the moment of the
 * call (some effects take the player's position). The item layer changes
 * come from the sender's game as before (G_ITEM...), so what these calls
 * write is taken back at once and an entry they start (a hole, a drop) is
 * only shown (afe-decomp bg_item pc_visual, m_field_info.c mFI_pc_fg_hold). */

#include "pc_platform.h"
#include "pc_mp_game.h"

#include "game.h"
#include "m_actor.h"
#include "m_common_data.h"
#include "m_field_info.h"
#include "m_name_table.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "bg_item_h.h"
#include "ef_effect_control.h"
#include "ac_effectbg.h"
#include "audio.h"

#include <stdlib.h>
#include <string.h>

int pc_mp_fx_player = 0;    /* afe-decomp m_player.c: the local player's move is running */
int pc_mp_fx_replaying = 0; /* pc_mp_game.c: item writes now are not a change to share */
extern int mFI_pc_fg_hold;
extern int mFI_pc_fx_visual;

static int s_active = 0; /* in a room */
static int s_depth = 0;  /* inside a wrapped call (what it makes itself is part of it) */

enum {
    FX_EFFECT = 1,
    FX_EFFECT_BG,
    FX_DIG_HOLE,
    FX_BURY_HOLE,
    FX_FLY,
    FX_FRUIT_DROP,
    FX_PLAYER_DROP,
    FX_DROP_V1,
    FX_SOUND,
};

#define FX_HEAD 15 /* kind, the player's position and facing */

/* --- Writing and reading a record --- */

static u8* put_f(u8* p, f32 v) {
    memcpy(p, &v, 4);
    return p + 4;
}
static u8* put_16(u8* p, u16 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    return p + 2;
}
static u8* put_pos(u8* p, const xyz_t* v) {
    p = put_f(p, v->x);
    p = put_f(p, v->y);
    return put_f(p, v->z);
}
static const u8* get_f(const u8* p, f32* v) {
    memcpy(v, p, 4);
    return p + 4;
}
static const u8* get_16(const u8* p, u16* v) {
    *v = (u16)(p[0] | (p[1] << 8));
    return p + 2;
}
static const u8* get_pos(const u8* p, xyz_t* v) {
    p = get_f(p, &v->x);
    p = get_f(p, &v->y);
    return get_f(p, &v->z);
}

/* the start of a record, when the local player's move makes it */
static u8* begin(u8* buf, int kind) {
    GAME_PLAY* play = (GAME_PLAY*)gamePT;
    PLAYER_ACTOR* pl;
    u8* p = buf;

    if (!s_active || pc_mp_fx_player <= 0 || s_depth != 0 || pc_mp_fx_replaying || play == NULL ||
        (pl = GET_PLAYER_ACTOR(play)) == NULL) {
        return NULL;
    }
    *p++ = (u8)kind;
    p = put_pos(p, &pl->actor_class.world.position);
    return put_16(p, (u16)pl->actor_class.shape_info.rotation.y);
}

static void send(const u8* buf, const u8* end) {
    pc_mp_game_fx_out(buf, (int)(end - buf));
}

/* --- The wrapped procs --- */

static eEC_NAME2EFFECTMAKE_PROC s_effect_make;
static EffectBG_MAKE_EFFECTBG_PROC s_effect_bg;
static bIT_DIG_HOLE_EFFECT_ENTRY_PROC s_dig_hole;
static bIT_BURY_HOLE_EFFECT_ENTRY_PROC s_bury_hole;
static bIT_FLY_ENTRY_PROC s_fly;
static bIT_ITEM_TREE_FRUIT_DROP_PROC s_fruit_drop;
static bIT_PLAYER_DROP_ENTRY_PROC s_player_drop;
static bIT_DROP_ENTRY_V1_PROC s_drop_v1;

static void fx_effect_make(int id, xyz_t pos, int prio, s16 angle, GAME* game, u16 item, s16 arg0, s16 arg1) {
    u8 buf[64];
    u8* p;

    /* (an arrow and sweat the player's game takes away again itself) */
    if (id != eEC_EFFECT_YAJIRUSHI && id != eEC_EFFECT_ASE2 && (p = begin(buf, FX_EFFECT)) != NULL) {
        p = put_16(p, (u16)id);
        p = put_pos(p, &pos);
        *p++ = (u8)prio;
        p = put_16(p, (u16)angle);
        p = put_16(p, item);
        p = put_16(p, (u16)arg0);
        p = put_16(p, (u16)arg1);
        send(buf, p);
    }
    s_depth++;
    s_effect_make(id, pos, prio, angle, game, item, arg0, arg1);
    s_depth--;
}

static void fx_effect_bg(GAME* game, s16 type, s16 variant, xyz_t* pos) {
    u8 buf[64];
    u8* p;

    if (pos != NULL && (p = begin(buf, FX_EFFECT_BG)) != NULL) {
        p = put_16(p, (u16)type);
        p = put_16(p, (u16)variant);
        p = put_pos(p, pos);
        send(buf, p);
    }
    s_depth++;
    s_effect_bg(game, type, variant, pos);
    s_depth--;
}

static int fx_dig_hole(mActor_name_t item, xyz_t* pos, u16 frames, u16 wait) {
    u8 buf[64];
    u8* p;
    int res;

    if (pos != NULL && (p = begin(buf, FX_DIG_HOLE)) != NULL) {
        p = put_16(p, item);
        p = put_pos(p, pos);
        p = put_16(p, frames);
        p = put_16(p, wait);
        send(buf, p);
    }
    s_depth++;
    res = s_dig_hole(item, pos, frames, wait);
    s_depth--;
    return res;
}

static void fx_bury_hole(mActor_name_t item, xyz_t* pos, u16 frames) {
    u8 buf[64];
    u8* p;

    if (pos != NULL && (p = begin(buf, FX_BURY_HOLE)) != NULL) {
        p = put_16(p, item);
        p = put_pos(p, pos);
        p = put_16(p, frames);
        send(buf, p);
    }
    s_depth++;
    s_bury_hole(item, pos, frames);
    s_depth--;
}

static void fx_fly(mActor_name_t item, xyz_t* pos, s16 angle) {
    u8 buf[64];
    u8* p;

    if (pos != NULL && (p = begin(buf, FX_FLY)) != NULL) {
        p = put_16(p, item);
        p = put_pos(p, pos);
        p = put_16(p, (u16)angle);
        send(buf, p);
    }
    s_depth++;
    s_fly(item, pos, angle);
    s_depth--;
}

static void fx_fruit_drop(mActor_name_t item, int ut_x, int ut_z, xyz_t* drop_pos) {
    u8 buf[64];
    u8* p;

    /* (a bees' tree: the bees are each game's own) */
    if (!IS_ITEM_BEE_TREE(item) && (p = begin(buf, FX_FRUIT_DROP)) != NULL) {
        p = put_16(p, item);
        p = put_16(p, (u16)ut_x);
        p = put_16(p, (u16)ut_z);
        send(buf, p);
    }
    s_depth++;
    s_fruit_drop(item, ut_x, ut_z, drop_pos);
    s_depth--;
}

static int fx_player_drop(GAME* game, mActor_name_t item, int ut_x, int ut_z, s16 layer) {
    u8 buf[64];
    u8* p;
    int res;

    if ((p = begin(buf, FX_PLAYER_DROP)) != NULL) {
        p = put_16(p, item);
        p = put_16(p, (u16)ut_x);
        p = put_16(p, (u16)ut_z);
        p = put_16(p, (u16)layer);
        send(buf, p);
    }
    s_depth++;
    res = s_player_drop(game, item, ut_x, ut_z, layer);
    s_depth--;
    return res;
}

static int fx_drop_v1(mActor_name_t item, xyz_t* pos, xyz_t* target) {
    u8 buf[64];
    u8* p;
    int res;

    if (pos != NULL && target != NULL && (p = begin(buf, FX_DROP_V1)) != NULL) {
        p = put_16(p, item);
        p = put_pos(p, pos);
        p = put_pos(p, target);
        send(buf, p);
    }
    s_depth++;
    res = s_drop_v1(item, pos, target);
    s_depth--;
    return res;
}

/* afe-decomp audio.c: a sound the local player's move plays */
void pc_mp_fx_sound(u16 id, const xyz_t* pos) {
    u8 buf[64];
    u8* p;

    if (pos != NULL && (p = begin(buf, FX_SOUND)) != NULL) {
        p = put_16(p, id);
        p = put_pos(p, pos);
        send(buf, p);
    }
}

#define WRAP(slot, mine, orig)                                                                                         \
    do {                                                                                                               \
        if ((slot) != NULL && (slot) != (mine)) {                                                                      \
            (orig) = (slot);                                                                                           \
            (slot) = (mine);                                                                                           \
        }                                                                                                              \
    } while (0)

void pc_mp_fx_frame(int active) {
    s_active = active;
    if (CLIP(effect_clip) != NULL) {
        WRAP(CLIP(effect_clip)->effect_make_proc, fx_effect_make, s_effect_make);
    }
    WRAP(CLIP(make_effect_bg_proc), fx_effect_bg, s_effect_bg);
    if (CLIP(bg_item_clip) != NULL) {
        bIT_Clip_c* c = CLIP(bg_item_clip);

        WRAP(c->dig_hole_effect_entry_proc, fx_dig_hole, s_dig_hole);
        WRAP(c->bury_hole_effect_entry_proc, fx_bury_hole, s_bury_hole);
        WRAP(c->fly_entry_proc, fx_fly, s_fly);
        WRAP(c->item_tree_fruit_drop_proc, fx_fruit_drop, s_fruit_drop);
        WRAP(c->player_drop_entry_proc, fx_player_drop, s_player_drop);
        WRAP(c->drop_entry_v1_proc, fx_drop_v1, s_drop_v1);
    }
}

/* --- Showing another player's --- */

/* the items of the field as they are (what a replayed call writes is taken
 * back: those changes come from the sender's game) */
#define SNAP_BLOCKS 160 /* (the town's acres with their border and the island's) */

typedef struct {
    u16* fg[SNAP_BLOCKS];
    u16* dep[SNAP_BLOCKS];
    u16 fg_copy[SNAP_BLOCKS][UT_TOTAL_NUM];
    u16 dep_copy[SNAP_BLOCKS][UT_Z_NUM];
    int n;
} Snapshot;

static Snapshot* s_snap = NULL;

static void snap_take(void) {
    int xm = mFI_GetBlockXMax(), zm = mFI_GetBlockZMax();

    if (s_snap == NULL && (s_snap = (Snapshot*)malloc(sizeof(Snapshot))) == NULL) return;
    s_snap->n = 0;
    for (int bz = 0; bz < zm; bz++) {
        for (int bx = 0; bx < xm && s_snap->n < SNAP_BLOCKS; bx++) {
            u16* fg = mFI_BkNumtoUtFGTop(bx, bz);
            u16* dep = mFI_GetDepositP(bx, bz);
            int i = s_snap->n;

            if (fg == NULL && dep == NULL) continue;
            s_snap->fg[i] = fg;
            s_snap->dep[i] = dep;
            if (fg != NULL) memcpy(s_snap->fg_copy[i], fg, sizeof(s_snap->fg_copy[i]));
            if (dep != NULL) memcpy(s_snap->dep_copy[i], dep, sizeof(s_snap->dep_copy[i]));
            s_snap->n++;
        }
    }
}

static void snap_put_back(void) {
    int changed = 0;

    if (s_snap == NULL) return;
    for (int i = 0; i < s_snap->n; i++) {
        if (s_snap->fg[i] != NULL && memcmp(s_snap->fg[i], s_snap->fg_copy[i], sizeof(s_snap->fg_copy[i])) != 0) {
            memcpy(s_snap->fg[i], s_snap->fg_copy[i], sizeof(s_snap->fg_copy[i]));
            changed = 1;
        }
        if (s_snap->dep[i] != NULL) memcpy(s_snap->dep[i], s_snap->dep_copy[i], sizeof(s_snap->dep_copy[i]));
    }
    if (changed) mFI_SetFGUpData();
}

void pc_mp_fx_replay(GAME_PLAY* play, const u8* m, int len) {
    PLAYER_ACTOR* pl = play != NULL ? GET_PLAYER_ACTOR(play) : NULL;
    const u8* p = m + 1;
    const u8* end = m + len;
    xyz_t ppos, pos, target, keep_pos;
    u16 a, b, c, d, e, f, g;
    s16 keep_rot, keep_angle;
    int kind, writes;

    if (pl == NULL || len < FX_HEAD) return;
    kind = m[0];
    p = get_pos(p, &ppos);
    p = get_16(p, &a);
    keep_pos = pl->actor_class.world.position;
    keep_rot = pl->actor_class.shape_info.rotation.y;
    keep_angle = pl->actor_class.world.angle.y;
    /* (the call takes the sender's place: some effects start from the player) */
    pl->actor_class.world.position = ppos;
    pl->actor_class.shape_info.rotation.y = (s16)a;
    pl->actor_class.world.angle.y = (s16)a;
    writes = kind >= FX_DIG_HOLE && kind <= FX_DROP_V1;
    pc_mp_fx_replaying = 1;
    s_depth++;
    if (writes) {
        snap_take();
        mFI_pc_fx_visual++;
    }

    switch (kind) {
        case FX_EFFECT:
            if (p + 2 + 12 + 1 + 8 > end || s_effect_make == NULL) break;
            p = get_16(p, &a);
            p = get_pos(p, &pos);
            g = *p++;
            p = get_16(p, &b);
            p = get_16(p, &c);
            p = get_16(p, &d);
            p = get_16(p, &e);
            if (a < eEC_EFFECT_NUM) s_effect_make(a, pos, g, (s16)b, (GAME*)play, c, (s16)d, (s16)e);
            break;
        case FX_EFFECT_BG:
            if (p + 4 + 12 > end || s_effect_bg == NULL) break;
            p = get_16(p, &a);
            p = get_16(p, &b);
            p = get_pos(p, &pos);
            s_effect_bg((GAME*)play, (s16)a, (s16)b, &pos);
            break;
        case FX_DIG_HOLE:
            if (p + 2 + 12 + 4 > end || s_dig_hole == NULL) break;
            p = get_16(p, &a);
            p = get_pos(p, &pos);
            p = get_16(p, &b);
            p = get_16(p, &c);
            s_dig_hole(a, &pos, b, c);
            break;
        case FX_BURY_HOLE:
            if (p + 2 + 12 + 2 > end || s_bury_hole == NULL) break;
            p = get_16(p, &a);
            p = get_pos(p, &pos);
            p = get_16(p, &b);
            s_bury_hole(a, &pos, b);
            break;
        case FX_FLY:
            if (p + 2 + 12 + 2 > end || s_fly == NULL) break;
            p = get_16(p, &a);
            p = get_pos(p, &pos);
            p = get_16(p, &b);
            s_fly(a, &pos, (s16)b);
            break;
        case FX_FRUIT_DROP: {
            xyz_t drop_pos;

            if (p + 6 > end || s_fruit_drop == NULL) break;
            p = get_16(p, &a);
            p = get_16(p, &b);
            p = get_16(p, &c);
            if (!IS_ITEM_BEE_TREE(a)) s_fruit_drop(a, (s16)b, (s16)c, &drop_pos);
            break;
        }
        case FX_PLAYER_DROP:
            if (p + 8 > end || s_player_drop == NULL) break;
            p = get_16(p, &a);
            p = get_16(p, &b);
            p = get_16(p, &c);
            p = get_16(p, &f);
            s_player_drop((GAME*)play, a, (s16)b, (s16)c, (s16)f);
            break;
        case FX_DROP_V1:
            if (p + 2 + 24 > end || s_drop_v1 == NULL) break;
            p = get_16(p, &a);
            p = get_pos(p, &pos);
            p = get_pos(p, &target);
            s_drop_v1(a, &pos, &target);
            break;
        case FX_SOUND:
            if (p + 2 + 12 > end) break;
            p = get_16(p, &a);
            p = get_pos(p, &pos);
            sAdo_OngenTrgStart(a, &pos);
            break;
        default:
            break;
    }

    if (writes) {
        mFI_pc_fx_visual--;
        snap_put_back();
    }
    s_depth--;
    pc_mp_fx_replaying = 0;
    pl->actor_class.world.position = keep_pos;
    pl->actor_class.shape_info.rotation.y = keep_rot;
    pl->actor_class.world.angle.y = keep_angle;
}
