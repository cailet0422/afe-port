/* pc_minimap.c - see pc_minimap.h */
#include "pc_minimap.h"
#include "pc_minimap_core.h"
#include "pc_pause_menu.h"
#include "pc_settings.h"

#include "game.h"
#include "m_common_data.h"
#include "m_play.h"
#include "m_field_info.h"
#include "m_field_make.h"
#include "m_random_field_h.h"
#include "m_submenu.h"
#include "m_event.h"
#include "m_demo.h"
#include "m_msg.h"
#include "m_npc.h"
#include "m_actor.h"
#include "m_player.h"
#include "m_player_lib.h"
#include "m_scene_table.h"
#include "m_lib.h"

#include <dolphin/pad.h>
#include <math.h>

#include "pc_minimap_tables.inc"

#define ATLAS_SIZE 256
#define DISC_X 192 /* a round marker in the atlas, DISC_SIZE across */
#define DISC_SIZE 32
#define WHITE_X 240 /* a white square for plain shapes */
#define WHITE_SIZE 8
#define MAX_VERTICES 1024
#define FADE_STEP 0.125f /* the minimap fades in and out over 8 frames */
#define CLOSE_FRAMES 3   /* frames B is held to close the map */
#define ANGLE_TO_RAD (3.14159265f / 32768.0f)

extern u32 pc_frame_counter; /* pc_vi.c */

#define DECLARE_TEXTURE(name) extern u8 name[];
PC_MINIMAP_TEXTURES(DECLARE_TEXTURE)
#undef DECLARE_TEXTURE

#define TEXTURE_POINTER(name) name,
static const u8* const s_textures[PC_MINIMAP_TEX_NUM] = { PC_MINIMAP_TEXTURES(TEXTURE_POINTER) };
#undef TEXTURE_POINTER

/* --- What the game shows, read after each game frame ------------------ */

typedef struct {
    float x, z;
} Spot;

static int s_visible;             /* the minimap should be on screen */
static int s_map_open;            /* the game's map takes input */
static int s_toggle_request;      /* M, a tap or the script asked to open or close the map */
static u32 s_close_until;         /* PADRead adds B until this frame */
static int s_map_held;            /* the Map control was held at the last PADRead */
static Spot s_player;
static s16 s_player_angle;
static Spot s_houses[ANIMAL_NUM_MAX];
static int s_house_count;
static u8 s_tile_tex[PC_MINIMAP_ACRES];
static u8 s_tile_pal[PC_MINIMAP_ACRES];
static u32 s_tiles_hash;          /* of the pictures in the atlas; 0 = none yet */
static int s_tiles_dirty;
static float s_fade;              /* 0 hidden to 1 shown */
static int s_clock_left;          /* the game's clock is (or goes) on the left, where the minimap is */
static int s_clock_timer;
static float s_clock_blend;       /* 0 in the corner to 1 above the clock, gliding */

static GAME_PLAY* play_of(GAME* game) {
    if (game == NULL || game->exec != play_main) {
        return NULL;
    }
    return (GAME_PLAY*)game;
}

/* Outside in the town with nothing else going on */
static int shows_minimap(GAME_PLAY* play, PLAYER_ACTOR* player) {
    mMsg_Window_c* msg = mMsg_Get_base_window_p();
    xyz_t pos;

    if (player == NULL || g_pc_nes_active || Common_Get(reset_flag)) {
        return FALSE;
    }
    if (Save_Get(scene_no) != SCENE_FG || mFI_GET_TYPE(mFI_GetFieldId()) != mFI_FIELD_FG ||
        Common_Get(field_type) != mFI_FIELDTYPE2_FG || mFI_CheckInIsland()) {
        return FALSE;
    }
    if (play->submenu.process_status != mSM_PROCESS_WAIT || play->fb_fade_type != FADE_TYPE_NONE ||
        play->fb_wipe_mode != WIPE_MODE_NONE) {
        return FALSE;
    }
    if (mEv_CheckFirstIntro() || mDemo_CheckDemo() || (msg != NULL && msg->main_index != mMsg_INDEX_HIDE)) {
        return FALSE; /* the opening, talking, a message on screen */
    }
    pos = player->actor_class.world.position;
    return pc_minimap_in_town(pos.x, pos.z);
}

