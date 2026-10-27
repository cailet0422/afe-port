/* pc_ko.c - see pc_ko.h and docs/pc_port/korean_patch.md */
#include "pc_ko.h"

#include "pc_platform.h"
#include "pc_disc.h"
#include "pc_bswap.h"
#include "jsyswrap.h"
#include "ko_charset.h"
#include "charmap.h"

int g_pc_ko_mode = 0;

/* Lead bytes in order; a lead's position selects a block of 50 glyphs. */
static const u8 ko_leads[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96,
};

#define KO_TRAIL_FIRST 0xCE
#define KO_TRAILS 50
#define KO_GLYPH_COUNT 2390

/* Glyphs follow the message data inside msg.bin */
#define KO_GLYPH_RES_OFFSET 0x332780
#define KO_GLYPH_SIZE 192 /* 24x32 at 2 bits per pixel, 12 GX tiles of 8x8 */

/* foresta.rel layout (GAEJ01_01, config/GAEJ01_01/foresta/symbols.txt) */
#define REL_SECTION_DATA 5
#define REL_SECTION_PATCH_CODE 7
#define FORESTA_DATA_FONT_OFFSET 0x7DC0 /* offset_data in mFont_Get_FontOffset */

/* Tables in the patch's code section (foresta.rel section 7, v0.9.2) */
#define S7_JOSA 0xA0CA        /* 5 particles x {after a final consonant, after a vowel} */
#define S7_BYTE_FINAL 0xA0DE  /* u16 per one-byte code: syllable index + 1, or 0 */
#define S7_ZERO_FINAL 0xB563  /* finals of 십 백 천 만 */
#define S7_DIGIT_FINAL 0xB567 /* finals of 영 일 이 삼 사 오 육 칠 팔 구 */
#define S7_DICT 0xD340        /* dictionary of long strings, see dict_entry */
#define S7_MIN_SIZE (S7_DICT + 16 + 2 * DICT_ENTRIES)

/* 0x96, 0xF6.., xx: a dictionary entry (long item names and the like) */
#define DICT_LEAD 0x96
#define DICT_FIRST_CODE (0xF6 * 255)
#define DICT_ENTRIES 1195
#define EXPAND_MAX 66

/* The second font bank is a 384x512 I4 texture: 16x16 cells of 24x32 */
#define FONT_BANK_TILES_PER_ROW 48
#define KO_SLOTS 256 /* slot 0 is never used */

extern u8 FONT_tex_font2[];
extern void pc_gx_texture_invalidate_data(const void* data);

static s8 lead_index[256];
static u8 font_offset[256];
static int slot_glyph[KO_SLOTS];
static u32 slot_used[KO_SLOTS];
static u32 use_clock = 0;
static u8* s7 = NULL; /* copy of the patch's section 7 */
static u32 s7_size = 0;

static u32 rd32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

void pc_ko_init(void) {
    u32 size = 0;
    u8* rel = pc_disc_load_file("foresta.rel.szs", &size);
    u32 num_sections, section_table, data_offset, data_size, patch_size;
    int i;

    memset(lead_index, -1, sizeof(lead_index));
    for (i = 0; i < (int)sizeof(ko_leads); i++) {
        lead_index[ko_leads[i]] = (s8)i;
    }
    for (i = 0; i < KO_SLOTS; i++) {
        slot_glyph[i] = -1;
    }

    if (rel == NULL || size < 0x40) {
        free(rel);
        return;
    }

    num_sections = rd32(rel + 0x0C);
    section_table = rd32(rel + 0x10);
    if (num_sections <= REL_SECTION_PATCH_CODE || section_table + num_sections * 8 > size) {
        free(rel);
        return;
    }
    patch_size = rd32(rel + section_table + REL_SECTION_PATCH_CODE * 8 + 4);
    data_offset = rd32(rel + section_table + REL_SECTION_DATA * 8) & ~1u;
    data_size = rd32(rel + section_table + REL_SECTION_DATA * 8 + 4);

    /* The unpatched game has no code in section 7 */
    if (patch_size != 0 && FORESTA_DATA_FONT_OFFSET + 256 <= data_size && data_offset + data_size <= size) {
        u32 s7_offset = rd32(rel + section_table + REL_SECTION_PATCH_CODE * 8) & ~1u;

        memcpy(font_offset, rel + data_offset + FORESTA_DATA_FONT_OFFSET, sizeof(font_offset));
        if (patch_size >= S7_MIN_SIZE && s7_offset + patch_size <= size) {
            s7 = (u8*)malloc(patch_size);
            memcpy(s7, rel + s7_offset, patch_size);
            s7_size = patch_size;
        } else {
            printf("[PC] Korean patch: unexpected code section size %u, text features off\n", (unsigned)patch_size);
        }
        g_pc_ko_mode = 1;
        printf("[PC] Korean patch detected\n");
    }
    free(rel);
}

