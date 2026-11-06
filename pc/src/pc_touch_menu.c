/* pc_touch_menu.c - the pockets and the map in the full touch mode
 * (pc_touch_full.h)
 *
 * The pockets: a tap on a slot takes the cursor there and presses A (the
 * item's commands, or what the hand holds put down there: on another item it
 * swaps, on the player it is worn, eaten or taken in hand); a tap on a line
 * of the command list chooses it, a tap beside the list goes back (B). A
 * finger dragged from an item takes it (L) and carries it: the cursor
 * follows the finger over the slots, and where the finger is lifted the item
 * goes (A), or back where it came from if that is no slot (B). A tap on a
 * page tab turns the page (R). The cursor is moved by the game's own rules
 * (m_tag_ovl.c mTG_PC_touch_cursor): what the hand holds can go only where
 * the stick could take it.
 *
 * The map: a tap on an acre shows it, a tap beside the map closes it.
 *
 * Coordinates: the menus are drawn in the game's 640x480 picture, kept in the
 * 4:3 middle of a wider window (pc_gx_window_to_ui); the tables' places are
 * those of m_tag_ovl.c (mTG_table_data) and m_map_ovl.c. */
#include "pc_touch_internal.h"
#include "pc_platform.h"

#include "m_common_data.h"
#include "m_submenu.h"
#include "m_submenu_ovl.h"
#include "m_tag_ovl.h"
#include "m_hand_ovl.h"
#include "m_inventory_ovl.h"
#include "m_map_ovl.h"
#include "m_mail.h"

#include <dolphin/pad.h>
#include <math.h>
#include <string.h>

extern u32 pc_frame_counter;

#define PRESS_FRAMES 3
#define STEP_TIMEOUT 90 /* frames a step may wait for the menu */
#define DRAG_SLOP 12.0f /* in the 640x480 picture */

enum {
    PH_NONE,
    PH_MOVE_THEN_A, /* a tap: the cursor there, then A */
    PH_GRAB,        /* a drag began: the cursor to the item, then L */
    PH_HOLD,        /* carrying: the cursor follows the finger */
    PH_DROP,        /* lifted: the cursor there, then A */
};

typedef struct {
    int table, col, row;
} Cell;

static struct {
    int phase;
    Cell target;
    u32 since;
    int pressed; /* the step's button went */
    int finger;  /* a finger of the pockets is down */
    s64 id;
    float ux0, uy0, ux, uy; /* 640x480 */
    int dragging;
} s_inv;

static u16 s_press;
static u32 s_press_from;
static u32 s_close_until;
static int s_map_tap;
static float s_map_ux, s_map_uy;

static void press(u16 buttons, u32 delay) {
    s_press = buttons;
    s_press_from = pc_frame_counter + delay;
}

static Submenu* submenu_of(GAME_PLAY* play) {
    if (play == NULL || play->submenu.process_status != mSM_PROCESS_PLAY || play->submenu.overlay == NULL) {
        return NULL;
    }
    return &play->submenu;
}

/* The pockets on their main page, taking input */
static mSM_MenuInfo_c* inventory_ready(Submenu* sm) {
    mSM_MenuInfo_c* info;

    if (sm == NULL || sm->menu_type != mSM_OVL_INVENTORY) {
        return NULL;
    }
    info = &sm->overlay->menu_info[mSM_OVL_INVENTORY];
    if (info->proc_status != mSM_OVL_PROC_PLAY || sm->overlay->tag_ovl == NULL || sm->overlay->hand_ovl == NULL ||
        sm->overlay->inventory_ovl == NULL) {
        return NULL;
    }
    return info;
}

