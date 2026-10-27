/* pc_input_script.c - see pc_input_script.h */
#include "pc_input_script.h"
#include "pc_touch.h"
#include "pc_typing.h"

#include <ctype.h>
#include <dolphin/pad.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#define SCRIPT_STICK 80
#define SCRIPT_DEFAULT_HOLD 6

typedef enum {
    SCRIPT_INPUT,
    SCRIPT_SHOT,
    SCRIPT_QUIT,
    SCRIPT_TYPE,
    SCRIPT_EDIT,
    SCRIPT_TEXT,
    SCRIPT_TOUCH,
} ScriptKind;

typedef struct {
    u32 frame;
    u32 duration;
    ScriptKind kind;
    u16 buttons;
    s8 stick_x;
    s8 stick_y;
    float touch[4]; /* TOUCH: where the finger lands, then where it moves (0-1) */
    int touch_moves;
    char name[128];
} ScriptEvent;

static ScriptEvent* s_events = NULL;
static int s_count = 0;
static u32 s_frame = 0;
static int s_fast = 0;

void pc_input_script_set_fast(int fast) {
    s_fast = fast;
}

static int parse_inputs(char* text, ScriptEvent* ev) {
    static const struct {
        const char* name;
        u16 button;
        s8 x, y;
    } table[] = {
        { "A", PAD_BUTTON_A, 0, 0 },          { "B", PAD_BUTTON_B, 0, 0 },
        { "X", PAD_BUTTON_X, 0, 0 },          { "Y", PAD_BUTTON_Y, 0, 0 },
        { "START", PAD_BUTTON_START, 0, 0 },  { "Z", PAD_TRIGGER_Z, 0, 0 },
        { "L", PAD_TRIGGER_L, 0, 0 },         { "R", PAD_TRIGGER_R, 0, 0 },
        { "UP", PAD_BUTTON_UP, 0, 0 },        { "DOWN", PAD_BUTTON_DOWN, 0, 0 },
        { "LEFT", PAD_BUTTON_LEFT, 0, 0 },    { "RIGHT", PAD_BUTTON_RIGHT, 0, 0 },
        { "SU", 0, 0, SCRIPT_STICK },         { "SD", 0, 0, -SCRIPT_STICK },
        { "SL", 0, -SCRIPT_STICK, 0 },        { "SR", 0, SCRIPT_STICK, 0 },
    };
    char* tok;

    for (tok = strtok(text, "+"); tok != NULL; tok = strtok(NULL, "+")) {
        size_t i;

        for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
            if (strcmp(tok, table[i].name) == 0) {
                ev->buttons |= table[i].button;
                ev->stick_x += table[i].x;
                ev->stick_y += table[i].y;
                break;
            }
        }
        if (i == sizeof(table) / sizeof(table[0])) {
            return 0;
        }
    }
    return 1;
}

int pc_input_script_load(const char* path) {
    char line[256];
    int line_no = 0;
    int capacity = 0;
    FILE* f = fopen(path, "r");

    if (f == NULL) {
        fprintf(stderr, "[INPUT] cannot open script %s\n", path);
        return 0;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        ScriptEvent ev;
        char word[64] = "";
        char arg[128] = "";
        char arg2[32] = "";
        char* hash = strchr(line, '#');
        unsigned long frame;
        int fields;

        line_no++;
        if (hash != NULL) {
            *hash = '\0';
        }
        fields = sscanf(line, "%lu %63s %127s %31s", &frame, word, arg, arg2);
        if (fields <= 0) {
            continue; /* blank or comment */
        }
        if (fields < 2) {
            fprintf(stderr, "[INPUT] %s:%d: expected '<frame> <inputs>'\n", path, line_no);
            fclose(f);
            return 0;
        }

        memset(&ev, 0, sizeof(ev));
        ev.frame = (u32)frame;
        for (char* p = word; *p != '\0'; p++) {
            *p = (char)toupper((unsigned char)*p);
        }
        if (strcmp(word, "SHOT") == 0) {
            ev.kind = SCRIPT_SHOT;
            snprintf(ev.name, sizeof(ev.name), "%s", fields >= 3 ? arg : "shot");
        } else if (strcmp(word, "QUIT") == 0) {
            ev.kind = SCRIPT_QUIT;
        } else if (strcmp(word, "TYPE") == 0 && fields >= 3) {
            ev.kind = SCRIPT_TYPE;
            snprintf(ev.name, sizeof(ev.name), "%s", arg);
        } else if (strcmp(word, "TOUCH") == 0 && fields >= 3) {
            float* t = ev.touch;
            int n = sscanf(arg, "%f,%f:%f,%f", &t[0], &t[1], &t[2], &t[3]);

            if (n != 2 && n != 4) {
                fprintf(stderr, "[INPUT] %s:%d: expected 'TOUCH x,y[:x2,y2] [frames]'\n", path, line_no);
                fclose(f);
                return 0;
            }
            ev.kind = SCRIPT_TOUCH;
            ev.touch_moves = n == 4;
            for (int i = 0; i < 4; i++) {
                t[i] /= 100.0f; /* percent of the window */
            }
            ev.duration = fields >= 4 ? (u32)strtoul(arg2, NULL, 10) : SCRIPT_DEFAULT_HOLD;
        } else if ((strcmp(word, "EDIT") == 0 || strcmp(word, "TEXT") == 0) && fields >= 2) {
            /* the system input method's composition ("EDIT" alone ends it) or committed text */
            ev.kind = word[0] == 'E' ? SCRIPT_EDIT : SCRIPT_TEXT;
            snprintf(ev.name, sizeof(ev.name), "%s", fields >= 3 ? arg : "");
        } else {
            ev.kind = SCRIPT_INPUT;
            ev.duration = fields >= 3 ? (u32)strtoul(arg, NULL, 10) : SCRIPT_DEFAULT_HOLD;
            if (!parse_inputs(word, &ev)) {
                fprintf(stderr, "[INPUT] %s:%d: unknown input in '%s'\n", path, line_no, word);
                fclose(f);
                return 0;
            }
        }

        if (s_count == capacity) {
            capacity = capacity ? capacity * 2 : 64;
            s_events = (ScriptEvent*)realloc(s_events, capacity * sizeof(ScriptEvent));
        }
        s_events[s_count++] = ev;
    }

    fclose(f);
    printf("[INPUT] loaded %d events from %s\n", s_count, path);
    return 1;
}

