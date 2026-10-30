/* ko_ime.c - Dubeolsik Hangul input method (see ko_ime.h). */
#include "ko_ime.h"

#include <stddef.h>

#define JAMO_CONS_FIRST 0x3131 /* ㄱ */
#define JAMO_CONS_LAST 0x314E  /* ㅎ */
#define JAMO_VOWEL_FIRST 0x314F /* ㅏ */
#define JAMO_VOWEL_LAST 0x3163  /* ㅣ */

#define SYLLABLE_BASE 0xAC00
#define NUM_VOWELS 21
#define NUM_FINALS 28

/* Choseong index per compatibility consonant (ㄱ..ㅎ), -1 if it cannot start a syllable. */
static const signed char s_cons_to_initial[JAMO_CONS_LAST - JAMO_CONS_FIRST + 1] = {
    0,  1,  -1, 2,  -1, -1, 3,  4,  5,  -1, -1, -1, -1, -1, -1,
    -1, 6,  7,  8,  -1, 9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
};

/* Jongseong index per compatibility consonant, 0 if it cannot end a syllable (ㄸ ㅃ ㅉ). */
static const unsigned char s_cons_to_final[JAMO_CONS_LAST - JAMO_CONS_FIRST + 1] = {
    1,  2,  3,  4,  5,  6,  7,  0,  8,  9,  10, 11, 12, 13, 14,
    15, 16, 17, 0,  18, 19, 20, 21, 22, 0,  23, 24, 25, 26, 27,
};

typedef struct {
    uint16_t first;
    uint16_t second;
    uint16_t combined;
} JamoPair;

static const JamoPair s_vowel_pairs[] = {
    { 0x3157, 0x314F, 0x3158 }, /* ㅗ + ㅏ = ㅘ */
    { 0x3157, 0x3150, 0x3159 }, /* ㅗ + ㅐ = ㅙ */
    { 0x3157, 0x3163, 0x315A }, /* ㅗ + ㅣ = ㅚ */
    { 0x315C, 0x3153, 0x315D }, /* ㅜ + ㅓ = ㅝ */
    { 0x315C, 0x3154, 0x315E }, /* ㅜ + ㅔ = ㅞ */
    { 0x315C, 0x3163, 0x315F }, /* ㅜ + ㅣ = ㅟ */
    { 0x3161, 0x3163, 0x3162 }, /* ㅡ + ㅣ = ㅢ */
};

static const JamoPair s_final_pairs[] = {
    { 0x3131, 0x3145, 0x3133 }, /* ㄱ + ㅅ = ㄳ */
    { 0x3134, 0x3148, 0x3135 }, /* ㄴ + ㅈ = ㄵ */
    { 0x3134, 0x314E, 0x3136 }, /* ㄴ + ㅎ = ㄶ */
    { 0x3139, 0x3131, 0x313A }, /* ㄹ + ㄱ = ㄺ */
    { 0x3139, 0x3141, 0x313B }, /* ㄹ + ㅁ = ㄻ */
    { 0x3139, 0x3142, 0x313C }, /* ㄹ + ㅂ = ㄼ */
    { 0x3139, 0x3145, 0x313D }, /* ㄹ + ㅅ = ㄽ */
    { 0x3139, 0x314C, 0x313E }, /* ㄹ + ㅌ = ㄾ */
    { 0x3139, 0x314D, 0x313F }, /* ㄹ + ㅍ = ㄿ */
    { 0x3139, 0x314E, 0x3140 }, /* ㄹ + ㅎ = ㅀ */
    { 0x3142, 0x3145, 0x3144 }, /* ㅂ + ㅅ = ㅄ */
};

