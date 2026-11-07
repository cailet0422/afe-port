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
static s8 s_stick_x, s_stick_y; /* a push of the stick (the notice board's pages), until s_stick_until */
static u32 s_stick_until;
static s8 s_cstick_x, s_cstick_y; /* a push of the C-stick (the catalog's and the music's pages) */
static u32 s_cstick_until;
static u32 s_close_until;
static int s_close_menu; /* the menu the close icon closes (another one come back up stops it) */
static int s_map_tap;
static float s_map_ux, s_map_uy;

static void press(u16 buttons, u32 delay) {
    s_press = buttons;
    s_press_from = pc_frame_counter + delay;
}

static void push_stick(s8 x, s8 y) {
    s_stick_x = x;
    s_stick_y = y;
    s_stick_until = pc_frame_counter + 6;
}

static void push_cstick(s8 x, s8 y) {
    s_cstick_x = x;
    s_cstick_y = y;
    s_cstick_until = pc_frame_counter + 3;
}

static Submenu* submenu_of(GAME_PLAY* play) {
    if (play == NULL || play->submenu.process_status != mSM_PROCESS_PLAY || play->submenu.overlay == NULL) {
        return NULL;
    }
    return &play->submenu;
}

/* The menus of tables like the pockets' (m_tag_ovl.c moves their cursor):
 * the pockets, the mailbox, the catalog, the designs, the music, the
 * gyroid's, the memory card's letters and designs */
static int table_menu(int type) {
    switch (type) {
        case mSM_OVL_INVENTORY:
        case mSM_OVL_MAILBOX:
        case mSM_OVL_CATALOG:
        case mSM_OVL_NEEDLEWORK:
        case mSM_OVL_MUSIC:
        case mSM_OVL_HANIWA:
        case mSM_OVL_CPORIGINAL:
        case mSM_OVL_CPMAIL:
            return TRUE;
        default:
            return FALSE;
    }
}

/* The pockets (or one of the menus like it) taking input */
static mSM_MenuInfo_c* inventory_ready(Submenu* sm) {
    mSM_MenuInfo_c* info;
    int type;

    if (sm == NULL || !table_menu(sm->menu_type)) {
        return NULL;
    }
    type = sm->menu_type;
    info = &sm->overlay->menu_info[type];
    if (info->proc_status != mSM_OVL_PROC_PLAY || sm->overlay->tag_ovl == NULL || sm->overlay->hand_ovl == NULL ||
        (type == mSM_OVL_INVENTORY && sm->overlay->inventory_ovl == NULL)) {
        return NULL;
    }
    return info;
}

/* The pockets in their own menu (not the mailbox's) */
static int is_pockets(const mSM_MenuInfo_c* info) {
    return info->menu_type == mSM_OVL_INVENTORY;
}