int pc_ko_is_pair(const u8* s) {
    return lead_index[s[0]] >= 0 && s[1] >= KO_TRAIL_FIRST;
}

int pc_ko_glyph_index(const u8* s) {
    int lead = lead_index[s[0]];
    int g;

    if (lead < 0 || s[1] < KO_TRAIL_FIRST) {
        return -1;
    }
    g = lead * KO_TRAILS + (s[1] - KO_TRAIL_FIRST);
    return g < KO_GLYPH_COUNT ? g : -1;
}

int pc_ko_glyph_index_w(const u16* s) {
    u8 pair[2];

    if (s[0] > 0xFF || s[1] > 0xFF) {
        return -1;
    }
    pair[0] = (u8)s[0];
    pair[1] = (u8)s[1];
    return pc_ko_glyph_index(pair);
}

int pc_ko_font_offset(u8 c) {
    return font_offset[c];
}

/* Expands a 2bpp glyph (12 tiles of 16 bytes, row-major 3x4) into its I4
 * cell of the font bank. */
static void write_glyph(int slot, const u8* src) {
    int col = slot % 16;
    int row = slot / 16;
    int tile_row, tile_col, i;

    for (tile_row = 0; tile_row < 4; tile_row++) {
        for (tile_col = 0; tile_col < 3; tile_col++) {
            int tile = (row * 4 + tile_row) * FONT_BANK_TILES_PER_ROW + col * 3 + tile_col;
            u8* dst = FONT_tex_font2 + tile * 32;
            const u8* in = src + (tile_row * 3 + tile_col) * 16;

            for (i = 0; i < 16; i++) {
                u8 b = in[i];

                dst[i * 2 + 0] = (u8)((((b >> 6) & 3) * 5) << 4 | ((b >> 4) & 3) * 5);
                dst[i * 2 + 1] = (u8)((((b >> 2) & 3) * 5) << 4 | (b & 3) * 5);
            }
        }
    }
}

int pc_ko_glyph_slot(int g) {
    static u8 glyph_buf[KO_GLYPH_SIZE] ATTRIBUTE_ALIGN(32);
    int best = 1;
    int slot;

    use_clock++;
    for (slot = 1; slot < KO_SLOTS; slot++) {
        if (slot_glyph[slot] == g) {
            slot_used[slot] = use_clock;
            return slot;
        }
        if (slot_used[slot] < slot_used[best]) {
            best = slot;
        }
    }

    _JW_GetResourceAram(JW_GetAramAddress(RESOURCE_MESSAGE) + KO_GLYPH_RES_OFFSET + (u32)g * KO_GLYPH_SIZE, glyph_buf, KO_GLYPH_SIZE);
    write_glyph(best, glyph_buf);
    pc_gx_texture_invalidate_data(FONT_tex_font2);

    slot_glyph[best] = g;
    slot_used[best] = use_clock;
    return best;
}

void pc_ko_encode_glyph(int g, u8 out[2]) {
    ko_charset_encode(g, out);
}

int pc_ko_encode_utf8(const char* text, u8* out, int max) {
    const u8* p = (const u8*)text;
    int len = 0;

    while (*p != '\0') {
        u32 cp;
        int glyph;

        if (p[0] < 0x80) {
            cp = *p++;
        } else if ((p[0] & 0xE0) == 0xC0 && p[1] != '\0') {
            cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && p[1] != '\0' && p[2] != '\0') {
            cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            p += 3;
        } else {
            p++;
            continue;
        }

        glyph = cp <= 0xFFFF ? ko_charset_glyph((u16)cp) : -1;
        if (glyph >= 0) {
            if (len + 2 > max) {
                break;
            }
            ko_charset_encode(glyph, out + len);
            len += 2;
        } else if (cp < 0x80 && ko_charset_ascii((char)cp) >= 0) {
            if (len + 1 > max) {
                break;
            }
            out[len++] = (u8)ko_charset_ascii((char)cp);
        }
    }
    return len;
}