/* Which slot a point of the 640x480 picture is on */
static int cell_at(const mSM_MenuInfo_c* info, float ux, float uy, Cell* c) {
    float x = ux - 2.0f * info->position[0];
    float y = uy + 2.0f * info->position[1];
    int col, row;

    /* the 15 pockets: 5 x 3, 48 apart, centres from (166, 260) */
    col = (int)floorf((x - 142.0f) / 48.0f);
    row = (int)floorf((y - 236.0f) / 48.0f);
    if (col >= 0 && col < 5 && row >= 0 && row < 3) {
        *c = (Cell){ mTG_TABLE_ITEM, col, row };
        return TRUE;
    }
    /* the letters: 2 x 5 from (430, 164) */
    col = (int)floorf((x - 406.0f) / 48.0f);
    row = (int)floorf((y - 140.0f) / 48.0f);
    if (col >= 0 && col < 2 && row >= 0 && row < 5) {
        *c = (Cell){ mTG_TABLE_MAIL, col, row };
        return TRUE;
    }
    /* the player (wear, eat, take in hand), the purse, the room's back */
    if (fabsf(x - 210.0f) < 56.0f && fabsf(y - 150.0f) < 70.0f) {
        *c = (Cell){ mTG_TABLE_PLAYER, 0, 0 };
        return TRUE;
    }
    if (fabsf(x - 352.0f) < 40.0f && fabsf(y - 204.0f) < 22.0f) {
        *c = (Cell){ mTG_TABLE_MONEY, 0, 0 };
        return TRUE;
    }
    return FALSE;
}

/* A page tab (fish, items, insects) */
static int tab_at(const mSM_MenuInfo_c* info, float ux, float uy) {
    float x = ux - 2.0f * info->position[0];
    float y = uy + 2.0f * info->position[1];
    int row;

    if (fabsf(x - 530.0f) > 30.0f) {
        return -1;
    }
    for (row = 0; row < 3; row++) {
        if (fabsf(y - (166.0f + 74.0f * row)) < 32.0f) {
            return row;
        }
    }
    return -1;
}

static int cursor_is(Submenu* sm, const Cell* c) {
    const mTG_tag_c* tag = &sm->overlay->tag_ovl->tags[0];

    return tag->table == c->table && tag->tag_col == c->col && tag->tag_row == c->row;
}

/* The hand at rest over the cursor, no list open: the menu takes a step */
static int hand_idle(Submenu* sm) {
    mHD_Ovl_c* hand = sm->overlay->hand_ovl;
    mTG_Ovl_c* tag = sm->overlay->tag_ovl;

    return hand->info.move_flag == mHD_MOVE_NONE && hand->info.wait_timer == 0 &&
           (hand->info.act == mHD_ACTION_CLOSE_KEEP || hand->info.act == mHD_ACTION_SASU) && tag->sel_tag_idx == 0 &&
           tag->tags[0].type == mTG_TYPE_NONE && sm->overlay->inventory_ovl->page_move_timer == 0 &&
           sm->overlay->segment.player_anime_timer <= 0 && sm->overlay->inventory_ovl->remove_timer == 0;
}

static int hand_holds(Submenu* sm) {
    mHD_Ovl_c* hand = sm->overlay->hand_ovl;

    return hand->info.item != EMPTY_NO || !mMl_check_not_used_mail(&hand->info.mail);
}

/* The item in a slot (to start a drag from) */
static int cell_has_item(const Cell* c) {
    if (c->table == mTG_TABLE_ITEM) {
        return Now_Private->inventory.pockets[c->col + c->row * 5] != EMPTY_NO;
    }
    if (c->table == mTG_TABLE_MAIL) {
        return !mMl_check_not_used_mail(&Now_Private->mail[c->col + c->row * 2]);
    }
    return FALSE;
}

static void set_phase(int phase, const Cell* c) {
    s_inv.phase = phase;
    if (c != NULL) {
        s_inv.target = *c;
    }
    s_inv.since = pc_frame_counter;
    s_inv.pressed = 0;
}

