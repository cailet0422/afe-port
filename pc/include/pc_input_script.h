/* pc_input_script.h - deterministic, frame-indexed input for automated testing
 *
 * A script is a text file of lines:
 *
 *     <frame> <inputs> [<duration>]   hold inputs for duration frames (default 6)
 *     <frame> SHOT <name>             save the framebuffer as shots/<name>.ppm
 *     <frame> QUIT                    exit the game
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
