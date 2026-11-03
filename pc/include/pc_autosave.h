/* pc_autosave.h - the port saves by itself
 *
 * What the gyroid's "save and continue" writes, without asking: between
 * scenes (afe-decomp m_play.c play_cleanup, when every actor has put its
 * data back), every few minutes while the player is outdoors and idle (in a
 * copy of the town with what live actors keep out of it put back:
 * mCD_PC_AutoSaveSnapshot), and when the game is quit from the menu or its
 * window is closed. The town file on the card is written in place (the
 * main copy, then the backup), so turning the game off never loses more
 * than a few minutes. A player visiting another town by train (the
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
/* An outdoor unit written (pc_mp_fg_written): the game busy with a unit */
void pc_autosave_unit_written(unsigned short old_item, unsigned short new_item);
/* afe-decomp m_card.c */
int pc_resetti_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_AUTOSAVE_H */
