/* pc_ko.h - support for discs carrying the community Korean patch
 *
 * The patch (see docs/korean_patch.md) encodes a Hangul syllable as
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

/* Strings the patch moved into its own code section (foresta.rel section 7).
 * pc_ko_patched_str returns the patch's text at s7_offset in Korean mode and
 * original otherwise. Offsets are those of v0.9.2, found with
 * `pc/tools/reldis.py retarget` (docs/korean_patch.md). */
#define PC_KO_S7_BIRTHDAY_FOOTER 0xC124 /* footer_str$510 in mBm_init_BDMsg_Mail, 4 bytes */
#define PC_KO_S7_FISH_LAND 0xC12C       /* l_name$544 in mEv_fishRecord_local, 6 bytes */
#define PC_KO_S7_LAND_SUFFIX 0xC134     /* land_name_end in m_notice.c, 4 bytes */
#define PC_KO_S7_LOST_ITEM 0xC13C       /* str$521 in watch_my_step_move, 6 bytes */
#define PC_KO_S7_TURI_LAND 0xC154       /* l_name$572 in aTRC_clip_random_topsize, 6 bytes */
#define PC_KO_S7_BOARD_FOOTER 0xC15C    /* footer_str$885 in mBD_board_ovl_init, 4 bytes */
const u8* pc_ko_s7(u32 offset);
const u8* pc_ko_patched_str(u32 s7_offset, const u8* original);

/* A pointer or 32-bit value at a foresta .data offset as the patched disc
 * has it, for tables of string pointers and lengths (not overlaid because
 * they hold pointers). pc_ko_data_ptr returns the text the field points
 * to on the disc (the patch's section or the disc's copy of .data, since the
 * patch lengthens some strings in place), or original. */
#define PC_KO_DATA_HANIWA_MSG_TABLE 0x590DC /* mes_data_tbl$461 in mHW_make_cond_message */
#define PC_KO_DATA_MAP_LABELS 0x59AF8       /* mMP_label_data */
#define PC_KO_DATA_ESTRING_CLASS1 0x604BC   /* eString_string1_class */
#define PC_KO_DATA_ESTRING_CLASS2 0x604D4   /* eString_string2_class */
const void* pc_ko_data_ptr(u32 offset, const void* original);
u32 pc_ko_data_u32(u32 offset, u32 original);

/* Width of the character at p in the board, diary and notice editors'
 * line checks, as the patch measures it (one-byte half-width punctuation,
 * a syllable's second byte 0, a new line 0). */
int pc_ko_line_char_width(const u16* p);

struct game_s;

/* Draws the keyboard help that replaces the text editor's input panel; x and
 * y are the editor's slide offsets. */
void pc_ko_editor_draw(struct game_s* game, f32 x, f32 y);

#ifdef __cplusplus
}
#endif

#endif