/* The game's mSM_submenu_ctrl for the X button (mSM_check_open_MAP), without
 * the condition that the player has the map. Called after game_main, where
 * the game would have opened another menu already this frame. */
static int open_map(GAME_PLAY* play) {
    Submenu* submenu = &play->submenu;
    PLAYER_ACTOR* player = GET_PLAYER_ACTOR(play);
    int bx, bz;

    if (submenu->process_status != mSM_PROCESS_WAIT || submenu->menu_type != mSM_OVL_NONE ||
        play->fb_fade_type != FADE_TYPE_NONE || play->fb_wipe_mode != WIPE_MODE_NONE) {
        return FALSE;
    }
    if (Common_Get(reset_flag) || submenu->start_refuse || submenu->start_refuse_timer != 0 ||
        !mPlib_able_submenu_type1((GAME*)play) || mEv_CheckFirstIntro()) {
        return FALSE;
    }
    if (player == NULL || Save_Get(scene_no) == SCENE_COTTAGE_MY || Save_Get(scene_no) == SCENE_COTTAGE_NPC) {
        return FALSE;
    }
    mFI_Wpos2BlockNum(&bx, &bz, player->actor_class.world.position);
    if (mFI_CheckBlockKind_OR(bx, bz, mRF_BLOCKKIND_OCEAN)) {
        return FALSE; /* the island, or at sea */
    }

    /* the player's own map, as with X (the map board's is mSM_open_submenu(.., 0, 0)) */
    mSM_open_submenu(submenu, mSM_OVL_MAP, 1, 0);
    player->a_btn_triggers_submenu = TRUE; /* mSM_Reset_player_btn_type2 */
    submenu->process_status = mSM_PROCESS_PREWAIT;
    submenu->mode = mSM_MODE_PRERENDER_INIT;
    SetGameFrame(1);
    printf("[MINIMAP] map opened\n");
    return TRUE;
}

static u32 tiles_hash(void) {
    u32 h = 0x811C9DC5u;
    int i;

    for (i = 0; i < PC_MINIMAP_ACRES; i++) {
        h = (h ^ (u32)(s_tile_tex[i] | (s_tile_pal[i] << 7))) * 0x01000193u;
    }
    return h | 1;
}

/* The game's clock moves to the bottom left once the player has been in
 * the bottom right of the screen for 10 frames, and back the same way
 * (banti_chk_disp_left, m_banti.c) */
static void follow_clock(GAME_PLAY* play, PLAYER_ACTOR* player) {
    xyz_t pos = player->actor_class.world.position;
    xyz_t screen;
    int bottom_right;

    Game_play_Projection_Trans(play, &pos, &screen);
    bottom_right = screen.x >= 180.0f && screen.y >= 150.0f;
    if (bottom_right != s_clock_left) {
        if (++s_clock_timer > 10) {
            s_clock_left = bottom_right;
            s_clock_timer = 0;
        }
    } else {
        s_clock_timer = 0;
    }
}

static void read_town(GAME_PLAY* play, PLAYER_ACTOR* player) {
    int i;
    u32 h;

    follow_clock(play, player);
    s_player.x = player->actor_class.world.position.x;
    s_player.z = player->actor_class.world.position.z;
    s_player_angle = player->actor_class.shape_info.rotation.y;

    s_house_count = 0;
    for (i = 0; i < ANIMAL_NUM_MAX; i++) {
        Animal_c* animal = Save_GetPointer(animals[i]);
        const Anmhome_c* home = &animal->home_info;

        if (mNpc_CheckFreeAnimalInfo(animal)) {
            continue;
        }
        s_houses[s_house_count].x = home->bx * PC_MINIMAP_BLOCK_WORLD + home->ux * 40.0f + 20.0f;
        s_houses[s_house_count].z = home->bz * PC_MINIMAP_BLOCK_WORLD + home->uz * 40.0f + 20.0f;
        s_house_count++;
    }

    pc_minimap_pick_tiles(g_block_type_p, Save_Get(bridge.exists), Save_Get(bridge.block_x),
                          Save_Get(bridge.block_z), s_tile_tex, s_tile_pal);
    h = tiles_hash();
    if (h != s_tiles_hash) {
        s_tiles_hash = h;
        s_tiles_dirty = 1;
    }
}

