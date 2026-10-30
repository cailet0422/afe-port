/* pc_ko_editor.c - input panel of the text editor on a Korean patch disc
 *
 * The patch replaced the editor's kana wheel with its own Hangul input and
 * reused the wheel's textures for it. The PC port draws an on-screen QWERTY
 * keyboard there instead (layout in ko_osk.c): the controller or a finger
 * picks keys, which go through the same input method as a real keyboard
 * (pc_typing.c). A physical keyboard still types directly. */
#include "pc_ko.h"

#include "ko_osk.h"
#include "pc_platform.h"
#include "pc_typing.h"
#include "m_font.h"
#include "m_rcp.h"
#include "graph.h"

#include <dolphin/pad.h>

/* in the editor's 320x240 screen */
#define KEY_W 20.0f
#define KEY_H 16.0f
#define KEY_GAP 1.0f
#define BORDER 4.0f
#define HEADER_H 13.0f
#define PANEL_WIDTH (BORDER * 2 + KO_OSK_COLS * (KEY_W + KEY_GAP) - KEY_GAP)
#define PANEL_HEIGHT (BORDER * 2 + HEADER_H + KO_OSK_ROWS * (KEY_H + KEY_GAP) - KEY_GAP)
#define PANEL_LEFT ((320.0f - PANEL_WIDTH) * 0.5f)
#define PANEL_TOP 130.0f
#define LABEL_SCALE 0.6f
#define HINT_SCALE 0.42f
#define HEADER_SCALE 0.6f

/* controller: the D-pad or the stick moves, repeating while held */
#define STICK_THRESHOLD 40
#define REPEAT_DELAY 15
#define REPEAT_RATE 5
#define OSK_BUTTONS                                                                                      \
    (PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_X | PAD_BUTTON_Y | PAD_TRIGGER_Z | PAD_TRIGGER_L |        \
     PAD_TRIGGER_R | PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)

extern u32 pc_frame_counter;

static KoOsk s_osk;
static u32 s_drawn_frame = (u32)-8;
static f32 s_left, s_top;         /* where the keys were drawn */
static const KoOskKey* s_touched; /* key under a finger, drawn pressed */
static u16 s_prev_buttons;
static int s_dir_x, s_dir_y, s_repeat;

void pc_ko_osk_reset(void) {
    ko_osk_init(&s_osk);
    s_touched = NULL;
    s_dir_x = s_dir_y = 0;
}

/* The editor drew the keyboard last frame and is taking input */
static int osk_active(void) {
    return g_pc_ko_mode && g_pc_editor_active && pc_frame_counter - s_drawn_frame <= 2 &&
           pc_typing_editor_listening();
}

static void press(const KoOskKey* key) {
    char ch;

    key = ko_osk_press(&s_osk, key, &ch);
    switch (key->kind) {
        case KO_OSK_CHAR:
        case KO_OSK_SPACE:     pc_typing_osk_key((u8)ch); break;
        case KO_OSK_HANENG:    pc_typing_osk_toggle_korean(); break;
        case KO_OSK_BACKSPACE: pc_typing_osk_key(PC_TYPING_CMD_BACKSPACE); break;
        case KO_OSK_ENTER:     pc_typing_osk_key(PC_TYPING_CMD_ENTER); break;
        default: break;
    }
}

static void navigate(u16 buttons, s8 stick_x, s8 stick_y) {
    int dx = 0, dy = 0;

    if ((buttons & PAD_BUTTON_LEFT) || stick_x <= -STICK_THRESHOLD) {
        dx = -1;
    } else if ((buttons & PAD_BUTTON_RIGHT) || stick_x >= STICK_THRESHOLD) {
        dx = 1;
    }
    if ((buttons & PAD_BUTTON_UP) || stick_y >= STICK_THRESHOLD) {
        dy = -1;
    } else if ((buttons & PAD_BUTTON_DOWN) || stick_y <= -STICK_THRESHOLD) {
        dy = 1;
    }
    if (dx != s_dir_x || dy != s_dir_y) {
        s_dir_x = dx;
        s_dir_y = dy;
        s_repeat = REPEAT_DELAY;
    } else if (dx == 0 && dy == 0) {
        return;
    } else if (--s_repeat > 0) {
        return;
    } else {
        s_repeat = REPEAT_RATE;
    }
    ko_osk_move(&s_osk, dx, dy);
}

void pc_ko_osk_pad(u16* buttons, s8* stick_x, s8* stick_y) {
    static u32 done_frame = (u32)-1;
    u16 pressed;

    if (!osk_active()) {
        s_prev_buttons = *buttons; /* what was held when it opens is not a press */
        return;
    }
    if (done_frame != pc_frame_counter) { /* once per frame, however often the pad is read */
        done_frame = pc_frame_counter;
        pressed = *buttons & ~s_prev_buttons;
        s_prev_buttons = *buttons;

        navigate(*buttons, *stick_x, *stick_y);
        if (pressed & PAD_BUTTON_A) {
            s_touched = NULL;
            press(NULL);
        }
        if (pressed & PAD_BUTTON_B) {
            pc_typing_osk_key(PC_TYPING_CMD_BACKSPACE);
        }
        if (pressed & PAD_BUTTON_X) {
            pc_typing_osk_toggle_korean();
        }
        if (pressed & PAD_BUTTON_Y) {
            s_osk.shift ^= 1;
        }
        if (pressed & PAD_TRIGGER_Z) {
            pc_typing_osk_key(PC_TYPING_CMD_ENTER);
        }
        if (pressed & PAD_TRIGGER_L) {
            pc_typing_osk_key(PC_TYPING_CMD_LEFT);
        }
        if (pressed & PAD_TRIGGER_R) {
            pc_typing_osk_key(PC_TYPING_CMD_RIGHT);
        }
    }
    /* START still ends editing */
    *buttons &= ~OSK_BUTTONS;
    *stick_x = 0;
    *stick_y = 0;
}

