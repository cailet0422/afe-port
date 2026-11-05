/* pc_gfx.c - the pc_gfx calls (pc_gfx.h): the state they set, kept for the
 * Vulkan and Direct3D 12 backends, and the OpenGL backend, where each call is
 * the GL call it stands for. */
#include "pc_gfx_internal.h"
#include "pc_settings.h"
#include "../gen/pc_shaders.h"

PCGfxApi g_pc_gfx_api = PC_GFX_OPENGL;
PCGfxState g_gfx_state;
PCGfxBackend* g_gfx_backend = NULL;

#define GL_ON (g_gfx_backend == NULL)

/* --- texture objects of the other backends --- */

static PCGfxTex* s_tex = NULL;
static int s_tex_cap = 0;
static int s_tex_free_hint = 0;
static int s_tex_error = 0;

PCGfxTex* pc_gfx_tex_obj(PCTex t) {
    if (t == 0 || (int)t > s_tex_cap || !s_tex[t - 1].used) return NULL;
    return &s_tex[t - 1];
}

static PCTex tex_alloc(void) {
    int i;

    for (i = 0; i < s_tex_cap; i++) {
        int k = (s_tex_free_hint + i) % s_tex_cap;

        if (!s_tex[k].used) {
            s_tex_free_hint = k + 1;
            break;
        }
    }
    if (i == s_tex_cap) {
        int cap = s_tex_cap ? s_tex_cap * 2 : 1024;
        PCGfxTex* n = (PCGfxTex*)realloc(s_tex, (size_t)cap * sizeof(PCGfxTex));

        if (n == NULL) return 0;
        memset(n + s_tex_cap, 0, (size_t)(cap - s_tex_cap) * sizeof(PCGfxTex));
        s_tex = n;
        i = s_tex_cap;
        s_tex_cap = cap;
        s_tex_free_hint = i + 1;
    } else {
        i = (s_tex_free_hint - 1 + s_tex_cap) % s_tex_cap;
    }
    memset(&s_tex[i], 0, sizeof(PCGfxTex));
    s_tex[i].used = 1;
    s_tex[i].wrap_s = s_tex[i].wrap_t = PC_WRAP_REPEAT;
    s_tex[i].min_filter = s_tex[i].mag_filter = PC_FILTER_LINEAR;
    s_tex[i].version = 1;
    return (PCTex)(i + 1);
}

void pc_gfx_rgb565_to_rgba(const unsigned short* src, unsigned char* dst, int count) {
    for (int i = 0; i < count; i++) {
        unsigned v = src[i];
        unsigned r = v & 0x1F, g = (v >> 5) & 0x3F, b = v >> 11;

        dst[i * 4 + 0] = (unsigned char)((r << 3) | (r >> 2));
        dst[i * 4 + 1] = (unsigned char)((g << 2) | (g >> 4));
        dst[i * 4 + 2] = (unsigned char)((b << 3) | (b >> 2));
        dst[i * 4 + 3] = 255;
    }
}

/* --- OpenGL helpers --- */

static GLenum gl_cmp(int c) { return (GLenum)(GL_NEVER + (c & 7)); }

static GLenum gl_factor(int f) {
    static const GLenum t[] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR,
                                GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA,
                                GL_ONE_MINUS_DST_ALPHA };
    return f >= 0 && f < 10 ? t[f] : GL_ONE;
}

static GLint gl_filter(int f) {
    return f == PC_FILTER_NEAREST ? GL_NEAREST : f == PC_FILTER_LINEAR_MIPMAP_LINEAR ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
}

static GLint gl_wrap(int w) {
    return w == PC_WRAP_MIRROR ? GL_MIRRORED_REPEAT : w == PC_WRAP_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT;
}

#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

/* level 0 of the texture bound on the active unit */
static void gl_tex_image(int w, int h, int format, const void* data, size_t size) {
    switch (format) {
        case PC_TEXF_RGB565:
#ifdef PC_GLES
        {   /* ES has no 5_6_5_REV: red and blue swapped into 5_6_5 */
            unsigned short* tmp = (unsigned short*)malloc((size_t)w * h * 2);
            const unsigned short* src = (const unsigned short*)data;

            if (tmp == NULL) return;
            for (int i = 0; i < w * h; i++) {
                unsigned short v = src[i];

                tmp[i] = (unsigned short)(((v & 0x1F) << 11) | (v & 0x07E0) | (v >> 11));
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, tmp);
            free(tmp);
        }
#else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, data);
#endif
            break;
        case PC_TEXF_BC1:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, w, h, 0, (GLsizei)size, data);
            break;
        case PC_TEXF_BC3:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, w, h, 0, (GLsizei)size, data);
            break;
        case PC_TEXF_BC7:
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_BPTC_UNORM, w, h, 0, (GLsizei)size, data);
            break;
        default:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            break;
    }
}