void pc_minimap_game_frame(GAME* game) {
    GAME_PLAY* play = play_of(game);
    PLAYER_ACTOR* player = play != NULL ? GET_PLAYER_ACTOR(play) : NULL;
    int toggle = s_toggle_request;

    s_toggle_request = 0;
    s_map_open = play != NULL && play->submenu.menu_type == mSM_OVL_MAP &&
                 play->submenu.process_status == mSM_PROCESS_PLAY;

    if (toggle && play != NULL && !g_pc_paused) {
        if (s_map_open) {
            s_close_until = pc_frame_counter + CLOSE_FRAMES; /* B, as the player would close it */
        } else {
            open_map(play);
        }
    }

    s_visible = g_pc_settings.minimap && play != NULL && shows_minimap(play, player);
    if (s_visible) {
        read_town(play, player);
    }
}

/* --- Input ----------------------------------------------------------- */

void pc_minimap_pad(int map_held, u16* buttons) {
    if (map_held && !s_map_held) {
        s_toggle_request = 1;
    }
    s_map_held = map_held;
    /* by frames, not reads: the pad is read twice a frame (JUTGamePad, padmgr) */
    if ((s32)(s_close_until - pc_frame_counter) > 0) {
        *buttons |= PAD_BUTTON_B;
    }
}

void pc_minimap_request_toggle(void) {
    s_toggle_request = 1;
}

int pc_minimap_map_open(void) {
    return s_map_open;
}

/* Where the game shows its clock (m_banti.c) on the left, in its 320 x 240
 * screen: it moves there from the bottom right while the player is in the
 * bottom right of the screen (follow_clock). The minimap then makes room. */
#define CLOCK_UI_X0 0.0f
#define CLOCK_UI_Y0 164.0f
#define CLOCK_UI_X1 132.0f
#define CLOCK_UI_Y1 232.0f

static void layout(PcMinimapRect* r) {
    float avoid[4];
    float left, right, top, unused;
    PcMinimapRect clear;

    pc_minimap_layout(g_pc_window_w, g_pc_window_h, g_pc_settings.touch_controls, NULL, r);
    if (s_clock_blend <= 0.0f) {
        return; /* the corner; the clock is on the right */
    }
    /* the game's screen in window pixels: narrower than the window when
     * a wide window shows it at 4:3 (pc_gx.c) */
    pc_gx_window_to_ui(0.0f, 0.0f, &left, &top);
    pc_gx_window_to_ui(1.0f, 1.0f, &right, &unused);
    if (right - left < 1.0f) {
        return;
    }
    /* (pc_gx_window_to_ui counts the 640 x 480 framebuffer) */
    avoid[0] = (CLOCK_UI_X0 * (PC_GC_WIDTH / 320.0f) - left) / (right - left) * g_pc_window_w;
    avoid[2] = (CLOCK_UI_X1 * (PC_GC_WIDTH / 320.0f) - left) / (right - left) * g_pc_window_w;
    avoid[1] = CLOCK_UI_Y0 / 240.0f * g_pc_window_h;
    avoid[3] = CLOCK_UI_Y1 / 240.0f * g_pc_window_h;
    (void)top;
    pc_minimap_layout(g_pc_window_w, g_pc_window_h, g_pc_settings.touch_controls, avoid, &clear);
    /* glides there while the clock comes over (a wide window has room for
     * both: then the two places are the same) */
    r->x += (clear.x - r->x) * s_clock_blend;
    r->y = floorf(r->y + (clear.y - r->y) * s_clock_blend);
    r->acre += (clear.acre - r->acre) * s_clock_blend;
}

