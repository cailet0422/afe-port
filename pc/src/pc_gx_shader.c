/* pc_gx_shader.c - the GX shader's uniform blocks and configuration from the
 * GX state (any backend).
 *
 * The blocks are kept in g_gx and rewritten only for the state groups that
 * changed. The configuration (pc_gx_config.h) holds the values that set the
 * shader up; each value the shader does not read in that configuration is
 * zeroed, so that the same drawing gets the same configuration (and the same
 * specialized program) whatever was left in the unused registers. Every
 * value is the one the uber shader of 1.4.x read from its uniforms, or a
 * value it treats the same (a flag that is only tested against 0 becomes 0
 * or 1). */
#include "pc_gx_internal.h"
#include <math.h>
#include <dolphin/gx/GXEnum.h>

/* GXTevColorArg / GXTevAlphaArg, as gx.frag numbers them */
#define CC_TEXC  8
#define CC_TEXA  9
#define CC_RASC  10
#define CC_RASA  11
#define CC_KONST 14
#define CA_TEXA  4
#define CA_RASA  5
#define CA_KONST 6

/* raw 0..9, GX enum 30..57 (stride 3), or 60 = identity (as pc_gx.c) */
static int tex_mtx_slot(int id) {
    if (id == GX_IDENTITY) return -1;
    if (id >= 0 && id < 10) return id;
    if (id >= GX_TEXMTX0 && id < GX_IDENTITY) return (id - GX_TEXMTX0) / 3;
    return -1;
}

static int stage_count(void) {
    int n = g_gx.num_tev_stages;

    return n < 0 ? 0 : n > PC_GX_MAX_TEV_STAGES ? PC_GX_MAX_TEV_STAGES : n;
}

/* the GL texture a stage samples, or 0 */
static PCTex stage_texture(int s) {
    int tex_map;

    if (s >= g_gx.num_tev_stages) return 0;
    tex_map = g_gx.tev_stages[s].tex_map;
    return tex_map >= 0 && tex_map < 8 ? g_gx.gl_textures[tex_map] : 0;
}

void pc_gx_shader_textures(PCTex tex[5]) {
    for (int s = 0; s < PC_GX_MAX_TEV_STAGES; s++) {
        tex[s] = stage_texture(s);
    }
    for (int i = 0; i < 2; i++) {
        int m = g_gx.ind_order[i].tex_map;

        tex[3 + i] = i < g_gx.num_ind_stages && m >= 0 && m < 8 ? g_gx.gl_textures[m] : 0;
    }
}

/* --- uniform blocks --- */

static void vert_matrices(PCGXVertBlock* b, unsigned int dirty) {
    if (dirty & PC_GX_DIRTY_PROJECTION) {
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                b->projection[c * 4 + r] = g_gx.projection_mtx[r][c];
    }
    if (dirty & PC_GX_DIRTY_MODELVIEW) {
        const float (*m)[4] = g_gx.pos_mtx[g_gx.current_mtx];
        const float (*n)[3] = g_gx.nrm_mtx[g_gx.current_mtx];

        for (int c = 0; c < 4; c++) {
            for (int r = 0; r < 3; r++) {
                b->modelview[c * 4 + r] = m[r][c];
            }
            b->modelview[c * 4 + 3] = c == 3 ? 1.0f : 0.0f;
        }
        for (int c = 0; c < 3; c++) {
            for (int r = 0; r < 3; r++) {
                b->normal_mtx[c * 4 + r] = n[r][c];
            }
            b->normal_mtx[c * 4 + 3] = 0.0f;
        }
    }
    if (dirty & PC_GX_DIRTY_TEXGEN) {
        for (int tg = 0; tg < 2; tg++) {
            int slot = tex_mtx_slot(g_gx.tex_gen_mtx[tg]);

            if (slot >= 0 && slot < 10) {
                memcpy(&b->texmtx[tg * 8], g_gx.tex_mtx[slot], 8 * sizeof(float)); /* its first two rows */
            }
        }
    }
}

