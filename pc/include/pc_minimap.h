/* pc_minimap.h - a small map of the town in the bottom right corner, and the
 * game's own map on a key or a tap
 *
 * Outside in the town (not in buildings, on the island or the train, not
 * while talking or in a menu) the town's acres are drawn in the bottom right
 * corner with the pictures of the game's map, with the player (pointing the
 * way they face) and the villagers' houses on it. settings.ini minimap = 0
 * (pause menu > Settings > Gameplay > Minimap) turns it off.
 *
 * The Map control (keybindings.ini Map, M by default; Settings > Controls)
 * opens the game's map at any time the game would let the X button open it,
 * also before the player has the map from the police station, and closes it
 * again. A tap or click on the minimap opens it too; while it is open, a tap
 * outside the on-screen pad's controls (or a click) closes it. */
#ifndef PC_MINIMAP_H
#define PC_MINIMAP_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* Every frame from graph_main, after game_main (through pc_pause_menu_draw):
 * reads what the minimap shows and opens the map if asked */
void pc_minimap_game_frame(struct game_s* game);

/* Draws the minimap over the finished frame (pc_vi.c, before the touch pad) */
void pc_minimap_draw(void);

/* PADRead: whether the Map control is held; adds B on the frame that closes
 * the map */
void pc_minimap_pad(int map_held, u16* buttons);

/* Open the map, or close it if it is open (input script MAP) */
void pc_minimap_request_toggle(void);

/* A tap or click at window coordinates from 0 to 1. Returns 1 if it opened
 * or closed the map (nothing else should take it) */
int pc_minimap_touch(float x, float y);

/* The game's map is open and takes input */
int pc_minimap_map_open(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_MINIMAP_H */
