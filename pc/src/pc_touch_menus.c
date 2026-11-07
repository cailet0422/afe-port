/* pc_touch_menus.c - the full touch mode in the game's other menus
 * (pc_touch_full.h; the pockets, the mailbox, the map and the notice board
 * are pc_touch_menu.c's)
 *
 * The menus here move their cursors with the stick and act with the buttons;
 * a touch is turned into those, a step at a time (a queue of presses), and
 * the menu's own rules do the rest. A tap on a field, a digit, a note, a day,
 * an answer or a name walks the menu's cursor there (the stick pushed and let
 * go until the cursor, read from the menu, is on it), and then presses A when
 * that is what a tap means. A swipe up or down changes the value under the
 * cursor (the stick's up and down), a swipe left or right turns a page.
 * Their keys (PC_HUD_KEY) give what has no place to tap: the values' up and
 * down, the editor's keys, a done key (START).
 *
 * Coordinates: the menus draw in a 320x240 picture (two of the port's
 * 640x480 units each, pc_gx_window_to_ui); the places are those of their
 * draw code, with the menu's position (0 at rest) added. */
#include "pc_touch_internal.h"
#include "pc_platform.h"
#include "pc_ko.h"

#include "m_common_data.h"
#include "m_submenu.h"
#include "m_submenu_ovl.h"
#include "m_editor_ovl.h"
#include "m_board_ovl.h"
#include "m_address_ovl.h"
#include "m_editEndChk_ovl.h"
#include "m_cpwarning_ovl.h"
#include "m_timeIn_ovl.h"
#include "m_alarmIn_ovl.h"
#include "m_birthday_ovl.h"
#include "m_calendar_ovl.h"
#include "m_diary_ovl.h"
#include "m_bank_ovl.h"
#include "m_repay_ovl.h"
#include "m_mscore_ovl.h"
#include "m_design_ovl.h"
#include "audio.h"

#include <dolphin/pad.h>
#include <math.h>
#include <string.h>

extern u32 pc_frame_counter;
extern int g_pc_window_w, g_pc_window_h;
int pc_gui_korean(void);

#define STICK 60 /* past the menus' half tilt (mSM_make_trigger_data) */
#define TAP_SLOP 8.0f /* 320x240 */
#define WALK_GIVE_UP 240

/* --- The queue of presses ---------------------------------------------- */

typedef struct {
    u16 buttons;
    s8 sx, sy; /* the stick */
    s8 cx, cy; /* the C-stick */
    u8 on, off;
} Step;

static Step s_q[32];
static int s_qn, s_qi, s_qt;

static void q_add(u16 buttons, int sx, int sy, int cx, int cy, int on, int off) {
    Step* s;

    if (s_qn >= (int)(sizeof(s_q) / sizeof(s_q[0])) && s_qi > 0) {
        /* (the steps given so far make room: the one being given stays first) */
        memmove(&s_q[0], &s_q[s_qi], (size_t)(s_qn - s_qi) * sizeof(s_q[0]));
        s_qn -= s_qi;
        s_qi = 0;
    }
    if (s_qn >= (int)(sizeof(s_q) / sizeof(s_q[0]))) {
        return;
    }
    s = &s_q[s_qn++];
    s->buttons = buttons;
    s->sx = (s8)sx;
    s->sy = (s8)sy;
    s->cx = (s8)cx;
    s->cy = (s8)cy;
    s->on = (u8)on;
    s->off = (u8)off;
}

static int q_busy(void) {
    return s_qi < s_qn;
}

static void q_clear(void) {
    s_qn = s_qi = s_qt = 0;
}

/* A button: pressed two frames, let go three (a fresh press each) */
static void q_button(u16 b) {
    q_add(b, 0, 0, 0, 0, 2, 3);
}

/* The stick one step: dx right, dy down */
static void q_stick(int dx, int dy) {
    q_add(0, dx * STICK, -dy * STICK, 0, 0, 2, 3);
}

/* The C-stick one step */
static void q_cstick(int dx, int dy) {
    q_add(0, 0, 0, dx * STICK, -dy * STICK, 2, 3);
}

static void q_tick(void) {
    if (!q_busy()) {
        q_clear();
        return;
    }
    if (++s_qt >= s_q[s_qi].on + s_q[s_qi].off) {
        s_qi++;
        s_qt = 0;
    }
}

/* --- The cursor walker ----------------------------------------------------
 * Pushes the stick toward a target and reads the menu's cursor back, until
 * it is there; then a button (or none). Gives up if the cursor stops
 * following (a wall, a value the menu skips). */

/* A menu's cursor: CUR_OK with it, CUR_WAIT while the menu takes no input
 * yet (sliding in, a tune playing), CUR_NONE when there is none to walk */
enum { CUR_NONE, CUR_OK, CUR_WAIT };
typedef int (*CursorFn)(Submenu* sm, int* x, int* y);

static struct {
    int on;
    CursorFn get;
    int tx, ty;
    u16 then;
    u32 since;
    int lx, ly, still;
    int c_stick; /* moves with the C-stick (raw C bits) */
    int dpad;    /* moves with the D-pad */
} s_walk;

static void walk_to(CursorFn get, int tx, int ty, u16 then) {
    s_walk.on = 1;
    s_walk.get = get;
    s_walk.tx = tx;
    s_walk.ty = ty;
    s_walk.then = then;
    s_walk.since = pc_frame_counter;
    s_walk.lx = s_walk.ly = -9999;
    s_walk.still = 0;
    s_walk.c_stick = 0;
    s_walk.dpad = 0;
}

