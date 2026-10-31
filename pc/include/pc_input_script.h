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
 *     <frame> TALK [frames]           press A every 20 frames until a choice window
 *                                     opens (or frames pass, default 600)
 *     <frame> CHOOSE <n> [frames]     when a choice window takes input, pick its n-th
 *                                     choice (from 0; 9 is the last) and press A until taken
 *     <frame> WAITMENU [frames]       press A every 20 frames until a submenu opens
 *     <frame> TALKEND [frames [quiet]] press A every 20 frames until the talk is over (no
 *                                     message window for quiet frames, default 30)
 *                                     TALK, CHOOSE, WAITMENU and TALKEND hold the script time while
 *                                     they wait (up to frames of real time): the lines
 *                                     after them happen that much later
 *     <frame> GIVE <item>             debug: put an item (hex id) in the first free pocket
 *     <frame> LEDIT <type> [<id>]     debug: open the name editor of that mLE_TYPE (2 is a
 *                                     catchphrase, of the villager with that hex id)
 *     <frame> RESETS <n>              debug: the player quit n times without saving, and
 *                                     the town just loaded is one (Resetti comes)
 *     <frame> CATCH <id>              print a villager's catchphrase bytes
 *     <frame> ITEM                    print the item a menu chose (a song requested of K.K.)
 *     <frame> NOJOB                   debug: end the part-time job for Nook (day one), so
 *                                     the shops, post office and houses act as later on
 *     <frame> JOBDONE                 debug: the part-time job finished as the game ends it
 *                                     (the errand cleared, Nook waits to tell of the HRA,
 *                                     the payment message); the town acts as on day two
 *     <frame> BELLS <n>               debug: n bells in the wallet
 *     <frame> WARP <x>,<z> [<dir>]    debug: the town again with the player at x,z (as on
 *                                     leaving a building), facing mSc_DIRECT_* (4 north)
 *     <frame> CLOCK <h>[:<m>[:<s>]]   debug: the clock forward to the next h:m:s (today if
 *                                     later, else tomorrow); the game goes on into that
 *                                     time, past midnight into the next day, without a
 *                                     restart (--days starts a run on another day)
 *     <frame> FG <x>,<z> <item>       debug: an item (hex id) on the ground in the unit at x,z,
 *                                     as the game sets one (008D Resetti's hole: walking in
 *                                     falls into the Reset Center)
 *     <frame> WEATHER <type> [<n>]    debug: today's weather (1 rain, 2 snow ...) and its
 *                                     intensity (0-3), shown from the next scene (WARP)
 *     <frame> MENU <name> [<a0> [<a1>]] debug: open a submenu by its mSM_OVL_ name without the
 *                                     prefix (ALARMIN, TIMEIN, HBOARD, PASSWORDMAKE, NOTICE,
 *                                     WARNING, FILM ...) or number, with its two arguments
 *     <frame> VOICELOG [0|1]          print the talking voice chosen for each character
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
