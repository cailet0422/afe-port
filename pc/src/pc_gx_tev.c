/* pc_gx_tev.c - the GX shader on OpenGL: the uber program and the programs
 * specialized for each configuration (pc_gx_config.h).
 *
 * gx.vert and gx.frag (pc/shaders, embedded by pc/tools/gen_shaders.py) get a
 * prelude: the #version, the macros the sources declare their inputs and
 * data with, and C_<name> for every configuration value - a read of gx_cfg[]
 * in the uber program, a literal in a specialized one. A specialized program
 * is compiled the first time its configuration is drawn; with
 * GL_KHR_parallel_shader_compile the driver compiles it on its own threads
 * and the uber program draws until it is ready.
 *
 * The blocks (PCGXVertBlock, PCGXFragBlock, the configuration) are plain
 * uniform arrays here, one glUniform4fv each: a uniform buffer written for
 * every draw made drivers wait for the GPU (Mesa's took 15-40 ms a frame).
 * Uniforms belong to a program, so each program remembers which version of
 * each block it has.
 *
 * Specialized programs are kept as program binaries in shader_cache/opengl.bin
 * and loaded at start, so each configuration is compiled only once (until the
 * driver or the shaders change). */
#include "pc_gx_internal.h"
#include "pc_settings.h"
#include "gen/pc_shaders.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#ifndef GL_COMPLETION_STATUS_KHR
#define GL_COMPLETION_STATUS_KHR 0x91B1
#endif

#define CACHE_DIR "shader_cache"
#define CACHE_FILE CACHE_DIR "/opengl.bin"
#define CACHE_MAGIC 0x31435847u  /* "GXC1" */
#define PRELUDE_VERSION 1        /* the prelude's text, for the cache */

#define CACHE_SIZE 2048 /* a power of two */

/* the header was made from the same configuration list */
typedef char pc_shaders_config_check[(PC_SHADERS_GX_CONFIG_COUNT == PC_GX_CONFIG_COUNT) ? 1 : -1];

typedef struct {
    GLuint program;
    GLint loc[3];       /* gx_vert, gx_frag, gx_cfg */
    unsigned have[3];   /* the block versions it has */
} Program;

typedef struct {
    PCGXShaderConfig config;
    unsigned hash;
    Program p;
    int state; /* 0 empty, 1 compiling, 2 ready, 3 failed */
} ProgramEntry;

static Program s_uber;
static unsigned s_version[3] = { 1, 1, 1 }; /* of the blocks in g_gx */
static ProgramEntry* s_cache = NULL;
static int s_count = 0;
static int s_parallel = 0; /* GL_KHR_parallel_shader_compile */
static void (*s_max_threads)(GLuint) = NULL;
static int s_compiled = 0, s_failed = 0, s_loaded = 0;
static Uint64 s_compile_ticks = 0;
static int s_binary = 0;      /* program binaries can be read back and loaded */
static FILE* s_cache_out = NULL;

static const char* const s_config_names[PC_GX_CONFIG_COUNT] = {
#define PC_GX_CONFIG_NAME(name) #name,
    PC_GX_CONFIG_FIELDS(PC_GX_CONFIG_NAME)
#undef PC_GX_CONFIG_NAME
};

/* --- source --- */

typedef struct {
    char* buf;
    size_t len, cap;
} Text;

static void text_add(Text* t, const char* s) {
    size_t n = strlen(s);

    if (t->len + n + 1 > t->cap) {
        t->cap = (t->len + n + 1) * 2;
        t->buf = (char*)realloc(t->buf, t->cap);
    }
    memcpy(t->buf + t->len, s, n + 1);
    t->len += n;
}