static void walk_step(Submenu* sm) {
    int x, y;

    if (!s_walk.on || q_busy()) {
        return;
    }
    {
        int st = s_walk.get(sm, &x, &y);

        if (st == CUR_NONE || pc_frame_counter - s_walk.since > WALK_GIVE_UP) {
            printf("[TOUCH] menu: the cursor did not get there\n");
            s_walk.on = 0;
            return;
        }
        if (st == CUR_WAIT) {
            return;
        }
    }
    if (x == s_walk.tx && y == s_walk.ty) {
        if (s_walk.then != 0) {
            q_button(s_walk.then);
        }
        s_walk.on = 0;
        return;
    }
    if (x == s_walk.lx && y == s_walk.ly) {
        if (++s_walk.still > 6) {
            printf("[TOUCH] menu: the cursor stays at %d,%d (wanted %d,%d)\n", x, y, s_walk.tx, s_walk.ty);
            s_walk.on = 0;
            return;
        }
    } else {
        s_walk.still = 0;
    }
    s_walk.lx = x;
    s_walk.ly = y;
    if (x != s_walk.tx) {
        int d = s_walk.tx > x ? 1 : -1;

        if (s_walk.dpad) {
            q_button(d > 0 ? PAD_BUTTON_RIGHT : PAD_BUTTON_LEFT);
        } else {
            s_walk.c_stick ? q_cstick(d, 0) : q_stick(d, 0);
        }
    } else {
        int d = s_walk.ty > y ? 1 : -1;

        if (s_walk.dpad) {
            q_button(d > 0 ? PAD_BUTTON_DOWN : PAD_BUTTON_UP);
        } else {
            s_walk.c_stick ? q_cstick(0, d) : q_stick(0, d);
        }
    }
}

/* --- Small things ----------------------------------------------------------- */

static mSM_MenuInfo_c* info_of(Submenu* sm, int type) {
    return &sm->overlay->menu_info[type];
}

static int playing(Submenu* sm, int type) {
    return info_of(sm, type)->proc_status == mSM_OVL_PROC_PLAY;
}

/* A point of the 320x240 picture in a menu's own place (its position) */
static void local(Submenu* sm, int type, float x, float y, float* lx, float* ly) {
    mSM_MenuInfo_c* info = info_of(sm, type);

    *lx = x - info->position[0];
    *ly = y + info->position[1];
}

static int in_box(float x, float y, float x0, float y0, float x1, float y1) {
    return x >= x0 && x < x1 && y >= y0 && y < y1;
}

/* The nearest of n centres within (rx, ry), -1 if none */
static int nearest(float x, float y, const float* cx, const float* cy, int n, float rx, float ry) {
    int best = -1;
    float bd = 1e9f;
    int i;

    for (i = 0; i < n; i++) {
        float dx = (x - cx[i]) / rx, dy = (y - cy[i]) / ry;
        float d = dx * dx + dy * dy;

        if (d <= 1.0f && d < bd) {
            best = i;
            bd = d;
        }
    }
    return best;
}

/* The steps of a swipe up or down: one each `per` pixels, at most `max` */
static int swipe_steps(float dy, float per, int max) {
    int n = (int)(fabsf(dy) / per);

    if (n < 1) n = 1;
    if (n > max) n = max;
    return n;
}

/* --- The address book (writing a letter) ---------------------------------
 * m_address_ovl.c: a tap on a name picks it (the cursor there, then A), a
 * swipe left or right turns the page. It also comes back while the editor
 * is up, when the editor's cursor reaches the name. */

static int address_active(Submenu* sm) {
    return sm->overlay->address_ovl != NULL && playing(sm, mSM_OVL_ADDRESS) &&
           (sm->menu_type == mSM_OVL_ADDRESS || sm->menu_type == mSM_OVL_EDITOR);
}

static int address_cursor(Submenu* sm, int* x, int* y) {
    mAD_Ovl_c* ad = sm->overlay->address_ovl;

    if (ad == NULL || ad->proc_idx != mAD_PROC_SELECT) {
        return ad != NULL && (ad->proc_idx == mAD_PROC_TURN_PAGE || ad->proc_idx == mAD_PROC_TURN_PAGE2) ? CUR_WAIT
                                                                                                         : CUR_NONE;
    }
    *x = 0;
    *y = ad->selected_entry;
    return CUR_OK;
}

static void address_tap(Submenu* sm, float x, float y) {
    mAD_Ovl_c* ad = sm->overlay->address_ovl;
    mBD_Ovl_c* bd = sm->overlay->board_ovl;
    float X, Y, top;
    int count, row;

    if (ad->proc_idx == mAD_PROC_REFUSE || ad->proc_idx == mAD_PROC_START) {
        q_button(PAD_BUTTON_A);
        return;
    }
    if (ad->proc_idx != mAD_PROC_SELECT || bd == NULL) {
        return;
    }
    X = bd->ofs_x + 4.0f;
    Y = bd->ofs_y - 20.0f;
    count = ad->page_entry_count[ad->curIdx];
    if (x < 120.0f + X || x > 200.0f + X || count <= 0) {
        return;
    }
    top = 103.0f - Y + 11.0f + (float)((count - 1) * 3) / 7.0f;
    row = (int)floorf((y - top) / 12.0f);
    if (ad->curIdx == 0 && ad->show_museum_address && row >= count) {
        row = count - 1; /* (the museum, a line lower) */
    }
    if (row < 0 || row >= count) {
        return;
    }
    printf("[TOUCH] address: %d of page %d\n", row, ad->curIdx);
    walk_to(address_cursor, 0, row, PAD_BUTTON_A);
}

/* --- The text editor --------------------------------------------------------
 * m_editor_ovl.c. With the Korean patch its on-screen keyboard takes the
 * taps (pc_ko_editor.c, before this mode sees them); the keys here move the
 * text's cursor and finish. On the original disc the stick's wheel picks the
 * characters: a tap on the wheel points the stick that way and presses A. */

enum {
    K_UP = 1, /* the stick up (a value up, a line up) */
    K_DOWN,
    K_LEFT,
    K_RIGHT,
    K_DUP, /* the D-pad (the text's cursor) */
    K_DDOWN,
    K_DLEFT,
    K_DRIGHT,
    K_A,
    K_B,
    K_X,
    K_Y,
    K_R,
    K_START,
    K_PICK, /* the design editor's eyedropper (the next tap) */
};

/* The stick's wheel is up: the original disc (the Korean patch's keyboard
 * replaces it, pc_ko_editor.c) */
static int editor_wheel(Submenu* sm) {
    (void)sm;
    return !g_pc_ko_mode;
}