static void frag_data(PCGXFragBlock* b, unsigned int dirty) {
    if (dirty & PC_GX_DIRTY_TEV_COLORS) {
        memcpy(b->tev_regs, g_gx.tev_colors, sizeof(b->tev_regs));
    }
    if (dirty & PC_GX_DIRTY_KONST) {
        memcpy(b->kcolor, g_gx.tev_k_colors, sizeof(b->kcolor));
    }
    if (dirty & PC_GX_DIRTY_LIGHTING) {
        memcpy(b->mat_color, g_gx.chan_mat_color[0], sizeof(b->mat_color));
        memcpy(b->amb_color, g_gx.chan_amb_color[0], sizeof(b->amb_color));
        for (int i = 0; i < 8; i++) {
            memcpy(b->light_pos[i], g_gx.lights[i].pos, 3 * sizeof(float));
            b->light_pos[i][3] = 0.0f;
            memcpy(b->light_color[i], g_gx.lights[i].color, sizeof(b->light_color[i]));
        }
    }
    if (dirty & PC_GX_DIRTY_FOG) {
        memcpy(b->fog_color, g_gx.fog_color, sizeof(b->fog_color));
        b->fog_range[0] = g_gx.fog_start;
        b->fog_range[1] = g_gx.fog_end;
    }
    if (dirty & (PC_GX_DIRTY_INDIRECT | PC_GX_DIRTY_TEXTURES)) {
        for (int i = 0; i < 3; i++) {
            float scale_val = ldexpf(1.0f, g_gx.ind_mtx_scale[i] + 17) / 1024.0f;
            const float* m = (const float*)g_gx.ind_mtx[i];

            for (int j = 0; j < 3; j++) {
                b->ind_mtx_r0[i][j] = m[j] * scale_val;
                b->ind_mtx_r1[i][j] = m[3 + j] * scale_val;
            }
        }
    }
    if (dirty & PC_GX_DIRTY_ALPHA_CMP) {
        b->alpha_ref[0] = (float)g_gx.alpha_ref0;
        b->alpha_ref[1] = (float)g_gx.alpha_ref1;
    }
}

/* --- configuration --- */

#define SF(s, name) v[PC_GXC_S0_##name + (s) * PC_GX_CONFIG_STAGE_SIZE]

static void config_fill(PCGXShaderConfig* c) {
    int* v = c->v;
    int n = stage_count();
    int uses_ras = 0, uses_tc[2] = { 0, 0 }, swap_used[4] = { 0, 0, 0, 0 };

    memset(c, 0, sizeof(*c));
    v[PC_GXC_NUM_STAGES] = n;
    for (int s = 0; s < n; s++) {
        const PCGXTevStage* t = &g_gx.tev_stages[s];
        int cin[4] = { t->color_a, t->color_b, t->color_c, t->color_d };
        int ain[4] = { t->alpha_a, t->alpha_b, t->alpha_c, t->alpha_d };
        int tex = 0, ras = 0, kc = 0, ka = 0;
        int tc = t->tex_coord >= 0 && t->tex_coord < 8 ? t->tex_coord : s;

        for (int i = 0; i < 4; i++) {
            tex |= cin[i] == CC_TEXC || cin[i] == CC_TEXA || ain[i] == CA_TEXA;
            ras |= cin[i] == CC_RASC || cin[i] == CC_RASA || ain[i] == CA_RASA;
            kc |= cin[i] == CC_KONST;
            ka |= ain[i] == CA_KONST;
        }
        SF(s, CA) = cin[0]; SF(s, CB) = cin[1]; SF(s, CC) = cin[2]; SF(s, CD) = cin[3];
        SF(s, AA) = ain[0]; SF(s, AB) = ain[1]; SF(s, AC) = ain[2]; SF(s, AD) = ain[3];
        SF(s, COP) = t->color_op == 1;
        SF(s, AOP) = t->alpha_op == 1;
        SF(s, CBIAS) = t->color_bias;
        SF(s, CSCALE) = t->color_scale;
        SF(s, ABIAS) = t->alpha_bias;
        SF(s, ASCALE) = t->alpha_scale;
        SF(s, CCLAMP) = t->color_clamp != 0;
        SF(s, ACLAMP) = t->alpha_clamp != 0;
        SF(s, COUT) = t->color_out;
        SF(s, AOUT) = t->alpha_out;
        /* the texture is sampled only for a stage whose inputs take it */
        SF(s, TEX) = tex && stage_texture(s) != 0;
        if (SF(s, TEX)) {
            SF(s, TSWAP) = t->tex_swap & 3;
            swap_used[SF(s, TSWAP)] = 1;
        }
        if (ras) {
            SF(s, RSWAP) = t->ras_swap & 3;
            swap_used[SF(s, RSWAP)] = 1;
            uses_ras = 1;
        }
        if (kc) SF(s, KCSEL) = t->k_color_sel;
        if (ka) SF(s, KASEL) = t->k_alpha_sel;
        SF(s, TC) = tc == 1;
        if (g_gx.num_ind_stages > 0 && t->ind_mtx != 0) {
            v[PC_GXC_NUM_IND] = 1;
            SF(s, IND) = t->ind_stage;
            SF(s, IMTX) = t->ind_mtx;
            SF(s, IBIAS) = t->ind_bias;
            SF(s, IWRAPS) = t->ind_wrap_s;
            SF(s, IWRAPT) = t->ind_wrap_t;
            SF(s, IADD) = t->ind_add_prev != 0;
            uses_tc[t->ind_stage == 0 ? 0 : 1] = 1;
            uses_tc[SF(s, TC)] = 1;
        }
        if (SF(s, TEX)) {
            uses_tc[SF(s, TC)] = 1;
        }
    }
    for (int t = 0; t < 4; t++) {
        const PCGXTevSwapTable* sw = &g_gx.tev_swap_table[t];
        int* o = &v[PC_GXC_SWAP0_R + t * 4];

        if (swap_used[t]) {
            o[0] = sw->r & 3; o[1] = sw->g & 3; o[2] = sw->b & 3; o[3] = sw->a & 3;
        } else {
            o[0] = 0; o[1] = 1; o[2] = 2; o[3] = 3;
        }
    }
    if (uses_ras && g_gx.num_chans > 0) {
        v[PC_GXC_NUM_CHANS] = 1;
        v[PC_GXC_MAT_SRC] = g_gx.chan_ctrl_mat_src[0] != 0;
        v[PC_GXC_ALPHA_LIGHTING] = g_gx.chan_ctrl_enable[1] != 0;
        v[PC_GXC_ALPHA_MAT_SRC] = g_gx.chan_ctrl_mat_src[1] != 0;
        if (g_gx.chan_ctrl_enable[0]) {
            v[PC_GXC_LIGHTING] = 1;
            v[PC_GXC_AMB_SRC] = g_gx.chan_ctrl_amb_src[0] != 0;
            v[PC_GXC_LIGHT_MASK] = g_gx.chan_ctrl_light_mask[0] & 0xFF;
        }
    }
    v[PC_GXC_FOG] = g_gx.fog_type != 0;
    v[PC_GXC_ACMP0] = g_gx.alpha_comp0 & 7;
    v[PC_GXC_ACMP1] = g_gx.alpha_comp1 & 7;
    if (v[PC_GXC_ACMP0] != 7 || v[PC_GXC_ACMP1] != 7) {
        v[PC_GXC_AOP] = g_gx.alpha_op & 3;
    }
    for (int tg = 0; tg < 2; tg++) {
        if (uses_tc[tg]) {
            int slot = tex_mtx_slot(g_gx.tex_gen_mtx[tg]);

            v[PC_GXC_TEXGEN0 + tg] = g_gx.tex_gen_src[tg] == 1; /* GX_TG_NRM */
            v[PC_GXC_TEXMTX0 + tg] = slot >= 0 && slot < 10;
        }
    }
}