/* Dictionary entry for a 0x96 code at s, or NULL */
static const u8* dict_entry(const u8* s, int* len) {
    int code;
    u32 entry;

    if (s7 == NULL || s[0] != DICT_LEAD || s[1] < 0xF6) {
        return NULL;
    }
    /* The patch reads the third byte as signed (0x80.. counts one less) */
    code = s[1] * 255 + s[2] - (s[2] >= 0x80 ? 1 : 0) - DICT_FIRST_CODE;
    if (code < 0 || code >= DICT_ENTRIES) {
        return NULL;
    }
    entry = S7_DICT + ((s7[S7_DICT + 16 + code * 2] << 8) | s7[S7_DICT + 17 + code * 2]);
    if (entry + 1011 > s7_size) {
        return NULL;
    }
    *len = s7[entry + 1010];
    if (entry + 1011 + *len > s7_size) {
        return NULL;
    }
    return s7 + entry + 1011;
}

int pc_ko_expand(const u8* str, int len, int slot, const u8** out) {
    static u8 bufs[PC_KO_EXPAND_SLOTS][EXPAND_MAX + 2];
    u8* buf;
    int i;
    int n = 0;

    *out = str;
    if (!g_pc_ko_mode || str == NULL || len < 3 || len > 256 || slot < 0 || slot >= PC_KO_EXPAND_SLOTS) {
        return len;
    }
    for (i = 0; i + 2 < len; i++) {
        if (str[i] == DICT_LEAD && str[i + 1] >= 0xF6) {
            break;
        }
    }
    if (i + 2 >= len) {
        return len;
    }

    buf = bufs[slot];
    i = 0;
    while (i < len && n < EXPAND_MAX) {
        int entry_len;
        const u8* entry = i + 2 < len ? dict_entry(str + i, &entry_len) : NULL;
        int j;

        if (entry == NULL) {
            buf[n++] = str[i++];
            continue;
        }
        for (j = 0; j < entry_len && n < EXPAND_MAX; j++) {
            buf[n++] = entry[j];
        }
        i += 3;
        /* Spaces after the last code only pad the field */
        for (j = i; j < len && str[j] == CHAR_SPACE; j++) {
        }
        if (j == len) {
            break;
        }
    }
    *out = buf;
    return n;
}

/* Writes a 12-bit value of a packed catchphrase: a glyph below 2400, a
 * one-byte code (value - 2400) below 2656. Returns bytes written. */
static int put_packed(int v, u8* out, int room) {
    if (v < 2400) {
        if (room < 2) {
            return -1;
        }
        ko_charset_encode(v, out);
        return 2;
    }
    if (v < 2656) {
        if (room < 1) {
            return -1;
        }
        out[0] = (u8)(v - 2400);
        return 1;
    }
    return 0;
}

/* Decodes a catchphrase as the patch stores it: 0xFE and six bytes holding
 * four 12-bit values, a dictionary code, or plain text. */
static int decode_ending(const u8* src, int src_len, u8* out, int max) {
    int entry_len;
    const u8* entry;
    int n = 0;
    int i;

    if (src[0] == 0xFE) {
        int v[4];

        v[0] = (src[1] << 4) | (src[2] >> 4);
        v[1] = ((src[2] & 0xF) << 8) | src[3];
        v[2] = (src[4] << 4) | (src[5] >> 4);
        v[3] = ((src[5] & 0xF) << 8) | src[6];
        for (i = 0; i < 4; i++) {
            int w = put_packed(v[i], out + n, max - n);

            if (w < 0) {
                break;
            }
            n += w;
        }
        return n;
    }
    if (src[0] == DICT_LEAD && src[1] >= 0xF6) {
        entry = dict_entry(src, &entry_len);
        if (entry == NULL) {
            return 0;
        }
    } else {
        entry = src;
        entry_len = src_len;
    }
    for (i = 0; i < entry_len && i < max; i++) {
        out[i] = entry[i];
    }
    return i;
}

u8* pc_ko_word_ending(u8* ending) {
    static u8 buf[PC_KO_WORD_ENDING_LEN];
    int n;

    if (!g_pc_ko_mode || s7 == NULL || ending == NULL) {
        return ending;
    }
    n = decode_ending(ending, 4, buf, sizeof(buf));
    for (; n < (int)sizeof(buf); n++) {
        buf[n] = CHAR_SPACE;
    }
    return buf;
}

