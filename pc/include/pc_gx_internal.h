/* pc_gx_internal.h - GX state machine, vertex format, TEV config, GL objects */
#ifndef PC_GX_INTERNAL_H
#define PC_GX_INTERNAL_H

#include "pc_platform.h"
#include "pc_gx_config.h"

/* Define PC_GL_DEBUG to check for GL errors after significant calls */
#ifdef PC_GL_DEBUG
#define PC_GL_CHECK(label) do { \
    GLenum err_ = glGetError(); \
    if (err_ != GL_NO_ERROR) \
        printf("[GL ERR] %s: 0x%04X at %s:%d\n", label, err_, __FILE__, __LINE__); \
} while(0)
#else
#define PC_GL_CHECK(label) ((void)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- Dirty flags for conditional uniform upload --- */
#define PC_GX_DIRTY_PROJECTION  (1u << 0)
#define PC_GX_DIRTY_MODELVIEW   (1u << 1)
#define PC_GX_DIRTY_TEV_COLORS  (1u << 2)
#define PC_GX_DIRTY_TEV_STAGES  (1u << 3)
#define PC_GX_DIRTY_SWAP_TABLES (1u << 4)
#define PC_GX_DIRTY_KONST       (1u << 5)
#define PC_GX_DIRTY_ALPHA_CMP   (1u << 6)
#define PC_GX_DIRTY_LIGHTING    (1u << 7)
#define PC_GX_DIRTY_TEXGEN      (1u << 8)
#define PC_GX_DIRTY_TEXTURES    (1u << 9)
#define PC_GX_DIRTY_INDIRECT    (1u << 10)
#define PC_GX_DIRTY_FOG         (1u << 11)
#define PC_GX_DIRTY_DEPTH       (1u << 12)
#define PC_GX_DIRTY_COLOR_MASK  (1u << 13)
#define PC_GX_DIRTY_CULL        (1u << 14)
#define PC_GX_DIRTY_BLEND       (1u << 15)
#define PC_GX_DIRTY_ALL         0xFFFFu
#define PC_GX_DIRTY_SET(flag) (g_gx.dirty |= (flag))
#define DIRTY(flag) PC_GX_DIRTY_SET(flag)

/* --- Vertex buffer --- */
#define PC_GX_MAX_VERTS       65536
#define PC_GX_MAX_ATTRIB_SIZE 64
#define PC_GX_MAX_ATTR        26
#define PC_GX_MAX_VTXFMT      8
#define PC_GX_MAX_TEV_STAGES  3

typedef struct {
    int has_position;
    int has_normal;
    int has_color0;
    int has_color1;
    int has_texcoord[8];
    int texcoord_frac[8];
    int position_size;
    int color_size;
    int texcoord_size;
    int stride;
} PCGXVertexFormat;

/* A vertex as the GPU takes it (36 bytes): the game only ever sets color 0
 * and texture coordinate 0 (emu64 makes the others with texture matrices) */
typedef struct {
    float position[3];
    float normal[3];
    unsigned char color0[4];
    float texcoord[2];
} PCGXVertex;

/* The GX shader's uniform blocks (std140; pc/shaders/gx.vert, gx.frag) */
typedef struct {
    float projection[16]; /* column-major */
    float modelview[16];
    float normal_mtx[12]; /* three columns */
    float texmtx[16];     /* texgen 0's two rows, then texgen 1's */
} PCGXVertBlock;

typedef struct {
    float tev_regs[4][4]; /* PREV, REG0, REG1, REG2 */
    float kcolor[4][4];
    float mat_color[4];
    float amb_color[4];
    float light_pos[8][4];
    float light_color[8][4];
    float fog_color[4];
    float fog_range[4]; /* start, end */
    float ind_mtx_r0[3][4];
    float ind_mtx_r1[3][4];
    float alpha_ref[4]; /* ref0, ref1 (0-255) */
} PCGXFragBlock;

typedef struct {
    int color_a, color_b, color_c, color_d;
    int alpha_a, alpha_b, alpha_c, alpha_d;
    int color_op, color_bias, color_scale, color_clamp, color_out;
    int alpha_op, alpha_bias, alpha_scale, alpha_clamp, alpha_out;
    int tex_coord, tex_map, color_chan;
    int k_color_sel, k_alpha_sel;
    int ras_swap, tex_swap;
    int ind_stage, ind_format, ind_bias, ind_mtx, ind_wrap_s, ind_wrap_t;
    int ind_add_prev, ind_lod, ind_alpha;
} PCGXTevStage;

typedef struct {
    int r, g, b, a;  /* channel indices: 0=R, 1=G, 2=B, 3=A */
} PCGXTevSwapTable;

