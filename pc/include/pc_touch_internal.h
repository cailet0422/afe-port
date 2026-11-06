/* pc_touch_internal.h - shared by pc_touch_full.c, pc_touch_menu.c and
 * pc_touch_room.c (the full touch mode, pc_touch_full.h); C with the game's
 * headers */
#ifndef PC_TOUCH_INTERNAL_H
#define PC_TOUCH_INTERNAL_H

#include "pc_touch_full.h"
#include "game.h"
#include "m_play.h"
#include "m_player.h"

/* --- pc_touch_full.c ----------------------------------------------------- */

/* The game's play state (NULL outside play_main) and its player */
GAME_PLAY* pc_touch_play(void);
PLAYER_ACTOR* pc_touch_player(GAME_PLAY* play);
/* A message window is up */
int pc_touch_msg_open(void);
/* Standing or walking, no message, no menu */
int pc_touch_player_free(GAME_PLAY* play);
/* A world point on the window (fractions; FALSE behind the camera), and the
 * ground under a window point (the last frame's camera, as on the screen) */
int pc_touch_world_to_window(GAME_PLAY* play, const xyz_t* p, float* fx, float* fy);
int pc_touch_window_to_ground(GAME_PLAY* play, float fx, float fy, xyz_t* out);
/* The choice under a window point, -1 if none */
int pc_touch_choice_at(float fx, float fy);
/* What a tool icon shows (PC_GLYPH_) */
int pc_touch_tool_glyph(mActor_name_t item, int* gold);

/* --- pc_touch_menu.c: the pockets and the map ------------------------------ */

/* A finger of the pockets or the map: kind 0 down, 1 move, 2 up; window fractions */
void pc_touch_menu_finger(int kind, s64 id, float x, float y);
/* The buttons it presses (after the game frame worked them out) */
void pc_touch_menu_read(u16* buttons, s8* stick_x, s8* stick_y);
void pc_touch_menu_frame(GAME_PLAY* play);
/* The close icon: B until the pockets or the map close */
void pc_touch_menu_close(void);

/* --- pc_touch_room.c: the furniture mode ----------------------------------- */

/* In one's own room, free to change it */
int pc_touch_room_available(GAME_PLAY* play);
int pc_touch_room_on(void);
void pc_touch_room_toggle(void);
/* Its icons after the n others; returns the new count */
int pc_touch_room_hud(PCTouchHudIcon* out, int n, int max);
void pc_touch_room_finger(int kind, s64 id, float x, float y);
/* An icon of its own tapped (index: the pocket slot of PC_HUD_FURN_SLOT) */
void pc_touch_room_icon(int icon, int index);
void pc_touch_room_frame(GAME_PLAY* play);

#endif /* PC_TOUCH_INTERNAL_H */
