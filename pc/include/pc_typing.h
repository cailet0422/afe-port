/* pc_typing.h - keyboard typing for the in-game text editor
 *
 * While the editor overlay (m_editor_ovl.c) is open, keys are turned into a
 * queue of editing operations that the editor runs at the start of its next
 * frame. Tab switches typing on and off; on a disc with the Korean patch it
 * starts on, and Korean is typed with the dubeolsik layout (ko_ime.h):
 * Hangul/English with the Hangul key, right Alt or Shift+Space.
 *
 * The syllable being composed lives in the editor's buffer like any other
 * character, so every text field shows it without extra drawing; the
 * PREEDIT operation replaces it and COMMIT makes it ordinary text. */
#ifndef PC_TYPING_H
#define PC_TYPING_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_TYPING_QUEUE_SIZE 64

/* Operations. Values 0-255 insert that one-byte character code. */
#define PC_TYPING_CMD_BACKSPACE 256
#define PC_TYPING_CMD_LEFT      257
#define PC_TYPING_CMD_RIGHT     258
#define PC_TYPING_CMD_UP        259
#define PC_TYPING_CMD_DOWN      260
#define PC_TYPING_CMD_ENTER     261 /* new line, or finish a one-line field */
#define PC_TYPING_CMD_END       262 /* finish editing (Ctrl/Shift+Enter) */
#define PC_TYPING_CMD_COMMIT    263 /* the composing syllable becomes text */
/* PC_TYPING_PAIR | glyph: insert a Korean patch two-byte character.
 * PC_TYPING_PREEDIT | glyph: replace the composing syllable with it. */
#define PC_TYPING_PAIR    0x10000
#define PC_TYPING_PREEDIT 0x20000
#define PC_TYPING_GLYPH_MASK 0xFFFF
#define PC_TYPING_PREEDIT_NONE (PC_TYPING_PREEDIT | PC_TYPING_GLYPH_MASK) /* remove it */

extern int g_pc_typing_mode;
extern int g_pc_editor_active;

/* Called by the editor when it opens and closes. A latin field (the town
 * name in English) starts in English. */
void pc_typing_editor_open(int latin);
void pc_typing_editor_close(void);

/* Takes the next operation; returns 0 when the queue is empty. */
int pc_typing_pop(int* op);

/* The editor could not insert the composing syllable (the field is full),
 * so the input method drops it. */
void pc_typing_preedit_rejected(void);

/* The editor acted on a controller command; whatever was composing stays
 * as typed text. */
void pc_typing_commit_composition(void);

/* Non-zero while typing in Korean (for the editor's mode display). */
int pc_typing_korean(void);

/* Feeds one key as the character it gives on a US QWERTY layout, or one of
 * the PC_TYPING_CMD_* values; also used by --input-script TYPE. Returns 0 if
 * typing is off. */
int pc_typing_key(int key);
int pc_typing_toggle_korean(void);

void pc_typing_handle_event(const SDL_Event* event);

#ifdef __cplusplus
}
#endif

#endif /* PC_TYPING_H */