/* Final consonant (jongseong index, 0 = none) of the last character of
 * str as read aloud, or -1 if unknown. Digits are read as Sino-Korean
 * numbers: 3 is 삼, 10 is 십, 100 is 백. */
static int final_consonant(const u8* str, int len, int nested) {
    int result = -1;
    int zeros = 0;
    int last_digit = -1;
    int i = 0;

    while (i < len) {
        u8 c = str[i];
        int g;

        if (c == DICT_LEAD && i + 2 < len && str[i + 1] >= 0xF6) {
            int entry_len;
            const u8* entry = nested ? NULL : dict_entry(str + i, &entry_len);

            result = entry != NULL ? final_consonant(entry, entry_len, TRUE) : -1;
            last_digit = -1;
            zeros = 0;
            i += 3;
            continue;
        }
        g = i + 1 < len ? pc_ko_glyph_index(str + i) : -1;
        if (g >= 0) {
            u16 u = ko_charset_unicode(g);

            result = (g < KO_GLYPH_SYLLABLES) ? (u - 0xAC00) % 28 : -1;
            last_digit = -1;
            zeros = 0;
            i += 2;
            continue;
        }

        i++;
        if (((s7[S7_BYTE_FINAL + c * 2] << 8) | s7[S7_BYTE_FINAL + c * 2 + 1]) != 0) {
            result = (((s7[S7_BYTE_FINAL + c * 2] << 8) | s7[S7_BYTE_FINAL + c * 2 + 1]) - 1) % 28;
            last_digit = -1;
            zeros = 0;
        } else if (c >= '1' && c <= '9') {
            result = s7[S7_DIGIT_FINAL + c - '0'];
            last_digit = c - '0';
            zeros = 0;
        } else if (c == '0') {
            if (last_digit < 0 && zeros == 0) {
                result = s7[S7_DIGIT_FINAL];
                last_digit = 0;
            } else {
                result = zeros > 3 ? 4 : s7[S7_ZERO_FINAL + zeros];
                zeros++;
            }
        } else if (c != CHAR_SPACE) {
            result = -1;
            last_digit = -1;
            zeros = 0;
        }
    }
    return result;
}

/* Skips message tags (0x80, length, ...) */
static u8* skip_tags(u8* p) {
    int n;

    for (n = 0; n < 6 && p[0] == CHAR_MESSAGE_TAG && p[1] >= 2; n++) {
        p += p[1];
    }
    return p;
}

void pc_ko_fix_josa(u8* text, int len) {
    /* particle glyph -> particle (row of S7_JOSA); 아/야 is the vocative */
    static const struct {
        u8 lead, trail, josa;
    } particles[] = {
        { 0x1E, 0xEB, 0 }, { 0x08, 0xD7, 0 }, /* 은 는 */
        { 0x1E, 0xEC, 1 }, { 0x0F, 0xD5, 1 }, /* 을 를 */
        { 0x1E, 0xFD, 2 }, { 0x00, 0xCE, 2 }, /* 이 가 */
        { 0x01, 0xF5, 3 }, { 0x1D, 0xD2, 3 }, /* 과 와 */
        { 0x1B, 0xD1, 4 }, { 0x1B, 0xEA, 4 }, /* 아 야 */
    };
    int final;
    int has_final;
    int josa = -1;
    u8* p;
    u8* next;
    int i;

    if (!g_pc_ko_mode || s7 == NULL || len <= 0) {
        return;
    }
    final = final_consonant(text, len, FALSE);
    if (final < 0) {
        return;
    }
    p = skip_tags(text + len);
    for (i = 0; i < (int)(sizeof(particles) / sizeof(particles[0])); i++) {
        if (p[0] == particles[i].lead && p[1] == particles[i].trail) {
            josa = particles[i].josa;
            break;
        }
    }
    if (josa < 0) {
        return;
    }

    /* A syllable right after it means a word, not a particle */
    next = skip_tags(p + 2);
    if (pc_ko_is_pair(next)) {
        return;
    }
    has_final = final != 0;
    if (next[0] == DICT_LEAD) {
        if (josa == 4 && has_final) {
            return;
        }
    } else if (josa == 4 && has_final && next[0] != ',' && next[0] != 0xC7) {
        /* 아 only before a pause */
        return;
    }
    p[0] = s7[S7_JOSA + josa * 4 + (has_final ? 0 : 2)];
    p[1] = s7[S7_JOSA + josa * 4 + (has_final ? 1 : 3)];
}