/* --- window --- */

static char s_device[192] = "";
static int s_gl_bc7 = 0, s_gl_s3tc = 0;

static int gl_has_extension(const char* name) {
    GLint n = 0;

    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);

        if (ext != NULL && strcmp(ext, name) == 0) return 1;
    }
    return 0;
}

static void gl_open(const char* title, int w, int h, Uint32 sdl_flags, int msaa) {
    int samples = msaa;

#ifdef PC_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    /* A GPU may not offer the MSAA asked for (a Galaxy Z Flip6 has no 8x
     * EGL config, and the game then ended at every start because the
     * setting was saved): try fewer samples, down to none */
    for (;;) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, samples > 0 ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, samples);
        g_pc_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                                       sdl_flags | SDL_WINDOW_OPENGL);
        if (g_pc_window) {
            g_pc_gl_context = SDL_GL_CreateContext(g_pc_window);
            if (g_pc_gl_context) {
                break;
            }
            fprintf(stderr, "SDL_GL_CreateContext failed (MSAA %dx): %s\n", samples, SDL_GetError());
            SDL_DestroyWindow(g_pc_window);
            g_pc_window = NULL;
        } else {
            fprintf(stderr, "SDL_CreateWindow failed (MSAA %dx): %s\n", samples, SDL_GetError());
        }
        if (samples <= 0) {
            pc_fatal("The game window could not be opened: %s", SDL_GetError());
        }
        samples = samples > 4 ? 4 : samples / 2;
    }
    if (samples != msaa) {
        printf("[PC] MSAA %dx is not available, using %dx\n", msaa, samples);
    }
#ifndef PC_GLES
    if (!gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        pc_fatal("OpenGL 3.3 could not be loaded (gladLoadGL failed).");
    }
#endif
    printf("[PC] OpenGL %s (%s, %s)\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER),
           (const char*)glGetString(GL_VENDOR));
    snprintf(s_device, sizeof(s_device), "%s", (const char*)glGetString(GL_RENDERER));
    s_gl_bc7 = gl_has_extension("GL_ARB_texture_compression_bptc") || gl_has_extension("GL_EXT_texture_compression_bptc");
    s_gl_s3tc = gl_has_extension("GL_EXT_texture_compression_s3tc");
#if defined(PC_ENHANCEMENTS) && !defined(PC_GLES)
    if (samples > 0) {
        glEnable(GL_MULTISAMPLE);
    }
#endif
}

int pc_gfx_api_built(PCGfxApi api) {
    switch (api) {
        case PC_GFX_OPENGL: return 1;
#ifdef PC_GFX_VULKAN_BUILT
        case PC_GFX_VULKAN: return 1;
#endif
#ifdef PC_GFX_D3D12_BUILT
        case PC_GFX_D3D12: return 1;
#endif
        default: return 0;
    }
}

const char* pc_gfx_api_name(PCGfxApi api) {
    switch (api) {
        case PC_GFX_VULKAN: return "Vulkan";
        case PC_GFX_D3D12: return "Direct3D 12";
        default: return "OpenGL";
    }
}

static void state_reset(void) {
    memset(&g_gfx_state, 0, sizeof(g_gfx_state));
    /* OpenGL's initial state */
    g_gfx_state.depth_func = PC_CMP_LESS;
    g_gfx_state.depth_write = 1;
    g_gfx_state.color_write = g_gfx_state.alpha_write = 1;
    g_gfx_state.blend_src = g_gfx_state.blend_src_a = PC_BF_ONE;
    g_gfx_state.blend_dst = g_gfx_state.blend_dst_a = PC_BF_ZERO;
    g_gfx_state.depth_far = 1.0f;
    g_gfx_state.line_width = 1.0f;
}

