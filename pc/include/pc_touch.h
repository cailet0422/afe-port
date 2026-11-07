/* pc_touch.h - on-screen controller for touch screens
 *
 * With settings.ini touch_controls = 1 (the default on Android) a
 * GameCube-style pad is drawn over the game: a stick that starts where the
 * left thumb lands, A/B/X/Y on the right, L/R/Z and START along the top,
 * and a small D-pad. It hides when a real controller is used and comes back
 * on the next touch. Each piece can be moved and resized in the layout
 * editor (touch_layout). With touch_controls = 2 there is no pad: the full
 * touch mode (pc_touch_full.h) takes the fingers.
 *
 * While the pause menu is open the pad is hidden and fingers go to the
 * menus (pc_gui: tap, drag to scroll). In the text editor on a Korean patch
 * disc a touch on the editor's own keyboard presses that key first
 * (pc_ko_editor.c). Touching the screen outside the controls while the text
 * editor uses the system input method shows the system's keyboard again.
 * A quick tap on the minimap (in the stick's corner) opens the town map, and
 * a tap outside the controls closes it again (pc_minimap.c). */
#ifndef PC_TOUCH_H
#define PC_TOUCH_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

void pc_touch_handle_event(const SDL_Event* event);

/* A finger by id, in window coordinates from 0 to 1; also used by
 * --input-script TOUCH. */
void pc_touch_finger_down(s64 id, float x, float y);
void pc_touch_finger_move(s64 id, float x, float y);
void pc_touch_finger_up(s64 id);

/* Adds the pad's buttons and stick to a controller reading */
void pc_touch_read(u16* buttons, s8* stick_x, s8* stick_y);

/* Draws the pad over the finished frame */
void pc_touch_draw(void);

/* The layout editor (Settings > Gameplay > Edit pad layout, pc_gui_menu.cpp).
 * The pieces are A, B, X, Y, L, R, Z, START, SAVE, the D-pad and the stick;
 * each can be moved and made bigger or smaller (settings.ini touch_layout). */
enum {
    PC_TOUCH_PIECE_DPAD = 9,
    PC_TOUCH_PIECE_STICK = 10,
    PC_TOUCH_PIECE_COUNT = 11,
};
#define PC_TOUCH_SIZE_MIN 0.5f
#define PC_TOUCH_SIZE_MAX 2.0f

/* 1: the pad is drawn even while paused (the editor shows it under its page) */
void pc_touch_preview(int on);
const char* pc_touch_piece_label(int piece);
/* The piece's centre and half-size as fractions of the window, and its size
 * factor (1 = where and how big the game puts it) */
void pc_touch_piece_get(int piece, float* x, float* y, float* rx, float* ry, float* size);
/* Moves (centre as fractions of the window) and resizes a piece; writes
 * g_pc_settings.touch_layout (the caller saves the settings) */
void pc_touch_piece_set(int piece, float x, float y, float size);
/* Puts a piece (-1: all) back where the game puts it */
void pc_touch_layout_reset(int piece);

#ifdef __cplusplus
}
#endif

#endif /* PC_TOUCH_H */
