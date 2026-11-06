/* pc_gfx.h - the port's graphics backends: OpenGL 3.3 / ES 3.0, Vulkan 1.1 and
 * Direct3D 12.
 *
 * Everything the port draws (the GX emulation in pc_gx*.c, the minimap, the
 * touch pad, FixNES, the menus) goes through these calls. They are the part
 * of OpenGL the port was written against, with OpenGL's rules: state set by a
 * call stays until it is set again, a draw takes the state of the moment,
 * texture objects keep their own wrap and filter modes and are bound to
 * texture units, coordinates are window pixels counted from the bottom left.
 * The OpenGL backend makes each call the GL call it stands for. Vulkan and
 * Direct3D 12 keep the same state and turn it into pipelines and command
 * lists when something is drawn, so every backend draws the same pictures
 * from the same calls. */
#ifndef PC_GFX_H
#define PC_GFX_H

#include <stddef.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_GFX_OPENGL = 0,
    PC_GFX_VULKAN = 1,
    PC_GFX_D3D12 = 2,
    PC_GFX_API_COUNT
} PCGfxApi;

/* the backend in use */
extern PCGfxApi g_pc_gfx_api;

/* --- window and frames --- */

/* The game window for an API (g_pc_window), with msaa samples (fewer when
 * the GPU has not as many). Returns 0, with no window, when the API cannot
 * be used here; the caller then tries OpenGL. */
int  pc_gfx_open_window(PCGfxApi api, const char* title, int w, int h, Uint32 sdl_flags, int msaa);
void pc_gfx_close_window(void);
/* the window's size in pixels */
void pc_gfx_drawable_size(int* w, int* h);
void pc_gfx_set_vsync(int on);
/* shows what was drawn since the last present */
void pc_gfx_present(void);
/* there while a start with Vulkan or Direct3D 12 has not shown its first
 * seconds yet: the next start uses OpenGL if it stays (a crashing driver) */
#define PC_GFX_START_MARK "graphics_starting.txt"
/* window and app events the backend has to follow (Android's background) */
void pc_gfx_handle_event(const SDL_Event* e);
/* "OpenGL", "Vulkan", "Direct3D 12" */
const char* pc_gfx_api_name(PCGfxApi api);
/* the API was built in (Direct3D 12 on Windows only) */
int  pc_gfx_api_built(PCGfxApi api);
/* the GPU, for logs and the menus */
const char* pc_gfx_device_name(void);
/* the GPU samples these compressed formats (texture packs) */
int  pc_gfx_has_bc7(void);
int  pc_gfx_has_s3tc(void);

/* --- textures (glGenTextures, glBindTexture, glTexImage2D...) --- */

typedef unsigned int PCTex; /* 0: none */

enum { PC_TEXF_RGBA8 = 0, PC_TEXF_RGB565, PC_TEXF_BC1, PC_TEXF_BC3, PC_TEXF_BC7 };
enum { PC_WRAP_CLAMP = 0, PC_WRAP_REPEAT, PC_WRAP_MIRROR };
enum { PC_FILTER_NEAREST = 0, PC_FILTER_LINEAR, PC_FILTER_LINEAR_MIPMAP_LINEAR };

PCTex pc_gfx_tex_new(void);
void  pc_gfx_tex_delete(PCTex t);           /* unbinds it from every unit, as glDeleteTextures */
void  pc_gfx_active_unit(int unit);         /* glActiveTexture */
void  pc_gfx_bind(PCTex t);                 /* glBindTexture on the active unit */
/* on the texture bound to the active unit: */
void  pc_gfx_tex_image(int w, int h, int format, const void* data, size_t size); /* level 0 */
void  pc_gfx_tex_subimage(int x, int y, int w, int h, const void* rgba);        /* RGBA8 */
void  pc_gfx_tex_generate_mipmap(void);
void  pc_gfx_tex_wrap(int wrap_s, int wrap_t);
void  pc_gfx_tex_filter(int min_filter, int mag_filter);

/* The overlays' textures (menus, minimap, FixNES), named but not bound:
 * these leave the texture units as they are. Clamped, filter for both. */
