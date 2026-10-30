/* Unit tests for the on-screen keyboard layout (pc/src/ko/ko_osk.c).
 *
 * Moves the cursor and presses keys, feeding the characters to the Hangul
 * input method as pc_typing.c does, and compares the text as UTF-8. */
#include "ko_ime.h"
#include "ko_osk.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void append_utf8(char* out, uint16_t c) {
    out += strlen(out);
    if (c < 0x80) {
        *out++ = (char)c;
    } else if (c < 0x800) {
        *out++ = (char)(0xC0 | (c >> 6));
        *out++ = (char)(0x80 | (c & 0x3F));
    } else {
        *out++ = (char)(0xE0 | (c >> 12));
        *out++ = (char)(0x80 | ((c >> 6) & 0x3F));
        *out++ = (char)(0x80 | (c & 0x3F));
    }
    *out = '\0';
}

/* Runs commands: hjkl move (vi style), '.' presses the current key */
static void run(const char* cmds, char* text) {
    KoOsk osk;
    KoIme ime;
    uint16_t out[KO_IME_MAX_COMMIT];
    int i, n;

    ko_osk_init(&osk);
    ko_ime_init(&ime);
    ko_ime_set_mode(&ime, KO_IME_MODE_KO, out);
    text[0] = '\0';
    for (; *cmds != '\0'; cmds++) {
        const KoOskKey* key;
        char ch;

        switch (*cmds) {
            case 'h': ko_osk_move(&osk, -1, 0); continue;
            case 'l': ko_osk_move(&osk, 1, 0); continue;
            case 'k': ko_osk_move(&osk, 0, -1); continue;
            case 'j': ko_osk_move(&osk, 0, 1); continue;
            default: break;
        }
        key = ko_osk_press(&osk, NULL, &ch);
        n = 0;
        if (ch != 0) {
            n = ko_ime_key(&ime, ch, out);
        } else if (key->kind == KO_OSK_HANENG) {
            n = ko_ime_toggle_mode(&ime, out);
        } else if (key->kind == KO_OSK_BACKSPACE) {
            ko_ime_backspace(&ime);
        }
        for (i = 0; i < n; i++) {
            append_utf8(text, out[i]);
        }
    }
    n = ko_ime_flush(&ime, out);
    for (i = 0; i < n; i++) {
        append_utf8(text, out[i]);
    }
}

static void expect(const char* cmds, const char* want) {
    char text[256];

    run(cmds, text);
    if (strcmp(text, want) != 0) {
        printf("FAIL \"%s\": got \"%s\", want \"%s\"\n", cmds, text, want);
        failures++;
    }
}

int main(void) {
    KoOsk osk;
    char label[16];

    /* the cursor starts on q (ㅂ) */
    expect(".", "ㅂ");
    /* ㄱ (r) then ㅏ (k): right x3, then down, right x4 */
    expect("lll.jllll.", "가");
    /* 한: ㅎ(g) ㅏ(k) ㄴ(s) */
    expect("jllll.lll.hhhhhh.", "한");
    /* wrapping: left from q is p (ㅔ), up from q is 1 */
    expect("h.", "ㅔ");
    expect("k.", "1");
    /* down from the last row comes back to the top: 5 rows */
    expect("jjjjj.", "ㅂ");
    /* shift: ㅃ, and only for one key */
    expect("jj.kk..", "ㅃㅂ");
    /* the bottom row is three rows down: space, backspace, 한/영 (English q) */
    expect("jjjl.", " ");
    expect(".jjjl.", "ㅂ ");
    expect(".jjjll.", "");
    expect(".jjj.jj.", "ㅂq");

    /* wide keys: from space (units 2-5) up lands on unit 2 (x) */
    ko_osk_init(&osk);
    osk.row = 4;
    osk.col = 4;
    CHECK(ko_osk_current(&osk)->kind == KO_OSK_SPACE);
    ko_osk_move(&osk, 0, -1);
    CHECK(ko_osk_current(&osk)->ch == 'x');
    CHECK(osk.col == 2);
    ko_osk_move(&osk, 0, 1);
    CHECK(ko_osk_current(&osk)->kind == KO_OSK_SPACE);
    ko_osk_move(&osk, 0, 1);
    CHECK(ko_osk_current(&osk)->ch == '3');
    /* right walks the bottom row's four keys and wraps */
    ko_osk_init(&osk);
    osk.row = 4;
    ko_osk_move(&osk, 1, 0);
    ko_osk_move(&osk, 1, 0);
    ko_osk_move(&osk, 1, 0);
    CHECK(ko_osk_current(&osk)->kind == KO_OSK_ENTER);
    ko_osk_move(&osk, 1, 0);
    CHECK(ko_osk_current(&osk)->kind == KO_OSK_HANENG);
    /* every unit of every row has a key */
    {
        int r, c;

        for (r = 0; r < KO_OSK_ROWS; r++) {
            for (c = 0; c < KO_OSK_COLS; c++) {
                const KoOskKey* key = ko_osk_key_at(r, c);

                CHECK(key != NULL && c >= key->col && c < key->col + key->width);
            }
        }
    }

    /* legends */
    ko_osk_label(ko_osk_key_at(1, 3), 1, 0, label);
    CHECK(strcmp(label, "ㄱ") == 0);
    ko_osk_label(ko_osk_key_at(1, 3), 1, 1, label);
    CHECK(strcmp(label, "ㄲ") == 0);
    ko_osk_label(ko_osk_key_at(1, 3), 0, 1, label);
    CHECK(strcmp(label, "R") == 0);
    ko_osk_label(ko_osk_key_at(0, 0), 1, 1, label);
    CHECK(strcmp(label, "!") == 0);
    ko_osk_label(ko_osk_key_at(4, 9), 1, 0, label);
    CHECK(strcmp(label, "확정") == 0);

    if (failures == 0) {
        printf("ko_osk: all tests passed\n");
    }
    return failures != 0;
}
