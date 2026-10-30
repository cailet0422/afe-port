/* ko_osk.c - see ko_osk.h */
#include "ko_osk.h"
#include "ko_ime.h"

#include <stddef.h>
#include <string.h>

#define CH(c, col) { KO_OSK_CHAR, c, col, 1 }

static const KoOskKey row0[] = { CH('1', 0), CH('2', 1), CH('3', 2), CH('4', 3), CH('5', 4),
                                 CH('6', 5), CH('7', 6), CH('8', 7), CH('9', 8), CH('0', 9) };
static const KoOskKey row1[] = { CH('q', 0), CH('w', 1), CH('e', 2), CH('r', 3), CH('t', 4),
                                 CH('y', 5), CH('u', 6), CH('i', 7), CH('o', 8), CH('p', 9) };
static const KoOskKey row2[] = { CH('a', 0), CH('s', 1), CH('d', 2), CH('f', 3), CH('g', 4),
                                 CH('h', 5), CH('j', 6), CH('k', 7), CH('l', 8), CH('?', 9) };
static const KoOskKey row3[] = { { KO_OSK_SHIFT, 0, 0, 1 }, CH('z', 1), CH('x', 2), CH('c', 3), CH('v', 4),
                                 CH('b', 5), CH('n', 6), CH('m', 7), CH(',', 8), CH('.', 9) };
static const KoOskKey row4[] = { { KO_OSK_HANENG, 0, 0, 2 }, { KO_OSK_SPACE, ' ', 2, 4 },
                                 { KO_OSK_BACKSPACE, 0, 6, 2 }, { KO_OSK_ENTER, 0, 8, 2 } };

static const struct {
    const KoOskKey* keys;
    int len;
} rows[KO_OSK_ROWS] = {
    { row0, 10 }, { row1, 10 }, { row2, 10 }, { row3, 10 }, { row4, 4 },
};

void ko_osk_init(KoOsk* osk) {
    osk->row = 1; /* on ㅂ, with the letters */
    osk->col = 0;
    osk->shift = 0;
}

int ko_osk_row_len(int row) {
    return row >= 0 && row < KO_OSK_ROWS ? rows[row].len : 0;
}

const KoOskKey* ko_osk_row_key(int row, int index) {
    if (index < 0 || index >= ko_osk_row_len(row)) {
        return NULL;
    }
    return &rows[row].keys[index];
}

const KoOskKey* ko_osk_key_at(int row, int col) {
    int i;

    if (row < 0 || row >= KO_OSK_ROWS) {
        return NULL;
    }
    if (col < 0) {
        col = 0;
    } else if (col >= KO_OSK_COLS) {
        col = KO_OSK_COLS - 1;
    }
    for (i = 0; i < rows[row].len; i++) {
        const KoOskKey* key = &rows[row].keys[i];

        if (col >= key->col && col < key->col + key->width) {
            return key;
        }
    }
    return &rows[row].keys[rows[row].len - 1];
}

const KoOskKey* ko_osk_current(const KoOsk* osk) {
    return ko_osk_key_at(osk->row, osk->col);
}

void ko_osk_move(KoOsk* osk, int dx, int dy) {
    if (dx != 0) {
        const KoOskKey* key = ko_osk_current(osk);
        int index = (int)(key - rows[osk->row].keys);
        int len = rows[osk->row].len;

        index = ((index + (dx > 0 ? 1 : -1)) % len + len) % len;
        osk->col = rows[osk->row].keys[index].col;
    }
    if (dy != 0) {
        const KoOskKey* key = ko_osk_current(osk);

        /* coming off a wide key, land under its first unit */
        if (key->width > 1) {
            osk->col = key->col;
        }
        osk->row = ((osk->row + (dy > 0 ? 1 : -1)) % KO_OSK_ROWS + KO_OSK_ROWS) % KO_OSK_ROWS;
    }
}

char ko_osk_char(const KoOskKey* key, int shift) {
    static const char digits[] = "1234567890";
    static const char shifted_digits[] = "!@#$%^&*()";

    if (key->kind == KO_OSK_SPACE) {
        return ' ';
    }
    if (key->kind != KO_OSK_CHAR) {
        return 0;
    }
    if (!shift) {
        return key->ch;
    }
    if (key->ch >= 'a' && key->ch <= 'z') {
        return (char)(key->ch - 'a' + 'A');
    }
    if (key->ch >= '0' && key->ch <= '9') {
        return shifted_digits[strchr(digits, key->ch) - digits];
    }
    switch (key->ch) {
        case ',': return '<';
        case '.': return '>';
        case '?': return '/';
        default: return key->ch;
    }
}

const KoOskKey* ko_osk_press(KoOsk* osk, const KoOskKey* key, char* ch) {
    if (key == NULL) {
        key = ko_osk_current(osk);
    }
    *ch = ko_osk_char(key, osk->shift);
    if (key->kind == KO_OSK_SHIFT) {
        osk->shift ^= 1;
    } else if (key->kind == KO_OSK_CHAR) {
        osk->shift = 0;
    }
    return key;
}

static void utf8(unsigned cp, char* out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        out[1] = '\0';
    } else {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        out[3] = '\0';
    }
}

void ko_osk_label(const KoOskKey* key, int korean, int shift, char* out) {
    char ch;

    switch (key->kind) {
        case KO_OSK_SHIFT:     strcpy(out, "^"); return;
        case KO_OSK_HANENG:    strcpy(out, "한/영"); return;
        case KO_OSK_SPACE:     strcpy(out, ""); return;
        case KO_OSK_BACKSPACE: strcpy(out, "<-"); return;
        case KO_OSK_ENTER:     strcpy(out, "확정"); return;
        default: break;
    }
    ch = ko_osk_char(key, shift);
    if (korean && ko_ime_key_jamo(ch) != 0) {
        utf8(ko_ime_key_jamo(ch), out);
    } else {
        utf8((unsigned char)ch, out);
    }
}
