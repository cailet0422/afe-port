/* pc_mp_puppet.c - the other players of a multiplayer room, as actors
 *
 * An actor of its own (profile slot 185, unused by the game, registered in
 * afe-decomp's m_actor_dlftbls.c under TARGET_PC) that draws the player's
 * model as the inventory screen does: the boy's or girl's skeleton playing
 * the player's animation (the arms holding a tool with the game's part
 * tables), the face (eyes, mouth, skin) and clothing textures of that
 * player's look, loaded into its own buffers, and the tool or item in its
 * hand drawn as the player actor draws it (m_player_item_*.c_inc). It
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
#include "m_needlework.h"
#include "m_field_info.h"
#include "bg_item_h.h"
#include "ac_shop_goods_h.h"
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
    cKF_SkeletonInfo_R_c kf1; /* the arms holding something (shares work and morph) */
    s8 part_table[mPlayer_JOINT_NUM + 1];
    int part_idx;
    int anim1;
    cKF_SkeletonInfo_R_c item_kf; /* a net, rod, balloon or pinwheel */
    s_xyz item_work[8];
    s_xyz item_morph[8];
    int item_shape; /* of item_kf, -1 none */
    int item_anim;
    MtxF hand_mtx; /* at the right hand, as drawn */
    xyz_t hand_pos;
    int hand_ok;
    PcMpState st;
    s_xyz work[mPlayer_JOINT_NUM + 1];
    s_xyz morph[mPlayer_JOINT_NUM + 1];
    int anim;
    int gender;
    PcMpLook look;
    int look_loaded;
    unsigned int design_gen; /* of the design worn, as loaded */
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
    cKF_Skeleton_R_c* skel = gender == mPr_SEX_MALE ? &cKF_bs_r_boy_1 : &cKF_bs_r_grl_1;

    p->gender = gender;
    cKF_SkeletonInfo_R_ct(&p->kf, skel, NULL, p->work, p->morph);
    cKF_SkeletonInfo_R_ct(&p->kf1, skel, NULL, p->work, p->morph);
    p->anim = -1;
    p->anim1 = -1;
    p->part_idx = -1;
}

static int frame_in(const cKF_SkeletonInfo_R_c* kf, float frame) {
    return frame >= kf->frame_control.start_frame && frame <= kf->frame_control.end_frame;
}

/* the arms: the animation for the item in hand on the joints of its part
 * table (the legs walk while the arm holds the net) */
static void set_upper(PC_MP_PLAYER_ACTOR* p, const PcMpState* st) {
    int anim1 = st->anim1 != 0xFF && mPlib_Get_Pointer_Animation(st->anim1) != NULL ? st->anim1 : -1;
    int part = anim1 >= 0 ? st->part : mPlayer_PART_TABLE_NORMAL;

    if (part != p->part_idx) {
        mPlib_DMA_player_Part_Table(p->part_table, part);
        p->part_idx = part;
    }
    if (anim1 < 0) {
        p->anim1 = -1;
        return;
    }
    if (anim1 != p->anim1) {
        cKF_SkeletonInfo_R_init_standard_repeat_morph(&p->kf1, mPlib_Get_Pointer_Animation(anim1), NULL,
                                                      p->anim1 < 0 ? 0.0f : -5.0f);
        p->anim1 = anim1;
        if (frame_in(&p->kf1, st->frame1)) p->kf1.frame_control.current_frame = st->frame1;
    } else if (frame_in(&p->kf1, st->frame1)) {
        float d = st->frame1 - p->kf1.frame_control.current_frame;

        if (d > 4.0f || d < -4.0f) p->kf1.frame_control.current_frame = st->frame1;
    }
}