static void editor_tap(Submenu* sm, float x, float y) {
    float lx, ly, dx, dy, d;
    int sector;
    static const s8 dir[8][2] = { { 1, 0 }, { 1, 1 }, { 0, 1 }, { -1, 1 }, { -1, 0 }, { -1, -1 }, { 0, -1 }, { 1, -1 } };

    if (!editor_wheel(sm) || !playing(sm, mSM_OVL_EDITOR)) {
        return; /* (the Korean keyboard's own taps) */
    }
    /* the wheel's middle (the ring's glyphs from about (51, 132) to (120, 175)) */
    local(sm, mSM_OVL_EDITOR, x, y, &lx, &ly);
    dx = lx - 86.0f;
    dy = ly - 166.0f;
    d = sqrtf(dx * dx + dy * dy);
    if (d < 10.0f || d > 60.0f) {
        return;
    }
    sector = (int)lrintf(atan2f(-dy, dx) / ((float)M_PI / 4.0f));
    sector = (sector + 8) % 8;
    /* pointed, then A with it still pointed */
    q_add(0, dir[sector][0] * STICK, dir[sector][1] * STICK, 0, 0, 4, 0);
    q_add(PAD_BUTTON_A, dir[sector][0] * STICK, dir[sector][1] * STICK, 0, 0, 2, 4);
    printf("[TOUCH] editor: wheel %d\n", sector);
}

/* --- The question after writing ("is this right?") ---------------------------
 * m_editEndChk_ovl.c: the first A shows the answers; a tap on one walks the
 * cursor there and presses A. */

static int endchk_cursor(Submenu* sm, int* x, int* y) {
    mEE_Ovl_c* ee = sm->overlay->editEndChk_ovl;

    if (ee == NULL) {
        return CUR_NONE;
    }
    if (!ee->moving_in || ee->scale < 1.0f || !playing(sm, mSM_OVL_EDITENDCHK)) {
        return CUR_WAIT;
    }
    *x = 0;
    *y = ee->selected_answer;
    return CUR_OK;
}

static void endchk_tap(Submenu* sm, float x, float y) {
    mEE_Ovl_c* ee = sm->overlay->editEndChk_ovl;
    int type = info_of(sm, mSM_OVL_EDITENDCHK)->data0;
    int n = type == mEE_TYPE_BOARD ? 2 : 3;
    float x0 = type == mEE_TYPE_BOARD ? 180.0f : (type == mEE_TYPE_MSCORE ? 121.0f : 139.0f);
    float y0 = type == mEE_TYPE_BOARD ? 151.0f : (type == mEE_TYPE_MSCORE ? 117.0f : 133.0f);
    float lx, ly;
    int row;

    if (!ee->moving_in) {
        q_button(PAD_BUTTON_A); /* the answers */
        return;
    }
    local(sm, mSM_OVL_EDITENDCHK, x, y, &lx, &ly);
    row = (int)floorf((ly - y0) / 16.0f);
    if (row < 0 || row >= n || lx < x0 - 24.0f || lx > x0 + 136.0f) {
        return;
    }
    printf("[TOUCH] question: answer %d\n", row);
    walk_to(endchk_cursor, 0, row, PAD_BUTTON_A);
}

/* --- The diary's "may the others read it?" (m_cpwarning_ovl.c) ------------ */

static void cpwarning_tap(Submenu* sm, float x, float y) {
    int side = x < 160.0f ? -1 : 1;

    (void)y;
    if (!playing(sm, mSM_OVL_CPWARNING)) {
        return; /* (still growing: it takes no input) */
    }
    q_add(0, side * STICK, 0, 0, 0, 3, 3);
    q_button(PAD_BUTTON_A);
    printf("[TOUCH] diary question: %s\n", side < 0 ? "yes" : "no");
}

/* --- Fields of numbers: the clock, the alarm, the birthday ------------------
 * A tap on a field moves the cursor there (the stick right and left); the
 * keys or a swipe up or down change it; a tap on "done" walks there and
 * presses A (or the done key: START). */

static int fields_cursor(Submenu* sm, int* x, int* y) {
    *y = 0;
    if (!playing(sm, sm->menu_type)) {
        return CUR_WAIT;
    }
    switch (sm->menu_type) {
        case mSM_OVL_TIMEIN:
            *x = sm->overlay->timeIn_ovl->sel_idx;
            return sm->overlay->timeIn_ovl->input_disabled_flag ? CUR_WAIT : CUR_OK;
        case mSM_OVL_ALARMIN:
            *x = sm->overlay->alarmIn_ovl->field;
            return CUR_OK;
        case mSM_OVL_BIRTHDAY:
            *x = sm->overlay->birthday_ovl->idx;
            return CUR_OK;
        default:
            return CUR_NONE;
    }
}

static void fields_tap(Submenu* sm, float x, float y) {
    float lx, ly;
    int f = -1, n;

    local(sm, sm->menu_type, x, y, &lx, &ly);
    if (sm->menu_type == mSM_OVL_TIMEIN) {
        static const float cx[6] = { 110.5f, 153.5f, 196.5f, 102.5f, 135.5f, 191.75f };
        static const float cy[6] = { 122.0f, 122.0f, 122.0f, 146.0f, 146.0f, 151.0f };

        f = nearest(lx, ly, cx, cy, 6, 22.0f, 12.0f);
        n = 6;
    } else if (sm->menu_type == mSM_OVL_ALARMIN) {
        int at = info_of(sm, mSM_OVL_ALARMIN)->data0 != 0;
        float cx[3] = { at ? 92.5f : 80.0f, at ? 126.5f : 127.5f, 214.0f };
        float cy[3] = { at ? 136.0f : 138.0f, at ? 136.0f : 138.0f, 157.0f };

        f = nearest(lx, ly, cx, cy, 3, 22.0f, 12.0f);
        n = 3;
    } else {
        static const float cx[3] = { 124.0f, 169.0f, 195.0f };
        static const float cy[3] = { 125.0f, 125.0f, 142.0f };

        f = nearest(lx, ly, cx, cy, 3, 22.0f, 12.0f);
        n = 3;
    }
    if (f < 0) {
        return;
    }
    printf("[TOUCH] field %d\n", f);
    /* the last is "done": there, A */
    walk_to(fields_cursor, f, 0, f == n - 1 ? PAD_BUTTON_A : 0);
}

/* --- The calendar ----------------------------------------------------------
 * m_calendar_ovl.c: a swipe left or right shows the month before or after; a
 * tap on a day takes the days' cursor there (A first, from the months), a
 * tap on the day it is on opens the month's diary (A). */

