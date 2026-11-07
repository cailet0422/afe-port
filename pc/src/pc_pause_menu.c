/* pc_pause_menu.c - the game's pause; the menu itself is drawn by pc_gui (pc/src/gui) */
#include "pc_platform.h"
#include "pc_pause_menu.h"
#include "pc_touch_full.h"
#include "pc_autosave.h"
#include "pc_gui.h"
#include "pc_minimap.h"
#include "pc_mp_game.h"

#include <stdio.h>

int g_pc_paused = 0;

/* Set by ac_animal_logo. Can't pause on the main menu. */
int g_pc_title_main_menu_visible = 0;

/* Don't allow pausing while NES is running. Crashes the game, also NES games has their own pause. */
int g_pc_nes_active = 0;

/* When the menu closes, the keys/buttons used to confirm Resume are usually
 * still physically held. Just drains those. */
int g_pc_pause_input_drain = 0;

extern int pc_matrix_ready(void); /* sys_matrix.c */

void pc_pause_menu_toggle(void) {
    if (!g_pc_paused && g_pc_title_main_menu_visible && g_pc_nes_active) return;
    /* too early in boot to draw the menu (no matrix stack yet) */
    if (!g_pc_paused && !pc_matrix_ready()) return;

    g_pc_paused = !g_pc_paused;
    if (g_pc_paused) {
        pc_gui_menu_opened();
    } else {
        pc_gui_menu_closed();
        g_pc_pause_input_drain = 1;
    }
}

void pc_pause_menu_open_update(void) {
    if (g_pc_paused) return;
    pc_pause_menu_toggle();
    if (!g_pc_paused) return;
    pc_gui_menu_open_update();
}

int pc_pause_menu_handle_event(const SDL_Event* e) {
    if (!g_pc_paused) return 0;
    return pc_gui_handle_event(e);
}

void pc_pause_menu_draw(struct game_s* game) {
    /* the hook after game_main, for the minimap (it draws later, in pc_vi.c);
     * the menu is drawn over the finished frame by pc_gui_frame */
    pc_minimap_game_frame(game);
    pc_mp_game_frame(game); /* the multiplayer reads and changes the game here */
    pc_autosave_frame(game);
    pc_touch_full_game_frame(game); /* the full touch mode's changes to the game */
}
