/* pc_mp_puppet.c - the other players of a multiplayer room, as actors
 *
 * An actor of its own (profile slot 185, unused by the game, registered in
 * afe-decomp's m_actor_dlftbls.c under TARGET_PC) that draws the player's
 * model as the inventory screen does: the boy's or girl's skeleton, the
 * standing/walking/running animations, and the face (eyes, mouth, skin) and
 * clothing textures of that player's look, loaded into its own buffers. It
 * follows the positions pc_mp_game.c receives, a little smoothed. It is not
 * in the PLAYER part (the game takes the first actor there for "the"
 * player); it has no collision. */
#include "m_actor.h"
#include "m_actor_shadow.h"
#include "m_common_data.h"
#include "m_lib.h"
#include "m_malloc.h"
#include "m_name_table.h"
#include "m_play.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_rcp.h"
#include "sys_matrix.h"
#include "c_keyframe.h"
#include "libforest/gbi_extensions.h"

#include "pc_mp.h"
#include "pc_mp_game.h"

extern void mPlib_PC_LoadFace(int sex, int face_type, int sunburn_rank, u8* tex_p, u16* pal_p);
extern void mPlib_Load_PlayerTexAndPallet(void* tex_p, void* pal_p, int idx);
extern cKF_Skeleton_R_c cKF_bs_r_boy_1;
extern cKF_Skeleton_R_c cKF_bs_r_grl_1;

typedef struct {
    ACTOR actor_class;
    int slot;
    cKF_SkeletonInfo_R_c kf;
    s_xyz work[mPlayer_JOINT_NUM + 1];
    s_xyz morph[mPlayer_JOINT_NUM + 1];
    int anim;
    int gender;
    PcMpLook look;
    int look_loaded;
    u8* face_tex;  /* 0xE00 */
    u16* face_pal; /* 0x20 */
    u8* cloth_tex; /* 0x200 */
    u16* cloth_pal;
    int blink;
} PC_MP_PLAYER_ACTOR;

static void Pc_Mp_Player_ct(ACTOR* actorx, GAME* game);
static void Pc_Mp_Player_dt(ACTOR* actorx, GAME* game);
static void Pc_Mp_Player_move(ACTOR* actorx, GAME* game);
static void Pc_Mp_Player_draw(ACTOR* actorx, GAME* game);

ACTOR_PROFILE Pc_Mp_Player_Profile = {
    mAc_PROFILE_185,
    ACTOR_PART_CONTROL,
    ACTOR_STATE_NO_DRAW_WHILE_CULLED | ACTOR_STATE_NO_MOVE_WHILE_CULLED,
    EMPTY_NO,
    ACTOR_OBJ_BANK_KEEP,
    sizeof(PC_MP_PLAYER_ACTOR),
    &Pc_Mp_Player_ct,
    &Pc_Mp_Player_dt,
    &Pc_Mp_Player_move,
    &Pc_Mp_Player_draw,
    NULL,
};

/* 0 still, 1 walk, 2 run, 3 dash (pc_mp_game.c) */
static int anim_index(int anim) {
    switch (anim) {
        case 1: return mPlayer_ANIM_WALK1;
        case 2: return mPlayer_ANIM_RUN1;
        case 3: return mPlayer_ANIM_DASH1;
        default: return mPlayer_ANIM_WAIT1;
    }
}

static void set_skeleton(PC_MP_PLAYER_ACTOR* p, int gender) {
    p->gender = gender;
    cKF_SkeletonInfo_R_ct(&p->kf, gender == mPr_SEX_MALE ? &cKF_bs_r_boy_1 : &cKF_bs_r_grl_1, NULL, p->work,
                          p->morph);
    p->anim = -1;
}

static void set_anim(PC_MP_PLAYER_ACTOR* p, int anim) {
    if (anim == p->anim) {
        return;
    }
    cKF_SkeletonInfo_R_init_standard_repeat_morph(&p->kf, mPlib_Get_Pointer_Animation(anim_index(anim)), NULL,
                                                  p->anim < 0 ? 0.0f : -5.0f);
    p->anim = anim;
}

static void load_look(PC_MP_PLAYER_ACTOR* p, const PcMpLook* look) {
    int cloth = ITEM_IS_CLOTH(look->cloth) ? look->cloth - ITM_CLOTH_START : 0;

    if (cloth < 0 || cloth > CLOTH_NUM) {
        cloth = 0; /* a design of their own: its picture is not here */
    }
    if (look->gender != p->gender || !p->look_loaded) {
        set_skeleton(p, look->gender == mPr_SEX_MALE ? mPr_SEX_MALE : mPr_SEX_FEMALE);
    }
    mPlib_PC_LoadFace(p->gender, look->face % mPr_FACE_TYPE_NUM, look->tan, p->face_tex, p->face_pal);
    mPlib_Load_PlayerTexAndPallet(p->cloth_tex, p->cloth_pal, cloth);
    p->look = *look;
    p->look_loaded = 1;
}

