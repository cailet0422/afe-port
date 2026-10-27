/* pc_input_script.h - deterministic, frame-indexed input for automated testing
 *
 * A script is a text file of lines:
 *
 *     <frame> <inputs> [<duration>]   hold inputs for duration frames (default 6)
 *     <frame> SHOT <name>             save the framebuffer as shots/<name>.ppm
 *     <frame> QUIT                    exit the game
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

/* Runs SHOT/QUIT commands due this frame; call before presenting it.
 * Returns 0 when the script asked to quit. */
int pc_input_script_frame(u32 frame);

#ifdef __cplusplus
}
#endif

#endif