/* the prelude; config NULL for the uber program */
static void prelude(Text* t, const PCGXShaderConfig* config) {
    char line[160];

#ifdef PC_GLES
    text_add(t, "#version 300 es\n"
                "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n");
#else
    text_add(t, "#version 330 core\n");
#endif
    text_add(t, "#define VS_IN(n) layout(location = n) in\n"
                "#define VARYING_OUT(n) out\n"
                "#define VARYING_IN(n) in\n"
                "#define FRAG_OUT out\n"
                "#define DATA_BLOCK(b, name, type, var, n) uniform type var[n]\n"
                "#define TEXTURE(b) uniform sampler2D\n");
    snprintf(line, sizeof(line), "#define GX_UBER %d\n#define GX_CFG_VEC4S %d\n#define GX_CFG_SWAP_VEC4 %d\n"
             "#define GX_CFG_STAGE_VEC4 %d\n", config == NULL, PC_GX_CONFIG_VEC4S, PC_GX_CONFIG_SWAP_VEC4,
             PC_GX_CONFIG_STAGE_VEC4);
    text_add(t, line);
    if (config == NULL) {
        for (int i = 0; i < PC_GX_CONFIG_COUNT; i++) {
            snprintf(line, sizeof(line), "#define C_%s (gx_cfg[%d].%c)\n", s_config_names[i], i / 4, "xyzw"[i % 4]);
            text_add(t, line);
        }
    } else {
        for (int i = 0; i < PC_GX_CONFIG_COUNT; i++) {
            snprintf(line, sizeof(line), "#define C_%s %d\n", s_config_names[i], config->v[i]);
            text_add(t, line);
        }
    }
}

static GLuint compile_shader(GLenum type, const PCGXShaderConfig* config) {
    Text t = { NULL, 0, 0 };
    const char* src;
    GLuint shader = glCreateShader(type);

    prelude(&t, config);
    text_add(&t, type == GL_VERTEX_SHADER ? pc_shader_gx_vert_glsl : pc_shader_gx_frag_glsl);
    src = t.buf;
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    free(t.buf);
    return shader;
}

/* a program from its two shaders; with parallel compiling it may still be compiling */
static GLuint start_program(const PCGXShaderConfig* config) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, config);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, config);
    GLuint prog = glCreateProgram();

    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    if (s_binary && config != NULL) {
        glProgramParameteri(prog, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    }
    glLinkProgram(prog);
    glDeleteShader(vs); /* freed with the program */
    glDeleteShader(fs);
    return prog;
}

/* after linking: report errors, find the blocks, bind the samplers; 0 on failure */
static int finish_program(Program* p, const char* what) {
    static const char* const samplers[5] = { "u_texture0", "u_texture1", "u_texture2", "u_ind_tex0", "u_ind_tex1" };
    static const char* const blocks[3] = { "gx_vert", "gx_frag", "gx_cfg" };
    GLuint prog = p->program;
    GLint ok = 0;

    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLuint shaders[2];
        GLsizei n = 0;

        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "[PC/TEV] %s program failed to link: %s\n", what, log);
        glGetAttachedShaders(prog, 2, &n, shaders);
        for (int i = 0; i < n; i++) {
            glGetShaderInfoLog(shaders[i], sizeof(log), NULL, log);
            if (log[0]) fprintf(stderr, "[PC/TEV] %s\n", log);
        }
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        p->loc[i] = glGetUniformLocation(prog, blocks[i]); /* -1 when unused (gx_cfg when specialized) */
        p->have[i] = 0;
    }
    glUseProgram(prog);
    for (int i = 0; i < 5; i++) {
        GLint loc = glGetUniformLocation(prog, samplers[i]);

        if (loc >= 0) glUniform1i(loc, i);
    }
    glUseProgram(g_gx.current_shader);
    return 1;
}

static unsigned config_hash(const PCGXShaderConfig* c) {
    unsigned h = 2166136261u;

    for (int i = 0; i < PC_GX_CONFIG_COUNT; i++) {
        h = (h ^ (unsigned)c->v[i]) * 16777619u;
    }
    return h;
}

static int has_extension(const char* name) {
    GLint n = 0;

    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);

        if (ext != NULL && strcmp(ext, name) == 0) return 1;
    }
    return 0;
}

/* the slot of a configuration: its entry, or the empty one it would go in */
static ProgramEntry* find_slot(const PCGXShaderConfig* config, unsigned hash) {
    for (unsigned i = 0; i < CACHE_SIZE; i++) {
        ProgramEntry* e = &s_cache[(hash + i) & (CACHE_SIZE - 1)];

        if (e->state == 0 || (e->hash == hash && memcmp(&e->config, config, sizeof(*config)) == 0)) return e;
    }
    return NULL;
}

/* --- program binaries on disk --- */