/* the item's own skeleton and animation (a net's swing, a rod, a balloon) */
static void set_item(PC_MP_PLAYER_ACTOR* p, const PcMpState* st) {
    int shape = st->item_main != 0 && st->item_shape != 0xFF ? st->item_shape : -1;
    int anim = st->item_anim != 0xFF ? st->item_anim : -1;

    if (shape < 0 || mPlib_Get_Item_DataPointerType(shape) == mPlayer_ITEM_DATA_TYPE_GFX ||
        mPlib_Get_Item_DataPointer(shape) == NULL) {
        p->item_shape = -1;
        return;
    }
    if (shape != p->item_shape) {
        cKF_SkeletonInfo_R_ct(&p->item_kf, (cKF_Skeleton_R_c*)mPlib_Get_Item_DataPointer(shape), NULL, p->item_work,
                              p->item_morph);
        p->item_shape = shape;
        p->item_anim = -1;
    }
    if (anim < 0 || mPlib_Get_Item_DataPointerType(anim) == mPlayer_ITEM_DATA_TYPE_GFX ||
        mPlib_Get_Item_DataPointerType(anim) == mPlayer_ITEM_DATA_TYPE_SKELETON ||
        mPlib_Get_Item_DataPointer(anim) == NULL) {
        return; /* (the skeleton as it stands) */
    }
    if (anim != p->item_anim) {
        cKF_SkeletonInfo_R_init_standard_setframeandspeedandmorphandmode(
            &p->item_kf, (cKF_Animation_R_c*)mPlib_Get_Item_DataPointer(anim), NULL, 1.0f, 1.0f, 0.0f,
            cKF_FRAMECONTROL_REPEAT);
        p->item_anim = anim;
        if (frame_in(&p->item_kf, st->item_frame)) p->item_kf.frame_control.current_frame = st->item_frame;
    } else if (frame_in(&p->item_kf, st->item_frame)) {
        float d = st->item_frame - p->item_kf.frame_control.current_frame;

        if (d > 4.0f || d < -4.0f) p->item_kf.frame_control.current_frame = st->item_frame;
    }
    cKF_SkeletonInfo_R_play(&p->item_kf);
}

/* the animation the player plays (anim0, an mPlayer_ANIM_*: picking up,
 * shaking a tree, fishing...), at its frame; or still/walk/run/dash */
static void set_anim(PC_MP_PLAYER_ACTOR* p, int anim, int anim0, float frame) {
    int idx = (anim0 >= 0 && anim0 < mPlayer_ANIM_NUM && mPlib_Get_Pointer_Animation(anim0) != NULL)
                  ? anim0
                  : (anim0 = -1, anim_index(anim));

    if (idx != p->anim) {
        cKF_SkeletonInfo_R_init_standard_repeat_morph(&p->kf, mPlib_Get_Pointer_Animation(idx), NULL,
                                                      p->anim < 0 ? 0.0f : -5.0f);
        p->anim = idx;
        if (anim0 >= 0 && frame >= p->kf.frame_control.start_frame && frame <= p->kf.frame_control.end_frame) {
            p->kf.frame_control.current_frame = frame;
        }
    } else if (anim0 >= 0 && frame >= p->kf.frame_control.start_frame &&
               frame <= p->kf.frame_control.end_frame) {
        float d = frame - p->kf.frame_control.current_frame;

        /* the same animation a little ahead or behind: catch up (positions
         * come 15 times a second, the frame with them) */
        if (d > 4.0f || d < -4.0f) {
            p->kf.frame_control.current_frame = frame;
        }
    }
}

static void load_look(PC_MP_PLAYER_ACTOR* p, const PcMpLook* look) {
    int cloth = ITEM_IS_CLOTH(look->cloth) ? look->cloth - ITM_CLOTH_START : 0;
    int design = (look->cloth & 0xFF00) == PC_MP_LOOK_DESIGN;
    unsigned char palette;
    const unsigned char* tex;

    if (cloth < 0 || cloth > CLOTH_NUM) {
        cloth = 0;
    }
    if (look->gender != p->gender || !p->look_loaded) {
        set_skeleton(p, look->gender == mPr_SEX_MALE ? mPr_SEX_MALE : mPr_SEX_FEMALE);
    }
    mPlib_PC_LoadFace(p->gender, look->face % mPr_FACE_TYPE_NUM, look->tan, p->face_tex, p->face_pal);
    if (design && pc_mp_game_design(p->slot, look->cloth & 7, &palette, &tex, &p->design_gen)) {
        /* a design of their own, as they sent it (the palette is one of the 16 fixed ones) */
        memcpy(p->cloth_tex, tex, mNW_DESIGN_TEX_SIZE);
        memcpy(p->cloth_pal, mNW_PaletteIdx2Palette(palette), mNW_PALETTE_SIZE);
    } else {
        mPlib_Load_PlayerTexAndPallet(p->cloth_tex, p->cloth_pal, design ? 0 : cloth);
    }
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
    p->item_shape = -1;
    p->item_anim = -1;
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
    set_anim(p, 0, -1, 0.0f);
    (void)game;
}