int pc_minimap_at(float x, float y) {
    PcMinimapRect r;

    if (g_pc_paused || !s_visible || s_fade <= 0.5f) {
        return 0;
    }
    layout(&r);
    return pc_minimap_hit(&r, x * g_pc_window_w, y * g_pc_window_h, r.acre * 0.25f);
}

int pc_minimap_touch(float x, float y) {
    if (pc_minimap_at(x, y) || (s_map_open && !g_pc_paused)) {
        s_toggle_request = 1; /* the minimap opens the map; with it open, any tap closes it */
        return 1;
    }
    return 0;
}

/* --- Drawing --------------------------------------------------------- */

typedef struct {
    float x, y; /* pixels */
    float u, v; /* atlas */
    u8 color[4];
} Vertex;

static Vertex s_vertices[MAX_VERTICES];
static int s_vertex_count;
static GLuint s_program, s_vao, s_vbo, s_texture;
static GLint s_size_uniform, s_tex_uniform;
static u8 s_atlas[ATLAS_SIZE * ATLAS_SIZE * 4];

static const char* s_vertex_shader =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "uniform vec2 u_size;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos.x / u_size.x * 2.0 - 1.0, 1.0 - a_pos.y / u_size.y * 2.0, 0.0, 1.0);\n"
    "    v_uv = a_uv;\n"
    "    v_color = a_color;\n"
    "}\n";

static const char* s_fragment_shader =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "in vec4 v_color;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag_color;\n"
    "void main() {\n"
    "    frag_color = texture(u_tex, v_uv) * v_color;\n"
    "}\n";

static GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    GLint ok = 0;

    pc_gl_shader_source(shader, source);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "[MINIMAP] shader: %s\n", log);
    }
    return shader;
}

/* The parts of the atlas that do not change: a white square and a disc
 * with a soft edge */
static void atlas_fixed_parts(void) {
    int x, y;

    for (y = 0; y < WHITE_SIZE; y++) {
        memset(&s_atlas[(y * ATLAS_SIZE + WHITE_X) * 4], 0xFF, WHITE_SIZE * 4);
    }
    for (y = 0; y < DISC_SIZE; y++) {
        for (x = 0; x < DISC_SIZE; x++) {
            float dx = x + 0.5f - DISC_SIZE * 0.5f;
            float dy = y + 0.5f - DISC_SIZE * 0.5f;
            float d = sqrtf(dx * dx + dy * dy) - (DISC_SIZE * 0.5f - 1.5f);
            float a = d <= -1.0f ? 1.0f : d >= 1.0f ? 0.0f : 0.5f - d * 0.5f;
            u8* p = &s_atlas[(y * ATLAS_SIZE + DISC_X + x) * 4];

            p[0] = p[1] = p[2] = 0xFF;
            p[3] = (u8)lrintf(a * 255.0f);
        }
    }
}

static int init_gl(void) {
    GLuint vs = compile(GL_VERTEX_SHADER, s_vertex_shader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, s_fragment_shader);
    GLint ok = 0;

    s_program = glCreateProgram();
    glAttachShader(s_program, vs);
    glAttachShader(s_program, fs);
    glLinkProgram(s_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glGetProgramiv(s_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        fprintf(stderr, "[MINIMAP] cannot build the minimap's shader\n");
        return 0;
    }
    s_size_uniform = glGetUniformLocation(s_program, "u_size");
    s_tex_uniform = glGetUniformLocation(s_program, "u_tex");

    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, color));

    atlas_fixed_parts();
    glGenTextures(1, &s_texture);
    glBindTexture(GL_TEXTURE_2D, s_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    s_tiles_dirty = 1;
    return 1;
}

