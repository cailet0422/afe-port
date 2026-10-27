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

#ifdef __cplusplus
}
#endif

#endif