int pc_gfx_open_window(PCGfxApi api, const char* title, int w, int h, Uint32 sdl_flags, int msaa) {
    state_reset();
    g_gfx_backend = NULL;
    g_pc_gfx_api = PC_GFX_OPENGL;
#ifdef PC_GFX_VULKAN_BUILT
    if (api == PC_GFX_VULKAN) {
        if (!pc_gfx_vk_open(title, w, h, sdl_flags, msaa)) return 0;
        g_pc_gfx_api = PC_GFX_VULKAN;
    }
#endif
#ifdef PC_GFX_D3D12_BUILT
    if (api == PC_GFX_D3D12) {
        if (!pc_gfx_d3d12_open(title, w, h, sdl_flags, msaa)) return 0;
        g_pc_gfx_api = PC_GFX_D3D12;
    }
#endif
    if (g_gfx_backend == NULL) {
        if (api != PC_GFX_OPENGL) return 0;
        gl_open(title, w, h, sdl_flags, msaa);
    }
    pc_gfx_drawable_size(&g_gfx_state.win_w, &g_gfx_state.win_h);
    g_gfx_state.vp[2] = g_gfx_state.win_w;
    g_gfx_state.vp[3] = g_gfx_state.win_h;
    g_gfx_state.scissor[2] = g_gfx_state.win_w;
    g_gfx_state.scissor[3] = g_gfx_state.win_h;
    return 1;
}

static GLuint s_ui_program = 0, s_ui_vao = 0, s_ui_vbo = 0, s_ui_ebo = 0;
static GLint s_ui_space = -1;
static GLuint s_gx_vao = 0, s_gx_vbo = 0, s_gx_ebo = 0;

void pc_gfx_close_window(void) {
    if (g_gfx_backend != NULL) {
        for (int i = 0; i < s_tex_cap; i++) {
            if (s_tex[i].used && s_tex[i].impl != NULL) g_gfx_backend->tex_free(&s_tex[i]);
        }
        g_gfx_backend->shutdown();
        g_gfx_backend = NULL;
    } else {
        if (s_ui_program) glDeleteProgram(s_ui_program);
        if (s_ui_vao) glDeleteVertexArrays(1, &s_ui_vao);
        if (s_ui_vbo) glDeleteBuffers(1, &s_ui_vbo);
        if (s_ui_ebo) glDeleteBuffers(1, &s_ui_ebo);
        s_ui_program = s_ui_vao = s_ui_vbo = s_ui_ebo = 0;
        if (g_pc_gl_context) {
            SDL_GL_DeleteContext(g_pc_gl_context);
            g_pc_gl_context = NULL;
        }
    }
    if (g_pc_window) {
        SDL_DestroyWindow(g_pc_window);
        g_pc_window = NULL;
    }
    free(s_tex);
    s_tex = NULL;
    s_tex_cap = 0;
}

void pc_gfx_drawable_size(int* w, int* h) {
    if (g_gfx_backend != NULL) {
        g_gfx_backend->drawable_size(w, h);
    } else {
        SDL_GL_GetDrawableSize(g_pc_window, w, h);
    }
    g_gfx_state.win_w = *w;
    g_gfx_state.win_h = *h;
}

void pc_gfx_set_vsync(int on) {
    if (GL_ON) {
        SDL_GL_SetSwapInterval(on);
    } else {
        g_gfx_backend->set_vsync(on);
    }
}

void pc_gfx_present(void) {
    if (GL_ON) {
        SDL_GL_SwapWindow(g_pc_window);
    } else {
        g_gfx_backend->present();
    }
}

void pc_gfx_handle_event(const SDL_Event* e) {
#ifdef PC_GFX_VULKAN_BUILT
    if (g_pc_gfx_api == PC_GFX_VULKAN) pc_gfx_vk_event(e);
#endif
    (void)e;
}

const char* pc_gfx_device_name(void) {
    return g_gfx_backend != NULL ? g_gfx_backend->device : s_device;
}

int pc_gfx_has_bc7(void) { return g_gfx_backend != NULL ? g_gfx_backend->bc7 : s_gl_bc7; }
int pc_gfx_has_s3tc(void) { return g_gfx_backend != NULL ? g_gfx_backend->s3tc : s_gl_s3tc; }

/* --- textures --- */

PCTex pc_gfx_tex_new(void) {
    if (GL_ON) {
        GLuint t = 0;

        glGenTextures(1, &t);
        return t;
    }
    return tex_alloc();
}

