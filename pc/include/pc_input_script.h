/* pc_input_script.h - deterministic, frame-indexed input for automated testing
 *
 * A script is a text file of lines:
 *
 *     <frame> <inputs> [<duration>]   hold inputs for duration frames (default 6)
 *     <frame> SHOT <name>             save the framebuffer as shots/<name>.ppm
 *     <frame> QUIT                    exit the game
 *     <frame> POS                     print where the player and the villagers are
 *     <frame> SEEK <id>|<x>,<z> [n]   walk for up to n frames (default 600) to the
 *                                     villager with that hex id, or to a point
 *     <frame> GIVE <item>             debug: put an item (hex id) in the first free pocket
 *     <frame> LEDIT <type> [<id>]     debug: open the name editor of that mLE_TYPE (2 is a
 *                                     catchphrase, of the villager with that hex id)
 *     <frame> RESETS <n>              debug: the player quit n times without saving, and
 *                                     the town just loaded is one (Resetti comes)
 *     <frame> CATCH <id>              print a villager's catchphrase bytes
 *     <frame> ITEM                    print the item a menu chose (a song requested of K.K.)
 *     <frame> BGM <id>                debug: start a piece of music (hex, audio_defs.h)
 *     <frame> TYPE <keys>             type into the text editor (pc_input_script.c)
 *     <frame> EDIT [<utf8>]           system input method: text being composed
 *     <frame> TEXT <utf8>             system input method: committed text
 *     <frame> TOUCH x,y[:x2,y2] [n]   a finger on the screen (percent of the window),
 *                                     moved to x2,y2 the next frame, lifted after n frames
 *     <frame> KEY <name>              a key press event (SDL key name, '_' for a space,
 *                                     e.g. Escape, Return, Down, Left_Shift): menus
 *     <frame> PAD <name>              a gamepad button or trigger event (SDL names: a,
 *                                     dpdown, leftshoulder, lefttrigger ...)
 *
 * <frame> counts VI retraces (60 per second) from boot. <inputs> joins any of
 * A B X Y START Z L R UP DOWN LEFT RIGHT (D-pad) and SU SD SL SR (main stick)
 * with '+', e.g. "SL+A". Text after '#' is a comment. */
#ifndef PC_INPUT_SCRIPT_H
#define PC_INPUT_SCRIPT_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Loads a script; returns 0 if the file cannot be read or parsed. */
int pc_input_script_load(const char* path);

/* Adds the scripted buttons and stick for the current frame. */
void pc_input_script_apply(u16* buttons, s8* stick_x, s8* stick_y);

/* Draws only the frames that a SHOT command captures (g_pc_skip_draw). */
void pc_input_script_set_fast(int fast);

/* Runs SHOT/QUIT commands due this frame; call before presenting it.
 * Returns 0 when the script asked to quit. */
int pc_input_script_frame(u32 frame);

#ifdef __cplusplus
}
#endif

#endif