static int calendar_cursor(Submenu* sm, int* x, int* y) {
    mCD_Ovl_c* cd = sm->overlay->calendar_ovl;

    if (cd->move_proc_idx != mCD_MVPL_DAY) {
        /* (the months, a slide or the bar changing: on its way to the days) */
        return cd->move_proc_idx == mCD_MVPL_SELECT && !q_busy() ? CUR_NONE : CUR_WAIT;
    }
    *x = cd->_1057;
    *y = 0;
    return CUR_OK;
}

static void calendar_tap(Submenu* sm, float x, float y) {
    mCD_Ovl_c* cd = sm->overlay->calendar_ovl;
    float lx, ly;
    int col, row, cell;

    local(sm, mSM_OVL_CALENDAR, x, y, &lx, &ly);
    if (cd->move_proc_idx == mCD_MVPL_DAY && in_box(lx, ly, 112.0f, 156.0f, 272.0f, 180.0f) && ly > 160.0f) {
        /* the day's events (more than one): the next, round to the first
         * (the stick down past the last would go a week down instead) */
        if (cd->_1043 > 1) {
            int i;

            if (cd->_1042 + 1 < cd->_1043) {
                q_stick(0, 1);
            } else {
                for (i = 0; i < cd->_1042; i++) {
                    q_stick(0, -1);
                }
            }
        }
        return;
    }
    col = (int)floorf((lx - 48.0f) / 32.0f);
    row = (int)floorf((ly - 57.0f) / 20.0f);
    if (col < 0 || col > 6 || row < 0 || row > 5) {
        return;
    }
    cell = 7 * row + col;
    if (cell > 36 || (cd->month_entries[cd->_1056].day_data[cell] & 0x1F) == 0) {
        return;
    }
    printf("[TOUCH] calendar: day %d\n", cd->month_entries[cd->_1056].day_data[cell] & 0x1F);
    if (cd->move_proc_idx == mCD_MVPL_SELECT) {
        q_button(PAD_BUTTON_A); /* the days */
        walk_to(calendar_cursor, cell, 0, 0);
    } else if (cd->move_proc_idx == mCD_MVPL_DAY) {
        walk_to(calendar_cursor, cell, 0, cell == cd->_1057 ? PAD_BUTTON_A : 0);
    }
}

static void calendar_swipe(Submenu* sm, float dx, float dy) {
    mCD_Ovl_c* cd = sm->overlay->calendar_ovl;

    if (fabsf(dx) > fabsf(dy)) {
        if (cd->move_proc_idx == mCD_MVPL_SELECT) {
            /* the stick is read as held there: one frame for one month */
            q_add(0, dx < 0.0f ? STICK : -STICK, 0, 0, 0, 1, 4);
        }
    } else if (cd->move_proc_idx == mCD_MVPL_DAY) {
        q_stick(0, dy < 0.0f ? 1 : -1);
    }
}

/* --- The diary ---------------------------------------------------------------
 * m_diary_ovl.c: a swipe scrolls it (a line a step; the C-stick ten); a tap
 * on its write button writes (A). */

static void diary_tap(Submenu* sm, float x, float y) {
    mDI_Ovl_c* di = sm->overlay->diary_ovl;

    if (di->move_proc == 0 && info_of(sm, mSM_OVL_DIARY)->data0 == 0 && in_box(x, y, 190.0f, 190.0f, 280.0f, 224.0f)) {
        q_button(PAD_BUTTON_A);
        printf("[TOUCH] diary: write\n");
    }
}

static void diary_swipe(Submenu* sm, float dx, float dy) {
    int i, n;

    (void)sm;
    if (fabsf(dy) < fabsf(dx)) {
        return;
    }
    /* a swipe up shows the lines below */
    if (fabsf(dy) > 100.0f) {
        q_cstick(0, dy < 0.0f ? 1 : -1);
        return;
    }
    n = swipe_steps(dy, 12.0f, 8);
    for (i = 0; i < n; i++) {
        q_stick(0, dy < 0.0f ? 1 : -1);
    }
}

/* --- The post office: savings and the loan --------------------------------
 * m_bank_ovl.c, m_repay_ovl.c: six digits and OK. A tap on a digit takes the
 * cursor there; up and down (the keys, a swipe) put in or take out that
 * digit's worth; a tap on OK (or the done key) settles it. */

static int digits_cursor(Submenu* sm, int* x, int* y) {
    *y = 0;
    *x = sm->menu_type == mSM_OVL_BANK ? sm->overlay->bank_ovl->cursol : sm->overlay->repay_ovl->cursor_idx;
    return playing(sm, sm->menu_type) ? CUR_OK : CUR_WAIT;
}

static void digits_tap(Submenu* sm, float x, float y) {
    int bank = sm->menu_type == mSM_OVL_BANK;
    float x0 = bank ? 157.0f : 145.0f, y0 = bank ? 124.0f : 113.0f;
    float lx, ly;
    int d;

    local(sm, sm->menu_type, x, y, &lx, &ly);
    if (bank ? in_box(lx, ly, 216.0f, 136.0f, 258.0f, 156.0f) : in_box(lx, ly, 208.0f, 128.0f, 250.0f, 148.0f)) {
        walk_to(digits_cursor, 6, 0, PAD_BUTTON_A);
        printf("[TOUCH] bells: OK\n");
        return;
    }
    if (ly < y0 - 6.0f || ly > y0 + 22.0f) {
        return;
    }
    d = (int)floorf((lx - x0) / 12.0f);
    if (d < 0 || d > 5) {
        return;
    }
    printf("[TOUCH] bells: digit %d\n", d);
    walk_to(digits_cursor, d, 0, 0);
}

/* --- The town tune (m_mscore_ovl.c) ------------------------------------------
 * Sixteen notes in two rows and the end. A tap on a note takes the cursor
 * there; up and down change it; the keys play it (X), clear it (Y) and
 * finish (START, the question); the close icon leaves it unsaved (R). */

static int mscore_cursor(Submenu* sm, int* x, int* y) {
    mMS_Ovl_c* ms = sm->overlay->mscore_ovl;

    if (ms->wait_timer > 0 || sAdo_InstCountGet() >= 0 || !playing(sm, mSM_OVL_MSCORE)) {
        return CUR_WAIT; /* (the tune playing: no input, m_mscore_ovl.c) */
    }
    *x = ms->cursor_idx;
    *y = 0;
    return CUR_OK;
}

