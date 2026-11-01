/* Unit tests for the minimap's game-independent parts (pc/src/pc_minimap_core.c):
 * which acre picture each map square gets, decoding the pictures, where the
 * minimap sits and how world positions land on it. */
#include "pc_minimap_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PC_MINIMAP_TABLE_DATA
#include "pc_minimap_tables.inc"

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/* Block types by name, from the generated table's comments being the
 * enum's order (m_field_make.h) */
enum {
    BT_BORDER_CLIFF_TOP = 0,
    BT_BORDER_CLIFF_LEFT = 2,
    BT_BORDER_CLIFF_RIGHT = 4,
    BT_TRACKS_RIVER = 13,
    BT_PLAYER_HOUSE = 14,
    BT_RIVER_SOUTH = 40,
    BT_RIVER_SOUTH_BRIDGE = 47,
    BT_BEACH = 63,
    BT_TRACKS_SHOP = 65,
};

/* A town: border all round, grass inside, a few landmarks */
static void make_field(uint8_t field[PC_MINIMAP_BLOCKS_X * PC_MINIMAP_BLOCKS_Z]) {
    int bx, bz;

    for (bz = 0; bz < PC_MINIMAP_BLOCKS_Z; bz++) {
        for (bx = 0; bx < PC_MINIMAP_BLOCKS_X; bx++) {
            uint8_t t = PC_MINIMAP_BT_FLAT;

            if (bz == 0) t = BT_BORDER_CLIFF_TOP;
            else if (bx == 0) t = BT_BORDER_CLIFF_LEFT;
            else if (bx == PC_MINIMAP_BLOCKS_X - 1) t = BT_BORDER_CLIFF_RIGHT;
            field[bz * PC_MINIMAP_BLOCKS_X + bx] = t;
        }
    }
    field[1 * PC_MINIMAP_BLOCKS_X + 3] = BT_TRACKS_SHOP;  /* acre 0,2 */
    field[3 * PC_MINIMAP_BLOCKS_X + 2] = BT_PLAYER_HOUSE; /* acre 2,1 */
    field[4 * PC_MINIMAP_BLOCKS_X + 4] = BT_RIVER_SOUTH;  /* acre 3,3 */
    for (bx = 1; bx <= PC_MINIMAP_ACRES_X; bx++) {
        field[6 * PC_MINIMAP_BLOCKS_X + bx] = BT_BEACH; /* the last row */
    }
}

static void test_tables(void) {
    int i;

    CHECK(PC_MINIMAP_BT_NUM > BT_TRACKS_SHOP);
    CHECK(pc_minimap_texture_count() == PC_MINIMAP_TEX_NUM);
    for (i = 0; i < PC_MINIMAP_BT_NUM; i++) {
        CHECK(s_type_tex[i] < PC_MINIMAP_TEX_NUM);
        CHECK(s_type_pal[i] <= 1);
        CHECK(s_type_bridge[i] == PC_MINIMAP_BT_NONE || s_type_bridge[i] < PC_MINIMAP_BT_NUM);
    }
    /* m_map_ovl.c: the river's bridge picture, the shop on palette 1 */
    CHECK(s_type_bridge[BT_RIVER_SOUTH] == BT_RIVER_SOUTH_BRIDGE);
    CHECK(s_type_bridge[BT_TRACKS_RIVER] != PC_MINIMAP_BT_NONE);
    CHECK(s_type_bridge[PC_MINIMAP_BT_FLAT] == PC_MINIMAP_BT_NONE);
    CHECK(s_type_pal[BT_TRACKS_SHOP] == 1);
    CHECK(s_type_pal[PC_MINIMAP_BT_FLAT] == 0);
    CHECK(s_palettes[0][0] == 0x0000 && s_palettes[0][13] == 0xFFFF);
}