static unsigned fnv(unsigned h, const void* data, size_t n) {
    const unsigned char* p = (const unsigned char*)data;

    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* what the binaries were made with: the driver and the shaders */
static unsigned cache_key(void) {
    const char* parts[5] = { (const char*)glGetString(GL_VENDOR), (const char*)glGetString(GL_RENDERER),
                             (const char*)glGetString(GL_VERSION), pc_shader_gx_vert_glsl, pc_shader_gx_frag_glsl };
    unsigned v[2] = { PRELUDE_VERSION, PC_GX_CONFIG_COUNT };
    unsigned h = fnv(2166136261u, v, sizeof(v));

    for (int i = 0; i < 5; i++) {
        if (parts[i] != NULL) h = fnv(h, parts[i], strlen(parts[i]) + 1);
    }
    return h;
}

static void cache_load(void) {
    unsigned head[2], key = cache_key();
    FILE* f = fopen(CACHE_FILE, "rb");
    int good = 0;

    if (f != NULL) {
        good = fread(head, sizeof(head), 1, f) == 1 && head[0] == CACHE_MAGIC && head[1] == key;
        while (good) {
            unsigned rec[2]; /* format, length */
            PCGXShaderConfig config;
            void* bin;

            if (fread(rec, sizeof(rec), 1, f) != 1 || fread(&config, sizeof(config), 1, f) != 1) break;
            if (rec[1] == 0 || rec[1] > (16u << 20) || (bin = malloc(rec[1])) == NULL) break;
            if (fread(bin, rec[1], 1, f) == 1) {
                unsigned hash = config_hash(&config);
                ProgramEntry* e = find_slot(&config, hash);
                GLint ok = 0;

                if (e != NULL && e->state == 0 && s_count < CACHE_SIZE * 3 / 4) {
                    GLuint prog = glCreateProgram();

                    glProgramBinary(prog, (GLenum)rec[0], bin, (GLsizei)rec[1]);
                    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
                    e->p.program = prog;
                    if (ok && finish_program(&e->p, "cached")) {
                        e->config = config;
                        e->hash = hash;
                        e->state = 2;
                        s_count++;
                        s_loaded++;
                    } else {
                        glDeleteProgram(prog); /* the driver no longer takes it: compiled again when drawn */
                        e->p.program = 0;
                    }
                }
            }
            free(bin);
        }
        fclose(f);
    }
#ifdef _WIN32
    _mkdir(CACHE_DIR);
#else
    mkdir(CACHE_DIR, 0755);
#endif
    s_cache_out = fopen(CACHE_FILE, good ? "ab" : "wb");
    if (s_cache_out != NULL && !good) {
        head[0] = CACHE_MAGIC;
        head[1] = key;
        fwrite(head, sizeof(head), 1, s_cache_out);
        fflush(s_cache_out);
    }
}

/* a new specialized program, at the end of the file (so a crash keeps it) */
static void cache_store(ProgramEntry* e) {
    GLint len = 0;
    GLsizei got = 0;
    GLenum format = 0;
    void* bin;

    if (s_cache_out == NULL) return;
    glGetProgramiv(e->p.program, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0 || (bin = malloc((size_t)len)) == NULL) return;
    glGetProgramBinary(e->p.program, len, &got, &format, bin);
    if (got > 0) {
        unsigned rec[2] = { (unsigned)format, (unsigned)got };

        fwrite(rec, sizeof(rec), 1, s_cache_out);
        fwrite(&e->config, sizeof(e->config), 1, s_cache_out);
        fwrite(bin, (size_t)got, 1, s_cache_out);
        fflush(s_cache_out);
    }
    free(bin);
}

static void program_ready(ProgramEntry* e) {
    e->state = finish_program(&e->p, "specialized") ? 2 : 3;
    s_compiled += e->state == 2;
    s_failed += e->state == 3;
    if (e->state == 2) cache_store(e);
}

void pc_gx_tev_init(void) {
    GLint formats = 0;

    s_uber.program = start_program(NULL);
    if (!finish_program(&s_uber, "uber")) {
        pc_fatal("The GX shader could not be compiled for this GPU (OpenGL).");
    }
    s_cache = (ProgramEntry*)calloc(CACHE_SIZE, sizeof(ProgramEntry));
#ifdef PC_GLES
    s_binary = 1;
#else
    s_binary = GLAD_GL_ARB_get_program_binary; /* (core from OpenGL 4.1, which drivers report as the extension) */
#endif
    if (s_binary) {
        glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
        s_binary = formats > 0;
    }
    if (s_binary && g_pc_settings.specialized_shaders) {
        cache_load();
    }
    s_parallel = has_extension("GL_KHR_parallel_shader_compile");
    if (s_parallel) {
        s_max_threads = (void (*)(GLuint))SDL_GL_GetProcAddress("glMaxShaderCompilerThreadsKHR");
        if (s_max_threads != NULL) s_max_threads(0xFFFFFFFFu); /* as many as the driver likes */
    }
    printf("[PC/TEV] GX shader: uber program ready; specialized programs %s%s, %d from %s\n",
           g_pc_settings.specialized_shaders ? "on" : "off",
           g_pc_settings.specialized_shaders && s_parallel ? " (compiled in parallel)" : "", s_loaded,
           s_binary ? CACHE_FILE : "no cache (the driver keeps no program binaries)");
}

void pc_gx_tev_shutdown(void) {
    if (s_cache_out != NULL) {
        fclose(s_cache_out);
        s_cache_out = NULL;
    }
    if (s_cache != NULL) {
        for (int i = 0; i < CACHE_SIZE; i++) {
            if (s_cache[i].p.program) glDeleteProgram(s_cache[i].p.program);
        }
        free(s_cache);
        s_cache = NULL;
    }
    s_count = 0;
    if (s_uber.program) {
        glDeleteProgram(s_uber.program);
        s_uber.program = 0;
    }
}

/* the program for a configuration: the uber one while a specialized one is not ready */
static Program* get_program(const PCGXShaderConfig* config) {
    static ProgramEntry* last = NULL;
    unsigned hash;
    ProgramEntry* e;

    if (!g_pc_settings.specialized_shaders || s_cache == NULL) return &s_uber;
    if (last != NULL && memcmp(&last->config, config, sizeof(*config)) == 0) {
        return &last->p;
    }
    hash = config_hash(config);
    e = find_slot(config, hash);
    if (e == NULL) return &s_uber;
    if (e->state == 0) {
        Uint64 t0;

        if (s_count >= CACHE_SIZE * 3 / 4) return &s_uber; /* full: the uber program draws the rest */
        t0 = SDL_GetPerformanceCounter();
        e->config = *config;
        e->hash = hash;
        e->p.program = start_program(config);
        e->state = 1;
        s_count++;
        if (!s_parallel) {
            program_ready(e);
        }
        s_compile_ticks += SDL_GetPerformanceCounter() - t0;
    }
    if (e->state == 1) {
        GLint done = 0;

        glGetProgramiv(e->p.program, GL_COMPLETION_STATUS_KHR, &done);
        if (!done) return &s_uber;
        program_ready(e);
    }
    if (e->state != 2) return &s_uber;
    last = e;
    return &e->p;
}

void pc_gx_tev_use(unsigned changed) {
    static const void* const data[3] = { &g_gx.vert_block, &g_gx.frag_block, &g_gx.shader_config };
    static const int vec4s[3] = { sizeof(PCGXVertBlock) / 16, sizeof(PCGXFragBlock) / 16, PC_GX_CONFIG_VEC4S };
    Program* p;

    if (changed & PC_GX_BLOCK_VERT) s_version[0]++;
    if (changed & PC_GX_BLOCK_FRAG) s_version[1]++;
    if (changed & PC_GX_BLOCK_CONFIG) s_version[2]++;
    p = get_program(&g_gx.shader_config);
    if (p->program != g_gx.current_shader) {
        glUseProgram(p->program);
        g_gx.current_shader = p->program;
    }
    for (int i = 0; i < 3; i++) {
        if (p->have[i] != s_version[i] && p->loc[i] >= 0) {
            if (i == 2) {
                glUniform4iv(p->loc[i], vec4s[i], (const GLint*)data[i]);
            } else {
                glUniform4fv(p->loc[i], vec4s[i], (const GLfloat*)data[i]);
            }
        }
        p->have[i] = s_version[i];
    }
}

/* once a frame: a line when new programs were made */
void pc_gx_tev_frame(void) {
    static int reported = 0;

    if (g_pc_verbose && s_compiled + s_failed != reported) {
        printf("[PC/TEV] %d specialized programs (%d failed), %.1f ms compiling\n", s_compiled, s_failed,
               (double)s_compile_ticks * 1000.0 / (double)SDL_GetPerformanceFrequency());
        reported = s_compiled + s_failed;
    }
}
