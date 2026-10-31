/* Unit tests for the Korean patch text encoding (pc/src/ko/ko_charset.c). */
#include "ko_charset.h"

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

int main(void) {
    uint8_t pair[2];
    int g;

    /* Glyph numbers follow KS X 1001 order */
    CHECK(ko_charset_glyph(0xAC00) == 0);    /* 가 */
    CHECK(ko_charset_glyph(0xAC01) == 1);    /* 각 */
    CHECK(ko_charset_glyph(0xAC04) == 2);    /* 간 */
    CHECK(ko_charset_glyph(0xD79D) == 2349); /* 힝 */
    CHECK(ko_charset_glyph(0xAC02) == -1);   /* 갂 is not in KS X 1001 */
    CHECK(ko_charset_glyph(0xB620) == -1);   /* 똠 */

    /* Jamo for composing */
    CHECK(ko_charset_glyph(0x3131) == 2350); /* ㄱ */
    CHECK(ko_charset_glyph(0x314E) == 2368); /* ㅎ */
    CHECK(ko_charset_glyph(0x314F) == 2369); /* ㅏ */
    CHECK(ko_charset_glyph(0x3163) == 2389); /* ㅣ */
    CHECK(ko_charset_glyph(0x3133) == 2350); /* ㄳ -> ㄱ */
    CHECK(ko_charset_glyph(0x3144) == 2357); /* ㅄ -> ㅂ */
    CHECK(ko_charset_glyph('A') == -1);

    /* Encoding: lead byte = g / 50 in the lead list, trail = 0xCE + g % 50 */
    ko_charset_encode(0, pair);
    CHECK(pair[0] == 0x00 && pair[1] == 0xCE);
    ko_charset_encode(49, pair);
    CHECK(pair[0] == 0x00 && pair[1] == 0xFF);
    ko_charset_encode(50 * 32, pair);
    CHECK(pair[0] == 0x86 && pair[1] == 0xCE);
    ko_charset_encode(50 * 42 + 1, pair);
    CHECK(pair[0] == 0x91 && pair[1] == 0xCF);
    ko_charset_encode(2389, pair);
    CHECK(pair[0] == 0x96 && pair[1] == 0xCE + 39);

    for (g = 0; g < KO_GLYPH_COUNT; g++) {
        ko_charset_encode(g, pair);
        if (ko_charset_decode(pair) != g || ko_charset_glyph(ko_charset_unicode(g)) != g) {
            printf("FAIL round trip of glyph %d\n", g);
            failures++;
            break;
        }
    }
    pair[0] = 0x41;
    pair[1] = 0xCE;
    CHECK(ko_charset_decode(pair) == -1);
    pair[0] = 0x00;
    pair[1] = 0x41;
    CHECK(ko_charset_decode(pair) == -1);

    /* One-byte characters of the patched font */
    CHECK(ko_charset_ascii('A') == 0x41);
    CHECK(ko_charset_ascii('z') == 0x7A);
    CHECK(ko_charset_ascii('7') == 0x37);
    CHECK(ko_charset_ascii(' ') == 0x20);
    CHECK(ko_charset_ascii('~') == 0x2A);
    CHECK(ko_charset_ascii('=') == 0x7D);
    CHECK(ko_charset_ascii('{') == -1);

    /* Catchphrases as the patch stores them */
    {
        uint8_t text[KO_ENDING_TEXT_LEN];
        uint8_t out[KO_ENDING_PACKED_LEN];
        int a = ko_charset_glyph(0xBA4D); /* 멍 */
        int b = ko_charset_glyph(0xC57C); /* 야 */

        /* fits in four bytes: stored as is */
        memcpy(text, "abc     ", 8);
        ko_charset_pack_ending(text, out);
        CHECK(memcmp(out, "abc \0\0\0", 7) == 0);
        ko_charset_encode(a, text);
        ko_charset_encode(b, text + 2);
        memset(text + 4, ' ', 4);
        ko_charset_pack_ending(text, out);
        CHECK(out[0] == text[0] && out[1] == text[1] && out[2] == text[2] && out[3] == text[3]);
        CHECK(out[4] == 0 && out[5] == 0 && out[6] == 0);

        /* 멍멍야 + '!': three glyphs and a one-byte code */
        ko_charset_encode(a, text);
        ko_charset_encode(a, text + 2);
        ko_charset_encode(b, text + 4);
        text[6] = '!';
        text[7] = ' ';
        ko_charset_pack_ending(text, out);
        CHECK(out[0] == 0xFE);
        CHECK(((out[1] << 4) | (out[2] >> 4)) == a);
        CHECK((((out[2] & 0xF) << 8) | out[3]) == a);
        CHECK(((out[4] << 4) | (out[5] >> 4)) == b);
        CHECK((((out[5] & 0xF) << 8) | out[6]) == 2400 + '!');

        /* five one-byte codes: only four fit, unused values are 0xFFF */
        memcpy(text, "abcde   ", 8);
        ko_charset_pack_ending(text, out);
        CHECK(out[0] == 0xFE && ((out[1] << 4) | (out[2] >> 4)) == 2400 + 'a');
        CHECK((((out[5] & 0xF) << 8) | out[6]) == 2400 + 'd');
        ko_charset_encode(a, text);
        memcpy(text + 2, "  x  ", 5);
        text[7] = ' ';
        ko_charset_pack_ending(text, out);
        CHECK(((out[1] << 4) | (out[2] >> 4)) == a);
        CHECK((((out[2] & 0xF) << 8) | out[3]) == 2400 + ' ');
        CHECK((((out[5] & 0xF) << 8) | out[6]) == 2400 + 'x');
        memcpy(text, "ab    c ", 8);
        ko_charset_pack_ending(text, out);
        CHECK((((out[5] & 0xF) << 8) | out[6]) == 2400 + ' ');
        memcpy(text, "abcd e  ", 8);
        ko_charset_pack_ending(text, out);
        CHECK(out[0] == 0xFE);
        memcpy(text, "a       ", 8);
        text[1] = 0x00; /* a lead byte without a second byte is a one-byte code */
        text[4] = 'z';
        text[5] = 0x86;
        ko_charset_pack_ending(text, out);
        CHECK((((out[2] & 0xF) << 8) | out[3]) == 2400 + 0x00);
    }

    if (failures == 0) {
        printf("all ko_charset tests passed\n");
    }
    return failures != 0;
}
