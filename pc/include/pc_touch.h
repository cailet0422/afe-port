/* pc_touch.h - on-screen controller for touch screens
 *
 * With settings.ini touch_controls = 1 (the default on Android) a
 * GameCube-style pad is drawn over the game: a stick that starts where the
 * left thumb lands, A/B/X/Y on the right, L/R/Z and START along the top,
 * and a small D-pad. It hides when a real controller is used and comes back
 * on the next touch.
 *
 * While the pause menu is open the pad drives it like the keyboard (D-pad =
 * arrows, A = Enter, B/START = Esc). In the text editor on a Korean patch
 * disc a touch on the editor's own keyboard presses that key first
 * (pc_ko_editor.c). Touching the screen outside the controls while the text
 * editor uses the system input method shows the system's keyboard again.
 * A tap on the minimap opens the town map, and a tap outside the controls
 * closes it again (pc_minimap.c). */
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

#ifdef __cplusplus
}
#endif

#endif /* PC_TOUCH_H */