typedef struct {
    /* Primitive assembly */
    int current_primitive;
    int current_vtxfmt;
    int vertex_count;
    int expected_vertex_count;
    int in_begin;
    PCGXVertex vertex_buffer[PC_GX_MAX_VERTS];
    int current_vertex_idx;
    PCGXVertex current_vertex;

    /* Vertex descriptor */
    int vtx_desc[PC_GX_MAX_ATTR];
    PCGXVertexFormat vtx_fmt[PC_GX_MAX_VTXFMT];

    /* Transforms */
    float projection_mtx[4][4];
    int projection_type;
    float pos_mtx[10][3][4];
    float nrm_mtx[10][3][3];
    float tex_mtx[10][3][4];
    int current_mtx;

    /* Viewport & scissor */
    float viewport[6];  /* x, y, w, h, near, far */
    int scissor[4];     /* left, top, w, h */

    /* TEV */
    int num_tev_stages;
    PCGXTevStage tev_stages[16];
    float tev_colors[4][4];    /* PREV, REG0, REG1, REG2 */
    float tev_k_colors[4][4];
    PCGXTevSwapTable tev_swap_table[4];

    /* Textures */
    int num_tex_gens;
    int tex_gen_type[8];
    int tex_gen_src[8];
    int tex_gen_mtx[8];
    GLuint gl_textures[8];
    int tex_obj_w[8];
    int tex_obj_h[8];
    int tex_obj_fmt[8];

    /* Lighting */
    int num_chans;
    float chan_amb_color[2][4];
    float chan_mat_color[2][4];
    int chan_ctrl_enable[4];
    int chan_ctrl_amb_src[4];
    int chan_ctrl_mat_src[4];
    int chan_ctrl_light_mask[4];
    int chan_ctrl_diff_fn[4];
    int chan_ctrl_attn_fn[4];

    struct {
        float pos[3];
        float dir[3];
        float color[4];
        float a0, a1, a2;  /* angular attenuation */
        float k0, k1, k2;  /* distance attenuation */
    } lights[8];

    /* Blend & depth */
    int blend_mode;
    int blend_src;
    int blend_dst;
    int blend_logic_op;
    int z_compare_enable;
    int z_compare_func;
    int z_update_enable;
    int color_update_enable;
    int alpha_update_enable;

    /* Alpha compare */
    int alpha_comp0;
    int alpha_ref0;
    int alpha_op;
    int alpha_comp1;
    int alpha_ref1;

    int cull_mode;

    /* Fog */
    int fog_type;
    float fog_start, fog_end, fog_near, fog_far;
    float fog_color[4];

    /* TLUT palette storage for CI4/CI8 textures */
    struct {
        const void* data;
        int format;      /* GX_TL_IA8=0, GX_TL_RGB5A3=1 */
        int n_entries;
        int is_be;       /* 1=big-endian (ROM/JSystem), 0=native LE (emu64 tlutconv) */
    } tlut[16];

    /* Indirect textures */
    int num_ind_stages;
    struct {
        int tex_coord;
        int tex_map;
        int scale_s;
        int scale_t;
    } ind_order[4];
    float ind_mtx[3][2][3];
    int   ind_mtx_scale[3];

    /* Deferred vertex commit: position starts vertex, commit on next position or GXEnd */
    int vertex_pending;

    /* GL objects */
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    GLuint current_shader;

    /* The shader's uniform blocks and configuration, kept up to date from
     * the state groups that change (pc_gx_shader.c) */
    PCGXVertBlock vert_block;
    PCGXFragBlock frag_block;
    PCGXShaderConfig shader_config;

    float clear_color[4];
    float clear_depth;

    /* Copy/framebuffer */
    int copy_src[4];       /* left, top, w, h */
    int copy_dst[2];       /* w, h */
    int tex_copy_src[4];
    int tex_copy_dst[2];
    unsigned int tex_copy_fmt;
    int tex_copy_mipmap;

    /* Indexed vertex data */
    const void* array_base[PC_GX_MAX_ATTR];
    unsigned char array_stride[PC_GX_MAX_ATTR];

    unsigned int dirty;

} PCGXState;

extern PCGXState g_gx;

/* --- Internal functions --- */
void pc_gx_init(void);
void pc_gx_shutdown(void);
void pc_gx_flush_vertices(void);
void pc_gx_flush_if_begin_complete(void);

/* The GX shader's blocks and configuration from the state groups in dirty
 * (pc_gx_shader.c); returns which of them changed (PC_GX_BLOCK_*) */
#define PC_GX_BLOCK_VERT   1u
#define PC_GX_BLOCK_FRAG   2u
#define PC_GX_BLOCK_CONFIG 4u
unsigned pc_gx_shader_update(unsigned int dirty);
/* the GL texture of each sampler: stages 0-2, indirect stages 0-1 */
void pc_gx_shader_textures(GLuint tex[5]);

/* TEV shader programs (pc_gx_tev.c): makes the program for the current
 * configuration current (the uber program while a specialized one is not
 * ready) with the blocks up to date; changed is pc_gx_shader_update's */
void   pc_gx_tev_use(unsigned changed);
void   pc_gx_tev_init(void);
void   pc_gx_tev_shutdown(void);
void   pc_gx_tev_frame(void);

/* Texture cache */
GLuint pc_gx_texture_upload(void* data, int width, int height, int format, int ci_format,
                            void* tlut, int tlut_format, int tlut_count);
void   pc_gx_texture_init(void);
void   pc_gx_texture_shutdown(void);
void   pc_gx_texture_cache_invalidate(void);
/* The texture pack (pc_texture_pack.c): a replacement that was loading is
 * there / one of its textures was freed */
void   pc_gx_texture_hd_ready(unsigned long long key);
void   pc_gx_texture_forget(GLuint tex);

#ifdef PC_ENHANCEMENTS
/* EFB capture: store full-res GL texture from GXCopyTex, retrieve on texture load */
void   pc_gx_efb_capture_store(uintptr_t dest_ptr, GLuint gl_tex);
GLuint pc_gx_efb_capture_find(uintptr_t data_ptr);
void   pc_gx_efb_capture_cleanup(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PC_GX_INTERNAL_H */