static void mscore_tap(Submenu* sm, float x, float y) {
    mMS_Ovl_c* ms = sm->overlay->mscore_ovl;
    mSM_MenuInfo_c* info = info_of(sm, mSM_OVL_MSCORE);
    float lx, ly;
    int i;

    local(sm, mSM_OVL_MSCORE, x, y, &lx, &ly);
    if (info->proc_status == mSM_OVL_PROC_OBEY) {
        /* "clear them all?": OK or not */
        if (in_box(lx, ly, 110.0f, 140.0f, 210.0f, 158.0f)) {
            if (ms->menu_idx != 0) {
                q_stick(0, -1);
            }
            q_button(PAD_BUTTON_A);
        } else if (in_box(lx, ly, 110.0f, 158.0f, 210.0f, 176.0f)) {
            q_button(PAD_BUTTON_B);
        }
        return;
    }
    if (in_box(lx, ly, 230.0f, 164.0f, 300.0f, 190.0f)) {
        walk_to(mscore_cursor, 16, 0, PAD_BUTTON_A);
        return;
    }
    {
        int top = ly >= 56.0f && ly < 104.0f;
        int col;

        if (!top && !(ly >= 106.0f && ly < 154.0f)) {
            return;
        }
        col = (int)lrintf((lx - (top ? 69.0f : 89.0f)) / 21.0f);
        if (col < 0 || col > 7) {
            return;
        }
        i = top ? col : 8 + col;
    }
    printf("[TOUCH] tune: note %d\n", i);
    walk_to(mscore_cursor, i, 0, 0);
}

/* --- The design editor (m_design_ovl.c) ----------------------------------------
 * The 32x32 canvas: the pen draws along the finger (A held, and the way
 * between two frames drawn by mDE_PC_pen_line); a fill, a stamp or the undo
 * tool acts where it is tapped; a shape goes from where the finger lands to
 * where it is lifted. A tap on a colour picks it, on the palette's button the
 * next palette; a tap on a tool picks it (the D-pad's rows), a tap on the
 * picked one its next kind. Keys: the grid (X), undo (Y), the eyedropper (B
 * where the next tap is), done (START: the question). */

void mDE_PC_set_cursor(Submenu* submenu, int x, int y);
void mDE_PC_pen_line(Submenu* submenu, int x0, int y0, int x1, int y1);
void mDE_PC_set_color(Submenu* submenu, int idx);
void mDE_PC_palette_step(Submenu* submenu, int dir);

static u16 s_held;   /* buttons held while a finger is (the pen: A) */
static int s_pick;   /* the eyedropper: the next tap on the canvas takes its colour */
static struct {
    int on;          /* a finger drawing */
    int x, y;        /* the pixel drawn last */
    int nx, ny;      /* where it is now (drawn in the next game frame) */
    int shape;       /* a shape: anchored where it landed */
} s_pen;

/* A point (320x240) on the canvas: its pixel */
static int design_pixel(Submenu* sm, float x, float y, int* px, int* py) {
    float lx, ly, X, Y;

    local(sm, mSM_OVL_DESIGN, x, y, &lx, &ly);
    X = lx - 160.0f;
    Y = 120.0f - ly;
    *px = (int)floorf((X + 81.0f) / 5.0f);
    *py = (int)floorf((80.0f - Y) / 5.0f);
    return *px >= 0 && *px < 32 && *py >= 0 && *py < 32;
}

static int design_ready(Submenu* sm) {
    mDE_Ovl_c* de = sm->overlay->design_ovl;

    return de != NULL && playing(sm, mSM_OVL_DESIGN) && de->mode == mDE_MODE_MAIN;
}

static int design_row_cursor(Submenu* sm, int* x, int* y) {
    mDE_Ovl_c* de = sm->overlay->design_ovl;

    if (de == NULL) {
        return CUR_NONE;
    }
    *x = 0;
    *y = de->_69F;
    return design_ready(sm) ? CUR_OK : CUR_WAIT;
}

static void design_down(Submenu* sm, float x, float y) {
    mDE_Ovl_c* de = sm->overlay->design_ovl;
    int px, py;

    if (!design_ready(sm) || s_pick || !design_pixel(sm, x, y, &px, &py)) {
        return;
    }
    if (de->main_mode_act == mDE_MAIN_MODE_PEN) {
        mDE_PC_set_cursor(sm, px, py);
        s_pen.on = 1;
        s_pen.shape = 0;
        s_pen.x = s_pen.nx = px;
        s_pen.y = s_pen.ny = py;
        s_held = PAD_BUTTON_A; /* (A's press keeps the undo, then it draws where the cursor is) */
    } else if (de->main_mode_act == mDE_MAIN_MODE_WAKU) {
        mDE_PC_set_cursor(sm, px, py);
        q_button(PAD_BUTTON_A); /* the shape's first corner */
        s_pen.on = 1;
        s_pen.shape = 1;
        s_pen.x = s_pen.nx = px;
        s_pen.y = s_pen.ny = py;
    }
}

static void design_move(Submenu* sm, float x, float y) {
    int px, py;

    if (!s_pen.on) {
        return;
    }
    (void)design_pixel(sm, x, y, &px, &py);
    s_pen.nx = px < 0 ? 0 : (px > 31 ? 31 : px);
    s_pen.ny = py < 0 ? 0 : (py > 31 ? 31 : py);
}

static void design_frame(Submenu* sm) {
    if (!s_pen.on || !design_ready(sm)) {
        return;
    }
    if (s_pen.nx != s_pen.x || s_pen.ny != s_pen.y) {
        if (!s_pen.shape) {
            mDE_PC_pen_line(sm, s_pen.x, s_pen.y, s_pen.nx, s_pen.ny);
        }
        mDE_PC_set_cursor(sm, s_pen.nx, s_pen.ny); /* (a shape follows it to its other corner) */
        s_pen.x = s_pen.nx;
        s_pen.y = s_pen.ny;
    }
}

