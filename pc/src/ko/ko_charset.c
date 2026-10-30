/* ko_charset.c - see ko_charset.h */
#include "ko_charset.h"

static const uint16_t ksx1001_syllables[KO_GLYPH_SYLLABLES] = {
#include "ko_ksx1001.inc"
};

/* Lead bytes in order; a lead's position selects a block of 50 glyphs. */
static const uint8_t leads[48] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
};

#define TRAIL_FIRST 0xCE
#define TRAILS 50

/* Compatibility jamo U+3131..U+314E -> initial consonant index (0-18). The
 * final-only clusters map to their first consonant. */
static const int8_t consonant_index[30] = {
    0,  /* ㄱ */ 1,  /* ㄲ */ 0,  /* ㄳ */ 2,  /* ㄴ */ 2,  /* ㄵ */ 2,  /* ㄶ */ 3,  /* ㄷ */ 4,  /* ㄸ */
    5,  /* ㄹ */ 5,  /* ㄺ */ 5,  /* ㄻ */ 5,  /* ㄼ */ 5,  /* ㄽ */ 5,  /* ㄾ */ 5,  /* ㄿ */ 5,  /* ㅀ */
    6,  /* ㅁ */ 7,  /* ㅂ */ 8,  /* ㅃ */ 7,  /* ㅄ */ 9,  /* ㅅ */ 10, /* ㅆ */ 11, /* ㅇ */ 12, /* ㅈ */
    13, /* ㅉ */ 14, /* ㅊ */ 15, /* ㅋ */ 16, /* ㅌ */ 17, /* ㅍ */ 18, /* ㅎ */
};

static const uint16_t initial_jamo[19] = {
    0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145,
    0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E,
};

int ko_charset_glyph(uint16_t unicode) {
    if (unicode >= 0xAC00 && unicode <= 0xD7A3) {
        int lo = 0;
        int hi = KO_GLYPH_SYLLABLES - 1;

        while (lo <= hi) {
            int mid = (lo + hi) / 2;

            if (ksx1001_syllables[mid] == unicode) {
                return mid;
            }
            if (ksx1001_syllables[mid] < unicode) {
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        return -1;
    }
    if (unicode >= 0x3131 && unicode <= 0x314E) {
        return KO_GLYPH_JAMO_FIRST + consonant_index[unicode - 0x3131];
    }
    if (unicode >= 0x314F && unicode <= 0x3163) {
        return KO_GLYPH_JAMO_FIRST + 19 + (unicode - 0x314F);
    }
    return -1;
}

uint16_t ko_charset_unicode(int g) {
    if (g < 0 || g >= KO_GLYPH_COUNT) {
        return 0;
    }
    if (g < KO_GLYPH_SYLLABLES) {
        return ksx1001_syllables[g];
    }
    g -= KO_GLYPH_JAMO_FIRST;
    return g < 19 ? initial_jamo[g] : (uint16_t)(0x314F + g - 19);
}

void ko_charset_encode(int g, uint8_t out[2]) {
    out[0] = leads[g / TRAILS];
    out[1] = (uint8_t)(TRAIL_FIRST + g % TRAILS);
}

int ko_charset_decode(const uint8_t* s) {
    int i;

    if (s[1] < TRAIL_FIRST) {
        return -1;
    }
    for (i = 0; i < (int)sizeof(leads); i++) {
        if (leads[i] == s[0]) {
            int g = i * TRAILS + (s[1] - TRAIL_FIRST);

            return g < KO_GLYPH_COUNT ? g : -1;
        }
    }
    return -1;
}

int ko_charset_ascii(char c) {
    /* The patched font keeps letters and digits at their ASCII codes; a few
     * symbols sit elsewhere and the rest are missing. */
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return (unsigned char)c;
    }
    switch (c) {
        case ' ': case '!': case '"': case '#': case '$': case '%': case '&': case '\'':
        case '(': case ')': case ',': case '-': case '.': case ':': case '<': case '>':
        case '?': case '@': case '[': case ']': case '^': case '_':
            return (unsigned char)c;
        case '~': return 0x2A;
        case '+': return 0x3D;
        case '\\': return 0x60;
        case ';': return 0x7B;
        case '*': return 0x7C;
        case '=': return 0x7D;
        case '/': return 0x7E;
        default: return -1;
    }
}