/* Unshifted letters a..z. */
static const uint16_t s_key_jamo[26] = {
    0x3141, /* a ㅁ */ 0x3160, /* b ㅠ */ 0x314A, /* c ㅊ */ 0x3147, /* d ㅇ */
    0x3137, /* e ㄷ */ 0x3139, /* f ㄹ */ 0x314E, /* g ㅎ */ 0x3157, /* h ㅗ */
    0x3151, /* i ㅑ */ 0x3153, /* j ㅓ */ 0x314F, /* k ㅏ */ 0x3163, /* l ㅣ */
    0x3161, /* m ㅡ */ 0x315C, /* n ㅜ */ 0x3150, /* o ㅐ */ 0x3154, /* p ㅔ */
    0x3142, /* q ㅂ */ 0x3131, /* r ㄱ */ 0x3134, /* s ㄴ */ 0x3145, /* t ㅅ */
    0x3155, /* u ㅕ */ 0x314D, /* v ㅍ */ 0x3148, /* w ㅈ */ 0x314C, /* x ㅌ */
    0x315B, /* y ㅛ */ 0x314B, /* z ㅋ */
};

static int is_consonant(uint16_t j) {
    return j >= JAMO_CONS_FIRST && j <= JAMO_CONS_LAST;
}

static int is_vowel(uint16_t j) {
    return j >= JAMO_VOWEL_FIRST && j <= JAMO_VOWEL_LAST;
}

static uint16_t combine(const JamoPair* pairs, size_t count, uint16_t first, uint16_t second) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (pairs[i].first == first && pairs[i].second == second) {
            return pairs[i].combined;
        }
    }

    return 0;
}

static uint16_t combine_vowel(uint16_t first, uint16_t second) {
    return combine(s_vowel_pairs, sizeof(s_vowel_pairs) / sizeof(s_vowel_pairs[0]), first, second);
}

static uint16_t combine_final(uint16_t first, uint16_t second) {
    return combine(s_final_pairs, sizeof(s_final_pairs) / sizeof(s_final_pairs[0]), first, second);
}

typedef struct {
    uint16_t initial; /* compatibility jamo, 0 if none */
    uint16_t vowel;
    uint16_t final;
    int vowel_keys; /* jamo keys spent on the vowel (0..2) */
    int final_keys; /* jamo keys spent on the final (0..2) */
} Parts;

static void split_seq(const uint16_t* seq, int len, Parts* p) {
    int i = 0;

    p->initial = 0;
    p->vowel = 0;
    p->final = 0;
    p->vowel_keys = 0;
    p->final_keys = 0;

    if (i < len && is_consonant(seq[i])) {
        p->initial = seq[i++];
    }

    if (i < len) {
        p->vowel = seq[i++];
        p->vowel_keys = 1;
        if (i < len && is_vowel(seq[i])) {
            p->vowel = combine_vowel(p->vowel, seq[i++]);
            p->vowel_keys = 2;
        }
    }

    if (i < len) {
        p->final = seq[i++];
        p->final_keys = 1;
        if (i < len) {
            p->final = combine_final(p->final, seq[i++]);
            p->final_keys = 2;
        }
    }
}

static uint16_t compose(const uint16_t* seq, int len) {
    Parts p;

    if (len == 0) {
        return 0;
    }

    split_seq(seq, len, &p);

    if (p.initial == 0) {
        return p.vowel;
    }

    if (p.vowel == 0) {
        return p.initial;
    }

    return (uint16_t)(SYLLABLE_BASE +
                      ((s_cons_to_initial[p.initial - JAMO_CONS_FIRST] * NUM_VOWELS) +
                       (p.vowel - JAMO_VOWEL_FIRST)) * NUM_FINALS +
                      (p.final != 0 ? s_cons_to_final[p.final - JAMO_CONS_FIRST] : 0));
}

static int can_be_final(uint16_t consonant) {
    return s_cons_to_final[consonant - JAMO_CONS_FIRST] != 0;
}

static void restart(KoIme* ime, uint16_t jamo) {
    ime->seq[0] = jamo;
    ime->seq_len = 1;
}

