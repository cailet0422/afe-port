/* pc_minimap_core.c - see pc_minimap_core.h */
#include "pc_minimap_core.h"

#include <math.h>

#define PC_MINIMAP_TABLE_DATA
#include "pc_minimap_tables.inc"

/* Border blocks are not on the game's map (mMP_check_bg_kind) */
static int on_map(int type) {
    return !(type <= PC_MINIMAP_BT_BORDER_CLIFF_CORNER_TOP_RIGHT || type == PC_MINIMAP_BT_BORDER_CLIFF_LEFT_TRANSITION ||
             type == PC_MINIMAP_BT_BORDER_CLIFF_RIGHT_TRANSITION || type == PC_MINIMAP_BT_BORDER_CLIFF_LEFT_TUNNEL ||
             type == PC_MINIMAP_BT_BORDER_CLIFF_RIGHT_TUNNEL);
}

/* mMP_make_max_no_table: the acres' block types in order, skipping border
 * blocks, grass where the field has fewer */
void pc_minimap_pick_tiles(const uint8_t* block_type, int bridge_exists, int bridge_bx, int bridge_bz,
                           uint8_t tex[PC_MINIMAP_ACRES], uint8_t pal[PC_MINIMAP_ACRES]) {
    int types[PC_MINIMAP_ACRES];
    int n = 0;
    int bx, bz, i;

    for (i = 0; i < PC_MINIMAP_ACRES; i++) {
        types[i] = PC_MINIMAP_BT_FLAT;
    }
    for (bz = 1; bz <= PC_MINIMAP_ACRES_Z && n < PC_MINIMAP_ACRES; bz++) {
        for (bx = 1; bx <= PC_MINIMAP_ACRES_X && n < PC_MINIMAP_ACRES; bx++) {
            int type = block_type[bz * PC_MINIMAP_BLOCKS_X + bx];

            if (!on_map(type)) {
                continue;
            }
            if (type < PC_MINIMAP_BT_NUM && bridge_exists && bridge_bx == bx && bridge_bz == bz &&
                s_type_bridge[type] != PC_MINIMAP_BT_NONE) {
                type = s_type_bridge[type];
            }
            types[n++] = type;
        }
    }
    for (i = 0; i < PC_MINIMAP_ACRES; i++) {
        int type = types[i] < PC_MINIMAP_BT_NUM ? types[i] : PC_MINIMAP_BT_FLAT;

        tex[i] = s_type_tex[type];
        pal[i] = s_type_pal[type];
    }
}

int pc_minimap_texture_count(void) {
    return PC_MINIMAP_TEX_NUM;
}

void pc_minimap_rgb5a3(uint16_t v, uint8_t rgba[4]) {
    if (v & 0x8000) {
        rgba[0] = (uint8_t)(((v >> 10) & 0x1F) * 255 / 31);
        rgba[1] = (uint8_t)(((v >> 5) & 0x1F) * 255 / 31);
        rgba[2] = (uint8_t)((v & 0x1F) * 255 / 31);
        rgba[3] = 255;
    } else {
        rgba[3] = (uint8_t)(((v >> 12) & 0x07) * 255 / 7);
        rgba[0] = (uint8_t)(((v >> 8) & 0x0F) * 255 / 15);
        rgba[1] = (uint8_t)(((v >> 4) & 0x0F) * 255 / 15);
        rgba[2] = (uint8_t)((v & 0x0F) * 255 / 15);
    }
}

/* GameCube CI4: 8x8 texel tiles of 32 bytes, left to right then down; two
 * texels a byte, the left one in the high nibble */
void pc_minimap_decode_tile(const uint8_t* ci4, int pal, uint8_t* dst, int stride) {
    uint8_t colors[16][4];
    int i, tx, ty, y, x;

    for (i = 0; i < 16; i++) {
        pc_minimap_rgb5a3(s_palettes[pal & 1][i], colors[i]);
    }
    for (ty = 0; ty < PC_MINIMAP_TILE / 8; ty++) {
        for (tx = 0; tx < PC_MINIMAP_TILE / 8; tx++) {
            for (y = 0; y < 8; y++) {
                uint8_t* row = dst + (ty * 8 + y) * stride + tx * 8 * 4;

                for (x = 0; x < 8; x += 2) {
                    uint8_t b = *ci4++;
                    const uint8_t* c0 = colors[b >> 4];
                    const uint8_t* c1 = colors[b & 0xF];

                    row[x * 4 + 0] = c0[0];
                    row[x * 4 + 1] = c0[1];
                    row[x * 4 + 2] = c0[2];
                    row[x * 4 + 3] = c0[3];
                    row[x * 4 + 4] = c1[0];
                    row[x * 4 + 5] = c1[1];
                    row[x * 4 + 6] = c1[2];
                    row[x * 4 + 7] = c1[3];
                }
            }
        }
    }
}

/* Sizes follow the window's shorter side, like the on-screen pad's
 * (pc_touch.c). With the pad, the corner below its X button and right of
 * its A button is free: the minimap fits there (pc/tests/test_minimap.c
 * checks it against the pad's layout). */
void pc_minimap_layout(int w, int h, int touch_pad, PcMinimapRect* r) {
    float u = (float)(w < h ? w : h);
    float acre = floorf((touch_pad ? 0.030f : 0.036f) * u);
    float margin = (touch_pad ? 0.02f : 0.025f) * u;

    if (acre < 8.0f) {
        acre = 8.0f;
    }
    r->acre = acre;
    r->x = floorf((float)w - margin - PC_MINIMAP_ACRES_X * acre);
    r->y = floorf((float)h - margin - PC_MINIMAP_ACRES_Z * acre);
}

float pc_minimap_width(const PcMinimapRect* r) {
    return PC_MINIMAP_ACRES_X * r->acre;
}

float pc_minimap_height(const PcMinimapRect* r) {
    return PC_MINIMAP_ACRES_Z * r->acre;
}

int pc_minimap_hit(const PcMinimapRect* r, float x, float y, float slack) {
    return x >= r->x - slack && x < r->x + pc_minimap_width(r) + slack && y >= r->y - slack &&
           y < r->y + pc_minimap_height(r) + slack;
}

void pc_minimap_world_to_screen(const PcMinimapRect* r, float wx, float wz, float* sx, float* sy) {
    float ax = wx / PC_MINIMAP_BLOCK_WORLD - 1.0f; /* acres from the town's left edge */
    float az = wz / PC_MINIMAP_BLOCK_WORLD - 1.0f;

    if (ax < 0.0f) ax = 0.0f;
    if (ax > PC_MINIMAP_ACRES_X) ax = PC_MINIMAP_ACRES_X;
    if (az < 0.0f) az = 0.0f;
    if (az > PC_MINIMAP_ACRES_Z) az = PC_MINIMAP_ACRES_Z;
    *sx = r->x + ax * r->acre;
    *sy = r->y + az * r->acre;
}

int pc_minimap_in_town(float wx, float wz) {
    float ax = wx / PC_MINIMAP_BLOCK_WORLD;
    float az = wz / PC_MINIMAP_BLOCK_WORLD;

    return ax >= 1.0f && ax < 1.0f + PC_MINIMAP_ACRES_X && az >= 1.0f && az < 1.0f + PC_MINIMAP_ACRES_Z;
}