static void Pc_Mp_Player_ct(ACTOR* actorx, GAME* game) {
    PC_MP_PLAYER_ACTOR* p = (PC_MP_PLAYER_ACTOR*)actorx;
    PcMpState st;
    PcMpLook look;

    p->slot = actorx->actor_specific;
    p->face_tex = (u8*)zelda_malloc_align(0xE00 + 0x20 + 0x200 + 0x20, 32);
    if (p->face_tex == NULL) {
        Actor_delete(actorx);
        return;
    }
    p->face_pal = (u16*)(p->face_tex + 0xE00);
    p->cloth_tex = p->face_tex + 0xE20;
    p->cloth_pal = (u16*)(p->face_tex + 0x1020);
    p->gender = -1;
    actorx->scale.x = actorx->scale.y = actorx->scale.z = 0.01f;
    Shape_Info_init(actorx, 0.0f, &mAc_ActorShadowCircle, 18.0f, 18.0f);
    actorx->shape_info.ofs_y = 200.0f;
    if (pc_mp_game_puppet_target(p->slot, &st, &look)) {
        load_look(p, &look);
        actorx->world.position.x = st.x;
        actorx->world.position.y = st.y;
        actorx->world.position.z = st.z;
        actorx->shape_info.rotation.y = st.angle;
        actorx->world.angle.y = st.angle;
    } else {
        set_skeleton(p, mPr_SEX_MALE);
    }
    set_anim(p, 0);
    (void)game;
}

static void Pc_Mp_Player_dt(ACTOR* actorx, GAME* game) {
    PC_MP_PLAYER_ACTOR* p = (PC_MP_PLAYER_ACTOR*)actorx;

    cKF_SkeletonInfo_R_dt(&p->kf);
    if (p->face_tex != NULL) {
        zelda_free(p->face_tex);
        p->face_tex = NULL;
    }
    pc_mp_game_puppet_gone(p->slot, actorx);
    (void)game;
}

static void Pc_Mp_Player_move(ACTOR* actorx, GAME* game) {
    PC_MP_PLAYER_ACTOR* p = (PC_MP_PLAYER_ACTOR*)actorx;
    xyz_t* pos = &actorx->world.position;
    PcMpState st;
    PcMpLook look;
    f32 dx, dy, dz;

    if (!pc_mp_game_puppet_target(p->slot, &st, &look)) {
        Actor_delete(actorx);
        return;
    }
    if (!p->look_loaded || memcmp(&look, &p->look, sizeof(look)) != 0) {
        load_look(p, &look);
    }
    /* follow, a little behind: positions come 15 times a second */
    dx = st.x - pos->x;
    dy = st.y - pos->y;
    dz = st.z - pos->z;
    if (dx * dx + dz * dz > 200.0f * 200.0f) {
        pos->x = st.x;
        pos->y = st.y;
        pos->z = st.z;
    } else {
        pos->x += dx * 0.35f;
        pos->y += dy * 0.35f;
        pos->z += dz * 0.35f;
    }
    {
        s16 diff = (s16)(st.angle - actorx->shape_info.rotation.y);

        actorx->shape_info.rotation.y += (s16)(diff / 3);
        actorx->world.angle.y = actorx->shape_info.rotation.y;
    }
    set_anim(p, st.anim);
    cKF_SkeletonInfo_R_play(&p->kf);
    if (p->blink > 0) {
        p->blink--;
    } else if ((game->frame_counter % 173) == (u32)(p->slot * 31) % 173) {
        p->blink = 6;
    }
}

static void Pc_Mp_Player_draw(ACTOR* actorx, GAME* game) {
    PC_MP_PLAYER_ACTOR* p = (PC_MP_PLAYER_ACTOR*)actorx;
    GRAPH* graph = game->graph;
    Mtx* mtx;
    int eye = p->blink > 3 ? 2 : p->blink > 0 ? 1 : 0;

    if (p->face_tex == NULL || !p->look_loaded) {
        return;
    }
    mtx = (Mtx*)GRAPH_ALLOC_TYPE(graph, Mtx, p->kf.skeleton->num_shown_joints);
    if (mtx == NULL) {
        return;
    }
    _texture_z_light_fog_prim(graph);
    OPEN_POLY_OPA_DISP(graph);
    gSPSegment(POLY_OPA_DISP++, ANIME_1_TXT_SEG, p->face_tex + eye * 0x100);
    gSPSegment(POLY_OPA_DISP++, ANIME_2_TXT_SEG, p->face_tex + mPlayer_EYE_TEX_NUM * 0x100);
    gSPSegment(POLY_OPA_DISP++, ANIME_3_TXT_SEG, p->cloth_tex);
    gSPSegment(POLY_OPA_DISP++, ANIME_4_TXT_SEG, p->cloth_pal);
    gSPSegment(POLY_OPA_DISP++, ANIME_5_TXT_SEG, p->face_pal);
    CLOSE_POLY_OPA_DISP(graph);
    cKF_Si3_draw_R_SV(game, &p->kf, mtx, NULL, NULL, NULL);

    /* where the name goes, above the head */
    {
        xyz_t head = actorx->world.position;
        xyz_t screen;

        head.y += 62.0f;
        Game_play_Projection_Trans((GAME_PLAY*)game, &head, &screen);
        pc_mp_game_puppet_screen(p->slot, screen.x, screen.y, screen.z);
    }
}
