/* pc_ko.c - see pc_ko.h and docs/pc_port/korean_patch.md */
#include "pc_ko.h"

#include "pc_platform.h"
#include "pc_disc.h"
#include "pc_bswap.h"
#include "jsyswrap.h"
#include "ko_charset.h"

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
        memcpy(font_offset, rel + data_offset + FORESTA_DATA_FONT_OFFSET, sizeof(font_offset));
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
