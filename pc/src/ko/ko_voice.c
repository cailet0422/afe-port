/* ko_voice.c - see ko_voice.h */
#include "ko_voice.h"

#define TRAIL_FIRST 0xCE
#define TRAILS 50
#define GLYPH_SYLLABLES 2350
#define GLYPH_INITIALS 19
#define GLYPH_COUNT 2390
#define ROW_VOICES 5

#define INITIAL_SILENT 11 /* ㅇ */
#define VOWEL_EU 18       /* ㅡ */
#define VOWEL_Y 0x10
#define VOWEL_W 0x20

/* Kana voices the patch picks by sound rather than from the table */
#define VOICE_YA 0x24
#define VOICE_YU 0x25
#define VOICE_YO 0x26
#define VOICE_WA 0x2C
#define VOICE_N 0x2D

static int rd16(const uint8_t* p, int i) {
    return (p[i * 2] << 8) | p[i * 2 + 1];
}

static int syllable_voice(const KoVoiceTables* t, int s) {
    int initial = s / (21 * 28);
    int vowel = (s / 28) % 21;
    int final = s % 28;
    int flags = t->jamo[GLYPH_INITIALS + vowel];
    int column = flags & 0xF;

    if (initial == INITIAL_SILENT) {
        /* 은 음 응 sound like ん */
        if (vowel == VOWEL_EU && (final == 4 || final == 16 || final == 21)) {
            return VOICE_N;
        }
        if (flags & VOWEL_Y) {
            switch (column) {
                case 0:
                    return VOICE_YA;
                case 2:
                    return VOICE_YU;
                case 4:
                    return VOICE_YO;
                default:
                    return t->kana[column];
            }
        }
        if (flags & VOWEL_W) {
            return VOICE_WA;
        }
    }
    return t->kana[t->jamo[initial] * ROW_VOICES + column];
}

int ko_voice_code(const KoVoiceTables* t, const uint8_t* p, int prev) {
    int c = p[0];
    int lead = t->lead[c];
    int v;

    if (lead != 0 && p[1] >= TRAIL_FIRST) {
        int g = (lead - 1) * TRAILS + p[1] - TRAIL_FIRST;

        if (g < GLYPH_SYLLABLES) {
            return syllable_voice(t, rd16(t->sound, 256 + g));
        }
        if (g < GLYPH_SYLLABLES + GLYPH_INITIALS) {
            /* A lone consonant sounds like its row's u */
            return t->kana[t->jamo[g - GLYPH_SYLLABLES] * ROW_VOICES + 2];
        }
        if (g < GLYPH_COUNT) {
            return t->kana[t->jamo[g - GLYPH_SYLLABLES] & 0xF];
        }
    }
    if (c >= TRAIL_FIRST && prev >= 0 && t->lead[prev] != 0) {
        return -1;
    }
    v = rd16(t->sound, c);
    return v != 0 ? syllable_voice(t, v - 1) : KO_VOICE_DEFAULT;
}
