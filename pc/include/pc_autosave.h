/* pc_autosave.h - the port saves by itself
 *
 * What the gyroid's "save and continue" writes, without asking: between
 * scenes (afe-decomp m_play.c play_cleanup, when every actor has put its
 * data back), every minute (settings.ini autosave_every) while the player is outdoors and idle (in a
 * copy of the town with what live actors keep out of it put back:
 * mCD_PC_AutoSaveSnapshot), and when the game is quit from the menu or its
 * window is closed. The town file on the card is written in place (the
 * main copy, then the backup), so turning the game off never loses more
 * than a minute or so. A new town, which has no file until its first save,
 * gets that save the gyroid's way first (mCD_PC_FirstSave), wherever the
 * gyroid would save (on day one, once the player has met a villager). A
 * player's first save (a new town's, or a new resident's) is made at the
 * first quiet moment after that meeting, not on the timer
 * (mCD_PC_FirstSaveDue). A player visiting another town by train (the
 * multiplayer's trip) has their pockets, bells, letters... saved into their
 * own town's file, as the trip home would (mCD_PC_PatchTraveller).
 *
 * With it, Mr. Resetti stays away by default (settings.ini resetti = 0;
 * afe-decomp m_card.c mCD_SetResetInfo asks pc_resetti_enabled). */
#ifndef PC_AUTOSAVE_H
#define PC_AUTOSAVE_H

#ifdef __cplusplus
extern "C" {
#endif

struct game_s;

/* Every frame after game_main (pc_pause_menu_draw) */
void pc_autosave_frame(struct game_s* game);
/* Between scenes (afe-decomp m_play.c) */
void pc_autosave_scene_end(void);
/* Before the game quits */
void pc_autosave_quit(void);
/* A save by hand (the touch pad's SAVE, Ctrl+S): done in the next frame, or
 * a notice says why it cannot be (also with the autosave off) */
void pc_autosave_request_save(void);
/* An outdoor unit written (pc_mp_fg_written): the game busy with a unit */
void pc_autosave_unit_written(unsigned short old_item, unsigned short new_item);
/* afe-decomp m_card.c */
int pc_resetti_enabled(void);

/* Settings > Gameplay > Town data (pc_gui_menu.cpp). The town file on card
 * chan (0 = A, 1 = B): 1 with the town's name and its players' names
 * ("A, B") in UTF-8, 0 if the card has none */
int pc_autosave_town_info(int chan, char* town, int town_size, char* players, int players_size);
/* Deletes the town file on card chan, so that the next start is a new game,
 * and stops the autosave until then (the caller restarts the game). 1 if
 * the file was deleted */
int pc_autosave_delete_town(int chan);

#ifdef __cplusplus
}
#endif

#endif /* PC_AUTOSAVE_H */