void pc_input_script_apply(u16* buttons, s8* stick_x, s8* stick_y) {
    int i;

    for (i = 0; i < s_count; i++) {
        const ScriptEvent* ev = &s_events[i];

        if (ev->kind == SCRIPT_INPUT && s_frame >= ev->frame && s_frame < ev->frame + ev->duration) {
            *buttons |= ev->buttons;
            if (ev->stick_x != 0) {
                *stick_x = ev->stick_x;
            }
            if (ev->stick_y != 0) {
                *stick_y = ev->stick_y;
            }
        }
    }
}

/* Types keys into the text editor: plain characters are QWERTY keys, and
 * {SP} {BS} {HAN} {ENTER} {END} {LEFT} {RIGHT} {UP} {DOWN} name the others. */
static void type_keys(const char* text) {
    static const struct {
        const char* name;
        int key;
    } names[] = {
        { "{SP}", ' ' },
        { "{BS}", PC_TYPING_CMD_BACKSPACE },
        { "{HAN}", -1 },
        { "{ENTER}", PC_TYPING_CMD_ENTER },
        { "{END}", PC_TYPING_CMD_END },
        { "{LEFT}", PC_TYPING_CMD_LEFT },
        { "{RIGHT}", PC_TYPING_CMD_RIGHT },
        { "{UP}", PC_TYPING_CMD_UP },
        { "{DOWN}", PC_TYPING_CMD_DOWN },
    };

    while (*text != '\0') {
        size_t i;
        int key = (unsigned char)*text;
        size_t len = 1;

        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (strncmp(text, names[i].name, strlen(names[i].name)) == 0) {
                key = names[i].key;
                len = strlen(names[i].name);
                break;
            }
        }
        if (key < 0) {
            pc_typing_toggle_korean();
        } else if (!pc_typing_key(key)) {
            printf("[INPUT] frame %u: no text editor to type into\n", s_frame);
            return;
        }
        text += len;
    }
}

/* Writes the back buffer (what is about to be presented) as a binary PPM. */
static void save_shot(const char* name) {
    char path[96];
    int w = g_pc_window_w;
    int h = g_pc_window_h;
    u8* pixels = (u8*)malloc((size_t)w * h * 4);
    FILE* f;
    int y;

    if (pixels == NULL) {
        return;
    }
#ifdef _WIN32
    _mkdir("shots");
#else
    mkdir("shots", 0755);
#endif
    snprintf(path, sizeof(path), "shots/%s.ppm", name);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
#ifdef PC_GLES
    { /* ES only guarantees RGBA reads */
        int i;

        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        for (i = 0; i < w * h; i++) {
            memmove(pixels + i * 3, pixels + i * 4, 3);
        }
    }
#else
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
#endif

    f = fopen(path, "wb");
    if (f != NULL) {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (y = h - 1; y >= 0; y--) { /* GL rows are bottom-up */
            fwrite(pixels + (size_t)y * w * 3, 1, (size_t)w * 3, f);
        }
        fclose(f);
        printf("[INPUT] frame %u: saved %s\n", s_frame, path);
    }
    free(pixels);
}

int pc_input_script_frame(u32 frame) {
    int keep_running = 1;
    int i;

    s_frame = frame;
    for (i = 0; i < s_count; i++) {
        const ScriptEvent* ev = &s_events[i];

        if (ev->kind == SCRIPT_TOUCH) {
            s64 id = 0x5C000 + i;

            if (frame == ev->frame) {
                pc_touch_finger_down(id, ev->touch[0], ev->touch[1]);
            } else if (frame == ev->frame + 1 && ev->touch_moves) {
                pc_touch_finger_move(id, ev->touch[2], ev->touch[3]);
            }
            if (frame == ev->frame + ev->duration) {
                pc_touch_finger_up(id);
            }
            continue;
        }
        if (ev->frame != frame) {
            continue;
        }
        if (ev->kind == SCRIPT_SHOT) {
            save_shot(ev->name);
        } else if (ev->kind == SCRIPT_TYPE) {
            type_keys(ev->name);
        } else if (ev->kind == SCRIPT_EDIT) {
            pc_typing_system_editing(ev->name);
        } else if (ev->kind == SCRIPT_TEXT) {
            pc_typing_system_text(ev->name);
        } else if (ev->kind == SCRIPT_QUIT) {
            printf("[INPUT] frame %u: quit\n", frame);
            keep_running = 0;
        }
    }
    if (s_fast) {
        /* Draw the next frame only if it is captured. Menus that keep a copy
         * of the scene behind them show black. */
        g_pc_skip_draw = 1;
        for (i = 0; i < s_count; i++) {
            if (s_events[i].kind == SCRIPT_SHOT && s_events[i].frame == frame + 1) {
                g_pc_skip_draw = 0;
                break;
            }
        }
    }
    return keep_running;
}