void pc_gfx_tex_delete(PCTex t) {
    if (t == 0) return;
    for (int u = 0; u < PC_GFX_UNITS; u++) {
        if (g_gfx_state.bound[u] == t) g_gfx_state.bound[u] = 0;
    }
    if (GL_ON) {
        GLuint name = t;

        glDeleteTextures(1, &name);
        return;
    }
    {
        PCGfxTex* o = pc_gfx_tex_obj(t);

        if (o == NULL) return;
        if (o->impl != NULL) g_gfx_backend->tex_free(o);
        o->used = 0;
        o->impl = NULL;
    }
}

void pc_gfx_active_unit(int unit) {
    g_gfx_state.unit = unit & (PC_GFX_UNITS - 1);
    if (GL_ON) glActiveTexture((GLenum)(GL_TEXTURE0 + g_gfx_state.unit));
}

void pc_gfx_bind(PCTex t) {
    g_gfx_state.bound[g_gfx_state.unit] = t;
    if (GL_ON) glBindTexture(GL_TEXTURE_2D, t);
}

static PCGfxTex* bound_obj(void) {
    return pc_gfx_tex_obj(g_gfx_state.bound[g_gfx_state.unit]);
}

void pc_gfx_tex_image(int w, int h, int format, const void* data, size_t size) {
    if (GL_ON) {
        gl_tex_image(w, h, format, data, size);
        return;
    }
    {
        PCGfxTex* o = bound_obj();

        if (o == NULL) return;
        if (!g_gfx_backend->tex_image(o, w, h, format, data, size)) s_tex_error = 1;
        o->version++;
    }
}

void pc_gfx_tex_subimage(int x, int y, int w, int h, const void* rgba) {
    if (GL_ON) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        return;
    }
    {
        PCGfxTex* o = bound_obj();

        if (o != NULL && o->impl != NULL) g_gfx_backend->tex_subimage(o, x, y, w, h, w, rgba);
    }
}

void pc_gfx_tex_generate_mipmap(void) {
    if (GL_ON) {
        glGenerateMipmap(GL_TEXTURE_2D);
        return;
    }
    {
        PCGfxTex* o = bound_obj();

        if (o != NULL && o->impl != NULL) {
            g_gfx_backend->tex_mipmap(o);
            o->version++;
        }
    }
}

void pc_gfx_tex_wrap(int wrap_s, int wrap_t) {
    if (GL_ON) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap(wrap_s));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap(wrap_t));
        return;
    }
    {
        PCGfxTex* o = bound_obj();

        if (o != NULL) {
            o->wrap_s = wrap_s == PC_WRAP_MIRROR || wrap_s == PC_WRAP_CLAMP ? wrap_s : PC_WRAP_REPEAT;
            o->wrap_t = wrap_t == PC_WRAP_MIRROR || wrap_t == PC_WRAP_CLAMP ? wrap_t : PC_WRAP_REPEAT;
            o->version++;
        }
    }
}

void pc_gfx_tex_filter(int min_filter, int mag_filter) {
    if (GL_ON) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter(min_filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter(mag_filter));
        return;
    }
    {
        PCGfxTex* o = bound_obj();

        if (o != NULL) {
            o->min_filter = min_filter;
            o->mag_filter = mag_filter == PC_FILTER_NEAREST ? PC_FILTER_NEAREST : PC_FILTER_LINEAR;
            o->version++;
        }
    }
}

void pc_gfx_tex_upload(PCTex t, int w, int h, int format, const void* data, size_t size, int filter) {
    if (GL_ON) {
        GLint last = 0;

        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter(filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter(filter));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl_tex_image(w, h, format, data, size);
        glBindTexture(GL_TEXTURE_2D, (GLuint)last);
        return;
    }
    {
        PCGfxTex* o = pc_gfx_tex_obj(t);

        if (o == NULL) return;
        o->min_filter = o->mag_filter = filter;
        o->wrap_s = o->wrap_t = PC_WRAP_CLAMP;
        if (!g_gfx_backend->tex_image(o, w, h, format, data, size)) s_tex_error = 1;
        o->version++;
    }
}

void pc_gfx_tex_update(PCTex t, int x, int y, int w, int h, int row_length, const void* rgba) {
    if (GL_ON) {
        GLint last = 0;

        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last);
        glBindTexture(GL_TEXTURE_2D, t);
#ifdef PC_GLES
        if (row_length != w) { /* no GL_UNPACK_ROW_LENGTH on ES 2; ES 3 has it */
        }
#endif
        glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, (GLuint)last);
        return;
    }
    {
        PCGfxTex* o = pc_gfx_tex_obj(t);

        if (o != NULL && o->impl != NULL) g_gfx_backend->tex_subimage(o, x, y, w, h, row_length, rgba);
    }
}