void  pc_gfx_tex_upload(PCTex t, int w, int h, int format, const void* data, size_t size, int filter);
void  pc_gfx_tex_update(PCTex t, int x, int y, int w, int h, int row_length, const void* rgba);
/* the texture calls since the last check failed (glGetError) */
int   pc_gfx_tex_error(void);

/* --- state --- */

enum { PC_CMP_NEVER = 0, PC_CMP_LESS, PC_CMP_EQUAL, PC_CMP_LEQUAL, PC_CMP_GREATER, PC_CMP_NOTEQUAL,
       PC_CMP_GEQUAL, PC_CMP_ALWAYS };
enum { PC_BF_ZERO = 0, PC_BF_ONE, PC_BF_SRC_COLOR, PC_BF_ONE_MINUS_SRC_COLOR, PC_BF_DST_COLOR,
       PC_BF_ONE_MINUS_DST_COLOR, PC_BF_SRC_ALPHA, PC_BF_ONE_MINUS_SRC_ALPHA, PC_BF_DST_ALPHA,
       PC_BF_ONE_MINUS_DST_ALPHA };
enum { PC_CULL_NONE = 0, PC_CULL_FRONT, PC_CULL_BACK, PC_CULL_ALL };

void pc_gfx_viewport(int x, int y, int w, int h);
void pc_gfx_depth_range(float near_z, float far_z);
void pc_gfx_scissor_test(int on);
void pc_gfx_scissor(int x, int y, int w, int h);
void pc_gfx_depth_test(int on);
void pc_gfx_depth_func(int cmp);
void pc_gfx_depth_write(int on);
void pc_gfx_color_write(int rgb, int alpha);
void pc_gfx_cull(int mode);
void pc_gfx_blend(int on);
void pc_gfx_blend_func(int src, int dst);
void pc_gfx_blend_func_separate(int src, int dst, int src_alpha, int dst_alpha);
void pc_gfx_blend_subtract(int on);         /* glBlendEquation(GL_FUNC_REVERSE_SUBTRACT / ADD) */
void pc_gfx_line_width(float w);
void pc_gfx_point_size(float size);
/* glFlush */
void pc_gfx_flush(void);
/* glClear of color and/or depth, within the scissor box when it is on */
void pc_gfx_clear(int color, int depth, const float rgba[4], float depth_value);

/* --- drawing --- */

enum { PC_PRIM_TRIANGLES = 0, PC_PRIM_TRIANGLE_STRIP, PC_PRIM_TRIANGLE_FAN, PC_PRIM_LINES,
       PC_PRIM_LINE_STRIP, PC_PRIM_POINTS, PC_PRIM_QUADS };

/* The GX shader (pc_gx_internal.h): g_gx's blocks and configuration,
 * changed = the PC_GX_BLOCK_* that changed since the last call, texture units
 * 0-2 the stages' textures, 3-4 the indirect stages'. */
struct PCGXVertex;
void pc_gfx_draw_gx(int prim, const struct PCGXVertex* v, int count, unsigned changed);
/* the GX shader's GPU objects, from pc_gx_init / pc_gx_shutdown */
void pc_gfx_gx_init(void);
void pc_gfx_gx_shutdown(void);

/* The menus, minimap, touch pad and FixNES: pixel positions in a space_w x
 * space_h space that the viewport spans (y down), the texture times the
 * color, then a shape (kind 1 ellipse, 2 ring: local = position inside it,
 * -1 to 1, inner = the ring's inner radius) with anti-aliased edges. tex 0
 * draws white. Takes the blend, scissor and viewport state; leaves the
 * texture units as they were. */
typedef struct {
    float x, y;
    float u, v;
    float lx, ly;
    unsigned char color[4];
    float kind, inner;
} PCGfxUIVertex;
void pc_gfx_draw_ui(PCTex tex, const PCGfxUIVertex* v, int count, const unsigned short* indices, int index_count,
                    float space_w, float space_h);

/* --- reading the picture --- */

/* glReadPixels of the window: rows from the bottom up, RGBA or RGB bytes */
void pc_gfx_read_pixels(int x, int y, int w, int h, int rgba, void* out);

#ifdef __cplusplus
}
#endif

#endif
