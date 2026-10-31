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
 * stays valid until the slot is used again. The patch's slots: 0 drawing a
 * line (mFont_SetLineStrings_AndSpace_new), 1 and 2 its width
 * (mFont_GetStringWidth, ...2F), 3 mMsg_Set_free_str, 4 mMsg_Set_item_str;
 * 5 is the PC's own (pc_ko_ledit_match). */
#define PC_KO_EXPAND_SLOTS 6
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
#define PC_KO_S7_PLAYER_SELECT 0xC6EC   /* _debug_player_select in player_select_draw_title, 13 bytes */
#define PC_KO_S7_PM_TOWN 0xC6FC         /* countory_title_str$540 in mPM_set_character, 4 bytes */
#define PC_KO_S7_PM_NAME 0xC704         /* person_title_str$541 in mPM_set_character, 4 bytes */
#define PC_KO_S7_AL_TIMER2 0xC70C       /* mes2_timer$595 in mAL_set_character, 4 bytes */
#define PC_KO_S7_AL_TIMER3 0xC714       /* mes3_timer$596 in mAL_set_character, 7 bytes */
#define PC_KO_S7_AL_TIME2 0xC71C        /* str_time_mes2$599 in mAL_set_character, 4 bytes */
#define PC_KO_S7_TI_TITLE 0xD30C        /* replaces chr_table$667 in mTI_set_character, 15 bytes */
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
#define PC_KO_DATA_LEDIT_WIN 0x595B4        /* mLE_win_data, 52 bytes each, edit_max at +0x20 */
#define PC_KO_DATA_HELLO_VOICE 0x3D730      /* voice$3952 in aNPC_act_say_hello_voice, {u8*, int} */
#define PC_KO_DATA_WARNING_WIN 0x5D518      /* wr_win_data, 16 bytes each: {lines, count, scale x, y} */
#define PC_KO_DATA_FILM_STRB 0x5E180        /* fl_strb_str_data, 28 bytes each: {lines, count, ...} */
#define PC_KO_DATA_FILM_SELB 0x5E724        /* fl_selb_str_data, 8 bytes each: {lines, count} */
const void* pc_ko_data_ptr(u32 offset, const void* original);
u32 pc_ko_data_u32(u32 offset, u32 original);

/* The foresta .data offset that the pointer at offset points to on the
 * disc, or PC_KO_NO_TARGET (not Korean mode, not a pointer into .data). */
#define PC_KO_NO_TARGET 0xFFFFFFFFu
u32 pc_ko_data_target(u32 offset);

/* Points count {text, length} pairs of a PC table at the disc's text and
 * length, for tables of lines whose text the patch moved or lengthened: the
 * PC fields are text[i * stride] and len[i * stride] (bytes), the disc's a
 * pointer at table_offset + i * disc_stride and a 32-bit length after it.
 * Does nothing for table_offset PC_KO_NO_TARGET. */
void pc_ko_redirect_strings(u8** text, int* len, size_t stride, int count, u32 table_offset, u32 disc_stride);

/* Talking voice of the patch for the character at p (prev: the byte before
 * it, or -1): a voice number, -1 for no sound (second byte of a syllable),
 * or PC_KO_VOICE_DEFAULT to use the game's table (and outside Korean mode). */
#define PC_KO_VOICE_DEFAULT (-2)
int pc_ko_voice(const u8* p, int prev);

/* Prints the voice chosen for the character at p of message msg_no while
 * g_pc_ko_voice_log is set (input script VOICELOG) */
extern int g_pc_ko_voice_log;
void pc_ko_voice_log(int msg_no, const u8* p, int voice);

/* Width of the character at p in the board, diary and notice editors'
 * line checks, as the patch measures it (one-byte half-width punctuation,
 * a syllable's second byte 0, a new line 0). */
int pc_ko_line_char_width(const u16* p);

/* Name editors (m_ledit_ovl.c): the patch has a catchphrase, the reply to
 * Resetti and a song request for K.K. typed into longer buffers of its own
 * (8, 18 and 18 bytes). pc_ko_ledit_buf returns the buffer for those
 * editors (mLE_TYPE_EPHRASE, _RESET, _REQUEST) in Korean mode, else NULL. */
#define PC_KO_LEDIT_LEN 18
u8* pc_ko_ledit_buf(int ledit_type);

/* How the patch matches a typed song request or reply to Resetti with a
 * song name or Resetti's phrase: equal once dictionary codes are expanded
 * and all spaces dropped. */
int pc_ko_ledit_match(const u8* name, int name_len, const u8* text, int text_len);

struct game_s;

/* Draws the keyboard help that replaces the text editor's input panel; x and
 * y are the editor's slide offsets. */
void pc_ko_editor_draw(struct game_s* game, f32 x, f32 y);

/* On-screen keyboard of that panel: back to its first key (editor opens);
 * takes the controller buttons it uses while the editor is taking input
 * (A B X Y Z L R, D-pad, stick; START is left to end editing); a finger
 * down at window coordinates 0-1 presses the key there (returns 1) */
void pc_ko_osk_reset(void);
void pc_ko_osk_pad(u16* buttons, s8* stick_x, s8* stick_y);
int pc_ko_osk_touch(int down, f32 x, f32 y);

#ifdef __cplusplus
}
#endif

#endif
