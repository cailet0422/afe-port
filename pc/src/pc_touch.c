/* pc_touch.c - see pc_touch.h */
#include "pc_touch.h"
#include "pc_pause_menu.h"
#include "pc_settings.h"
#include "pc_typing.h"

#include <dolphin/pad.h>
#include <math.h>

#define MAX_FINGERS 10
#define STICK_MAGNITUDE 80 /* full tilt, as pc_pad.c gives the keyboard */
#define STICK_DEADZONE 0.12f
#define HIT_SCALE 1.2f     /* buttons react a little outside what is drawn */
#define MAX_VERTICES 6144

enum {
    CTRL_A,
    CTRL_B,
    CTRL_X,
    CTRL_Y,
    CTRL_L,
    CTRL_R,
    CTRL_Z,
    CTRL_START,
    CTRL_UP,
    CTRL_DOWN,
    CTRL_LEFT,
    CTRL_RIGHT,
    CTRL_COUNT,
    CTRL_STICK = CTRL_COUNT,
    CTRL_NONE,
};

typedef struct {
    float x, y;   /* centre, pixels */
    float rx, ry; /* ellipse radii */
} Shape;

typedef struct {
    u16 button;
    u8 color[3];
    const char* label; /* NULL: a D-pad arrow */
} Control;

static const Control s_controls[CTRL_COUNT] = {
    { PAD_BUTTON_A, { 0x30, 0xB8, 0x80 }, "A" },
    { PAD_BUTTON_B, { 0xD8, 0x48, 0x48 }, "B" },
    { PAD_BUTTON_X, { 0x90, 0x90, 0x98 }, "X" },
    { PAD_BUTTON_Y, { 0x90, 0x90, 0x98 }, "Y" },
    { PAD_TRIGGER_L, { 0x80, 0x80, 0x88 }, "L" },
    { PAD_TRIGGER_R, { 0x80, 0x80, 0x88 }, "R" },
    { PAD_TRIGGER_Z, { 0x70, 0x58, 0xC8 }, "Z" },
    { PAD_BUTTON_START, { 0x80, 0x80, 0x88 }, "START" },
    { PAD_BUTTON_UP, { 0x70, 0x70, 0x78 }, NULL },
    { PAD_BUTTON_DOWN, { 0x70, 0x70, 0x78 }, NULL },
    { PAD_BUTTON_LEFT, { 0x70, 0x70, 0x78 }, NULL },
    { PAD_BUTTON_RIGHT, { 0x70, 0x70, 0x78 }, NULL },
};

typedef struct {
    int active;
    s64 id;
    float x, y; /* pixels */
    int ctrl;   /* CTRL_STICK, or the button under the finger (CTRL_NONE if none) */
} Finger;

static Finger s_fingers[MAX_FINGERS];
static float s_stick_x, s_stick_y; /* where the stick's finger landed */
static int s_hidden = 0;           /* a real controller was used since the last touch */

/* --- Layout ---------------------------------------------------------- */

static Shape s_shapes[CTRL_COUNT];
static Shape s_stick_home;
static float s_stick_radius;