static void test_pick(void) {
    uint8_t field[PC_MINIMAP_BLOCKS_X * PC_MINIMAP_BLOCKS_Z];
    uint8_t tex[PC_MINIMAP_ACRES], pal[PC_MINIMAP_ACRES];
    int i;

    make_field(field);
    pc_minimap_pick_tiles(field, 0, 0, 0, tex, pal);
    CHECK(tex[2] == s_type_tex[BT_TRACKS_SHOP] && pal[2] == 1);
    CHECK(tex[2 * PC_MINIMAP_ACRES_X + 1] == s_type_tex[BT_PLAYER_HOUSE]);
    CHECK(tex[3 * PC_MINIMAP_ACRES_X + 3] == s_type_tex[BT_RIVER_SOUTH]);
    CHECK(tex[0] == s_type_tex[PC_MINIMAP_BT_FLAT] && pal[0] == 0);
    for (i = 0; i < PC_MINIMAP_ACRES_X; i++) {
        CHECK(tex[5 * PC_MINIMAP_ACRES_X + i] == s_type_tex[BT_BEACH]);
    }

    /* the bridge only in its own acre */
    pc_minimap_pick_tiles(field, 1, 4, 4, tex, pal);
    CHECK(tex[3 * PC_MINIMAP_ACRES_X + 3] == s_type_tex[BT_RIVER_SOUTH_BRIDGE]);
    pc_minimap_pick_tiles(field, 1, 3, 4, tex, pal);
    CHECK(tex[3 * PC_MINIMAP_ACRES_X + 3] == s_type_tex[BT_RIVER_SOUTH]);
    pc_minimap_pick_tiles(field, 0, 4, 4, tex, pal);
    CHECK(tex[3 * PC_MINIMAP_ACRES_X + 3] == s_type_tex[BT_RIVER_SOUTH]);

    /* a border block inside is skipped and the rest move up, grass at the end
     * (as the game's map does it) */
    field[1 * PC_MINIMAP_BLOCKS_X + 1] = BT_BORDER_CLIFF_LEFT;
    pc_minimap_pick_tiles(field, 0, 0, 0, tex, pal);
    CHECK(tex[1] == s_type_tex[BT_TRACKS_SHOP]);
    CHECK(tex[PC_MINIMAP_ACRES - 1] == s_type_tex[PC_MINIMAP_BT_FLAT]);

    /* a type past the table is drawn as grass, not read out of bounds */
    make_field(field);
    field[2 * PC_MINIMAP_BLOCKS_X + 1] = 200;
    pc_minimap_pick_tiles(field, 0, 0, 0, tex, pal);
    CHECK(tex[PC_MINIMAP_ACRES_X] == s_type_tex[PC_MINIMAP_BT_FLAT]);
}

static void test_decode(void) {
    uint8_t ci4[PC_MINIMAP_TILE * PC_MINIMAP_TILE / 2];
    uint8_t rgba[PC_MINIMAP_TILE * PC_MINIMAP_TILE * 4];
    uint8_t c[4];
    int x, y;

    pc_minimap_rgb5a3(0xFFFF, c);
    CHECK(c[0] == 255 && c[1] == 255 && c[2] == 255 && c[3] == 255);
    pc_minimap_rgb5a3(0x0000, c);
    CHECK(c[3] == 0);
    pc_minimap_rgb5a3(0x8000 | (31 << 10), c);
    CHECK(c[0] == 255 && c[1] == 0 && c[2] == 0 && c[3] == 255);
    pc_minimap_rgb5a3(0x7F00, c); /* alpha 7/7, red 15/15 */
    CHECK(c[3] == 255 && c[0] == 255 && c[1] == 0);

    /* the colour index is the texel's position in its 8x8 tile, so the
     * picture's layout shows: tile n is bytes 32n.., left texel high nibble */
    memset(ci4, 0, sizeof(ci4));
    for (y = 0; y < PC_MINIMAP_TILE; y++) {
        for (x = 0; x < PC_MINIMAP_TILE; x++) {
            int tile = (y / 8) * (PC_MINIMAP_TILE / 8) + x / 8;
            int off = tile * 32 + (y % 8) * 4 + (x % 8) / 2;
            int index = 13; /* white */

            if (x == 9 && y == 2) index = 0; /* clear */
            ci4[off] |= (uint8_t)((x & 1) ? index : index << 4);
        }
    }
    pc_minimap_decode_tile(ci4, 0, rgba, PC_MINIMAP_TILE * 4);
    CHECK(rgba[(2 * PC_MINIMAP_TILE + 9) * 4 + 3] == 0);
    CHECK(rgba[(2 * PC_MINIMAP_TILE + 8) * 4 + 3] == 255);
    CHECK(rgba[(2 * PC_MINIMAP_TILE + 10) * 4 + 3] == 255);
    CHECK(rgba[(3 * PC_MINIMAP_TILE + 9) * 4 + 3] == 255);
    CHECK(rgba[(31 * PC_MINIMAP_TILE + 31) * 4 + 0] == 255);

    /* palette 1's index 3 differs from palette 0's */
    memset(ci4, 0x33, sizeof(ci4));
    pc_minimap_decode_tile(ci4, 0, rgba, PC_MINIMAP_TILE * 4);
    pc_minimap_rgb5a3(s_palettes[0][3], c);
    CHECK(memcmp(rgba, c, 4) == 0);
    pc_minimap_decode_tile(ci4, 1, rgba, PC_MINIMAP_TILE * 4);
    pc_minimap_rgb5a3(s_palettes[1][3], c);
    CHECK(memcmp(rgba, c, 4) == 0);
}