static void design_up(Submenu* sm, float x, float y, int moved) {
    mDE_Ovl_c* de = sm->overlay->design_ovl;
    float lx, ly, X, Y;
    int px, py;

    if (s_pen.on) {
        if (s_pen.shape) {
            design_move(sm, x, y);
            design_frame(sm);
            q_button(PAD_BUTTON_A); /* the other corner: drawn */
        }
        s_pen.on = 0;
        s_held = 0;
        return;
    }
    if (!design_ready(sm) || moved) {
        return;
    }
    if (design_pixel(sm, x, y, &px, &py)) {
        mDE_PC_set_cursor(sm, px, py);
        if (s_pick) {
            s_pick = 0;
            q_button(PAD_BUTTON_B); /* the colour under it */
        } else if (de->main_mode_act != mDE_MAIN_MODE_PEN) {
            q_button(PAD_BUTTON_A); /* fill, stamp, undo */
        }
        return;
    }
    local(sm, mSM_OVL_DESIGN, x, y, &lx, &ly);
    X = lx - 160.0f;
    Y = 120.0f - ly;
    if (X >= 96.0f && X < 124.0f) {
        if (Y >= 57.0f && Y < 100.0f) {
            mDE_PC_palette_step(sm, 1); /* the palette's button (and its number) */
        } else if (Y >= -83.0f && Y < 57.0f) {
            int i = (int)floorf((68.0f - Y) / 10.0f);

            if (i >= 1 && i < 16) {
                mDE_PC_set_color(sm, i);
            }
        }
        return;
    }
    if (X >= -128.0f && X < -96.0f && Y >= 46.0f && Y < 82.0f) {
        q_button(PAD_BUTTON_X); /* the grid */
        return;
    }
    if (X >= -127.0f && X < -97.0f && Y >= -98.0f && Y < 31.0f) {
        int r = (int)floorf((29.0f - Y) / 24.0f);

        if (r >= 0 && r < 5) {
            if (r == de->_69F) {
                q_button(PAD_BUTTON_RIGHT); /* its next kind */
            } else {
                s_walk.on = 0;
                walk_to(design_row_cursor, 0, r, 0);
                s_walk.dpad = 1;
            }
            printf("[TOUCH] design: tool row %d\n", r);
        }
    }
}

/* --- Which menus, their keys -------------------------------------------------- */

static int editor_open(Submenu* sm) {
    return sm->menu_type == mSM_OVL_EDITOR || sm->menu_type == mSM_OVL_LEDIT || sm->menu_type == mSM_OVL_HBOARD ||
           sm->menu_type == mSM_OVL_PASSWORDMAKE || sm->menu_type == mSM_OVL_PASSWORDCHK;
}

int pc_touch_menus_owns(Submenu* sm) {
    if (sm == NULL || sm->overlay == NULL) {
        return FALSE;
    }
    switch (sm->menu_type) {
        case mSM_OVL_EDITOR:
        case mSM_OVL_LEDIT:
        case mSM_OVL_HBOARD:
        case mSM_OVL_PASSWORDMAKE:
        case mSM_OVL_PASSWORDCHK:
        case mSM_OVL_ADDRESS:
        case mSM_OVL_EDITENDCHK:
        case mSM_OVL_CPWARNING:
        case mSM_OVL_TIMEIN:
        case mSM_OVL_ALARMIN:
        case mSM_OVL_BIRTHDAY:
        case mSM_OVL_CALENDAR:
        case mSM_OVL_DIARY:
        case mSM_OVL_BANK:
        case mSM_OVL_REPAY:
        case mSM_OVL_MSCORE:
        case mSM_OVL_DESIGN:
            return TRUE;
        case mSM_OVL_BOARD:
            return TRUE; /* (writing: the address book and the editor come and go over it) */
        default:
            return FALSE;
    }
}

typedef struct {
    int key;
    int glyph;
    const char* en;
    const char* ko;
} Key;

static int keys_of(Submenu* sm, Key* k) {
    int n = 0;

    if (address_active(sm)) {
        return 0;
    }
    if (editor_open(sm)) {
        if (sm->menu_type == mSM_OVL_EDITOR && editor_wheel(sm)) {
            k[n++] = (Key){ K_Y, PC_KEYG_TEXT, "abc", "abc" };   /* the next set of characters */
            k[n++] = (Key){ K_X, PC_KEYG_TEXT, "\"", "\"" };       /* dakuten, capitals */
            k[n++] = (Key){ K_R, PC_KEYG_TEXT, "SPC", "띄움" };
            k[n++] = (Key){ K_B, PC_KEYG_BACK, NULL, NULL };
        }
        k[n++] = (Key){ K_DLEFT, PC_KEYG_LEFT, NULL, NULL };
        k[n++] = (Key){ K_DRIGHT, PC_KEYG_RIGHT, NULL, NULL };
        k[n++] = (Key){ K_DUP, PC_KEYG_UP, NULL, NULL };
        k[n++] = (Key){ K_DDOWN, PC_KEYG_DOWN, NULL, NULL };
        return n;
    }
    switch (sm->menu_type) {
        case mSM_OVL_TIMEIN:
        case mSM_OVL_ALARMIN:
        case mSM_OVL_BIRTHDAY:
        case mSM_OVL_BANK:
        case mSM_OVL_REPAY:
            k[n++] = (Key){ K_UP, PC_KEYG_UP, NULL, NULL };
            k[n++] = (Key){ K_DOWN, PC_KEYG_DOWN, NULL, NULL };
            break;
        case mSM_OVL_MSCORE:
            k[n++] = (Key){ K_UP, PC_KEYG_UP, NULL, NULL };
            k[n++] = (Key){ K_DOWN, PC_KEYG_DOWN, NULL, NULL };
            k[n++] = (Key){ K_X, PC_KEYG_PLAY, NULL, NULL };
            k[n++] = (Key){ K_Y, PC_KEYG_TEXT, "Clear", "지우기" };
            break;
        case mSM_OVL_CALENDAR:
            k[n++] = (Key){ K_LEFT, PC_KEYG_LEFT, NULL, NULL };
            k[n++] = (Key){ K_RIGHT, PC_KEYG_RIGHT, NULL, NULL };
            break;
        case mSM_OVL_DIARY:
            k[n++] = (Key){ K_UP, PC_KEYG_UP, NULL, NULL };
            k[n++] = (Key){ K_DOWN, PC_KEYG_DOWN, NULL, NULL };
            break;
        case mSM_OVL_DESIGN:
            k[n++] = (Key){ K_Y, PC_KEYG_BACK, NULL, NULL }; /* undo */
            k[n++] = (Key){ K_X, PC_KEYG_TEXT, "Grid", "격자" };
            k[n++] = (Key){ K_PICK, PC_KEYG_TEXT, s_pick ? "[Pick]" : "Pick", s_pick ? "[스포이트]" : "스포이트" };
            break;
        default:
            break;
    }
    return n;
}