/* Sizes follow the window's shorter side; everything hugs a corner or an edge */
static void layout(void) {
    float w = (float)g_pc_window_w;
    float h = (float)g_pc_window_h;
    float u = w < h ? w : h;
    float ax = w - 0.30f * u;
    float ay = h - 0.30f * u;
    float dx = 0.17f * u;
    float dy = 0.40f * u;

    s_shapes[CTRL_A] = (Shape){ ax, ay, 0.105f * u, 0.105f * u };
    s_shapes[CTRL_B] = (Shape){ ax - 0.20f * u, ay + 0.10f * u, 0.07f * u, 0.07f * u };
    s_shapes[CTRL_X] = (Shape){ ax + 0.18f * u, ay - 0.07f * u, 0.07f * u, 0.07f * u };
    s_shapes[CTRL_Y] = (Shape){ ax - 0.06f * u, ay - 0.19f * u, 0.07f * u, 0.07f * u };
    s_shapes[CTRL_L] = (Shape){ 0.17f * u, 0.08f * u, 0.13f * u, 0.05f * u };
    s_shapes[CTRL_R] = (Shape){ w - 0.17f * u, 0.08f * u, 0.13f * u, 0.05f * u };
    s_shapes[CTRL_Z] = (Shape){ w - 0.17f * u, 0.20f * u, 0.09f * u, 0.045f * u };
    s_shapes[CTRL_START] = (Shape){ w * 0.5f, 0.07f * u, 0.12f * u, 0.045f * u };
    s_shapes[CTRL_UP] = (Shape){ dx, dy - 0.08f * u, 0.045f * u, 0.045f * u };
    s_shapes[CTRL_DOWN] = (Shape){ dx, dy + 0.08f * u, 0.045f * u, 0.045f * u };
    s_shapes[CTRL_LEFT] = (Shape){ dx - 0.08f * u, dy, 0.045f * u, 0.045f * u };
    s_shapes[CTRL_RIGHT] = (Shape){ dx + 0.08f * u, dy, 0.045f * u, 0.045f * u };
    s_stick_radius = 0.16f * u;
    s_stick_home = (Shape){ 0.32f * u, h - 0.30f * u, s_stick_radius, s_stick_radius };
}

static int hit_control(float x, float y) {
    int i;

    for (i = 0; i < CTRL_COUNT; i++) {
        const Shape* s = &s_shapes[i];
        float nx = (x - s->x) / (s->rx * HIT_SCALE);
        float ny = (y - s->y) / (s->ry * HIT_SCALE);

        if (nx * nx + ny * ny <= 1.0f) {
            return i;
        }
    }
    return CTRL_NONE;
}

/* The stick takes the lower part of the left half */
static int in_stick_zone(float x, float y) {
    return x < g_pc_window_w * 0.45f && y > g_pc_window_h * 0.45f;
}

/* --- Input ----------------------------------------------------------- */

static int enabled(void) {
    return g_pc_settings.touch_controls != 0;
}

/* The pause menu reads keys only */
static void press_paused(int ctrl) {
    SDL_Event key;

    memset(&key, 0, sizeof(key));
    key.type = SDL_KEYDOWN;
    switch (ctrl) {
        case CTRL_A:     key.key.keysym.sym = SDLK_RETURN; break;
        case CTRL_B:
        case CTRL_START: key.key.keysym.sym = SDLK_ESCAPE; break;
        case CTRL_UP:    key.key.keysym.sym = SDLK_UP; break;
        case CTRL_DOWN:  key.key.keysym.sym = SDLK_DOWN; break;
        case CTRL_LEFT:  key.key.keysym.sym = SDLK_LEFT; break;
        case CTRL_RIGHT: key.key.keysym.sym = SDLK_RIGHT; break;
        default: return;
    }
    pc_pause_menu_handle_event(&key);
}

static Finger* find_finger(s64 id) {
    int i;

    for (i = 0; i < MAX_FINGERS; i++) {
        if (s_fingers[i].active && s_fingers[i].id == id) {
            return &s_fingers[i];
        }
    }
    return NULL;
}

static int stick_held(void) {
    int i;

    for (i = 0; i < MAX_FINGERS; i++) {
        if (s_fingers[i].active && s_fingers[i].ctrl == CTRL_STICK) {
            return 1;
        }
    }
    return 0;
}

void pc_touch_finger_down(s64 id, float x, float y) {
    Finger* f = find_finger(id);
    int i;

    if (!enabled()) {
        return;
    }
    s_hidden = 0;
    for (i = 0; f == NULL && i < MAX_FINGERS; i++) {
        if (!s_fingers[i].active) {
            f = &s_fingers[i];
        }
    }
    if (f == NULL) {
        return;
    }
    layout();
    f->active = 1;
    f->id = id;
    f->x = x * g_pc_window_w;
    f->y = y * g_pc_window_h;
    f->ctrl = hit_control(f->x, f->y);

    if (f->ctrl != CTRL_NONE) {
        if (g_pc_paused) {
            press_paused(f->ctrl);
        }
    } else if (in_stick_zone(f->x, f->y) && !stick_held()) {
        float r = s_stick_radius;

        /* The stick centres where the thumb lands, kept on screen */
        f->ctrl = CTRL_STICK;
        s_stick_x = SDL_clamp(f->x, r, g_pc_window_w - r);
        s_stick_y = SDL_clamp(f->y, r, g_pc_window_h - r);
    } else if (g_pc_editor_active && pc_typing_system_ime()) {
        SDL_StartTextInput(); /* brings back a keyboard the user put away */
    }
}

