/* ko_voice.h - the Korean patch's choice of a talking voice for a character
 *
 * The game gives every one-byte character one of its kana voices
 * (mMsg_sound_voice_get). The patch's mMsg_sound_CodeVoice hook splits a
 * Hangul syllable into its initial consonant, vowel and final consonant and
 * picks the kana voice of the matching consonant row and vowel column from
 * tables in its code section (docs/korean_patch.md). The tables come from the
 * user's patched disc. */
#ifndef KO_VOICE_H
#define KO_VOICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint8_t* lead;  /* 256 bytes: a lead byte's index + 1, 0 for other bytes */
    const uint8_t* sound; /* big-endian u16: [c] syllable + 1 of one-byte code c (0: none),
                             [256 + g] syllable of glyph g (syllable = Unicode - 0xAC00) */
    const uint8_t* jamo;  /* 19 initial consonants' rows, then 21 vowels' column | 0x10 (y) | 0x20 (w) */
    const uint8_t* kana;  /* 5 voices (columns) per consonant row */
} KoVoiceTables;

/* p[0] has no voice of the patch's own: use the game's table */
#define KO_VOICE_DEFAULT (-2)

/* Voice for the character at p (p[1] must be readable), -1 for the second
 * byte of a pair (prev is the byte before p, or -1 at the start), or
 * KO_VOICE_DEFAULT. */
int ko_voice_code(const KoVoiceTables* t, const uint8_t* p, int prev);

#ifdef __cplusplus
}
#endif

#endif