/* The done key: START where it finishes (the editor, the numbers, the tune) */
static int done_wanted(Submenu* sm) {
    if (address_active(sm)) {
        return FALSE;
    }
    if (editor_open(sm)) {
        return sm->menu_type == mSM_OVL_EDITOR && playing(sm, mSM_OVL_EDITOR);
    }
    switch (sm->menu_type) {
        case mSM_OVL_TIMEIN:
        case mSM_OVL_ALARMIN:
        case mSM_OVL_BIRTHDAY:
        case mSM_OVL_BANK:
        case mSM_OVL_REPAY:
        case mSM_OVL_MSCORE:
        case mSM_OVL_DESIGN:
            return TRUE;
        default:
            return FALSE;
    }
}

/* The close icon: what leaves the menu (0: none; the editor has no way out
 * but its done key) */
static u16 close_button(Submenu* sm) {
    if (address_active(sm)) {
        return PAD_BUTTON_B;
    }
    if (editor_open(sm) || sm->menu_type == mSM_OVL_CPWARNING) {
        return 0;
    }
    if (sm->menu_type == mSM_OVL_MSCORE) {
        return PAD_TRIGGER_R; /* (leaves it as it was) */
    }
    if (sm->menu_type == mSM_OVL_TIMEIN || sm->menu_type == mSM_OVL_ALARMIN || sm->menu_type == mSM_OVL_BIRTHDAY ||
        sm->menu_type == mSM_OVL_DESIGN) {
        return 0; /* (no way out but done: the clock set, the design kept or not) */
    }
    return PAD_BUTTON_B;
}

int pc_touch_menus_hud(Submenu* sm, PCTouchHudIcon* out, int n, int max) {
    float w = (float)g_pc_window_w, h = (float)g_pc_window_h;
    float u = w < h ? w : h;
    float r = 0.05f * u;
    float y = 0.085f * u;
    int ko = pc_gui_korean();
    Key keys[16];
    int nk = keys_of(sm, keys);
    int i;

    if (u <= 0.0f) {
        return n;
    }
#define ADD(kind_, x_, y_, r_)                                                                                       \
    do {                                                                                                               \
        if (n < max) {                                                                                                 \
            memset(&out[n], 0, sizeof(out[n]));                                                                        \
            out[n].kind = (kind_);                                                                                     \
            out[n].x = (x_) / w;                                                                                       \
            out[n].y = (y_) / h;                                                                                       \
            out[n].r = (r_) / u;                                                                                       \
            n++;                                                                                                       \
        }                                                                                                              \
    } while (0)
    if (close_button(sm) != 0) {
        ADD(PC_HUD_CLOSE, w - 0.08f * u, y, r);
    }
    if (done_wanted(sm)) {
        ADD(PC_HUD_DONE, w - (close_button(sm) != 0 ? 0.20f : 0.08f) * u, y, r);
    }
    for (i = 0; i < nk; i++) {
        /* down the right edge, in a second column when the window is short */
        int rows = (int)((h - 0.06f * u - (y + 0.13f * u)) / (0.115f * u)) + 1;
        int col, row;
        float ky;

        if (rows < 1) {
            rows = 1;
        }
        col = i / rows;
        row = i % rows;
        ky = y + 0.13f * u + 0.115f * u * row;
        ADD(PC_HUD_KEY, w - (0.08f + 0.12f * col) * u, ky, r * 0.85f);
        if (n > 0 && out[n - 1].kind == PC_HUD_KEY) {
            out[n - 1].index = keys[i].key;
            out[n - 1].glyph = keys[i].glyph;
            out[n - 1].label = ko ? keys[i].ko : keys[i].en;
        }
    }
#undef ADD
    return n;
}

static void press_key(int key) {
    switch (key) {
        case K_UP: q_stick(0, -1); break;
        case K_DOWN: q_stick(0, 1); break;
        case K_LEFT: q_add(0, -STICK, 0, 0, 0, 1, 4); break; /* (the calendar reads it held: one frame) */
        case K_RIGHT: q_add(0, STICK, 0, 0, 0, 1, 4); break;
        case K_DUP: q_button(PAD_BUTTON_UP); break;
        case K_DDOWN: q_button(PAD_BUTTON_DOWN); break;
        case K_DLEFT: q_button(PAD_BUTTON_LEFT); break;
        case K_DRIGHT: q_button(PAD_BUTTON_RIGHT); break;
        case K_A: q_button(PAD_BUTTON_A); break;
        case K_B: q_button(PAD_BUTTON_B); break;
        case K_X: q_button(PAD_BUTTON_X); break;
        case K_Y: q_button(PAD_BUTTON_Y); break;
        case K_R: q_button(PAD_TRIGGER_R); break;
        case K_START: q_button(PAD_BUTTON_START); break;
        case K_PICK: s_pick = !s_pick; break;
        default: break;
    }
}

void pc_touch_menus_icon(int kind, int index) {
    GAME_PLAY* play = pc_touch_play();
    Submenu* sm = play != NULL ? &play->submenu : NULL;

    if (sm == NULL || !pc_touch_menus_owns(sm)) {
        return;
    }
    if (kind == PC_HUD_CLOSE) {
        u16 b = close_button(sm);

        if (b != 0) {
            s_walk.on = 0;
            q_button(b);
        }
    } else if (kind == PC_HUD_DONE) {
        s_walk.on = 0;
        q_button(PAD_BUTTON_START);
    } else if (kind == PC_HUD_KEY) {
        press_key(index);
    }
    printf("[TOUCH] menu %d: icon %d %d\n", sm->menu_type, kind, index);
}

/* The top menu's overlay is there (a menu opened over another takes the top
 * at once, its overlay only in the next frame: m_submenu_ovl.c mSM_set_proc) */