static void Pc_Mp_Player_dt(ACTOR* actorx, GAME* game) {
    PC_MP_PLAYER_ACTOR* p = (PC_MP_PLAYER_ACTOR*)actorx;

    cKF_SkeletonInfo_R_dt(&p->kf);
    cKF_SkeletonInfo_R_dt(&p->kf1);
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
    {
        /* a design they wear that they changed (or that came after the look) */
        unsigned char palette;
        const unsigned char* tex;
        unsigned int gen = p->design_gen;

        if ((look.cloth & 0xFF00) == PC_MP_LOOK_DESIGN &&
            pc_mp_game_design(p->slot, look.cloth & 7, &palette, &tex, &gen) && gen != p->design_gen) {
            p->look_loaded = 0;
        }
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
    set_anim(p, st.anim, st.anim0, st.frame);
    set_upper(p, &st);
    if (p->anim1 >= 0) {
        cKF_SkeletonInfo_R_combine_play(&p->kf, &p->kf1, p->part_table);
    } else {
        cKF_SkeletonInfo_R_play(&p->kf);
    }
    set_item(p, &st);
    p->st = st;
    if (p->blink > 0) {
        p->blink--;
    } else if ((game->frame_counter % 173) == (u32)(p->slot * 31) % 173) {
        p->blink = 6;
    }
}

/* --- The item in hand (as m_player_item_*.c_inc and ac_t_umbrella.c) --- */

static int after_body(GAME* game, cKF_SkeletonInfo_R_c* kf, int joint_idx, Gfx** joint_shape, u8* joint_flags,
                      void* arg, s_xyz* joint_rot, xyz_t* joint_pos) {
    if (joint_idx == mPlayer_JOINT_HAND) {
        PC_MP_PLAYER_ACTOR* p = (PC_MP_PLAYER_ACTOR*)arg;

        Matrix_get(&p->hand_mtx);
        Matrix_Position_Zero(&p->hand_pos);
        p->hand_ok = 1;
    }
    return TRUE;
}

static int after_net(GAME* game, cKF_SkeletonInfo_R_c* kf, int joint_idx, Gfx** joint_shape, u8* joint_flags,
                     void* arg, s_xyz* joint_rot, xyz_t* joint_pos) {
    if (joint_idx == 3) {
        const PC_MP_PLAYER_ACTOR* p = (const PC_MP_PLAYER_ACTOR*)arg;

        Matrix_rotateXYZ(p->st.item_rot[0], p->st.item_rot[1], p->st.item_rot[2], MTX_MULT);
    }
    return TRUE;
}

static int balloon_edge(GAME* game, int joint_idx, int alpha) {
    if (joint_idx >= 1 && joint_idx <= 3) {
        OPEN_POLY_OPA_DISP(game->graph);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetTexEdgeAlpha(POLY_OPA_DISP++, alpha);
        CLOSE_POLY_OPA_DISP(game->graph);
    }
    return TRUE;
}

static int before_balloon(GAME* game, cKF_SkeletonInfo_R_c* kf, int joint_idx, Gfx** joint_shape, u8* joint_flags,
                          void* arg, s_xyz* joint_rot, xyz_t* joint_pos) {
    return balloon_edge(game, joint_idx, 80);
}

static int after_balloon(GAME* game, cKF_SkeletonInfo_R_c* kf, int joint_idx, Gfx** joint_shape, u8* joint_flags,
                         void* arg, s_xyz* joint_rot, xyz_t* joint_pos) {
    return balloon_edge(game, joint_idx, 144);
}

static void draw_item_skeleton(PC_MP_PLAYER_ACTOR* p, GAME* game, cKF_draw_callback before,
                               cKF_draw_callback after) {
    Mtx* mtx;

    if (p->item_shape < 0) return;
    mtx = (Mtx*)GRAPH_ALLOC_TYPE(game->graph, Mtx, p->item_kf.skeleton->num_shown_joints);
    if (mtx != NULL) cKF_Si3_draw_R_SV(game, &p->item_kf, mtx, before, after, p);
}

#define UMB(n) extern Gfx e_umb##n##_model[]; extern Gfx kasa_umb##n##_model[];
UMB(01) UMB(02) UMB(03) UMB(04) UMB(05) UMB(06) UMB(07) UMB(08) UMB(09) UMB(10) UMB(11) UMB(12) UMB(13) UMB(14)
UMB(15) UMB(16) UMB(17) UMB(18) UMB(19) UMB(20) UMB(21) UMB(22) UMB(23) UMB(24) UMB(25) UMB(26) UMB(27) UMB(28)
UMB(29) UMB(30) UMB(31) UMB(32) UMB(_w)
#undef UMB

static void draw_umbrella(PC_MP_PLAYER_ACTOR* p, GAME* game) {
#define U(n) { e_umb##n##_model, kasa_umb##n##_model }
    static Gfx* const models[33][2] = {
        U(01), U(02), U(03), U(04), U(05), U(06), U(07), U(08), U(09), U(10), U(11),
        U(12), U(13), U(14), U(15), U(16), U(17), U(18), U(19), U(20), U(21), U(22),
        U(23), U(24), U(25), U(26), U(27), U(28), U(29), U(30), U(31), U(32), U(_w),
    };
#undef U
    GRAPH* graph = game->graph;
    int idx = (int)p->st.item - mPlayer_ITEM_KIND_UMBRELLA00;
    unsigned char palette = 0;
    const unsigned char* tex = NULL;
    unsigned int gen;

    if (idx < 0 || idx >= 40) return;
    if (idx >= 32) {
        /* one of their designs: as they sent it; a plain umbrella until it came */
        if (pc_mp_game_design(p->slot, (idx - 32) & 7, &palette, &tex, &gen)) {
            idx = 32;
        } else {
            idx = 0;
        }
    }
    _texture_z_light_fog_prim_npc(graph);
    OPEN_POLY_OPA_DISP(graph);
    Matrix_rotateXYZ(0, -0x4000, 0, MTX_MULT);
    gSPMatrix(POLY_OPA_DISP++, _Matrix_to_Mtx_new(graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, models[idx][0]);
    Matrix_translate(4500.0f, 0.0f, 0.0f, MTX_MULT);
    gSPMatrix(POLY_OPA_DISP++, _Matrix_to_Mtx_new(graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    if (idx == 32) {
        gSPSegment(POLY_OPA_DISP++, ANIME_1_TXT_SEG, mNW_PaletteIdx2Palette(palette));
        gSPSegment(POLY_OPA_DISP++, ANIME_2_TXT_SEG, tex);
    }
    gSPDisplayList(POLY_OPA_DISP++, models[idx][1]);
    CLOSE_POLY_OPA_DISP(graph);
}

static void draw_balloon(PC_MP_PLAYER_ACTOR* p, GAME* game) {
    ACTOR* actorx = &p->actor_class;
    GRAPH* graph = game->graph;
    f32 scale = p->st.item_scale / 100.0f;

    if (p->item_shape < 0) return;
    Matrix_push();
    Matrix_translate(p->hand_pos.x, p->hand_pos.y, p->hand_pos.z, MTX_LOAD);
    Matrix_RotateY(actorx->shape_info.rotation.y, MTX_MULT);
    Matrix_RotateX((s16)(-0x4000 + p->st.item_rot[0]), MTX_MULT);
    Matrix_RotateZ(0x4000, MTX_MULT);
    Matrix_RotateX(p->st.item_rot[2], MTX_MULT);
    Matrix_scale(actorx->scale.x * scale, actorx->scale.y * scale, actorx->scale.z * scale, MTX_MULT);
    _texture_z_light_fog_prim(graph);
    OPEN_POLY_OPA_DISP(graph);
    gSPMatrix(POLY_OPA_DISP++, _Matrix_to_Mtx_new(graph), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    CLOSE_POLY_OPA_DISP(graph);
    Setpos_HiliteReflect_init(&p->hand_pos, (GAME_PLAY*)game);
    draw_item_skeleton(p, game, &before_balloon, &after_balloon);
    Matrix_pull();
}

static void draw_item(PC_MP_PLAYER_ACTOR* p, GAME* game) {
    const PcMpState* st = &p->st;
    int main = st->item_main;
    GRAPH* graph = game->graph;
    Gfx* gfx = st->item_shape != 0xFF ? (Gfx*)mPlib_Get_Item_DataPointer(st->item_shape) : NULL;
    f32 scale = st->item_scale / 100.0f;
    Mtx* mtx;

    if (!p->hand_ok || main <= mPlayer_ITEM_MAIN_NONE || main >= mPlayer_ITEM_MAIN_NUM) return;
    if (main == mPlayer_ITEM_MAIN_FLOWER_NORMAL && st->anim0 == mPlayer_ANIM_HANA_PICKUP1 && st->frame < 22.0f) {
        return; /* (still picking it) */
    }
    if (main == mPlayer_ITEM_MAIN_BALLOON_NORMAL) {
        draw_balloon(p, game);
        return;
    }
    Matrix_push();
    Matrix_put(&p->hand_mtx);
    if (scale != 1.0f) Matrix_scale(scale, scale, scale, MTX_MULT);
    mtx = _Matrix_to_Mtx_new(graph);
    if (mtx != NULL) {
        _texture_z_light_fog_prim(graph);
        OPEN_POLY_OPA_DISP(graph);
        gSPMatrix(POLY_OPA_DISP++, mtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        CLOSE_POLY_OPA_DISP(graph);
        if (main == mPlayer_ITEM_MAIN_UMBRELLA_NORMAL) {
            draw_umbrella(p, game);
        } else if (main >= mPlayer_ITEM_MAIN_NET_NORMAL && main <= mPlayer_ITEM_MAIN_NET_COMPLETE_COLLECTION) {
            draw_item_skeleton(p, game, NULL, &after_net);
        } else if ((main >= mPlayer_ITEM_MAIN_ROD_NORMAL && main <= mPlayer_ITEM_MAIN_ROD_PUTAWAY) ||
                   main == mPlayer_ITEM_MAIN_WINDMILL_NORMAL) {
            draw_item_skeleton(p, game, NULL, NULL);
        } else if (gfx != NULL && mPlib_Get_Item_DataPointerType(st->item_shape) == mPlayer_ITEM_DATA_TYPE_GFX) {
            /* the axe, the shovel, a fan, a party popper, a flower */
            if (main != mPlayer_ITEM_MAIN_FLOWER_NORMAL || mPlib_Set_ItemFlowerPalletFromItemKind(game, st->item)) {
                OPEN_POLY_OPA_DISP(graph);
                if (main == mPlayer_ITEM_MAIN_CRACKER_NORMAL) {
                    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
                }
                gSPDisplayList(POLY_OPA_DISP++, gfx);
                CLOSE_POLY_OPA_DISP(graph);
            }
        }
    }
    Matrix_pull();
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
    p->hand_ok = 0;
    cKF_Si3_draw_R_SV(game, &p->kf, mtx, NULL, &after_body, p);
    draw_item(p, game);
    if (p->st.shown != EMPTY_NO && p->st.shown_scale > 0) {
        /* an item held up (picked up, dug up), where the player holds it */
        xyz_t at = actorx->world.position;
        f32 scale = p->st.shown_scale / 100.0f;

        at.x += p->st.shown_at[0];
        at.y += p->st.shown_at[1];
        at.z += p->st.shown_at[2];
        if (mFI_GET_TYPE(mFI_GetFieldId()) == mFI_FIELD_FG) {
            if (Common_Get(clip).bg_item_clip != NULL && Common_Get(clip).bg_item_clip->single_draw_proc != NULL) {
                Common_Get(clip).bg_item_clip->single_draw_proc(game, p->st.shown, &at, scale, NULL, NULL, NULL);
            }
        } else if (Common_Get(clip).shop_goods_clip != NULL &&
                   Common_Get(clip).shop_goods_clip->single_draw_proc != NULL) {
            Common_Get(clip).shop_goods_clip->single_draw_proc(game, p->st.shown, &at, scale, 0, FALSE);
        }
    }

    /* where the name goes, above the head */
    {
        xyz_t head = actorx->world.position;
        xyz_t screen;

        head.y += 62.0f;
        Game_play_Projection_Trans((GAME_PLAY*)game, &head, &screen);
        pc_mp_game_puppet_screen(p->slot, screen.x, screen.y, screen.z);
    }
}