/* The 30 acre pictures side by side as on the map, top left of the atlas */
static void upload_atlas(void) {
    GLint row_length = 0, alignment = 4;
    int i;

    for (i = 0; i < PC_MINIMAP_ACRES; i++) {
        int ax = i % PC_MINIMAP_ACRES_X;
        int az = i / PC_MINIMAP_ACRES_X;
        u8* dst = &s_atlas[((az * PC_MINIMAP_TILE) * ATLAS_SIZE + ax * PC_MINIMAP_TILE) * 4];

        pc_minimap_decode_tile(s_textures[s_tile_tex[i]], s_tile_pal[i], dst, ATLAS_SIZE * 4);
    }
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ATLAS_SIZE, ATLAS_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_atlas);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
    glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    s_tiles_dirty = 0;
}

static void vertex(float x, float y, float u, float v, const u8* color) {
    Vertex* p;

    if (s_vertex_count >= MAX_VERTICES) {
        return;
    }
    p = &s_vertices[s_vertex_count++];
    p->x = x;
    p->y = y;
    p->u = u / ATLAS_SIZE;
    p->v = v / ATLAS_SIZE;
    memcpy(p->color, color, 4);
}

/* A rectangle showing the atlas area u0,v0 - u1,v1 (texels) */
static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const u8* color) {
    vertex(x0, y0, u0, v0, color);
    vertex(x1, y0, u1, v0, color);
    vertex(x1, y1, u1, v1, color);
    vertex(x0, y0, u0, v0, color);
    vertex(x1, y1, u1, v1, color);
    vertex(x0, y1, u0, v1, color);
}

static void fill_rect(float x0, float y0, float x1, float y1, const u8* color) {
    const float c = WHITE_X + WHITE_SIZE * 0.5f;

    quad(x0, y0, x1, y1, c, WHITE_SIZE * 0.5f, c, WHITE_SIZE * 0.5f, color);
}

static void disc(float x, float y, float r, const u8* color) {
    quad(x - r, y - r, x + r, y + r, DISC_X, 0, DISC_X + DISC_SIZE, DISC_SIZE, color);
}

static void triangle(float x0, float y0, float x1, float y1, float x2, float y2, const u8* color) {
    const float c = WHITE_X + WHITE_SIZE * 0.5f;

    vertex(x0, y0, c, WHITE_SIZE * 0.5f, color);
    vertex(x1, y1, c, WHITE_SIZE * 0.5f, color);
    vertex(x2, y2, c, WHITE_SIZE * 0.5f, color);
}

static void faded(u8* out, u8 r, u8 g, u8 b, u8 a) {
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = (u8)lrintf(a * s_fade);
}

