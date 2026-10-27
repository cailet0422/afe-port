/* ko_ime.h - Hangul input method for the Korean patch.
 *
 * Dubeolsik (two-set) layout on a QWERTY keyboard with a Korean/English
 * toggle. Used by both the on-screen QWERTY keyboard and physical keyboard
 * typing. Output is Unicode (UTF-16 code units, BMP only): precomposed
 * syllables U+AC00..U+D7A3 and compatibility jamo U+3131..U+3163 for
 * jamo that do not form a syllable. Mapping to the game's text encoding
 * happens in a separate layer.
 *
 * The composing syllable is kept as the sequence of jamo keys that built
 * it, so backspace removes exactly one jamo (e.g. 값 -> 갑 -> 가 -> ㄱ).
 */
#ifndef KO_IME_H
#define KO_IME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KO_IME_MODE_EN = 0,
    KO_IME_MODE_KO = 1,
} KoImeMode;

/* Longest jamo sequence for one syllable: L V V T T (e.g. ㄱ ㅗ ㅏ ㄹ ㄱ = 괅) */
#define KO_IME_MAX_SEQ 5

/* Upper bound on code units a single call can commit. */
#define KO_IME_MAX_COMMIT 3

typedef struct {
    KoImeMode mode;
    int seq_len;
    uint16_t seq[KO_IME_MAX_SEQ]; /* compatibility jamo of the composing syllable */
} KoIme;

void ko_ime_init(KoIme* ime);

/* Switches between Korean and English. Any composing syllable is committed
 * into out. Returns the number of code units written. */
int ko_ime_toggle_mode(KoIme* ime, uint16_t* out);
int ko_ime_set_mode(KoIme* ime, KoImeMode mode, uint16_t* out);

/* Feeds one key as the ASCII character it produces on a US QWERTY layout
 * (shift already applied: 'r' vs 'R'). Letters compose in Korean mode;
 * every other character commits the composition and then passes through.
 * Writes committed code units to out (at least KO_IME_MAX_COMMIT entries)
 * and returns how many were written. */
int ko_ime_key(KoIme* ime, char key, uint16_t* out);

/* Removes the last jamo of the composing syllable. Returns 1 if the IME
 * consumed the backspace, 0 if nothing was composing and the caller should
 * delete the previous committed character itself. */
int ko_ime_backspace(KoIme* ime);

/* Commits the composing syllable, if any. Returns code units written (0/1). */
int ko_ime_flush(KoIme* ime, uint16_t* out);

/* The syllable or jamo being composed, or 0 if none. */
uint16_t ko_ime_preedit(const KoIme* ime);

/* Jamo produced by a QWERTY key in Korean mode, or 0 if the key is not a
 * letter. Exposed for drawing Korean legends on the on-screen keyboard. */
uint16_t ko_ime_key_jamo(char key);

#ifdef __cplusplus
}
#endif

#endif
