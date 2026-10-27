/* pc_ko.h - support for discs carrying the community Korean patch
 *
 * The patch (see docs/pc_port/korean_patch.md) encodes a Hangul syllable as
 * two bytes and stores 24x32 2bpp glyphs inside the message resource. The PC
 * port reads everything from the user's patched disc at runtime. */
#ifndef PC_KO_H
#define PC_KO_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero when the disc carries the Korean patch. */
extern int g_pc_ko_mode;

/* Detects the patch and loads its tables from the disc. Call after the disc
 * image is open and before the game starts. */
void pc_ko_init(void);

/* Returns the glyph number of the two-byte character starting at s, or -1
 * if s does not start one. */
int pc_ko_glyph_index(const u8* s);

/* Non-zero if s starts a two-byte character (including the patch's
 * dictionary codes, which have no glyph). */
int pc_ko_is_pair(const u8* s);

/* Same for strings stored one byte per u16 (mFontW). */
int pc_ko_glyph_index_w(const u16* s);

/* Glyph width table that replaces the game's monospaced font offsets. */
int pc_ko_font_offset(u8 c);

/* Loads glyph g into the second font bank and returns its character code
 * there (1-255). The caller draws that code from mFont_TEX_TYPE_FONT2. */
int pc_ko_glyph_slot(int g);

/* Writes the two bytes of glyph g (see ko_charset.h). */
void pc_ko_encode_glyph(int g, u8 out[2]);

/* Converts UTF-8 text to the patch's encoding, dropping characters it has
 * no glyph for. Returns the number of bytes written. */
int pc_ko_encode_utf8(const char* text, u8* out, int max);

/* Strings may hold 0x96 0xF6.. codes that stand for dictionary entries
 * (long item names). Returns the length of str with them expanded and sets
 * *out to the text: str itself, or one of PC_KO_EXPAND_SLOTS buffers that
 * stays valid until the slot is used again. */
#define PC_KO_EXPAND_SLOTS 5
int pc_ko_expand(const u8* str, int len, int slot, const u8** out);

/* A villager's catchphrase as text (the patch packs it), padded with spaces
 * to PC_KO_WORD_ENDING_LEN; returns ending unchanged without the patch. */
#define PC_KO_WORD_ENDING_LEN 16
u8* pc_ko_word_ending(u8* ending);

/* After a name was copied into a message at text, turns the particle that
 * follows (은/는 을/를 이/가 과/와 아/야) into the form that fits the name's
 * last sound. */
void pc_ko_fix_josa(u8* text, int len);

struct game_s;

/* Draws the keyboard help that replaces the text editor's input panel; x and
 * y are the editor's slide offsets. */
void pc_ko_editor_draw(struct game_s* game, f32 x, f32 y);

#ifdef __cplusplus
}
#endif

#endif