static void build(const PcMinimapRect* r) {
    float w = pc_minimap_width(r);
    float h = pc_minimap_height(r);
    float border = r->acre * 0.08f < 2.0f ? 2.0f : r->acre * 0.08f;
    float line = r->acre * 0.03f < 1.0f ? 1.0f : r->acre * 0.03f;
    float house_r = r->acre * 0.10f < 2.5f ? 2.5f : r->acre * 0.10f;
    float player_r = r->acre * 0.16f < 3.5f ? 3.5f : r->acre * 0.16f;
    float px, py, dx, dy, nose;
    u8 frame[4], paper[4], tiles[4], grid[4], house_edge[4], house[4], player_edge[4], player[4];
    int i;

    faded(frame, 0x3A, 0x2C, 0x1E, 0xD8);
    faded(paper, 0xE8, 0xDE, 0xBE, 0xFF);
    faded(tiles, 0xFF, 0xFF, 0xFF, 0xF0);
    faded(grid, 0x30, 0x24, 0x18, 0x50);
    faded(house_edge, 0x40, 0x28, 0x10, 0xFF);
    faded(house, 0xFF, 0xB0, 0x30, 0xFF);
    faded(player_edge, 0xFF, 0xFF, 0xFF, 0xFF);
    faded(player, 0xE8, 0x30, 0x20, 0xFF);

    s_vertex_count = 0;
    fill_rect(r->x - border, r->y - border, r->x + w + border, r->y + h + border, frame);
    fill_rect(r->x, r->y, r->x + w, r->y + h, paper);
    /* the pictures sit side by side in the atlas, so one rectangle shows them
     * all; half a texel in at the edges keeps the filter inside them */
    quad(r->x, r->y, r->x + w, r->y + h, 0.5f, 0.5f, PC_MINIMAP_ACRES_X * PC_MINIMAP_TILE - 0.5f,
         PC_MINIMAP_ACRES_Z * PC_MINIMAP_TILE - 0.5f, tiles);
    for (i = 1; i < PC_MINIMAP_ACRES_X; i++) {
        float x = r->x + i * r->acre;

        fill_rect(x - line * 0.5f, r->y, x + line * 0.5f, r->y + h, grid);
    }
    for (i = 1; i < PC_MINIMAP_ACRES_Z; i++) {
        float y = r->y + i * r->acre;

        fill_rect(r->x, y - line * 0.5f, r->x + w, y + line * 0.5f, grid);
    }

    for (i = 0; i < s_house_count; i++) {
        pc_minimap_world_to_screen(r, s_houses[i].x, s_houses[i].z, &px, &py);
        disc(px, py, house_r, house_edge);
        disc(px, py, house_r * 0.7f, house);
    }

    /* the player, and a point the way they face (angle 0 faces +z, down the map) */
    pc_minimap_world_to_screen(r, s_player.x, s_player.z, &px, &py);
    dx = sinf(s_player_angle * ANGLE_TO_RAD);
    dy = cosf(s_player_angle * ANGLE_TO_RAD);
    nose = player_r * 1.9f;
    triangle(px + dx * nose, py + dy * nose, px - dy * player_r * 0.85f, py + dx * player_r * 0.85f,
             px + dy * player_r * 0.85f, py - dx * player_r * 0.85f, player_edge);
    disc(px, py, player_r, player_edge);
    disc(px, py, player_r * 0.72f, player);
}

extern void pc_gx_restore_after_nes(void); /* rebinds the game's GL state after foreign drawing */

void pc_minimap_draw(void) {
    static int gl_state = 0; /* 0 not built, 1 ready, -1 failed */
    PcMinimapRect r;
    GLint active_texture = GL_TEXTURE0, bound_texture = 0;

    if (s_visible && !g_pc_paused) {
        s_fade = s_fade + FADE_STEP > 1.0f ? 1.0f : s_fade + FADE_STEP;
        s_clock_blend += ((s_clock_left ? 1.0f : 0.0f) - s_clock_blend) * 0.2f;
        if (s_clock_blend < 0.01f) s_clock_blend = 0.0f;
        if (s_clock_blend > 0.99f) s_clock_blend = 1.0f;
    } else {
        s_fade = 0.0f; /* gone at once: a menu or a message is coming */
    }
    if (s_fade <= 0.0f || g_pc_skip_draw || s_tiles_hash == 0) {
        return;
    }
    if (gl_state == 0) {
        gl_state = init_gl() ? 1 : -1;
    }
    if (gl_state < 0) {
        return;
    }

    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound_texture);
    glBindTexture(GL_TEXTURE_2D, s_texture);
    if (s_tiles_dirty) {
        upload_atlas();
    }

    layout(&r);
    build(&r);

    glViewport(0, 0, g_pc_window_w, g_pc_window_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    /* keep the frame's alpha: some Android surfaces show it through */
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glUseProgram(s_program);
    glUniform2f(s_size_uniform, (float)g_pc_window_w, (float)g_pc_window_h);
    glUniform1i(s_tex_uniform, 0);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, s_vertex_count * sizeof(Vertex), s_vertices, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, s_vertex_count);

    glBindTexture(GL_TEXTURE_2D, (GLuint)bound_texture);
    glActiveTexture((GLenum)active_texture);
    pc_gx_restore_after_nes();
}
