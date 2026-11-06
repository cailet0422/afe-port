/* pc_touch.c - see pc_touch.h */
#include "pc_touch.h"
#include "pc_pause_menu.h"
#include "pc_settings.h"
#include "pc_typing.h"
#include "pc_autosave.h"
#include "pc_ko.h"
#include "pc_minimap.h"
#include "pc_gui.h"
#include "pc_touch_full.h"

#include <dolphin/pad.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FINGERS 10
#define STICK_MAGNITUDE 80 /* full tilt, as pc_pad.c gives the keyboard */
#define STICK_DEADZONE 0.12f
#define HIT_SCALE 1.2f     /* buttons react a little outside what is drawn */
#define MAX_VERTICES 6144
#define TAP_MS 300         /* a finger up this soon, */
#define TAP_SLOP 0.03f     /* having moved less than this (of the shorter side), is a tap */

enum {
    CTRL_A,
    CTRL_B,
    CTRL_X,
    CTRL_Y,
    CTRL_L,
    CTRL_R,
    CTRL_Z,
    CTRL_START,
    CTRL_SAVE, /* not a pad button: saves (pc_autosave_request_save) */
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
    { 0, { 0x40, 0x90, 0xC8 }, "SAVE" },
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
    float x0, y0;    /* where it landed */
    Uint32 down_ms;
    int minimap_tap; /* landed on the minimap and has not moved off: a tap opens the map */
} Finger;

static Finger s_fingers[MAX_FINGERS];
static float s_stick_x, s_stick_y; /* where the stick's finger landed */
static int s_hidden = 0;           /* a real controller was used since the last touch */

/* --- Layout ---------------------------------------------------------- */

static Shape s_shapes[CTRL_COUNT];
static Shape s_stick_home;
static float s_stick_radius;
static int s_preview = 0; /* the layout editor shows the pad over the menus */

/* What the layout editor moved or resized (settings.ini touch_layout,
 * "A=0.71,0.66,1.2;DPAD=...": the piece's centre as fractions of the
 * window, its size factor). The pieces are the pad's buttons up to SAVE (as
 * their CTRL_), the D-pad's four as one and the stick. */
static const char* const s_piece_keys[PC_TOUCH_PIECE_COUNT] = {
    "A", "B", "X", "Y", "L", "R", "Z", "START", "SAVE", "DPAD", "STICK",
};

typedef struct {
    int set;
    float x, y, size;
} Override;

static Override s_over[PC_TOUCH_PIECE_COUNT];
static char s_parsed[sizeof(g_pc_settings.touch_layout)];
static int s_parsed_once = 0;

static int piece_by_key(const char* key, size_t len) {
    int i;

    for (i = 0; i < PC_TOUCH_PIECE_COUNT; i++) {
        if (strlen(s_piece_keys[i]) == len && memcmp(s_piece_keys[i], key, len) == 0) {
            return i;
        }
    }
    return -1;
}

static int sane(float x, float y, float size) {
    return isfinite(x) && isfinite(y) && isfinite(size) && x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f &&
           size >= PC_TOUCH_SIZE_MIN && size <= PC_TOUCH_SIZE_MAX;
}

/* Reads touch_layout again when it changed (the settings file, the editor) */
static void parse_layout(void) {
    const char* p = g_pc_settings.touch_layout;

    if (s_parsed_once && strcmp(s_parsed, p) == 0) {
        return;
    }
    s_parsed_once = 1;
    snprintf(s_parsed, sizeof(s_parsed), "%s", p);
    memset(s_over, 0, sizeof(s_over));
    while (*p != '\0') {
        const char* end = strchr(p, ';');
        const char* eq = strchr(p, '=');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);

        if (eq != NULL && (size_t)(eq - p) < len) {
            int piece = piece_by_key(p, (size_t)(eq - p));
            char num[64];
            float x, y, size;
            size_t nlen = len - (size_t)(eq - p) - 1;

            if (nlen >= sizeof(num)) {
                nlen = sizeof(num) - 1;
            }
            memcpy(num, eq + 1, nlen);
            num[nlen] = '\0';
            if (piece >= 0 && sscanf(num, "%f,%f,%f", &x, &y, &size) == 3 && sane(x, y, size)) {
                s_over[piece].set = 1;
                s_over[piece].x = x;
                s_over[piece].y = y;
                s_over[piece].size = size;
            }
        }
        p += len;
        if (*p == ';') {
            p++;
        }
    }
}