static void append(KoIme* ime, uint16_t jamo) {
    ime->seq[ime->seq_len++] = jamo;
}

static int input_jamo(KoIme* ime, uint16_t jamo, uint16_t* out) {
    Parts p;
    int n = 0;

    if (ime->seq_len == 0) {
        restart(ime, jamo);
        return 0;
    }

    split_seq(ime->seq, ime->seq_len, &p);

    if (is_consonant(jamo)) {
        if (p.initial != 0 && p.vowel_keys > 0) {
            if (p.final_keys == 0 && can_be_final(jamo)) {
                append(ime, jamo);
                return 0;
            }
            if (p.final_keys == 1 && combine_final(p.final, jamo) != 0) {
                append(ime, jamo);
                return 0;
            }
        }

        n = ko_ime_flush(ime, out);
        restart(ime, jamo);
        return n;
    }

    /* Vowel. */
    if (p.final_keys > 0) {
        /* The last final consonant moves on to start the next syllable: 갑 + ㅣ = 가비, 값 + ㅣ = 갑시 */
        uint16_t moved = ime->seq[ime->seq_len - 1];

        ime->seq_len--;
        n = ko_ime_flush(ime, out);
        restart(ime, moved);
        append(ime, jamo);
        return n;
    }

    if (p.vowel_keys == 0) {
        append(ime, jamo);
        return 0;
    }

    if (p.vowel_keys == 1 && combine_vowel(p.vowel, jamo) != 0) {
        append(ime, jamo);
        return 0;
    }

    n = ko_ime_flush(ime, out);
    restart(ime, jamo);
    return n;
}

void ko_ime_init(KoIme* ime) {
    ime->mode = KO_IME_MODE_EN;
    ime->seq_len = 0;
}

int ko_ime_set_mode(KoIme* ime, KoImeMode mode, uint16_t* out) {
    int n = ko_ime_flush(ime, out);

    ime->mode = mode;
    return n;
}

int ko_ime_toggle_mode(KoIme* ime, uint16_t* out) {
    return ko_ime_set_mode(ime, ime->mode == KO_IME_MODE_KO ? KO_IME_MODE_EN : KO_IME_MODE_KO, out);
}

uint16_t ko_ime_key_jamo(char key) {
    switch (key) {
        case 'Q':
            return 0x3143; /* ㅃ */
        case 'W':
            return 0x3149; /* ㅉ */
        case 'E':
            return 0x3138; /* ㄸ */
        case 'R':
            return 0x3132; /* ㄲ */
        case 'T':
            return 0x3146; /* ㅆ */
        case 'O':
            return 0x3152; /* ㅒ */
        case 'P':
            return 0x3156; /* ㅖ */
    }

    if (key >= 'a' && key <= 'z') {
        return s_key_jamo[key - 'a'];
    }

    if (key >= 'A' && key <= 'Z') {
        return s_key_jamo[key - 'A'];
    }

    return 0;
}

int ko_ime_key(KoIme* ime, char key, uint16_t* out) {
    int n;

    if (ime->mode == KO_IME_MODE_KO) {
        uint16_t jamo = ko_ime_key_jamo(key);

        if (jamo != 0) {
            return input_jamo(ime, jamo, out);
        }
    }

    n = ko_ime_flush(ime, out);
    out[n++] = (uint16_t)(unsigned char)key;
    return n;
}

int ko_ime_backspace(KoIme* ime) {
    if (ime->seq_len == 0) {
        return 0;
    }

    ime->seq_len--;
    return 1;
}

int ko_ime_flush(KoIme* ime, uint16_t* out) {
    if (ime->seq_len == 0) {
        return 0;
    }

    out[0] = compose(ime->seq, ime->seq_len);
    ime->seq_len = 0;
    return 1;
}

uint16_t ko_ime_preedit(const KoIme* ime) {
    return compose(ime->seq, ime->seq_len);
}