void pc_touch_finger_move(s64 id, float x, float y) {
    Finger* f = find_finger(id);

    if (f == NULL) {
        return;
    }
    f->x = x * g_pc_window_w;
    f->y = y * g_pc_window_h;
    if (f->ctrl != CTRL_STICK) {
        /* Sliding between buttons presses the one under the finger */
        int ctrl;

        layout();
        ctrl = hit_control(f->x, f->y);
        if (ctrl != f->ctrl && ctrl != CTRL_NONE && g_pc_paused) {
            press_paused(ctrl);
        }
        f->ctrl = ctrl;
    }
}

void pc_touch_finger_up(s64 id) {
    Finger* f = find_finger(id);

    if (f != NULL) {
        f->active = 0;
    }
}

void pc_touch_handle_event(const SDL_Event* event) {
    switch (event->type) {
        case SDL_FINGERDOWN:
            pc_touch_finger_down((s64)event->tfinger.fingerId, event->tfinger.x, event->tfinger.y);
            break;
        case SDL_FINGERMOTION:
            pc_touch_finger_move((s64)event->tfinger.fingerId, event->tfinger.x, event->tfinger.y);
            break;
        case SDL_FINGERUP:
            pc_touch_finger_up((s64)event->tfinger.fingerId);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            s_hidden = 1;
            break;
        default:
            break;
    }
}

void pc_touch_read(u16* buttons, s8* stick_x, s8* stick_y) {
    int i;

    if (!enabled()) {
        return;
    }
    for (i = 0; i < MAX_FINGERS; i++) {
        const Finger* f = &s_fingers[i];

        if (!f->active) {
            continue;
        }
        if (f->ctrl == CTRL_STICK) {
            float dx = (f->x - s_stick_x) / s_stick_radius;
            float dy = (f->y - s_stick_y) / s_stick_radius;
            float len = sqrtf(dx * dx + dy * dy);

            if (len < STICK_DEADZONE) {
                continue;
            }
            if (len > 1.0f) {
                dx /= len;
                dy /= len;
            }
            *stick_x = (s8)lrintf(dx * STICK_MAGNITUDE);
            *stick_y = (s8)lrintf(-dy * STICK_MAGNITUDE);
        } else if (f->ctrl != CTRL_NONE) {
            *buttons |= s_controls[f->ctrl].button;
        }
    }
}

/* --- Drawing --------------------------------------------------------- */

typedef struct {
    float x, y;   /* pixels */
    float lx, ly; /* position inside an ellipse, -1 to 1 */
    u8 color[4];
    float kind;   /* 0 solid, 1 ellipse, 2 ring */
    float inner;  /* ring: inner radius, 0 to 1 */
} Vertex;

static Vertex s_vertices[MAX_VERTICES];
static int s_vertex_count;
static GLuint s_program, s_vao, s_vbo;
static GLint s_size_uniform;

static const char* s_vertex_shader =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_local;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "layout(location = 3) in vec2 a_shape;\n"
    "uniform vec2 u_size;\n"
    "out vec2 v_local;\n"
    "out vec4 v_color;\n"
    "out vec2 v_shape;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);\n"
    "    v_local = a_local;\n"
    "    v_color = a_color;\n"
    "    v_shape = a_shape;\n"
    "}\n";

