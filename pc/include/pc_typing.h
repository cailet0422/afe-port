/* pc_typing.h - keyboard typing for the in-game text editor
 *
 * While the editor overlay (m_editor_ovl.c) is open, keys are turned into a
 * queue of editing operations that the editor runs at the start of its next
 * frame. Two ways to type (settings.ini text_input):
 *
 *  - built-in (0): keys are read as a US QWERTY layout. On a disc with the
 *    Korean patch, Hangul is composed with the dubeolsik layout (ko_ime.h);
 *    Hangul/English with the Hangul key, right Alt or Shift+Space. Tab
 *    turns typing on and off (off, the keyboard is the controller).
 *  - system (1, the default on Android): the platform's input method (the
 *    on-screen keyboard) composes the text; the game receives SDL's editing
 *    and text events.
 *
 * Text being composed lives in the editor's buffer like any other text, so
 * every field shows it without extra drawing: operations flagged
 * PC_TYPING_PREEDIT insert it, and PC_TYPING_CMD_PREEDIT_CLEAR removes it
 * again before it is replaced or committed. */
#ifndef PC_TYPING_H
#define PC_TYPING_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_TYPING_QUEUE_SIZE 128

/* Operations. Values 0-255 insert that one-byte character code. */
#define PC_TYPING_CMD_BACKSPACE     256
#define PC_TYPING_CMD_LEFT          257
#define PC_TYPING_CMD_RIGHT         258
#define PC_TYPING_CMD_UP            259
#define PC_TYPING_CMD_DOWN          260
#define PC_TYPING_CMD_ENTER         261 /* new line, or finish a one-line field */
#define PC_TYPING_CMD_END           262 /* finish editing (Ctrl/Shift+Enter) */
#define PC_TYPING_CMD_PREEDIT_CLEAR 263 /* remove the text being composed */
/* PC_TYPING_PAIR | glyph: insert a Korean patch two-byte character. */
#define PC_TYPING_PAIR    0x10000
#define PC_TYPING_GLYPH_MASK 0xFFFF
/* With a character or pair: insert it as text being composed. */
#define PC_TYPING_PREEDIT 0x20000

extern int g_pc_typing_mode;
extern int g_pc_editor_active;

/* Called by the editor when it opens and closes. A latin field (the town
 * name in English) starts in English. */
void pc_typing_editor_open(int latin);
void pc_typing_editor_close(void);

/* Takes the next operation; returns 0 when the queue is empty. */
int pc_typing_pop(int* op);

/* The editor has been asking for operations (it is taking input, not
 * sliding in or waiting for another menu) */
int pc_typing_editor_listening(void);

/* The editor could not insert the text being composed (the field is full),
 * so the input method drops it. */
void pc_typing_preedit_rejected(void);

/* The editor acted on a controller command; what was being composed stays
 * as typed text. */
void pc_typing_commit_composition(void);

/* Non-zero while typing in Korean with the built-in input (for the
 * editor's mode display), or with the system's input method. */
int pc_typing_korean(void);
int pc_typing_system_ime(void);

/* Feeds one key as the character it gives on a US QWERTY layout, or one of
 * the PC_TYPING_CMD_* values; also used by --input-script TYPE. Returns 0 if
 * typing is off. */
int pc_typing_key(int key);
int pc_typing_toggle_korean(void);

/* The same from the on-screen keyboard (pc_ko_editor.c): always through the
 * built-in input method, whether or not keyboard typing is on */
int pc_typing_osk_key(int key);
int pc_typing_osk_toggle_korean(void);

/* The built-in input method is in Korean (for the on-screen keyboard) */
int pc_typing_ime_korean(void);

/* The system input method's text: being composed (replaces the previous
 * composition; "" ends it) or committed. UTF-8. Also used by
 * --input-script EDIT/TEXT. */
void pc_typing_system_editing(const char* text);
void pc_typing_system_text(const char* text);

void pc_typing_handle_event(const SDL_Event* event);

#ifdef __cplusplus
}
#endif

#endif /* PC_TYPING_H */
