/* ko_charset.h - Unicode <-> Korean patch text encoding
 *
 * The community Korean patch (docs/pc_port/korean_patch.md) stores a Hangul
 * syllable as two bytes that select one of its glyphs:
 *   glyphs 0-2349     KS X 1001 syllables in code order
 *   glyphs 2350-2368  the 19 initial consonants (ㄱ ㄲ ㄴ ... ㅎ)
 *   glyphs 2369-2389  the 21 vowels (ㅏ ... ㅣ)
 * Everything else is the game's own one-byte character set. */
#ifndef KO_CHARSET_H
#define KO_CHARSET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KO_GLYPH_SYLLABLES 2350
#define KO_GLYPH_JAMO_FIRST 2350
#define KO_GLYPH_COUNT 2390

/* Glyph for a syllable (U+AC00..U+D7A3) or compatibility jamo
 * (U+3131..U+3163), or -1 if the patch has none. A final-only cluster such
 * as ㄳ falls back to its first consonant. */
int ko_charset_glyph(uint16_t unicode);

/* Inverse of ko_charset_glyph; 0 if g is out of range. */
uint16_t ko_charset_unicode(int g);

/* Writes the two bytes that encode glyph g. */
void ko_charset_encode(int g, uint8_t out[2]);

/* Glyph encoded by s[0], s[1], or -1 if they are not a Hangul pair. */
int ko_charset_decode(const uint8_t* s);

/* One-byte code of a printable ASCII character in the patched font, or -1
 * if the font has no such character. */
int ko_charset_ascii(char c);

#ifdef __cplusplus
}
#endif

#endif