/* Which slot a point of the 640x480 picture is on */
static int cell_at(const mSM_MenuInfo_c* info, float ux, float uy, Cell* c) {
    float x = ux - 2.0f * info->position[0];
    float y = uy + 2.0f * info->position[1];
    int col, row;
    /* in the menus' own 320x240 place, the centre the origin and y up (their models') */
    float X = x * 0.5f - 160.0f, Y = 120.0f - y * 0.5f;

    switch (info->menu_type) {
        case mSM_OVL_CATALOG:
            /* the 7 names, the 9 tabs (m_catalog_ovl.c, clg_win1_v / clg_tagu_v) */
            if (X >= -66.0f && X < 74.0f) {
                row = (int)floorf((69.0f - Y) / 18.0f);
                if (row >= 0 && row < 7) {
                    *c = (Cell){ mTG_TABLE_CATALOG, 0, row };
                    return TRUE;
                }
            } else if (X >= 76.0f && X < 114.0f) {
                row = (int)floorf((71.0f - Y) / 16.0f);
                if (row >= 0 && row < 9) {
                    *c = (Cell){ mTG_TABLE_CATALOG_WC, 0, row };
                    return TRUE;
                }
            }
            return FALSE;
        case mSM_OVL_NEEDLEWORK: {
            /* the 8 designs, 2 x 4 (inv_original_v; the frame moved right) */
            float ox = info->data0 == 1 ? 46.0f : 10.0f;

            col = (int)floorf((X - ox + 38.0f) / 38.0f);
            row = (int)floorf((69.0f - Y) / 34.0f);
            if (col >= 0 && col < 2 && row >= 0 && row < 4) {
                *c = (Cell){ mTG_TABLE_NEEDLEWORK, col, row };
                return TRUE;
            }
            return FALSE;
        }
        case mSM_OVL_MUSIC: {
            /* the 7 lines: the title or the first icon, then the second
             * (m_music_ovl.c: titles from (79, 56), icons by (209, 64) and (235, 64)) */
            float sx = x * 0.5f, sy = y * 0.5f;

            row = (int)floorf((sy - 54.0f) / 20.0f);
            if (row >= 0 && row < 7 && sx >= 76.0f && sx < 248.0f) {
                *c = (Cell){ mTG_TABLE_MUSIC_MAIN, sx < 222.0f ? 0 : 1, row };
                return TRUE;
            }
            return FALSE;
        }
        case mSM_OVL_HANIWA:
            /* the gyroid's 4 (from (250, 148), 48 apart), then the pockets */
            col = (int)floorf((x - 226.0f) / 48.0f);
            if (fabsf(y - 148.0f) < 24.0f && col >= 0 && col < 4) {
                *c = (Cell){ mTG_TABLE_HANIWA, col, 0 };
                return TRUE;
            }
            col = (int)floorf((x - 142.0f) / 48.0f);
            row = (int)floorf((y - 236.0f) / 48.0f);
            if (col >= 0 && col < 5 && row >= 0 && row < 3) {
                *c = (Cell){ mTG_TABLE_ITEM, col, row };
                return TRUE;
            }
            return FALSE;
        case mSM_OVL_CPORIGINAL:
            /* the folder's title, its 12 (3 x 4), the 8 folders, the player's 8 (2 x 4) */
            if (X >= -61.0f && X < 131.0f && Y >= 59.0f && Y < 87.0f) {
                *c = (Cell){ mTG_TABLE_CPORIGINAL_TI, 0, 0 };
                return TRUE;
            }
            if (X >= -16.0f && X < 80.0f) {
                col = (int)floorf((X + 16.0f) / 32.0f);
                row = (int)floorf((58.0f - Y) / 29.0f);
                if (col >= 0 && col < 3 && row >= 0 && row < 4) {
                    *c = (Cell){ mTG_TABLE_CPORIGINAL, col, row };
                    return TRUE;
                }
            }
            if (X >= 86.0f && X < 124.0f) {
                row = (int)lrintf((52.0f - Y) / 14.5f);
                if (row >= 0 && row < 8) {
                    *c = (Cell){ mTG_TABLE_CPORIGINAL_WC, 0, row };
                    return TRUE;
                }
            }
            col = (int)floorf((X + 104.0f) / 32.0f);
            row = (int)floorf((59.0f - Y) / 29.0f);
            if (col >= 0 && col < 2 && row >= 0 && row < 4) {
                *c = (Cell){ mTG_TABLE_CPORIGINAL_NW, col, row };
                return TRUE;
            }
            return FALSE;
        case mSM_OVL_CPMAIL:
            /* the card's 20 letters (4 x 5 from (288, 164)), the 8 folders (516, 140 + 30 each), the
             * folder's title (368, 98), the player's 10 letters (2 x 5 from (120, 164)) */
            if (fabsf(x - 368.0f) < 70.0f && fabsf(y - 98.0f) < 16.0f) {
                *c = (Cell){ mTG_TABLE_CPMAIL_TI, 0, 0 };
                return TRUE;
            }
            if (fabsf(x - 516.0f) < 24.0f) {
                row = (int)lrintf((y - 140.0f) / 30.0f);
                if (row >= 0 && row < 8) {
                    *c = (Cell){ mTG_TABLE_CPMAIL_WC, 0, row };
                    return TRUE;
                }
            }
            col = (int)floorf((x - 264.0f) / 48.0f);
            row = (int)floorf((y - 140.0f) / 48.0f);
            if (col >= 0 && col < 4 && row >= 0 && row < 5) {
                *c = (Cell){ mTG_TABLE_CPMAIL, col, row };
                return TRUE;
            }
            col = (int)floorf((x - 96.0f) / 48.0f);
            if (col >= 0 && col < 2 && row >= 0 && row < 5) {
                *c = (Cell){ mTG_TABLE_MAIL, col, row };
                return TRUE;
            }
            return FALSE;
        default:
            break;
    }
    if (info->menu_type == mSM_OVL_MAILBOX) {
        /* the mailbox's letters: 2 x 5 from (248, 162) (mTG_mbox_col_pos, mTG_mbox_line_pos) */
        col = (int)floorf((x - 224.0f) / 48.0f);
        row = (int)floorf((y - 138.0f) / 48.0f);
        if (col >= 0 && col < 2 && row >= 0 && row < 5) {
            *c = (Cell){ mTG_TABLE_MBOX, col, row };
            return TRUE;
        }
        /* the player's letters, as in the pockets */
        col = (int)floorf((x - 406.0f) / 48.0f);
        row = (int)floorf((y - 140.0f) / 48.0f);
        if (col >= 0 && col < 2 && row >= 0 && row < 5) {
            *c = (Cell){ mTG_TABLE_MAIL, col, row };
            return TRUE;
        }
        return FALSE;
    }
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

/* The cursor's tag, NULL while a command list is open (the memory card's
 * letters keep theirs a level up, over the pockets': m_cpmail_ovl.c) */
static mTG_tag_c* cur_tag(Submenu* sm) {
    mTG_Ovl_c* tag_ovl = sm->overlay->tag_ovl;
    mTG_tag_c* tag;

    if (tag_ovl->sel_tag_idx < 0 || tag_ovl->sel_tag_idx >= mTG_TAG_NUM) {
        return NULL;
    }
    tag = &tag_ovl->tags[tag_ovl->sel_tag_idx];
    return tag->type == mTG_TYPE_NONE ? tag : NULL;
}

static int cursor_is(Submenu* sm, const Cell* c) {
    const mTG_tag_c* tag = cur_tag(sm);

    return tag != NULL && tag->table == c->table && tag->tag_col == c->col && tag->tag_row == c->row;
}

/* The hand at rest over the cursor, no list open: the menu takes a step */
static int hand_idle(Submenu* sm) {
    mHD_Ovl_c* hand = sm->overlay->hand_ovl;
    mIV_Ovl_c* inv = sm->overlay->inventory_ovl;

    return hand->info.move_flag == mHD_MOVE_NONE && hand->info.wait_timer == 0 &&
           (hand->info.act == mHD_ACTION_CLOSE_KEEP || hand->info.act == mHD_ACTION_SASU) && cur_tag(sm) != NULL &&
           (inv == NULL || (inv->page_move_timer == 0 && inv->remove_timer == 0)) &&
           sm->overlay->segment.player_anime_timer <= 0;
}

static int hand_holds(Submenu* sm) {
    mHD_Ovl_c* hand = sm->overlay->hand_ovl;

    return hand->info.item != EMPTY_NO || !mMl_check_not_used_mail(&hand->info.mail);
}

/* The item in a slot (to start a drag from) */
static int cell_has_item(Submenu* sm, const Cell* c) {
    if (c->table == mTG_TABLE_HANIWA) {
        int house = sm->overlay->menu_info[mSM_OVL_HANIWA].data1;

        return Save_Get(homes[house]).haniwa.items[c->col].item != EMPTY_NO;
    }
    if (c->table == mTG_TABLE_CPORIGINAL || c->table == mTG_TABLE_CPORIGINAL_NW || c->table == mTG_TABLE_CPMAIL ||
        c->table == mTG_TABLE_NEEDLEWORK) {
        return TRUE; /* (an empty one: the hand takes nothing) */
    }
    if (c->table == mTG_TABLE_ITEM) {
        return Now_Private->inventory.pockets[c->col + c->row * 5] != EMPTY_NO;
    }
    if (c->table == mTG_TABLE_MAIL) {
        return !mMl_check_not_used_mail(&Now_Private->mail[c->col + c->row * 2]);
    }
    if (c->table == mTG_TABLE_MBOX) {
        return TRUE; /* (an empty one: the hand takes nothing and the drag ends) */
    }
    return FALSE;
}

/* A drag takes the item (L): the pockets, the mailbox, the gyroid's (to
 * leave things with it), the memory card's, the designs in the pockets */
static int drag_takes(Submenu* sm, const mSM_MenuInfo_c* info) {
    switch (info->menu_type) {
        case mSM_OVL_CATALOG:
        case mSM_OVL_MUSIC:
            return FALSE;
        case mSM_OVL_NEEDLEWORK:
            return info->data0 == 1;
        case mSM_OVL_HANIWA:
            return info->data0 == mSM_HANIWA_OPEN_ENTRUST;
        default:
            (void)sm;
            return TRUE;
    }
}

static void set_phase(int phase, const Cell* c) {
    s_inv.phase = phase;
    if (c != NULL) {
        s_inv.target = *c;
    }
    s_inv.since = pc_frame_counter;
    s_inv.pressed = 0;
}

/* The notice board (m_notice_ovl.c mNT_Play_page_read): the stick turns its
 * pages (left, right; up to the newest, down to the first), A writes, B or
 * START closes. A swipe on it turns them, a tap on its left or right half too;
 * its own buttons below (640x480: write by (467, 352), quit by (452, 395), the
 * newest and the first by (105, 408) and (280, 408)) are pressed by a tap. */
static void notice_tap(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;

    if (fabsf(dx) > 40.0f && fabsf(dx) > fabsf(dy)) {
        push_stick(dx < 0.0f ? 60 : -60, 0); /* a swipe to the left brings the next page */
    } else if (fabsf(dy) > 40.0f) {
        push_stick(0, dy < 0.0f ? 60 : -60); /* up: the newest; down: the first */
    } else if (x1 > 420.0f && x1 < 530.0f && y1 > 325.0f && y1 < 377.0f) {
        press(PAD_BUTTON_A, 0); /* write */
    } else if (x1 > 410.0f && x1 < 530.0f && y1 >= 377.0f && y1 < 430.0f) {
        press(PAD_BUTTON_B, 0); /* quit */
    } else if (x1 > 50.0f && x1 < 170.0f && y1 > 385.0f && y1 < 430.0f) {
        push_stick(0, 60);
    } else if (x1 > 220.0f && x1 < 330.0f && y1 > 385.0f && y1 < 430.0f) {
        push_stick(0, -60);
    } else if (y1 < 385.0f) {
        push_stick(x1 < 320.0f ? -60 : 60, 0);
    }
    printf("[TOUCH] notice board: %.0f,%.0f -> %.0f,%.0f\n", x0, y0, x1, y1);
}

/* The warning's question (m_warning_ovl.c mWR_move_Play; "the letters are
 * full, throw away ...?"): the stick to the left or right moves between its
 * two answers, A takes one, B is no. A tap on the window's left half answers
 * yes, on its right half no; one with only a message takes a tap anywhere. */
static void warning_tap(Submenu* sm, float ux, float uy) {
    mSM_MenuInfo_c* info = &sm->overlay->menu_info[mSM_OVL_WARNING];

    if (info->data0 != 0) {
        press(PAD_BUTTON_A, 0);
    } else if (ux < 80.0f || ux > 560.0f || uy < 110.0f || uy > 390.0f) {
        return; /* (beside the window) */
    } else if (ux < 320.0f) {
        push_stick(-60, 0); /* yes: to the left, then A */
        press(PAD_BUTTON_A, 10);
    } else {
        press(PAD_BUTTON_B, 0);
    }
    printf("[TOUCH] warning: %.0f,%.0f\n", ux, uy);
}

/* A letter being read (a tap closes it) */
static int board_reading(Submenu* sm) {
    int mode;

    if (sm == NULL || sm->menu_type != mSM_OVL_BOARD) {
        return FALSE;
    }
    mode = sm->overlay->menu_info[mSM_OVL_BOARD].data0;
    return mode == mSM_BD_OPEN_READ || mode == mSM_BD_OPEN_READ_ISLAND;
}

/* One of the menus of pc_touch_menus.c */
static int other_menu(Submenu* sm) {
    return sm != NULL && !board_reading(sm) && pc_touch_menus_owns(sm);
}

void pc_touch_menu_finger(int kind, s64 id, float x, float y) {
    GAME_PLAY* play = pc_touch_play();
    Submenu* sm = submenu_of(play);
    float ux, uy;

    pc_gx_window_to_ui(x, y, &ux, &uy);
    if (other_menu(sm)) {
        pc_touch_menus_finger(sm, kind, id, ux, uy);
        return;
    }
    if (sm != NULL &&
        (sm->menu_type == mSM_OVL_NOTICE || sm->menu_type == mSM_OVL_BOARD || sm->menu_type == mSM_OVL_WARNING)) {
        if (kind == 0) {
            s_inv.finger = 1;
            s_inv.id = id;
            s_inv.ux0 = ux;
            s_inv.uy0 = uy;
        } else if (kind == 2 && s_inv.finger && id == s_inv.id) {
            s_inv.finger = 0;
            if (sm->menu_type == mSM_OVL_BOARD) {
                press(PAD_BUTTON_A, 0); /* a letter read: a tap closes it */
            } else if (sm->menu_type == mSM_OVL_WARNING) {
                warning_tap(sm, ux, uy);
            } else {
                notice_tap(s_inv.ux0, s_inv.uy0, ux, uy);
            }
        }
        return;
    }
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
            if (info != NULL) {
                printf("[TOUCH] pockets: drag from %.0f,%.0f\n", s_inv.ux0, s_inv.uy0);
            }
            if (info != NULL &&
                (!is_pockets(info) || info->data0 == mSM_IV_OPEN_NORMAL || info->data0 == mSM_IV_OPEN_EXCHANGE) &&
                drag_takes(sm, info) && s_inv.phase == PH_NONE && cell_at(info, s_inv.ux0, s_inv.uy0, &c) &&
                cell_has_item(sm, &c) && !hand_holds(sm) && cur_tag(sm) != NULL) {
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
        {
            mTG_Ovl_c* tag = sm->overlay->tag_ovl;
            float dx = ux - s_inv.ux0, dy = uy - s_inv.uy0;

            if (tag->sel_tag_idx >= 1 && tag->tags[tag->sel_tag_idx].type == mTG_TYPE_HANIWA_PRICE) {
                /* the gyroid's price: a swipe up or down changes the digit,
                 * left or right moves to another, a tap settles it (A) */
                if (!s_inv.dragging) {
                    press(PAD_BUTTON_A, 0);
                } else if (fabsf(dy) > fabsf(dx)) {
                    push_stick(0, dy < 0.0f ? 60 : -60);
                } else {
                    push_stick(dx < 0.0f ? -60 : 60, 0);
                }
                return;
            }
            if (s_inv.dragging) {
                /* the catalog's and the music's lists: a swipe up shows the next page */
                if ((info->menu_type == mSM_OVL_CATALOG || info->menu_type == mSM_OVL_MUSIC) && cur_tag(sm) != NULL &&
                    (cur_tag(sm)->table == mTG_TABLE_CATALOG || cur_tag(sm)->table == mTG_TABLE_MUSIC_MAIN) &&
                    fabsf(dy) > fabsf(dx)) {
                    push_cstick(0, dy < 0.0f ? -60 : 60);
                    printf("[TOUCH] list: page %s\n", dy < 0.0f ? "down" : "up");
                }
                return;
            }
        }
        /* a tap: the lists' scroll arrows (a page) */
        if (info->menu_type == mSM_OVL_CATALOG || info->menu_type == mSM_OVL_MUSIC) {
            float sx = ux * 0.5f - 2.0f * info->position[0] * 0.5f, sy = uy * 0.5f + info->position[1];
            float ax = info->menu_type == mSM_OVL_CATALOG ? 152.0f : 132.0f;
            float up = info->menu_type == mSM_OVL_CATALOG ? 41.0f : 46.0f;
            float down = info->menu_type == mSM_OVL_CATALOG ? 187.0f : 202.0f;

            if (fabsf(sx - ax) < 14.0f && fabsf(sy - up) < 12.0f) {
                push_cstick(0, 60);
                return;
            }
            if (fabsf(sx - ax) < 14.0f && fabsf(sy - down) < 12.0f) {
                push_cstick(0, -60);
                return;
            }
        }
        if (cur_tag(sm) == NULL) {
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
            printf("[TOUCH] pockets: beside the list (%.0f,%.0f, list %d): back\n", ux * 0.5f, uy * 0.5f,
                   sm->overlay->tag_ovl->sel_tag_idx);
            press(PAD_BUTTON_B, 0); /* beside the list: back */
            return;
        }
        if (cell_at(info, ux, uy, &c)) {
            if (is_pockets(info) && info->data0 != mSM_IV_OPEN_NORMAL && info->data0 != mSM_IV_OPEN_EXCHANGE &&
                c.table != sm->overlay->tag_ovl->tags[0].table) {
                return; /* (selling, giving ...: the cursor stays in its table) */
            }
            set_phase(PH_MOVE_THEN_A, &c);
            return;
        }
        {
            int tab = tab_at(info, ux, uy);

            if (tab >= 0 && is_pockets(info) && info->data0 == mSM_IV_OPEN_NORMAL &&
                sm->overlay->inventory_ovl->page_order[0] != tab) {
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
            if (s_inv.pressed) {
                /* (A given: whatever it opened, a list is no cursor to follow) */
                mTG_PC_touch_cursor(-1, 0, 0);
                if (pc_frame_counter - s_press_from > PRESS_FRAMES) {
                    s_inv.phase = PH_NONE;
                }
            } else if (!cursor_is(sm, &s_inv.target)) {
                mTG_PC_touch_cursor(s_inv.target.table, s_inv.target.col, s_inv.target.row);
            } else if (hand_idle(sm)) {
                mTG_PC_touch_cursor(-1, 0, 0);
                press(PAD_BUTTON_A, 0);
                s_inv.pressed = 1;
                printf("[TOUCH] pockets: A at table %d %d,%d\n", s_inv.target.table, s_inv.target.col,
                       s_inv.target.row);
            }
            break;
        case PH_GRAB:
            if (!cursor_is(sm, &s_inv.target)) {
                mTG_PC_touch_cursor(s_inv.target.table, s_inv.target.col, s_inv.target.row);
            } else if (hand_idle(sm) && !s_inv.pressed) {
                mTG_PC_touch_cursor(-1, 0, 0);
                press(PAD_TRIGGER_L, 0);
                s_inv.pressed = 1;
                printf("[TOUCH] pockets: L at table %d %d,%d\n", sm->overlay->tag_ovl->tags[0].table,
                       sm->overlay->tag_ovl->tags[0].tag_col, sm->overlay->tag_ovl->tags[0].tag_row);
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

    pc_touch_menus_frame(other_menu(sm) ? sm : NULL);
    if (sm == NULL) {
        s_inv.phase = PH_NONE;
        s_inv.finger = 0;
        s_map_tap = 0;
        s_close_until = 0;
        mTG_PC_touch_cursor(-1, 0, 0);
        mTG_PC_touch_select_row(-1);
        return;
    }
    if (sm->overlay->tag_ovl == NULL || (table_menu(sm->menu_type) && cur_tag(sm) != NULL) ||
        sm->overlay->tag_ovl->sel_tag_idx < 1) {
        mTG_PC_touch_select_row(-1); /* no list open: a line tapped as it closed is forgotten */
    }
    if (table_menu(sm->menu_type)) {
        inventory_frame(sm);
    } else {
        mTG_PC_touch_cursor(-1, 0, 0);
        s_inv.phase = PH_NONE;
    }
    map_frame(sm);
}

void pc_touch_menu_close(void) {
    Submenu* sm = submenu_of(pc_touch_play());

    if (other_menu(sm)) {
        pc_touch_menus_icon(PC_HUD_CLOSE, 0); /* (its own way out, once) */
        return;
    }
    if (sm != NULL && (sm->menu_type == mSM_OVL_CPMAIL || sm->menu_type == mSM_OVL_CPORIGINAL)) {
        press(PAD_BUTTON_B, 0); /* (once: B asks "finished?", and B there goes back to the menu) */
        return;
    }
    /* B a few times: the hand gives back what it holds, a list closes, then the menu */
    s_close_until = pc_frame_counter + 40;
    s_close_menu = sm != NULL ? sm->menu_type : -1;
}

void pc_touch_menu_read(u16* buttons, s8* stick_x, s8* stick_y, s8* cstick_x, s8* cstick_y) {
    pc_touch_menus_read(buttons, stick_x, stick_y, cstick_x, cstick_y);
    if ((s32)(s_cstick_until - pc_frame_counter) > 0) {
        *cstick_x = s_cstick_x;
        *cstick_y = s_cstick_y;
    }
    if ((s32)(s_stick_until - pc_frame_counter) > 0) {
        *stick_x = s_stick_x;
        *stick_y = s_stick_y;
    }
    if (s_press != 0 && (s32)(pc_frame_counter - s_press_from) >= 0 &&
        (s32)(pc_frame_counter - s_press_from) < PRESS_FRAMES) {
        *buttons |= s_press;
    }
    /* the close icon: B on every other few frames (a fresh press each time) */
    if ((s32)(s_close_until - pc_frame_counter) > 0 && (s_close_until - pc_frame_counter) % 8 < 3) {
        GAME_PLAY* play = pc_touch_play();

        Submenu* sm = submenu_of(play);

        if (sm != NULL && sm->menu_type == s_close_menu) {
            *buttons |= PAD_BUTTON_B;
        } else if (sm == NULL || sm->menu_type != s_close_menu) {
            s_close_until = 0; /* (closed, or back to the menu under it: that one stays) */
        }
    }
}

/* The menus this mode works by touch: the pockets (but beside the letters'
 * copy), the map, the mailbox, the notice board, a letter being read, the
 * pockets' questions */
int pc_touch_menu_owns(GAME_PLAY* play) {
    Submenu* sm;
    int type;

    if (play == NULL || play->submenu.process_status == mSM_PROCESS_WAIT || play->submenu.overlay == NULL) {
        return FALSE;
    }
    sm = &play->submenu;
    type = sm->menu_type;
    switch (type) {
        case mSM_OVL_INVENTORY:
            return sm->overlay->menu_info[mSM_OVL_INVENTORY].data0 != mSM_IV_OPEN_CPMAIL;
        case mSM_OVL_MAP:
        case mSM_OVL_MAILBOX:
        case mSM_OVL_NOTICE:
        case mSM_OVL_WARNING: /* (the pockets' and the mailbox's questions) */
        case mSM_OVL_CATALOG:
        case mSM_OVL_NEEDLEWORK:
        case mSM_OVL_MUSIC:
        case mSM_OVL_HANIWA:
        case mSM_OVL_CPORIGINAL:
        case mSM_OVL_CPMAIL:
            return TRUE;
        default:
            return board_reading(sm) || pc_touch_menus_owns(sm);
    }
}
