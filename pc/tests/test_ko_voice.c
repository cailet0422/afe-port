/* Unit tests for the Korean talking voice choice (pc/src/ko/ko_voice.c).
 * The tables here are made up; the real ones are on the patched disc. */
#include "ko_charset.h"
#include "ko_voice.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                \
        }                                                              \
    } while (0)

static uint8_t lead[256];
static uint8_t sound[(256 + KO_GLYPH_COUNT) * 2];
static uint8_t jamo[19 + 21];
static uint8_t kana[12 * 5];

static void set_sound(int i, int v) {
    sound[i * 2] = (uint8_t)(v >> 8);
    sound[i * 2 + 1] = (uint8_t)v;
}

static int syllable(int initial, int vowel, int final) {
    return (initial * 21 + vowel) * 28 + final;
}

static int voice_of(const KoVoiceTables* t, uint16_t unicode) {
    uint8_t s[2];

    ko_charset_encode(ko_charset_glyph(unicode), s);
    return ko_voice_code(t, s, -1);
}

int main(void) {
    static const uint8_t leads[48] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
        0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
    };
    KoVoiceTables t = { lead, sound, jamo, kana };
    uint8_t s[3];
    int i;

    for (i = 0; i < 48; i++) {
        lead[leads[i]] = (uint8_t)(i + 1);
    }
    for (i = 0; i < KO_GLYPH_SYLLABLES; i++) {
        set_sound(256 + i, ko_charset_unicode(i) - 0xAC00);
    }
    for (i = 0; i < 19; i++) {
        jamo[i] = (uint8_t)(i % 12); /* consonant row */
    }
    for (i = 0; i < 21; i++) {
        jamo[19 + i] = (uint8_t)(i % 5); /* vowel column */
    }
    jamo[19 + 2] = 0x10;  /* ㅑ: column 0 with y */
    jamo[19 + 6] = 0x13;  /* ㅕ: column 3 with y */
    jamo[19 + 9] |= 0x20; /* ㅘ */
    for (i = 0; i < 12 * 5; i++) {
        kana[i] = (uint8_t)(0x40 + i);
    }

    /* Consonant row and vowel column */
    CHECK(voice_of(&t, 0xAC00) == kana[0 * 5 + 0]);                     /* 가 */
    CHECK(voice_of(&t, 0xB098) == kana[2 * 5 + 0]);                     /* 나 */
    CHECK(voice_of(&t, 0xD558) == kana[(18 % 12) * 5 + 0]);             /* 하 */
    CHECK(voice_of(&t, 0xAC00 + syllable(0, 4, 0)) == kana[0 * 5 + 4]); /* 거 */
    CHECK(voice_of(&t, 0xAC00 + syllable(0, 2, 0)) == kana[0]);         /* 갸: y only after ㅇ */
    CHECK(voice_of(&t, 0xAC00 + syllable(0, 9, 0)) == kana[9 % 5]);     /* 과 */

    /* ㅇ: 은 음 응 as ん, y and w vowels */
    CHECK(voice_of(&t, 0xC740) == 0x2D);                     /* 은 */
    CHECK(voice_of(&t, 0xC74C) == 0x2D);                     /* 음 */
    CHECK(voice_of(&t, 0xC751) == 0x2D);                     /* 응 */
    CHECK(voice_of(&t, 0xC744) == kana[11 * 5 + 18 % 5]);    /* 을 */
    CHECK(voice_of(&t, 0xC57C) == 0x24);                     /* 야 */
    CHECK(voice_of(&t, 0xC5EC) == kana[3]);                  /* 여: column 3 with y is plain */
    CHECK(voice_of(&t, 0xC640) == 0x2C);                     /* 와 */
    CHECK(voice_of(&t, 0xC544) == kana[11 * 5 + 0]);         /* 아 */

    /* Lone jamo */
    CHECK(voice_of(&t, 0x3134) == kana[2 * 5 + 2]); /* ㄴ */
    CHECK(voice_of(&t, 0x3153) == kana[4]);         /* ㅓ */

    /* The second byte of a pair is silent */
    ko_charset_encode(ko_charset_glyph(0xAC00), s);
    CHECK(ko_voice_code(&t, s + 1, s[0]) == -1);

    /* One-byte syllables (0x97..) and the game's own characters */
    set_sound(0xAC, syllable(6, 0, 0) + 1); /* 마 */
    s[0] = 0xAC;
    s[1] = 0xC0;
    CHECK(ko_voice_code(&t, s, -1) == kana[6 * 5 + 0]);
    s[0] = 'A';
    s[1] = ' ';
    CHECK(ko_voice_code(&t, s, -1) == KO_VOICE_DEFAULT);
    s[0] = 0xD0; /* not after a lead */
    CHECK(ko_voice_code(&t, s, 'A') == KO_VOICE_DEFAULT);

    if (failures == 0) {
        printf("ko_voice: all tests passed\n");
    }
    return failures != 0;
}