/* the groups the configuration depends on */
#define CONFIG_DIRTY (PC_GX_DIRTY_TEV_STAGES | PC_GX_DIRTY_SWAP_TABLES | PC_GX_DIRTY_ALPHA_CMP | \
                      PC_GX_DIRTY_LIGHTING | PC_GX_DIRTY_TEXGEN | PC_GX_DIRTY_TEXTURES | PC_GX_DIRTY_INDIRECT | \
                      PC_GX_DIRTY_FOG)
#define VERT_DIRTY   (PC_GX_DIRTY_PROJECTION | PC_GX_DIRTY_MODELVIEW | PC_GX_DIRTY_TEXGEN)
#define FRAG_DIRTY   (PC_GX_DIRTY_TEV_COLORS | PC_GX_DIRTY_KONST | PC_GX_DIRTY_LIGHTING | PC_GX_DIRTY_FOG | \
                      PC_GX_DIRTY_INDIRECT | PC_GX_DIRTY_TEXTURES | PC_GX_DIRTY_ALPHA_CMP)

unsigned pc_gx_shader_update(unsigned int dirty) {
    unsigned changed = 0;

    if (dirty & VERT_DIRTY) {
        vert_matrices(&g_gx.vert_block, dirty);
        changed |= PC_GX_BLOCK_VERT;
    }
    if (dirty & FRAG_DIRTY) {
        frag_data(&g_gx.frag_block, dirty);
        changed |= PC_GX_BLOCK_FRAG;
    }
    if (dirty & CONFIG_DIRTY) {
        PCGXShaderConfig c;

        config_fill(&c);
        if (memcmp(&c, &g_gx.shader_config, sizeof(c)) != 0) {
            g_gx.shader_config = c;
            changed |= PC_GX_BLOCK_CONFIG;
        }
    }
    return changed;
}