/* The on-screen pad's left side as pc_touch.c lays it out: the stick's
 * resting circle (drawn there until a thumb lands) and the D-pad, with the
 * 1.2 of the pad's hit test for the buttons. In units of the shorter side,
 * x from the left, y from the bottom for the stick and from the top for the
 * D-pad. */
static int overlaps_circle(const PcMinimapRect* r, float cx, float cy, float rad) {
    float nx = cx < r->x ? r->x : cx > r->x + pc_minimap_width(r) ? r->x + pc_minimap_width(r) : cx;
    float ny = cy < r->y ? r->y : cy > r->y + pc_minimap_height(r) ? r->y + pc_minimap_height(r) : cy;

    return (nx - cx) * (nx - cx) + (ny - cy) * (ny - cy) < rad * rad;
}

static int overlaps_pad(const PcMinimapRect* r, int w, int h) {
    static const float dpad[4][2] = { { 0.17f, 0.32f }, { 0.17f, 0.48f }, { 0.09f, 0.40f }, { 0.25f, 0.40f } };
    float u = (float)(w < h ? w : h);
    int i;

    if (overlaps_circle(r, 0.32f * u, h - 0.30f * u, 0.16f * u)) {
        return 1;
    }
    for (i = 0; i < 4; i++) {
        if (overlaps_circle(r, dpad[i][0] * u, dpad[i][1] * u, 0.045f * u * 1.2f)) {
            return 1;
        }
    }
    return 0;
}

static void test_layout(void) {
    static const int sizes[][2] = { { 640, 480 }, { 1280, 720 }, { 1920, 1080 }, { 2400, 1080 },
                                    { 3200, 1440 }, { 1080, 2400 }, { 800, 600 }, { 2560, 1600 } };
    unsigned i;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        int w = sizes[i][0], h = sizes[i][1];
        PcMinimapRect pc, touch;

        pc_minimap_layout(w, h, 0, NULL, &pc);
        pc_minimap_layout(w, h, 1, NULL, &touch);
        /* inside the window, in its bottom left quarter */
        CHECK(pc.x > 0 && pc.x + pc_minimap_width(&pc) < w / 2 && pc.y > h / 2);
        CHECK(pc.y + pc_minimap_height(&pc) < h);
        CHECK(touch.x > 0 && touch.x + pc_minimap_width(&touch) < w / 2 && touch.y > h / 2);
        CHECK(touch.y + pc_minimap_height(&touch) < h);
        /* whole pixels, 5 x 6 acres */
        CHECK(pc.acre == floorf(pc.acre) && pc.x == floorf(pc.x));
        CHECK(pc_minimap_width(&pc) == 5 * pc.acre && pc_minimap_height(&pc) == 6 * pc.acre);
        /* with the pad: clear of its buttons, with the finger slack too */
        CHECK(!overlaps_pad(&touch, w, h));
        {
            PcMinimapRect slack = touch;
            float s = touch.acre * 0.25f;

            slack.x -= s;
            slack.y -= s;
            slack.acre += 0.0f;
            CHECK(!overlaps_pad(&slack, w, h));
        }
        CHECK(touch.acre <= pc.acre);
    }
}

/* The game's clock on the left in a window of w x h that shows the game at
 * 4:3 in the middle (pc_minimap.c's layout()) */
static void clock_rect(int w, int h, float out[4]) {
    float game_w = h * 4.0f / 3.0f < w ? h * 4.0f / 3.0f : (float)w;
    float left = (w - game_w) * 0.5f;

    out[0] = left;
    out[2] = left + 132.0f / 320.0f * game_w;
    out[1] = 164.0f / 240.0f * h;
    out[3] = 232.0f / 240.0f * h;
}