int pc_gfx_tex_error(void) {
    if (GL_ON) {
        int err = 0;

        for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {
            err = 1;
        }
        return err;
    }
    {
        int err = s_tex_error;

        s_tex_error = 0;
        return err;
    }
}

/* --- state --- */

void pc_gfx_viewport(int x, int y, int w, int h) {
    g_gfx_state.vp[0] = x;
    g_gfx_state.vp[1] = y;
    g_gfx_state.vp[2] = w;
    g_gfx_state.vp[3] = h;
    if (GL_ON) glViewport(x, y, w, h);
}

void pc_gfx_depth_range(float near_z, float far_z) {
    g_gfx_state.depth_near = near_z;
    g_gfx_state.depth_far = far_z;
    if (GL_ON) glDepthRange(near_z, far_z);
}

void pc_gfx_scissor_test(int on) {
    g_gfx_state.scissor_test = on != 0;
    if (GL_ON) {
        if (on) glEnable(GL_SCISSOR_TEST);
        else glDisable(GL_SCISSOR_TEST);
    }
}

void pc_gfx_scissor(int x, int y, int w, int h) {
    g_gfx_state.scissor[0] = x;
    g_gfx_state.scissor[1] = y;
    g_gfx_state.scissor[2] = w;
    g_gfx_state.scissor[3] = h;
    if (GL_ON) glScissor(x, y, w, h);
}

void pc_gfx_depth_test(int on) {
    g_gfx_state.depth_test = on != 0;
    if (GL_ON) {
        if (on) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
    }
}

void pc_gfx_depth_func(int cmp) {
    g_gfx_state.depth_func = cmp & 7;
    if (GL_ON) glDepthFunc(gl_cmp(cmp));
}

void pc_gfx_depth_write(int on) {
    g_gfx_state.depth_write = on != 0;
    if (GL_ON) glDepthMask(on ? GL_TRUE : GL_FALSE);
}

void pc_gfx_color_write(int rgb, int alpha) {
    g_gfx_state.color_write = rgb != 0;
    g_gfx_state.alpha_write = alpha != 0;
    if (GL_ON) glColorMask(rgb ? GL_TRUE : GL_FALSE, rgb ? GL_TRUE : GL_FALSE, rgb ? GL_TRUE : GL_FALSE,
                           alpha ? GL_TRUE : GL_FALSE);
}

void pc_gfx_cull(int mode) {
    g_gfx_state.cull = mode & 3;
    if (GL_ON) {
        switch (mode) {
            case PC_CULL_FRONT: glEnable(GL_CULL_FACE); glCullFace(GL_FRONT); break;
            case PC_CULL_BACK:  glEnable(GL_CULL_FACE); glCullFace(GL_BACK); break;
            case PC_CULL_ALL:   glEnable(GL_CULL_FACE); glCullFace(GL_FRONT_AND_BACK); break;
            default:            glDisable(GL_CULL_FACE); break;
        }
    }
}

void pc_gfx_blend(int on) {
    g_gfx_state.blend = on != 0;
    if (GL_ON) {
        if (on) glEnable(GL_BLEND);
        else glDisable(GL_BLEND);
    }
}

void pc_gfx_blend_func(int src, int dst) {
    g_gfx_state.blend_src = g_gfx_state.blend_src_a = src;
    g_gfx_state.blend_dst = g_gfx_state.blend_dst_a = dst;
    if (GL_ON) glBlendFunc(gl_factor(src), gl_factor(dst));
}

void pc_gfx_blend_func_separate(int src, int dst, int src_alpha, int dst_alpha) {
    g_gfx_state.blend_src = src;
    g_gfx_state.blend_dst = dst;
    g_gfx_state.blend_src_a = src_alpha;
    g_gfx_state.blend_dst_a = dst_alpha;
    if (GL_ON) glBlendFuncSeparate(gl_factor(src), gl_factor(dst), gl_factor(src_alpha), gl_factor(dst_alpha));
}

void pc_gfx_blend_subtract(int on) {
    g_gfx_state.blend_subtract = on != 0;
    if (GL_ON) glBlendEquation(on ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD);
}

void pc_gfx_line_width(float w) {
    g_gfx_state.line_width = w;
    if (GL_ON) glLineWidth(w);
}

