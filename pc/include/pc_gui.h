/* pc_gui.h - the port's own menus, drawn with Dear ImGui (pc/lib/imgui)
 *
 * The pause menu, the settings, the HD texture pack and the multiplayer pages
 * are windows of their own over the game's frame, not text in the game's
 * font driven by the game's buttons: on PC they take the mouse (hover, click,
 * wheel) as well as the keyboard (arrows, Enter, Esc) and a gamepad (D-pad,
 * A, B), on Android fingers (tap, drag to scroll) with larger controls.
 * Labels are Korean or English (Settings > Interface > Language; automatic:
 * Korean with the Korean patch or a Korean system language), drawn with
 * pc/res/gui_font.ttf (a subset of NanumGothic, SIL OFL).
 *
 * pc_pause_menu.c still owns g_pc_paused (the game's pause); this draws the
 * pages and calls back into it. pc_vi.c draws a frame of this after the
 * game's frame, the minimap and the touch pad; pc_main.c hands it the events
 * first. */
#ifndef PC_GUI_H
#define PC_GUI_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An SDL event for the menus; 1 if they took it (the game must not see it) */
int pc_gui_handle_event(const SDL_Event* e);

/* A finger (pc_touch.c, --input-script TOUCH), window coordinates 0-1:
 * kind 0 down, 1 move, 2 up. 1 if the menus took it. */
int pc_gui_touch(int kind, s64 id, float x, float y);

/* Draws the menus and overlays over the finished frame (pc_vi.c) */
void pc_gui_frame(void);

/* The pause menu opened or closed (pc_pause_menu.c) */
void pc_gui_menu_opened(void);
void pc_gui_menu_closed(void);
/* Opens the update page over the pause menu (pc_update.c found a version) */
void pc_gui_menu_open_update(void);

/* 1 while the menus want the keyboard or a held key would go to them */
int pc_gui_wants_keyboard(void);

/* Small messages at the top of the screen while playing ("Mina joined"),
 * UTF-8; shown for a few seconds */
void pc_gui_notify(const char* text);

/* 1 if the UI is in Korean */
int pc_gui_korean(void);

/* The multiplayer chat box while playing (in a room): opened with the Chat
 * control (T by default; a gamepad button bound to it calls
 * pc_gui_chat_request) or, on a touch screen, the chat button. While it is
 * open the game gets no input (pc_pad.c) and keeps running. */
int pc_gui_chat_active(void);
void pc_gui_chat_request(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_GUI_H */