static int menu_built(Submenu* sm) {
    Submenu_Overlay_c* o = sm->overlay;

    switch (sm->menu_type) {
        case mSM_OVL_EDITOR: return o->editor_ovl != NULL;
        case mSM_OVL_ADDRESS: return o->address_ovl != NULL;
        case mSM_OVL_EDITENDCHK: return o->editEndChk_ovl != NULL;
        case mSM_OVL_CPWARNING: return o->cpwarning_ovl != NULL;
        case mSM_OVL_TIMEIN: return o->timeIn_ovl != NULL;
        case mSM_OVL_ALARMIN: return o->alarmIn_ovl != NULL;
        case mSM_OVL_BIRTHDAY: return o->birthday_ovl != NULL;
        case mSM_OVL_CALENDAR: return o->calendar_ovl != NULL;
        case mSM_OVL_DIARY: return o->diary_ovl != NULL;
        case mSM_OVL_BANK: return o->bank_ovl != NULL;
        case mSM_OVL_REPAY: return o->repay_ovl != NULL;
        case mSM_OVL_MSCORE: return o->mscore_ovl != NULL;
        case mSM_OVL_DESIGN: return o->design_ovl != NULL;
        default: return TRUE;
    }
}

/* --- Fingers ------------------------------------------------------------------ */

static struct {
    int down;
    s64 id;
    float x0, y0, x, y; /* 320x240 */
    int moved;
} s_f;

void pc_touch_menus_finger(Submenu* sm, int kind, s64 id, float ux, float uy) {
    float x = ux * 0.5f, y = uy * 0.5f;
    float dx, dy;

    if (kind == 0) {
        s_f.down = 1;
        s_f.id = id;
        s_f.x0 = s_f.x = x;
        s_f.y0 = s_f.y = y;
        s_f.moved = 0;
        if (sm != NULL && sm->menu_type == mSM_OVL_DESIGN) {
            design_down(sm, x, y);
        }
        return;
    }
    if (!s_f.down || id != s_f.id) {
        return;
    }
    s_f.x = x;
    s_f.y = y;
    dx = x - s_f.x0;
    dy = y - s_f.y0;
    if (fabsf(dx) > TAP_SLOP || fabsf(dy) > TAP_SLOP) {
        s_f.moved = 1;
    }
    if (sm != NULL && sm->menu_type == mSM_OVL_DESIGN) {
        if (kind == 1) {
            design_move(sm, x, y);
        } else {
            s_f.down = 0;
            design_up(sm, x, y, s_f.moved);
        }
        return;
    }
    if (kind != 2 || sm == NULL || !pc_touch_menus_owns(sm)) {
        return;
    }
    s_f.down = 0;
    if (!menu_built(sm)) {
        return; /* (just opened: its overlay comes in the next frame) */
    }
    if (address_active(sm)) {
        if (!s_f.moved) {
            address_tap(sm, s_f.x0, s_f.y0);
        } else if (fabsf(dx) > fabsf(dy) && sm->overlay->address_ovl->proc_idx == mAD_PROC_SELECT) {
            q_stick(dx < 0.0f ? 1 : -1, 0); /* a swipe to the left: the next page */
        }
        return;
    }
    if (s_f.moved) {
        /* a swipe: up or down changes the value (up = more), left or right a page */
        switch (sm->menu_type) {
            case mSM_OVL_CALENDAR:
                calendar_swipe(sm, dx, dy);
                break;
            case mSM_OVL_DIARY:
                diary_swipe(sm, dx, dy);
                break;
            case mSM_OVL_TIMEIN:
            case mSM_OVL_ALARMIN:
            case mSM_OVL_BIRTHDAY:
            case mSM_OVL_BANK:
            case mSM_OVL_REPAY:
            case mSM_OVL_MSCORE:
                if (fabsf(dy) > fabsf(dx)) {
                    int i, n = swipe_steps(dy, 14.0f, 9);

                    for (i = 0; i < n; i++) {
                        q_stick(0, dy < 0.0f ? -1 : 1);
                    }
                }
                break;
            default:
                break;
        }
        return;
    }
    switch (sm->menu_type) {
        case mSM_OVL_EDITOR:
            editor_tap(sm, x, y);
            break;
        case mSM_OVL_EDITENDCHK:
            endchk_tap(sm, x, y);
            break;
        case mSM_OVL_CPWARNING:
            cpwarning_tap(sm, x, y);
            break;
        case mSM_OVL_TIMEIN:
        case mSM_OVL_ALARMIN:
        case mSM_OVL_BIRTHDAY:
            fields_tap(sm, x, y);
            break;
        case mSM_OVL_CALENDAR:
            calendar_tap(sm, x, y);
            break;
        case mSM_OVL_DIARY:
            diary_tap(sm, x, y);
            break;
        case mSM_OVL_BANK:
        case mSM_OVL_REPAY:
            digits_tap(sm, x, y);
            break;
        case mSM_OVL_MSCORE:
            mscore_tap(sm, x, y);
            break;
        default:
            break;
    }
}

/* --- Each frame, the reading -------------------------------------------------- */

void pc_touch_menus_frame(Submenu* sm) {
    if (sm == NULL || !pc_touch_menus_owns(sm)) {
        s_walk.on = 0;
        s_f.down = 0;
        s_pen.on = 0;
        s_held = 0;
        s_pick = 0;
        if (!q_busy()) {
            q_clear();
        }
        q_tick();
        return;
    }
    q_tick();
    walk_step(sm);
    if (sm->menu_type == mSM_OVL_DESIGN) {
        design_frame(sm);
    } else {
        s_pen.on = 0;
        s_held = 0;
    }
}

void pc_touch_menus_read(u16* buttons, s8* stick_x, s8* stick_y, s8* cstick_x, s8* cstick_y) {
    const Step* s;

    *buttons |= s_held;
    if (!q_busy()) {
        return;
    }
    s = &s_q[s_qi];
    if (s_qt >= s->on) {
        return;
    }
    *buttons |= s->buttons;
    if (s->sx != 0 || s->sy != 0) {
        *stick_x = s->sx;
        *stick_y = s->sy;
    }
    if (s->cx != 0 || s->cy != 0) {
        *cstick_x = s->cx;
        *cstick_y = s->cy;
    }
}
