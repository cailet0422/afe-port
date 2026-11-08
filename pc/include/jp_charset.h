/* jp_charset.h - the original disc's font pages as Unicode
 *
 * jp_charset[page][code]: page 0 the kana and signs every string draws with,
 * pages 1 and 2 the kanji pages a kanji tag selects for its ruby (m_font.c,
 * mFont_TEX_TYPE_FONT2 and FONT3); 0 for codes with no character (control
 * codes, tags, unused codes, icons with no Unicode sign). Generated from
 * afe-decomp's include/charmap.h (pc/tools/gen_jp_charset.py). */
#ifndef JP_CHARSET_H
#define JP_CHARSET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint16_t jp_charset[3][256];

#ifdef __cplusplus
}
#endif

#endif