static const char* s_fragment_shader =
    "#version 330 core\n"
    "in vec2 v_local;\n"
    "in vec4 v_color;\n"
    "in vec2 v_shape;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    float a = 1.0;\n"
    "    if (v_shape.x > 0.5) {\n"
    "        float d = length(v_local);\n"
    "        float aa = max(fwidth(d), 0.001);\n"
    "        a = 1.0 - smoothstep(1.0 - aa, 1.0, d);\n"
    "        if (v_shape.x > 1.5) {\n"
    "            a *= smoothstep(v_shape.y - aa, v_shape.y, d);\n"
    "        }\n"
    "    }\n"
    "    frag_color = vec4(v_color.rgb, v_color.a * a);\n"
    "}\n";

static GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    GLint ok = 0;

    pc_gl_shader_source(shader, source);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "[TOUCH] shader: %s\n", log);
    }
    return shader;
}

static int init_gl(void) {
    GLuint vs = compile(GL_VERTEX_SHADER, s_vertex_shader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, s_fragment_shader);
    GLint ok = 0;

    s_program = glCreateProgram();
    glAttachShader(s_program, vs);
    glAttachShader(s_program, fs);
    glLinkProgram(s_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(s_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "[TOUCH] cannot build the on-screen pad's shader\n");
        return 0;
    }
    s_size_uniform = glGetUniformLocation(s_program, "u_size");

    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, lx));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, kind));
    return 1;
}

static void vertex(float x, float y, float lx, float ly, const u8* color, float kind, float inner) {
    Vertex* v;

    if (s_vertex_count >= MAX_VERTICES) {
        return;
    }
    v = &s_vertices[s_vertex_count++];
    v->x = x;
    v->y = y;
    v->lx = lx;
    v->ly = ly;
    memcpy(v->color, color, 4);
    v->kind = kind;
    v->inner = inner;
}

static void quad(float x0, float y0, float x1, float y1, const u8* color, float kind, float inner) {
    vertex(x0, y0, -1, -1, color, kind, inner);
    vertex(x1, y0, 1, -1, color, kind, inner);
    vertex(x1, y1, 1, 1, color, kind, inner);
    vertex(x0, y0, -1, -1, color, kind, inner);
    vertex(x1, y1, 1, 1, color, kind, inner);
    vertex(x0, y1, -1, 1, color, kind, inner);
}

static void ellipse(float x, float y, float rx, float ry, const u8* color) {
    quad(x - rx, y - ry, x + rx, y + ry, color, 1, 0);
}

static void ring(float x, float y, float rx, float ry, float inner, const u8* color) {
    quad(x - rx, y - ry, x + rx, y + ry, color, 2, inner);
}

static void triangle(float x0, float y0, float x1, float y1, float x2, float y2, const u8* color) {
    vertex(x0, y0, 0, 0, color, 0, 0);
    vertex(x1, y1, 0, 0, color, 0, 0);
    vertex(x2, y2, 0, 0, color, 0, 0);
}

