/* ko_osk.h - on-screen QWERTY keyboard for typing with a controller or touch
 *
 * The layout and the cursor only; drawing and input live in pc_ko_editor.c.
 * Five rows of ten units:
 *
 *     1 2 3 4 5 6 7 8 9 0
 *     q w e r t y u i o p        ㅂ ㅈ ㄷ ㄱ ㅅ ㅛ ㅕ ㅑ ㅐ ㅔ
 *     a s d f g h j k l ?        ㅁ ㄴ ㅇ ㄹ ㅎ ㅗ ㅓ ㅏ ㅣ
 *     ^ z x c v b n m , .        ㅋ ㅌ ㅊ ㅍ ㅠ ㅜ ㅡ
 *     [한/영] [  space  ] [ <- ] [확정]
 *
 * Letters go through the dubeolsik input method (ko_ime.h) as the QWERTY
 * key they sit on, so the keyboard composes exactly like a real one. Shift
 * applies to the next key only (ㄲ ㄸ ㅃ ㅆ ㅉ ㅒ ㅖ, capitals, !@#...). */
#ifndef KO_OSK_H
#define KO_OSK_H

#ifdef __cplusplus
extern "C" {
#endif

#define KO_OSK_ROWS 5
#define KO_OSK_COLS 10 /* units per row */

typedef enum {
    KO_OSK_CHAR,      /* a QWERTY character */
    KO_OSK_SHIFT,
    KO_OSK_HANENG,    /* Korean/English */
    KO_OSK_SPACE,
    KO_OSK_BACKSPACE,
    KO_OSK_ENTER,     /* new line, or finish a one-line field */
} KoOskKind;

typedef struct {
    KoOskKind kind;
    char ch;   /* KO_OSK_CHAR: the unshifted QWERTY character */
    int col;   /* first unit */
    int width; /* in units */
} KoOskKey;

typedef struct {
    int row;
    int col;   /* unit under the cursor; up/down keep it */
    int shift;
} KoOsk;

void ko_osk_init(KoOsk* osk);

/* Number of keys in a row and the key by index */
int ko_osk_row_len(int row);
const KoOskKey* ko_osk_row_key(int row, int index);

/* The key covering unit col of row (col is clamped) */
const KoOskKey* ko_osk_key_at(int row, int col);
const KoOskKey* ko_osk_current(const KoOsk* osk);

/* Moves the cursor a key left/right (wrapping in the row) or a row up/down
 * (wrapping), staying over the same unit where it can */
void ko_osk_move(KoOsk* osk, int dx, int dy);

/* What the key types: a character for KO_OSK_CHAR (shift applied) and
 * KO_OSK_SPACE, 0 for the others */
char ko_osk_char(const KoOskKey* key, int shift);

/* Presses the current key, or the given one. Returns the key; for a
 * character, *ch is what to type. Shift turns itself off after a character
 * and toggles on the shift key. */
const KoOskKey* ko_osk_press(KoOsk* osk, const KoOskKey* key, char* ch);

/* The legend as UTF-8 (at least 8 bytes): the jamo in Korean mode, else the
 * character; "한/영", "<-", "확정" for the special keys, "" for space. */
void ko_osk_label(const KoOskKey* key, int korean, int shift, char* out);

#ifdef __cplusplus
}
#endif

#endif /* KO_OSK_H */
