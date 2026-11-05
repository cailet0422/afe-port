/* pc_gfx_internal.h - what pc_gfx.c shares with the Vulkan and Direct3D 12
 * backends: the state the pc_gfx calls set (OpenGL's rules) and the texture
 * objects. pc_gfx.c is also the OpenGL backend. */
#ifndef PC_GFX_INTERNAL_H
#define PC_GFX_INTERNAL_H

#include "pc_gfx.h"
#include "pc_gx_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_GFX_UNITS 8

typedef struct {
    int vp[4];              /* window pixels, from the bottom left */
    float depth_near, depth_far;
    int scissor_test;
    int scissor[4];
    int depth_test, depth_func, depth_write;
    int color_write, alpha_write;
    int cull;
    int blend, blend_src, blend_dst, blend_src_a, blend_dst_a, blend_subtract;
    float line_width;
    int unit;               /* the active texture unit */
    PCTex bound[PC_GFX_UNITS];
    int win_w, win_h;       /* the window (the framebuffer) */
} PCGfxState;

extern PCGfxState g_gfx_state;

/* A texture object of Vulkan or Direct3D 12 (OpenGL keeps its own) */
typedef struct {
    int used;
    int w, h, format;
    int levels;             /* mipmap levels the image has */
    int wrap_s, wrap_t, min_filter, mag_filter;
    unsigned version;       /* changes when the image or the modes change */
    void* impl;             /* the backend's */
} PCGfxTex;

/* the object of a texture name, NULL for 0 or a deleted one */
PCGfxTex* pc_gfx_tex_obj(PCTex t);

/* A backend other than OpenGL */
typedef struct {
    void (*shutdown)(void);
    void (*drawable_size)(int* w, int* h);
    void (*set_vsync)(int on);
    void (*present)(void);
    int  (*tex_image)(PCGfxTex* t, int w, int h, int format, const void* data, size_t size);
    void (*tex_subimage)(PCGfxTex* t, int x, int y, int w, int h, int row_length, const void* rgba);
    void (*tex_mipmap)(PCGfxTex* t);
    void (*tex_free)(PCGfxTex* t);
    void (*clear)(int color, int depth, const float rgba[4], float depth_value);
    void (*draw_gx)(int prim, const PCGXVertex* v, int count, unsigned changed);
    void (*draw_ui)(PCTex tex, const PCGfxUIVertex* v, int count, const unsigned short* indices, int index_count,
                    float space_w, float space_h);
    void (*read_pixels)(int x, int y, int w, int h, int rgba, void* out);
    char device[160];
    int bc7, s3tc;
} PCGfxBackend;

extern PCGfxBackend* g_gfx_backend; /* NULL on OpenGL */

/* the backends' window and device; 0 when the API cannot be used */
int pc_gfx_vk_open(const char* title, int w, int h, Uint32 sdl_flags, int msaa);
int pc_gfx_d3d12_open(const char* title, int w, int h, Uint32 sdl_flags, int msaa);

/* RGB565 with red in the low bits (FixNES) to RGBA8 */
void pc_gfx_rgb565_to_rgba(const unsigned short* src, unsigned char* dst, int count);

#ifdef __cplusplus
}
#endif

#endif