/* The key at a point of the editor's screen, or NULL */
static const KoOskKey* key_at(f32 x, f32 y) {
    int row, col;

    x -= s_left;
    y -= s_top;
    if (x < 0.0f || y < 0.0f) {
        return NULL;
    }
    row = (int)(y / (KEY_H + KEY_GAP));
    col = (int)(x / (KEY_W + KEY_GAP));
    if (row >= KO_OSK_ROWS || col >= KO_OSK_COLS) {
        return NULL;
    }
    return ko_osk_key_at(row, col);
}

int pc_ko_osk_touch(int down, f32 wx, f32 wy) {
    f32 x, y;
    const KoOskKey* key;

    if (!down) {
        s_touched = NULL;
        return 0;
    }
    if (!osk_active()) {
        return 0;
    }
    pc_gx_window_to_ui(wx, wy, &x, &y);
    key = key_at(x * 0.5f, y * 0.5f);
    if (key == NULL) {
        return 0;
    }
    s_osk.row = (int)((y * 0.5f - s_top) / (KEY_H + KEY_GAP));
    s_osk.col = key->col;
    s_touched = key;
    press(key);
    return 1;
}

static void draw_rect(GRAPH* graph, f32 x, f32 y, f32 w, f32 h, int r, int g, int b, int a) {
    Gfx* gfx;

    OPEN_DISP(graph);
    gfx = NOW_POLY_OPA_DISP;
    gDPPipeSync(gfx++);
    gDPSetOtherMode(gfx++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                    G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gfx = gfx_gSPTextureRectangle1(gfx, (int)(x * 4.0f), (int)(y * 4.0f), (int)((x + w) * 4.0f),
                                   (int)((y + h) * 4.0f), 0, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    SET_POLY_OPA_DISP(gfx);
    CLOSE_DISP(graph);
}

/* Draws text with its top left at x, y, or centred on x when centre is set */
static void draw_text(GAME* game, const char* text, f32 x, f32 y, int r, int g, int b, f32 scale, int centre) {
    u8 buf[64];
    int len = pc_ko_encode_utf8(text, buf, sizeof(buf));

    if (len == 0) {
        return;
    }
    if (centre) {
        x -= mFont_GetStringWidth(buf, len, TRUE) * scale * 0.5f;
    }
    mFont_SetLineStrings(game, buf, len, x, y, r, g, b, 255, FALSE, FALSE, scale, scale, mFont_MODE_POLY);
}

static const char* hint(KoOskKind kind) {
    switch (kind) {
        case KO_OSK_SHIFT:     return "Y";
        case KO_OSK_HANENG:    return "X";
        case KO_OSK_BACKSPACE: return "B";
        case KO_OSK_ENTER:     return "Z";
        default:               return NULL;
    }
}

void pc_ko_editor_draw(GAME* game, f32 x, f32 y) {
    const KoOskKey* cursor = ko_osk_current(&s_osk);
    int korean = pc_typing_ime_korean();
    f32 left = PANEL_LEFT + x;
    f32 top = PANEL_TOP - y;
    int row, i;

    s_drawn_frame = pc_frame_counter;
    s_left = left + BORDER;
    s_top = top + BORDER + HEADER_H;

    draw_rect(game->graph, left, top, PANEL_WIDTH, PANEL_HEIGHT, 40, 60, 110, 200);
    draw_text(game, korean ? "한글" : "영문", left + BORDER + 2.0f, top + BORDER - 1.0f, 255, 235, 120,
              HEADER_SCALE, FALSE);
    draw_text(game, "A 입력  START 끝내기", left + PANEL_WIDTH * 0.5f + 24.0f, top + BORDER - 1.0f, 235, 235, 235,
              HEADER_SCALE, TRUE);

    for (row = 0; row < KO_OSK_ROWS; row++) {
        for (i = 0; i < ko_osk_row_len(row); i++) {
            const KoOskKey* key = ko_osk_row_key(row, i);
            f32 kx = s_left + key->col * (KEY_W + KEY_GAP);
            f32 ky = s_top + row * (KEY_H + KEY_GAP);
            f32 kw = key->width * (KEY_W + KEY_GAP) - KEY_GAP;
            int lit = key == cursor || key == s_touched;
            int special = key->kind != KO_OSK_CHAR;
            char label[16];

            if (lit) {
                draw_rect(game->graph, kx, ky, kw, KEY_H, 255, 220, 90, 255);
            } else if (key->kind == KO_OSK_SHIFT && s_osk.shift) {
                draw_rect(game->graph, kx, ky, kw, KEY_H, 120, 170, 250, 240);
            } else if (special) {
                draw_rect(game->graph, kx, ky, kw, KEY_H, 55, 80, 140, 240);
            } else {
                draw_rect(game->graph, kx, ky, kw, KEY_H, 85, 110, 175, 240);
            }
            ko_osk_label(key, korean, s_osk.shift, label);
            draw_text(game, label, kx + kw * 0.5f, ky + 2.0f, lit ? 40 : 245, lit ? 40 : 245, lit ? 60 : 245,
                      LABEL_SCALE, TRUE);
            if (hint(key->kind) != NULL) {
                draw_text(game, hint(key->kind), kx + 1.0f, ky, lit ? 120 : 190, lit ? 90 : 200, lit ? 40 : 230,
                          HINT_SCALE, FALSE);
            }
        }
    }
}