void pc_gfx_point_size(float size) {
    if (GL_ON) glPointSize(size); /* (nothing on ES: points are sized in the shader) */
}

void pc_gfx_flush(void) {
    if (GL_ON) glFlush();
}

void pc_gfx_clear(int color, int depth, const float rgba[4], float depth_value) {
    if (GL_ON) {
        GLbitfield bits = 0;

        if (color) {
            glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
            bits |= GL_COLOR_BUFFER_BIT;
        }
        if (depth) {
            glClearDepth(depth_value);
            bits |= GL_DEPTH_BUFFER_BIT;
        }
        glClear(bits);
        return;
    }
    g_gfx_backend->clear(color, depth, rgba, depth_value);
}

/* --- drawing (OpenGL) --- */

static unsigned short s_quad_indices[PC_GX_MAX_VERTS / 4 * 6];

const unsigned short* pc_gfx_quad_indices(void) {
    if (s_quad_indices[1] == 0) {
        for (int q = 0; q < PC_GX_MAX_VERTS / 4; q++) {
            unsigned short b = (unsigned short)(q * 4);

            s_quad_indices[q * 6 + 0] = b;
            s_quad_indices[q * 6 + 1] = (unsigned short)(b + 1);
            s_quad_indices[q * 6 + 2] = (unsigned short)(b + 2);
            s_quad_indices[q * 6 + 3] = b;
            s_quad_indices[q * 6 + 4] = (unsigned short)(b + 2);
            s_quad_indices[q * 6 + 5] = (unsigned short)(b + 3);
        }
    }
    return s_quad_indices;
}

static void gl_gx_buffers(void) {
    size_t stride = sizeof(PCGXVertex);

    glGenVertexArrays(1, &s_gx_vao);
    glGenBuffers(1, &s_gx_vbo);
    glGenBuffers(1, &s_gx_ebo);
    /* the attribute pointers stay: the layout and the buffer never change */
    glBindVertexArray(s_gx_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_gx_vbo);
    glBufferData(GL_ARRAY_BUFFER, PC_GX_MAX_VERTS * sizeof(PCGXVertex), NULL, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(PCGXVertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(PCGXVertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)stride, (void*)offsetof(PCGXVertex, color0));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(PCGXVertex, texcoord));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_gx_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(s_quad_indices), pc_gfx_quad_indices(), GL_STATIC_DRAW);
    glBindVertexArray(0);
}

void pc_gfx_gx_init(void) {
    if (GL_ON) {
        gl_gx_buffers();
        pc_gx_tev_init();
    }
}

void pc_gfx_gx_shutdown(void) {
    if (GL_ON) {
        pc_gx_tev_shutdown();
        if (s_gx_ebo) glDeleteBuffers(1, &s_gx_ebo);
        if (s_gx_vbo) glDeleteBuffers(1, &s_gx_vbo);
        if (s_gx_vao) glDeleteVertexArrays(1, &s_gx_vao);
        s_gx_vao = s_gx_vbo = s_gx_ebo = 0;
    }
}

static GLenum gl_prim(int prim) {
    switch (prim) {
        case PC_PRIM_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
        case PC_PRIM_TRIANGLE_FAN:   return GL_TRIANGLE_FAN;
        case PC_PRIM_LINES:          return GL_LINES;
        case PC_PRIM_LINE_STRIP:     return GL_LINE_STRIP;
        case PC_PRIM_POINTS:         return GL_POINTS;
        default:                     return GL_TRIANGLES;
    }
}

void pc_gfx_draw_gx(int prim, const PCGXVertex* v, int count, unsigned changed) {
    if (!GL_ON) {
        g_gfx_backend->draw_gx(prim, v, count, changed);
        return;
    }
    pc_gx_tev_use(changed);
    glBindVertexArray(s_gx_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_gx_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * (GLsizeiptr)sizeof(PCGXVertex), v, GL_STREAM_DRAW);
    if (prim == PC_PRIM_QUADS) {
        glDrawElements(GL_TRIANGLES, count / 4 * 6, GL_UNSIGNED_SHORT, 0); /* two triangles a quad */
    } else {
        glDrawArrays(gl_prim(prim), 0, count);
    }
}