static void write_layout(void) {
    char* out = g_pc_settings.touch_layout;
    size_t cap = sizeof(g_pc_settings.touch_layout);
    size_t n = 0;
    int i;

    out[0] = '\0';
    for (i = 0; i < PC_TOUCH_PIECE_COUNT; i++) {
        int k;

        if (!s_over[i].set) {
            continue;
        }
        k = snprintf(out + n, cap - n, "%s%s=%.4f,%.4f,%.3f", n > 0 ? ";" : "", s_piece_keys[i], s_over[i].x,
                     s_over[i].y, s_over[i].size);
        if (k < 0 || (size_t)k >= cap - n) {
            out[n] = '\0'; /* (cannot happen: 11 pieces fit) */
            break;
        }
        n += (size_t)k;
    }
    snprintf(s_parsed, sizeof(s_parsed), "%s", out);
    s_parsed_once = 1;
}

/* Sizes follow the window's shorter side; everything hugs a corner or an edge
 * unless the layout editor moved it */
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
    /* left of Start: the chat button (pc_gui_mp.cpp) is on its right */
    s_shapes[CTRL_SAVE] = (Shape){ w * 0.5f - 0.24f * u, 0.07f * u, 0.08f * u, 0.045f * u };
    s_shapes[CTRL_UP] = (Shape){ dx, dy - 0.08f * u, 0.045f * u, 0.045f * u };
    s_shapes[CTRL_DOWN] = (Shape){ dx, dy + 0.08f * u, 0.045f * u, 0.045f * u };
    s_shapes[CTRL_LEFT] = (Shape){ dx - 0.08f * u, dy, 0.045f * u, 0.045f * u };
    s_shapes[CTRL_RIGHT] = (Shape){ dx + 0.08f * u, dy, 0.045f * u, 0.045f * u };
    s_stick_radius = 0.16f * u;
    s_stick_home = (Shape){ 0.32f * u, h - 0.30f * u, s_stick_radius, s_stick_radius };

    parse_layout();
    {
        int i;

        for (i = 0; i <= CTRL_SAVE; i++) {
            if (s_over[i].set) {
                Shape* s = &s_shapes[i];

                s->x = s_over[i].x * w;
                s->y = s_over[i].y * h;
                s->rx *= s_over[i].size;
                s->ry *= s_over[i].size;
            }
        }
        if (s_over[PC_TOUCH_PIECE_DPAD].set) {
            const Override* o = &s_over[PC_TOUCH_PIECE_DPAD];

            for (i = CTRL_UP; i <= CTRL_RIGHT; i++) {
                Shape* s = &s_shapes[i];

                s->x = o->x * w + (s->x - dx) * o->size;
                s->y = o->y * h + (s->y - dy) * o->size;
                s->rx *= o->size;
                s->ry *= o->size;
            }
        }
        if (s_over[PC_TOUCH_PIECE_STICK].set) {
            const Override* o = &s_over[PC_TOUCH_PIECE_STICK];

            s_stick_radius *= o->size;
            s_stick_home = (Shape){ o->x * w, o->y * h, s_stick_radius, s_stick_radius };
        }
    }
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

/* The stick takes the lower part of the left half (or the quarter the
 * layout editor moved it to) */
static int in_stick_zone(float x, float y) {
    float w = (float)g_pc_window_w, h = (float)g_pc_window_h;
    int right = s_stick_home.x > w * 0.5f;
    int top = s_stick_home.y < h * 0.5f;

    return (right ? x > w * 0.55f : x < w * 0.45f) && (top ? y < h * 0.55f : y > h * 0.45f);
}

/* --- Input ----------------------------------------------------------- */