static int overlaps_rect(const PcMinimapRect* r, const float* a) {
    return r->x < a[2] && r->x + pc_minimap_width(r) > a[0] && r->y < a[3] && r->y + pc_minimap_height(r) > a[1];
}

static void test_clock(void) {
    static const int sizes[][2] = { { 640, 480 }, { 1024, 768 }, { 1280, 720 }, { 1920, 1080 },
                                    { 2400, 1080 }, { 2560, 1600 }, { 800, 600 }, { 1280, 1024 } };
    unsigned i;

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        int w = sizes[i][0], h = sizes[i][1];
        float clock[4];
        PcMinimapRect free_r, pc, touch;

        clock_rect(w, h, clock);
        pc_minimap_layout(w, h, 0, NULL, &free_r);
        pc_minimap_layout(w, h, 0, clock, &pc);
        pc_minimap_layout(w, h, 1, clock, &touch);
        /* never on the clock without the pad, and still on the left side */
        CHECK(!overlaps_rect(&pc, clock));
        CHECK(pc.x >= 0 && pc.y >= 0 && pc.x + pc_minimap_width(&pc) < w / 2);
        CHECK(pc.acre >= free_r.acre * 0.75f);
        /* with the pad: not on the clock where the window leaves room, never on the pad */
        CHECK(!overlaps_pad(&touch, w, h));
        if ((float)w / h >= 16.0f / 10.0f) {
            CHECK(!overlaps_rect(&touch, clock));
        }
    }
    /* wide: still the bottom left corner, full size */
    {
        float clock[4];
        PcMinimapRect free_r, r;

        clock_rect(1920, 1080, clock);
        pc_minimap_layout(1920, 1080, 0, NULL, &free_r);
        pc_minimap_layout(1920, 1080, 0, clock, &r);
        CHECK(r.y == free_r.y && r.x == free_r.x && r.acre == free_r.acre);
    }
    /* 4:3: above the clock, at the left */
    {
        float clock[4];
        PcMinimapRect r;

        clock_rect(640, 480, clock);
        pc_minimap_layout(640, 480, 0, clock, &r);
        CHECK(r.y + pc_minimap_height(&r) <= clock[1] && r.x < 40);
    }
}

static void test_positions(void) {
    PcMinimapRect r = { 100.0f, 50.0f, 20.0f };
    float x, y;

    /* the town's top left corner is block 1,1 */
    pc_minimap_world_to_screen(&r, 640.0f, 640.0f, &x, &y);
    CHECK(fabsf(x - 100.0f) < 0.01f && fabsf(y - 50.0f) < 0.01f);
    /* the middle of acre 2,3 (block 3,4) */
    pc_minimap_world_to_screen(&r, 3.5f * 640.0f, 4.5f * 640.0f, &x, &y);
    CHECK(fabsf(x - (100.0f + 2.5f * 20.0f)) < 0.01f && fabsf(y - (50.0f + 3.5f * 20.0f)) < 0.01f);
    /* outside the town: held at the edge */
    pc_minimap_world_to_screen(&r, 0.0f, 9000.0f, &x, &y);
    CHECK(x == 100.0f && y == 50.0f + 6 * 20.0f);

    CHECK(pc_minimap_in_town(640.0f, 640.0f));
    CHECK(pc_minimap_in_town(6.0f * 640.0f - 1.0f, 7.0f * 640.0f - 1.0f));
    CHECK(!pc_minimap_in_town(639.0f, 1000.0f));       /* the left border */
    CHECK(!pc_minimap_in_town(1000.0f, 7.0f * 640.0f)); /* the sea, the island */
    CHECK(!pc_minimap_in_town(6.0f * 640.0f, 1000.0f)); /* the right border */

    CHECK(pc_minimap_hit(&r, 100.0f, 50.0f, 0.0f));
    CHECK(pc_minimap_hit(&r, 199.0f, 169.0f, 0.0f));
    CHECK(!pc_minimap_hit(&r, 200.0f, 100.0f, 0.0f));
    CHECK(pc_minimap_hit(&r, 203.0f, 100.0f, 5.0f));
    CHECK(!pc_minimap_hit(&r, 99.0f, 100.0f, 0.0f));
}

int main(void) {
    test_tables();
    test_pick();
    test_decode();
    test_layout();
    test_clock();
    test_positions();
    if (failures == 0) {
        printf("minimap: all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