void pc_touch_menu_finger(int kind, s64 id, float x, float y) {
    GAME_PLAY* play = pc_touch_play();
    Submenu* sm = submenu_of(play);
    float ux, uy;

    pc_gx_window_to_ui(x, y, &ux, &uy);
    if (sm != NULL && sm->menu_type == mSM_OVL_MAP) {
        if (kind == 2) {
            s_map_tap = 1;
            s_map_ux = ux;
            s_map_uy = uy;
        }
        return;
    }
    if (kind == 0) {
        s_inv.finger = 1;
        s_inv.id = id;
        s_inv.ux0 = s_inv.ux = ux;
        s_inv.uy0 = s_inv.uy = uy;
        s_inv.dragging = 0;
        return;
    }
    if (!s_inv.finger || id != s_inv.id) {
        return;
    }
    s_inv.ux = ux;
    s_inv.uy = uy;
    if (kind == 1) {
        if (!s_inv.dragging && (fabsf(ux - s_inv.ux0) > DRAG_SLOP || fabsf(uy - s_inv.uy0) > DRAG_SLOP)) {
            mSM_MenuInfo_c* info = inventory_ready(sm);
            Cell c;

            s_inv.dragging = 1;
            /* a drag from an item, the hand empty: take it */
            if (info != NULL && s_inv.phase == PH_NONE && cell_at(info, s_inv.ux0, s_inv.uy0, &c) &&
                cell_has_item(&c) && !hand_holds(sm) && sm->overlay->tag_ovl->sel_tag_idx == 0) {
                set_phase(PH_GRAB, &c);
            }
        }
        return;
    }
    /* up */
    s_inv.finger = 0;
    {
        mSM_MenuInfo_c* info = inventory_ready(sm);
        Cell c;

        if (info == NULL) {
            set_phase(PH_NONE, NULL);
            return;
        }
        if (s_inv.phase == PH_GRAB || s_inv.phase == PH_HOLD) {
            if (cell_at(info, ux, uy, &c)) {
                set_phase(PH_DROP, &c);
            } else if (s_inv.phase == PH_HOLD) {
                set_phase(PH_NONE, NULL);
                press(PAD_BUTTON_B, 0); /* back where it came from */
            } else {
                set_phase(PH_NONE, NULL);
            }
            return;
        }
        if (s_inv.dragging) {
            return;
        }
        /* a tap */
        if (sm->overlay->tag_ovl->sel_tag_idx >= 1) {
            float lx, ly, row_h;
            int lines;

            if (mTG_PC_select_geometry(sm, info, &lx, &ly, &row_h, &lines)) {
                float px = ux * 0.5f, py = uy * 0.5f;
                int row = (int)floorf((py - ly) / row_h);

                if (px > lx - 16.0f && px < lx + 96.0f && py >= ly && row >= 0 && row < lines) {
                    mTG_PC_touch_select_row(row);
                    printf("[TOUCH] pockets: line %d\n", row);
                    return;
                }
            }
            press(PAD_BUTTON_B, 0); /* beside the list: back */
            return;
        }
        if (cell_at(info, ux, uy, &c)) {
            set_phase(PH_MOVE_THEN_A, &c);
            return;
        }
        {
            int tab = tab_at(info, ux, uy);

            if (tab >= 0 && sm->overlay->inventory_ovl->page_order[0] != tab) {
                press(PAD_TRIGGER_R, 0);
            }
        }
    }
}

static void inventory_frame(Submenu* sm) {
    mSM_MenuInfo_c* info = inventory_ready(sm);
    Cell c;

    if (info == NULL) {
        s_inv.phase = PH_NONE;
        mTG_PC_touch_cursor(-1, 0, 0);
        return;
    }
    if (s_inv.phase != PH_NONE && pc_frame_counter - s_inv.since > STEP_TIMEOUT) {
        printf("[TOUCH] pockets: gave up (phase %d)\n", s_inv.phase);
        s_inv.phase = PH_NONE;
    }
    switch (s_inv.phase) {
        case PH_MOVE_THEN_A:
        case PH_DROP:
            if (!cursor_is(sm, &s_inv.target)) {
                mTG_PC_touch_cursor(s_inv.target.table, s_inv.target.col, s_inv.target.row);
            } else if (hand_idle(sm) && !s_inv.pressed) {
                mTG_PC_touch_cursor(-1, 0, 0);
                press(PAD_BUTTON_A, 0);
                s_inv.pressed = 1;
                printf("[TOUCH] pockets: A at table %d %d,%d\n", s_inv.target.table, s_inv.target.col,
                       s_inv.target.row);
            } else if (s_inv.pressed && pc_frame_counter - s_press_from > PRESS_FRAMES) {
                s_inv.phase = PH_NONE;
            }
            break;
        case PH_GRAB:
            if (!cursor_is(sm, &s_inv.target)) {
                mTG_PC_touch_cursor(s_inv.target.table, s_inv.target.col, s_inv.target.row);
            } else if (hand_idle(sm) && !s_inv.pressed) {
                mTG_PC_touch_cursor(-1, 0, 0);
                press(PAD_TRIGGER_L, 0);
                s_inv.pressed = 1;
            } else if (s_inv.pressed && hand_holds(sm)) {
                set_phase(PH_HOLD, NULL);
                printf("[TOUCH] pockets: took the item\n");
            }
            break;
        case PH_HOLD:
            s_inv.since = pc_frame_counter; /* as long as the finger is down */
            if (cell_at(info, s_inv.ux, s_inv.uy, &c) && !cursor_is(sm, &c)) {
                mTG_PC_touch_cursor(c.table, c.col, c.row);
            } else {
                mTG_PC_touch_cursor(-1, 0, 0);
            }
            break;
        default:
            mTG_PC_touch_cursor(-1, 0, 0);
            break;
    }
}