static GLuint gl_ui_shader(GLenum type, const char* body) {
    static const char* const prelude =
#ifdef PC_GLES
        "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
#else
        "#version 330 core\n"
#endif
        "#define VS_IN(n) layout(location = n) in\n"
        "#define VARYING_OUT(n) out\n"
        "#define VARYING_IN(n) in\n"
        "#define FRAG_OUT out\n"
        "#define DATA_BLOCK(b, name, type, var, n) uniform type var[n]\n"
        "#define TEXTURE(b) uniform sampler2D\n";
    const char* src[2] = { prelude, body };
    GLuint s = glCreateShader(type);
    GLint ok = 0;

    glShaderSource(s, 2, src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];

        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "[PC/gfx] UI shader: %s\n", log);
    }
    return s;
}

static int gl_ui_init(void) {
    GLuint vs = gl_ui_shader(GL_VERTEX_SHADER, pc_shader_ui_vert_glsl);
    GLuint fs = gl_ui_shader(GL_FRAGMENT_SHADER, pc_shader_ui_frag_glsl);
    GLint ok = 0;
    size_t stride = sizeof(PCGfxUIVertex);

    s_ui_program = glCreateProgram();
    glAttachShader(s_ui_program, vs);
    glAttachShader(s_ui_program, fs);
    glLinkProgram(s_ui_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(s_ui_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "[PC/gfx] UI program failed to link\n");
        return 0;
    }
    s_ui_space = glGetUniformLocation(s_ui_program, "ui_space");
    glUseProgram(s_ui_program);
    glUniform1i(glGetUniformLocation(s_ui_program, "u_tex"), 0);
    glUseProgram(g_gx.current_shader);
    glGenVertexArrays(1, &s_ui_vao);
    glGenBuffers(1, &s_ui_vbo);
    glGenBuffers(1, &s_ui_ebo);
    glBindVertexArray(s_ui_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_ui_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(PCGfxUIVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(PCGfxUIVertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(PCGfxUIVertex, lx));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)stride, (void*)offsetof(PCGfxUIVertex, color));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)offsetof(PCGfxUIVertex, kind));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ui_ebo);
    glBindVertexArray(0);
    return 1;
}

static PCTex s_white = 0;

void pc_gfx_draw_ui(PCTex tex, const PCGfxUIVertex* v, int count, const unsigned short* indices, int index_count,
                    float space_w, float space_h) {
    if (!GL_ON) {
        g_gfx_backend->draw_ui(tex, v, count, indices, index_count, space_w, space_h);
        return;
    }
    {
        static int ready = 0;
        GLint last_unit = 0, last_tex = 0;
        float space[4] = { space_w, space_h, 0.0f, 0.0f };

        if (ready == 0) ready = gl_ui_init() ? 1 : -1;
        if (ready < 0) return;
        if (tex == 0) {
            if (s_white == 0) {
                static const unsigned char white[4] = { 255, 255, 255, 255 };

                s_white = pc_gfx_tex_new();
                pc_gfx_tex_upload(s_white, 1, 1, PC_TEXF_RGBA8, white, 4, PC_FILTER_NEAREST);
            }
            tex = s_white;
        }
        glGetIntegerv(GL_ACTIVE_TEXTURE, &last_unit);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glUseProgram(s_ui_program);
        g_gx.current_shader = s_ui_program;
        glUniform4fv(s_ui_space, 1, space);
        glBindVertexArray(s_ui_vao);
        glBindBuffer(GL_ARRAY_BUFFER, s_ui_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * (GLsizeiptr)sizeof(PCGfxUIVertex), v, GL_STREAM_DRAW);
        if (indices != NULL) {
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)index_count * 2, indices, GL_STREAM_DRAW);
            glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_SHORT, 0);
        } else {
            glDrawArrays(GL_TRIANGLES, 0, count);
        }
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)last_tex);
        glActiveTexture((GLenum)last_unit);
    }
}

void pc_gfx_read_pixels(int x, int y, int w, int h, int rgba, void* out) {
    if (!GL_ON) {
        g_gfx_backend->read_pixels(x, y, w, h, rgba, out);
        return;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    if (rgba) {
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
        return;
    }
    glReadBuffer(GL_BACK);
#ifdef PC_GLES
    { /* ES only guarantees RGBA reads */
        unsigned char* p = (unsigned char*)out;

        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p);
        for (int i = 0; i < w * h; i++) {
            memmove(p + i * 3, p + i * 4, 3);
        }
    }
#else
    glReadPixels(x, y, w, h, GL_RGB, GL_UNSIGNED_BYTE, out);
#endif
}