/* 5x7 letters for the labels, top row first, bit 4 = left column */
static const u8* glyph(char c) {
    static const u8 a[7] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
    static const u8 b[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
    static const u8 l[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
    static const u8 r[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
    static const u8 s[7] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
    static const u8 t[7] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
    static const u8 x[7] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
    static const u8 y[7] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
    static const u8 z[7] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };

    switch (c) {
        case 'A': return a;
        case 'B': return b;
        case 'L': return l;
        case 'R': return r;
        case 'S': return s;
        case 'T': return t;
        case 'X': return x;
        case 'Y': return y;
        case 'Z': return z;
        default: return NULL;
    }
}

static void text(float cx, float cy, float height, const char* str, const u8* color) {
    float px = height / 7.0f;
    float x = cx - (strlen(str) * 6 - 1) * px * 0.5f;
    float y = cy - height * 0.5f;

    for (; *str != '\0'; str++, x += 6 * px) {
        const u8* rows = glyph(*str);
        int row, col;

        for (row = 0; rows != NULL && row < 7; row++) {
            for (col = 0; col < 5; col++) {
                if (rows[row] & (0x10 >> col)) {
                    float x0 = x + col * px;
                    float y0 = y + row * px;

                    quad(x0, y0, x0 + px, y0 + px, color, 0, 0);
                }
            }
        }
    }
}

static void arrow(const Shape* s, int ctrl, const u8* color) {
    float r = s->rx * 0.45f;

    switch (ctrl) {
        case CTRL_UP:    triangle(s->x, s->y - r, s->x - r, s->y + r * 0.6f, s->x + r, s->y + r * 0.6f, color); break;
        case CTRL_DOWN:  triangle(s->x, s->y + r, s->x + r, s->y - r * 0.6f, s->x - r, s->y - r * 0.6f, color); break;
        case CTRL_LEFT:  triangle(s->x - r, s->y, s->x + r * 0.6f, s->y + r, s->x + r * 0.6f, s->y - r, color); break;
        case CTRL_RIGHT: triangle(s->x + r, s->y, s->x - r * 0.6f, s->y - r, s->x - r * 0.6f, s->y + r, color); break;
    }
}

static void build(void) {
    static const u8 white[4] = { 0xFF, 0xFF, 0xFF, 0xE0 };
    static const u8 base[4] = { 0x20, 0x20, 0x28, 0x50 };
    static const u8 edge[4] = { 0xFF, 0xFF, 0xFF, 0x70 };
    int pressed[CTRL_COUNT + 1] = { 0 };
    float knob_x = s_stick_home.x;
    float knob_y = s_stick_home.y;
    float stick_x = s_stick_home.x;
    float stick_y = s_stick_home.y;
    float r = s_stick_radius;
    int i;

    for (i = 0; i < MAX_FINGERS; i++) {
        const Finger* f = &s_fingers[i];

        if (!f->active || f->ctrl == CTRL_NONE) {
            continue;
        }
        pressed[f->ctrl] = 1;
        if (f->ctrl == CTRL_STICK) {
            float dx = f->x - s_stick_x;
            float dy = f->y - s_stick_y;
            float len = sqrtf(dx * dx + dy * dy);

            if (len > r) {
                dx *= r / len;
                dy *= r / len;
            }
            stick_x = s_stick_x;
            stick_y = s_stick_y;
            knob_x = stick_x + dx;
            knob_y = stick_y + dy;
        }
    }

    s_vertex_count = 0;
    ellipse(stick_x, stick_y, r, r, base);
    ring(stick_x, stick_y, r, r, 0.94f, edge);
    {
        u8 knob[4] = { 0xC8, 0xC8, 0xD0, (u8)(pressed[CTRL_STICK] ? 0xB0 : 0x70) };

        ellipse(knob_x, knob_y, r * 0.5f, r * 0.5f, knob);
    }
    for (i = 0; i < CTRL_COUNT; i++) {
        const Shape* s = &s_shapes[i];
        const Control* c = &s_controls[i];
        u8 fill[4] = { c->color[0], c->color[1], c->color[2], (u8)(pressed[i] ? 0xC0 : 0x68) };

        ellipse(s->x, s->y, s->rx, s->ry, fill);
        ring(s->x, s->y, s->rx, s->ry, 0.9f, edge);
        if (c->label != NULL) {
            text(s->x, s->y, (s->ry < s->rx ? s->ry : s->rx) * 0.75f, c->label, white);
        } else {
            arrow(s, i, white);
        }
    }
}

extern void pc_gx_restore_after_nes(void); /* rebinds the game's GL state after foreign drawing */

void pc_touch_draw(void) {
    static int gl_state = 0; /* 0 not built, 1 ready, -1 failed */

    if (!enabled() || s_hidden || g_pc_skip_draw) {
        return;
    }
    if (gl_state == 0) {
        gl_state = init_gl() ? 1 : -1;
    }
    if (gl_state < 0) {
        return;
    }
    layout();
    build();

    glViewport(0, 0, g_pc_window_w, g_pc_window_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    /* keep the frame's alpha: some Android surfaces show it through */
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glUseProgram(s_program);
    glUniform2f(s_size_uniform, (float)g_pc_window_w, (float)g_pc_window_h);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, s_vertex_count * sizeof(Vertex), s_vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, s_vertex_count);

    pc_gx_restore_after_nes();
}