static void map_frame(Submenu* sm) {
    mSM_MenuInfo_c* info;
    mMP_Ovl_c* map;
    int tap = s_map_tap;

    s_map_tap = 0;
    if (sm == NULL || sm->menu_type != mSM_OVL_MAP || !tap) {
        return;
    }
    info = &sm->overlay->menu_info[mSM_OVL_MAP];
    map = sm->overlay->map_ovl;
    if (map == NULL || info->proc_status != mSM_OVL_PROC_PLAY) {
        return;
    }
    {
        /* the acres: 44 apart in the 640x480 picture, the first's centre at
         * (343.4, 148.6) (m_map_ovl.c: 22 units from (11.7, 45.7)) */
        float x = s_map_ux - 2.0f * info->position[0];
        float y = s_map_uy + 2.0f * info->position[1];
        int bx = (int)floorf((x - 343.4f + 22.0f) / 44.0f);
        int bz = (int)floorf((y - 148.6f + 22.0f) / 44.0f);

        if (bx >= 0 && bx < FG_BLOCK_X_NUM && bz >= 0 && bz < FG_BLOCK_Z_NUM) {
            if (bx != map->sel_bx || bz != map->sel_bz) {
                map->sel_bx = (u8)bx;
                map->sel_bz = (u8)bz;
                map->map_cursor_target_xpos = (f32)bx * mMP_BLOCK_SIZE_F;
                map->map_cursor_target_zpos = -(f32)bz * mMP_BLOCK_SIZE_F;
                info->proc_status = mSM_OVL_PROC_WAIT; /* the cursor glides there (mMP_move_Wait) */
                sAdo_SysTrgStart(NA_SE_CURSOL);
            }
        } else {
            press(PAD_BUTTON_B, 0); /* beside the map: closed */
        }
    }
}

void pc_touch_menu_frame(GAME_PLAY* play) {
    Submenu* sm = submenu_of(play);

    if (sm == NULL) {
        s_inv.phase = PH_NONE;
        s_inv.finger = 0;
        s_map_tap = 0;
        s_close_until = 0;
        mTG_PC_touch_cursor(-1, 0, 0);
        return;
    }
    if (sm->menu_type == mSM_OVL_INVENTORY) {
        inventory_frame(sm);
    } else {
        mTG_PC_touch_cursor(-1, 0, 0);
        s_inv.phase = PH_NONE;
    }
    map_frame(sm);
}

void pc_touch_menu_close(void) {
    /* B a few times: the hand gives back what it holds, a list closes, then the menu */
    s_close_until = pc_frame_counter + 40;
}

void pc_touch_menu_read(u16* buttons, s8* stick_x, s8* stick_y) {
    (void)stick_x;
    (void)stick_y;
    if (s_press != 0 && (s32)(pc_frame_counter - s_press_from) >= 0 &&
        (s32)(pc_frame_counter - s_press_from) < PRESS_FRAMES) {
        *buttons |= s_press;
    }
    /* the close icon: B on every other few frames (a fresh press each time) */
    if ((s32)(s_close_until - pc_frame_counter) > 0 && (s_close_until - pc_frame_counter) % 8 < 3) {
        GAME_PLAY* play = pc_touch_play();

        if (submenu_of(play) != NULL) {
            *buttons |= PAD_BUTTON_B;
        } else {
            s_close_until = 0;
        }
    }
}
