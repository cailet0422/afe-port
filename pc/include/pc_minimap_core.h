/* pc_minimap_core.h - the parts of the minimap that need neither the game nor
 * a window: which acre picture goes where, decoding the pictures, the
 * minimap's place on the screen. pc_minimap.c puts them together with the
 * game's state; pc/tests/test_minimap.c tests them. */
#ifndef PC_MINIMAP_CORE_H
#define PC_MINIMAP_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The town's acres as the game's map shows them (FG_BLOCK_X_NUM x
 * FG_BLOCK_Z_NUM), inside the field's 7 x 10 blocks (BLOCK_X_NUM x
 * BLOCK_Z_NUM) that also count the border and the sea */
#define PC_MINIMAP_ACRES_X 5
#define PC_MINIMAP_ACRES_Z 6
#define PC_MINIMAP_ACRES (PC_MINIMAP_ACRES_X * PC_MINIMAP_ACRES_Z)
#define PC_MINIMAP_BLOCKS_X 7
#define PC_MINIMAP_BLOCKS_Z 10
#define PC_MINIMAP_BLOCK_WORLD 640.0f /* world units per acre (16 units of 40) */
#define PC_MINIMAP_TILE 32            /* texels per acre picture, each way */

/* Picks the picture (index into PC_MINIMAP_TEXTURES, pc_minimap_tables.inc)
 * and the palette (0 or 1) of every acre, row by row from the top left, the
 * way the game's map does (mMP_set_map_texture_pal): block_type is the
 * field's 70 block types (g_block_type_p); an acre with the bridge uses the
 * bridged picture. */
void pc_minimap_pick_tiles(const uint8_t* block_type, int bridge_exists, int bridge_bx, int bridge_bz,
                           uint8_t tex[PC_MINIMAP_ACRES], uint8_t pal[PC_MINIMAP_ACRES]);

/* Number of distinct acre pictures */
int pc_minimap_texture_count(void);

/* One acre picture (32x32 GameCube CI4, 8x8 tiles) with palette pal into
 * RGBA8 at dst, stride bytes per row */
void pc_minimap_decode_tile(const uint8_t* ci4, int pal, uint8_t* dst, int stride);

/* RGB5A3 to RGBA8 */
void pc_minimap_rgb5a3(uint16_t v, uint8_t rgba[4]);

/* Where the minimap goes in a w x h window, in pixels: the top left corner
 * and the size of one acre (the minimap is 5 x 6 acres). It sits in the
 * bottom right corner; with the on-screen pad (touch_pad) it is smaller and
 * keeps clear of the pad's A/B/X/Y buttons.
 *
 * avoid (may be NULL) is x0, y0, x1, y1 of something it must not cover (the
 * game's clock, pc_minimap.c): then it gets narrower to fit right of it if
 * it stays big enough, else it moves up above it (not with the pad, whose X
 * button is there; then it fits right of it as it can). */
typedef struct {
    float x, y;
    float acre;
} PcMinimapRect;

void pc_minimap_layout(int w, int h, int touch_pad, const float* avoid, PcMinimapRect* r);
float pc_minimap_width(const PcMinimapRect* r);
float pc_minimap_height(const PcMinimapRect* r);

/* Is (x, y) (pixels) on the minimap, with a little slack for fingers */
int pc_minimap_hit(const PcMinimapRect* r, float x, float y, float slack);

/* A world position (x, z) to minimap pixels, kept inside the minimap */
void pc_minimap_world_to_screen(const PcMinimapRect* r, float wx, float wz, float* sx, float* sy);

/* Is a world position (x, z) inside the town's acres (not the border, the
 * sea or the island) */
int pc_minimap_in_town(float wx, float wz);

#ifdef __cplusplus
}
#endif

#endif /* PC_MINIMAP_CORE_H */