/* The GameCube pad: its own mode, or the full touch mode's when it wants it */
static int enabled(void) {
    return g_pc_settings.touch_controls == 1 || (g_pc_settings.touch_controls == 2 && pc_touch_full_pad_shown());
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

    if (pc_gui_touch(0, id, x, y)) {
        return; /* the menus (while paused) */
    }
    if (pc_ko_osk_touch(1, x, y)) {
        return; /* a key of the editor's on-screen keyboard */
    }
    if (pc_touch_full_finger_down(id, x, y)) {
        return; /* the full touch mode's (pc_touch_full.c) */
    }
    if (!enabled()) {
        if (pc_touch_full_on()) {
            return;
        }
        pc_minimap_touch(x, y); /* no pad: the minimap opens the map at once */
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
    f->x0 = f->x;
    f->y0 = f->y;
    f->down_ms = SDL_GetTicks();
    /* the minimap is in the stick's corner: a quick tap on it opens the map,
     * a thumb that lands there and moves steers */
    f->minimap_tap = f->ctrl == CTRL_NONE && pc_minimap_at(x, y);

    if (f->ctrl == CTRL_SAVE) {
        pc_autosave_request_save();
    } else if (f->ctrl != CTRL_NONE) {
    } else if (in_stick_zone(f->x, f->y) && !stick_held()) {
        float r = s_stick_radius;

        /* The stick centres where the thumb lands, kept on screen */
        f->ctrl = CTRL_STICK;
        s_stick_x = SDL_clamp(f->x, r, g_pc_window_w - r);
        s_stick_y = SDL_clamp(f->y, r, g_pc_window_h - r);
    } else if (pc_typing_editor_takes_text() && pc_typing_system_ime()) {
        SDL_StartTextInput(); /* brings back a keyboard the user put away */
    } else if (!g_pc_paused && pc_minimap_map_open()) {
        f->active = 0; /* a tap outside the controls closes the map */
        pc_minimap_request_toggle();
    }
}

void pc_touch_finger_move(s64 id, float x, float y) {
    Finger* f = find_finger(id);

    if (f == NULL) {
        pc_touch_full_finger_move(id, x, y);
        pc_gui_touch(1, id, x, y);
        return;
    }
    f->x = x * g_pc_window_w;
    f->y = y * g_pc_window_h;
    if (f->minimap_tap) {
        float u = (float)(g_pc_window_w < g_pc_window_h ? g_pc_window_w : g_pc_window_h);

        if (fabsf(f->x - f->x0) > TAP_SLOP * u || fabsf(f->y - f->y0) > TAP_SLOP * u) {
            f->minimap_tap = 0;
        }
    }
    if (f->ctrl != CTRL_STICK) {
        /* Sliding between buttons presses the one under the finger */
        int ctrl;

        layout();
        ctrl = hit_control(f->x, f->y);
        f->ctrl = ctrl;
    }
}

void pc_touch_finger_up(s64 id) {
    Finger* f = find_finger(id);

    if (f == NULL && pc_touch_full_finger_up(id)) {
        return;
    }
    if (f == NULL && pc_gui_touch(2, id, 0.0f, 0.0f)) {
        return;
    }
    pc_ko_osk_touch(0, 0.0f, 0.0f);

    if (f != NULL) {
        if (f->minimap_tap && SDL_GetTicks() - f->down_ms <= TAP_MS && !g_pc_paused) {
            pc_minimap_request_toggle();
        }
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

    pc_touch_full_read(buttons, stick_x, stick_y);
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

/* x, y pixels; lx, ly the position inside an ellipse, -1 to 1; kind 0
 * solid, 1 ellipse, 2 ring; inner: a ring's inner radius, 0 to 1 */
typedef PCGfxUIVertex Vertex;

static Vertex s_vertices[MAX_VERTICES];
static int s_vertex_count;

static void vertex(float x, float y, float lx, float ly, const u8* color, float kind, float inner) {
    Vertex* v;

    if (s_vertex_count >= MAX_VERTICES) {
        return;
    }
    v = &s_vertices[s_vertex_count++];
    memset(v, 0, sizeof(*v));
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
    static const u8 e[7] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
    static const u8 l[7] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
    static const u8 r[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
    static const u8 s[7] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
    static const u8 t[7] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
    static const u8 v[7] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
    static const u8 x[7] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
    static const u8 y[7] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
    static const u8 z[7] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };

    switch (c) {
        case 'A': return a;
        case 'B': return b;
        case 'E': return e;
        case 'L': return l;
        case 'R': return r;
        case 'S': return s;
        case 'T': return t;
        case 'V': return v;
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
    if (PC_DRAW_SKIPPED()) {
        return;
    }
    if (!s_preview && (!enabled() || s_hidden || g_pc_paused || pc_gui_chat_active())) {
        return; /* (the menus take the fingers while paused, the chat box while it is open) */
    }
    layout();
    build();

    pc_gfx_viewport(0, 0, g_pc_window_w, g_pc_window_h);
    pc_gfx_depth_test(0);
    pc_gfx_scissor_test(0);
    pc_gfx_cull(PC_CULL_NONE);
    pc_gfx_color_write(1, 1);
    pc_gfx_blend(1);
    pc_gfx_blend_subtract(0);
    /* keep the frame's alpha: some Android surfaces show it through */
    pc_gfx_blend_func_separate(PC_BF_SRC_ALPHA, PC_BF_ONE_MINUS_SRC_ALPHA, PC_BF_ZERO, PC_BF_ONE);
    pc_gfx_draw_ui(0, s_vertices, s_vertex_count, NULL, 0, (float)g_pc_window_w, (float)g_pc_window_h);

    pc_gx_restore_after_nes();
}

/* --- Layout editor (Settings) ----------------------------------------- */

void pc_touch_preview(int on) {
    s_preview = on != 0;
}

const char* pc_touch_piece_label(int piece) {
    return piece >= 0 && piece < PC_TOUCH_PIECE_COUNT ? s_piece_keys[piece] : "";
}

void pc_touch_piece_get(int piece, float* x, float* y, float* rx, float* ry, float* size) {
    float w = (float)(g_pc_window_w > 0 ? g_pc_window_w : 1);
    float h = (float)(g_pc_window_h > 0 ? g_pc_window_h : 1);
    Shape s;

    layout();
    if (piece == PC_TOUCH_PIECE_STICK) {
        s = s_stick_home;
    } else if (piece == PC_TOUCH_PIECE_DPAD) {
        /* the box around the four arrows */
        float x0 = s_shapes[CTRL_LEFT].x - s_shapes[CTRL_LEFT].rx;
        float x1 = s_shapes[CTRL_RIGHT].x + s_shapes[CTRL_RIGHT].rx;
        float y0 = s_shapes[CTRL_UP].y - s_shapes[CTRL_UP].ry;
        float y1 = s_shapes[CTRL_DOWN].y + s_shapes[CTRL_DOWN].ry;

        s = (Shape){ (x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (x1 - x0) * 0.5f, (y1 - y0) * 0.5f };
    } else if (piece >= 0 && piece <= CTRL_SAVE) {
        s = s_shapes[piece];
    } else {
        s = (Shape){ 0, 0, 0, 0 };
    }
    *x = s.x / w;
    *y = s.y / h;
    *rx = s.rx / w;
    *ry = s.ry / h;
    *size = piece >= 0 && piece < PC_TOUCH_PIECE_COUNT && s_over[piece].set ? s_over[piece].size : 1.0f;
}

void pc_touch_piece_set(int piece, float x, float y, float size) {
    if (piece < 0 || piece >= PC_TOUCH_PIECE_COUNT) {
        return;
    }
    parse_layout();
    x = SDL_clamp(x, 0.0f, 1.0f);
    y = SDL_clamp(y, 0.0f, 1.0f);
    size = SDL_clamp(size, PC_TOUCH_SIZE_MIN, PC_TOUCH_SIZE_MAX);
    if (!isfinite(x) || !isfinite(y) || !isfinite(size)) {
        return;
    }
    s_over[piece].set = 1;
    s_over[piece].x = x;
    s_over[piece].y = y;
    s_over[piece].size = size;
    write_layout();
}

void pc_touch_layout_reset(int piece) {
    parse_layout();
    if (piece < 0) {
        memset(s_over, 0, sizeof(s_over));
    } else if (piece < PC_TOUCH_PIECE_COUNT) {
        s_over[piece].set = 0;
    }
    write_layout();
}
