/* pc_settings.c - runtime settings loaded from settings.ini */
#include "pc_settings.h"
#include "pc_platform.h"
#include <limits.h>

PCSettings g_pc_settings = {
    .window_width  = PC_SCREEN_WIDTH,
    .window_height = PC_SCREEN_HEIGHT,
#ifdef __ANDROID__
    .fullscreen    = 2,
    .vsync         = 1,
    .msaa          = 0,
#else
    .fullscreen    = 0,
    .vsync         = 0,
    .msaa          = 4,
#endif
    .preload_textures = 0,
    .disable_resetti = 1,
    .autosave = 1,
    .autosave_minutes = 1,
    .nes_aspect = 1,
    .master_volume = 100,
#ifdef __ANDROID__
    .text_input = 1,
    .touch_controls = 1,
#else
    .text_input = 0,
    .touch_controls = 0,
#endif
    .minimap = 1,
    .seamless_blocks = 1,
    .auto_update = 1,
    .hd_textures = 1,
    .specialized_shaders = 1,
#ifdef __ANDROID__
    .hd_quality = 1,
#endif
    .mp_port = 47070,
};

static const char* SETTINGS_FILE = "settings.ini";

static const char* skip_ws(const char* s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void trim_end(char* s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' ||
                       s[len-1] == '\r' || s[len-1] == '\n')) {
        s[--len] = '\0';
    }
}

/* Settings that take text */
static const struct {
    const char* key;
    char* field;
    size_t size;
} s_text_settings[] = {
    { "rom_dir", g_pc_settings.rom_dir, sizeof(g_pc_settings.rom_dir) },
    { "ko_image", g_pc_settings.ko_image, sizeof(g_pc_settings.ko_image) },
    { "update_skip", g_pc_settings.update_skip, sizeof(g_pc_settings.update_skip) },
    { "mp_name", g_pc_settings.mp_name, sizeof(g_pc_settings.mp_name) },
    { "touch_layout", g_pc_settings.touch_layout, sizeof(g_pc_settings.touch_layout) },
};

/* Settings that take a number in a range (others are left as they are) */
static const struct {
    const char* key;
    int* field;
    int min, max;
} s_number_settings[] = {
    { "korean", &g_pc_settings.korean, 0, 1 },
    { "window_width", &g_pc_settings.window_width, 640, INT_MAX },
    { "window_height", &g_pc_settings.window_height, 480, INT_MAX },
    { "fullscreen", &g_pc_settings.fullscreen, 0, 2 },
    { "vsync", &g_pc_settings.vsync, 0, 1 },
    { "autosave", &g_pc_settings.autosave, 0, 1 },
    { "autosave_every", &g_pc_settings.autosave_minutes, 1, 60 },
    { "nes_aspect", &g_pc_settings.nes_aspect, 0, 1 },
    { "master_volume", &g_pc_settings.master_volume, 0, 100 },
    { "text_input", &g_pc_settings.text_input, 0, 1 },
    { "touch_controls", &g_pc_settings.touch_controls, 0, 2 },
    { "minimap", &g_pc_settings.minimap, 0, 1 },
    { "seamless_blocks", &g_pc_settings.seamless_blocks, 0, 1 },
    { "auto_update", &g_pc_settings.auto_update, 0, 1 },
    { "ui_language", &g_pc_settings.ui_language, 0, 2 },
    { "hd_textures", &g_pc_settings.hd_textures, 0, 1 },
    { "hd_quality", &g_pc_settings.hd_quality, 0, 1 },
    { "specialized_shaders", &g_pc_settings.specialized_shaders, 0, 1 },
    { "renderer", &g_pc_settings.renderer, 0, 2 },
    { "mp_port", &g_pc_settings.mp_port, 1024, 65535 },
};

static void apply_setting(const char* key, const char* value) {
    int val = atoi(value);

    for (size_t i = 0; i < sizeof(s_text_settings) / sizeof(s_text_settings[0]); i++) {
        if (strcmp(key, s_text_settings[i].key) == 0) {
            snprintf(s_text_settings[i].field, s_text_settings[i].size, "%s", value);
            return;
        }
    }
    for (size_t i = 0; i < sizeof(s_number_settings) / sizeof(s_number_settings[0]); i++) {
        if (strcmp(key, s_number_settings[i].key) == 0) {
            if (val >= s_number_settings[i].min && val <= s_number_settings[i].max) {
                *s_number_settings[i].field = val;
            }
            return;
        }
    }
    if (strcmp(key, "msaa") == 0) {
        if (val == 0 || val == 2 || val == 4 || val == 8) g_pc_settings.msaa = val;
    } else if (strcmp(key, "preload_textures") == 0) {
        if (val >= 0 && val <= 2) g_pc_settings.preload_textures = val ? 1 : 0;
    } else if (strcmp(key, "resetti") == 0) {
        if (val == 0 || val == 1) g_pc_settings.disable_resetti = !val;
    } else if (strcmp(key, "disable_resetti") == 0) {
        /* (the key before 1.3.0, written as 0 by default: only a 1 means something) */
        if (val == 1) g_pc_settings.disable_resetti = 1;
    } else if (strcmp(key, "autosave_minutes") == 0) {
        /* (the key of 1.3.0, written as 5 by default: only another value
         * was chosen; now every minute by default) */
        if (val >= 1 && val <= 60 && val != 5) g_pc_settings.autosave_minutes = val;
    } else if (strcmp(key, "ui_scale") == 0) {
        if (val == 0 || (val >= 50 && val <= 300)) g_pc_settings.ui_scale = val;
    }
}

void pc_settings_save(void) {
    FILE* f = fopen(SETTINGS_FILE, "w");
    if (!f) {
        printf("[Settings] Failed to write %s\n", SETTINGS_FILE);
        return;
    }
    fprintf(f, "[Graphics]\n");
    fprintf(f, "# Window size (ignored in fullscreen)\n");
    fprintf(f, "window_width = %d\n", g_pc_settings.window_width);
    fprintf(f, "window_height = %d\n", g_pc_settings.window_height);
    fprintf(f, "\n");
    fprintf(f, "# 0 = windowed, 1 = fullscreen, 2 = borderless fullscreen\n");
    fprintf(f, "fullscreen = %d\n", g_pc_settings.fullscreen);
    fprintf(f, "\n");
    fprintf(f, "# Vertical sync: 0 = off, 1 = on\n");
    fprintf(f, "vsync = %d\n", g_pc_settings.vsync);
    fprintf(f, "\n");
    fprintf(f, "# Anti-aliasing samples: 0 = off, 2, 4, or 8\n");
    fprintf(f, "msaa = %d\n", g_pc_settings.msaa);
    fprintf(f, "\n");
    fprintf(f, "# Graphics API: 0 = OpenGL, 1 = Vulkan, 2 = Direct3D 12 (Windows); OpenGL when it cannot be used\n");
    fprintf(f, "renderer = %d\n", g_pc_settings.renderer);
    fprintf(f, "# GX shader: 1 = one made for each way the game draws (less work for the GPU), 0 = one for all\n");
    fprintf(f, "specialized_shaders = %d\n", g_pc_settings.specialized_shaders);
    fprintf(f, "\n");
    fprintf(f, "[Enhancements]\n");
    fprintf(f, "# Load HD textures: 0 = while playing (on a thread), 1 = all at startup\n");
    fprintf(f, "preload_textures = %d\n", g_pc_settings.preload_textures);
    fprintf(f, "\n");
    fprintf(f, "# Use the HD texture pack installed with Settings > HD textures: 0 = no, 1 = yes\n");
    fprintf(f, "hd_textures = %d\n", g_pc_settings.hd_textures);
    fprintf(f, "# HD texture size: 0 = as in the pack, 1 = half (less memory)\n");
    fprintf(f, "hd_quality = %d\n", g_pc_settings.hd_quality);
    fprintf(f, "\n");
    fprintf(f, "[Interface]\n");
    fprintf(f, "# Language of the pause and settings menus: 0 = automatic, 1 = Korean, 2 = English\n");
    fprintf(f, "ui_language = %d\n", g_pc_settings.ui_language);
    fprintf(f, "# Size of the menus in percent (50-300), 0 = automatic\n");
    fprintf(f, "ui_scale = %d\n", g_pc_settings.ui_scale);
    fprintf(f, "\n");
    fprintf(f, "[Multiplayer]\n");
    fprintf(f, "# UDP port a room you host listens on (1024-65535); open or forward it for players from outside\n");
    fprintf(f, "mp_port = %d\n", g_pc_settings.mp_port);
    fprintf(f, "# The name other players see; empty = your player's name\n");
    fprintf(f, "mp_name = %s\n", g_pc_settings.mp_name);
    fprintf(f, "\n");
    fprintf(f, "[Gameplay]\n");
    fprintf(f, "# Save by itself: between scenes, every minute outside and on quitting: 0 = no, 1 = yes\n");
    fprintf(f, "autosave = %d\n", g_pc_settings.autosave);
    fprintf(f, "# How often outside, in minutes (1-60)\n");
    fprintf(f, "autosave_every = %d\n", g_pc_settings.autosave_minutes);
    fprintf(f, "# Mr. Resetti comes when the game was turned off without saving: 0 = no, 1 = yes\n");
    fprintf(f, "resetti = %d\n", !g_pc_settings.disable_resetti);
    fprintf(f, "\n");
    fprintf(f, "# NES emulator aspect ratio: 0 = stretch to fullscreen, 1 = 4:3 pillarbox\n");
    fprintf(f, "nes_aspect = %d\n", g_pc_settings.nes_aspect);
    fprintf(f, "\n");
    fprintf(f, "# Map of the town in the bottom left corner outside: 0 = off, 1 = on\n");
    fprintf(f, "minimap = %d\n", g_pc_settings.minimap);
    fprintf(f, "\n");
    fprintf(f, "# Outside, walk on into the next acre with the camera following, with no stop at its edge:\n");
    fprintf(f, "# 0 = no (as on the GameCube), 1 = yes\n");
    fprintf(f, "seamless_blocks = %d\n", g_pc_settings.seamless_blocks);
    fprintf(f, "\n");
    fprintf(f, "[Audio]\n");
    fprintf(f, "# Master output volume as a percentage (0-100)\n");
    fprintf(f, "master_volume = %d\n", g_pc_settings.master_volume);
    fprintf(f, "\n");
    fprintf(f, "[Input]\n");
    fprintf(f, "# Typing in name and letter fields: 0 = built-in Hangul input (dubeolsik on the keys,\n");
    fprintf(f, "# Hangul/English with the Hangul key, right Alt or Shift+Space), 1 = the system's input method\n");
    fprintf(f, "text_input = %d\n", g_pc_settings.text_input);
    fprintf(f, "\n");
    fprintf(f, "# Touch screens: 0 = off, 1 = a GameCube pad drawn over the game, 2 = full touch (tap where to\n");
    fprintf(f, "# walk, hold to run, tap villagers and things to use them, icons for the pockets and the map)\n");
    fprintf(f, "touch_controls = %d\n", g_pc_settings.touch_controls);
    fprintf(f, "# The pad's pieces moved or resized in Settings > Edit pad layout (name=x,y,size; x and y as\n");
    fprintf(f, "# fractions of the window); empty = where the game puts them\n");
    fprintf(f, "touch_layout = %s\n", g_pc_settings.touch_layout);
    fprintf(f, "\n");
    fprintf(f, "[Paths]\n");
    fprintf(f, "# Folder with the disc image (.iso/.ciso/.gcm), or the image file itself.\n");
    fprintf(f, "# Empty = the rom folder next to the game. Pause menu > Settings > Disc changes it.\n");
    fprintf(f, "rom_dir = %s\n", g_pc_settings.rom_dir);
    fprintf(f, "\n");
    fprintf(f, "[Korean]\n");
    fprintf(f, "# Run the Korean disc made with Settings > Disc > Korean patch: 0 = no (the disc above), 1 = yes\n");
    fprintf(f, "korean = %d\n", g_pc_settings.korean);
    fprintf(f, "# Where that disc is (written by the game)\n");
    fprintf(f, "ko_image = %s\n", g_pc_settings.ko_image);
    fprintf(f, "\n");
    fprintf(f, "[Update]\n");
    fprintf(f, "# Look for a new version on GitHub when the game starts (release builds): 0 = no, 1 = yes\n");
    fprintf(f, "auto_update = %d\n", g_pc_settings.auto_update);
    fprintf(f, "# A version not to ask about again (\"Skip this version\")\n");
    fprintf(f, "update_skip = %s\n", g_pc_settings.update_skip);
    fclose(f);
    printf("[Settings] Saved %s\n", SETTINGS_FILE);
}

/* Accessor for TUs that can't include pc_settings.h (pc_nes_fixnes.c). */
int pc_settings_get_nes_aspect(void) {
    return g_pc_settings.nes_aspect;
}

/* --- Resolution preset table (shared) ---
 * Ordered by width then height. The desktop's native size is injected at
 * first use (de-duplicated against the static list) so the user can snap
 * to whatever their monitor is running.*/
#define RES_MAX 32
static int res_w_tbl[RES_MAX];
static int res_h_tbl[RES_MAX];
static int res_count = 0;

static void add_preset(int w, int h) {
    if (res_count >= RES_MAX) return;
    for (int i = 0; i < res_count; i++) {
        if (res_w_tbl[i] == w && res_h_tbl[i] == h) return; /* de-dupe */
    }
    int at = res_count;
    for (int i = 0; i < res_count; i++) {
        if (res_w_tbl[i] > w || (res_w_tbl[i] == w && res_h_tbl[i] > h)) {
            at = i;
            break;
        }
    }
    for (int i = res_count; i > at; i--) {
        res_w_tbl[i] = res_w_tbl[i - 1];
        res_h_tbl[i] = res_h_tbl[i - 1];
    }
    res_w_tbl[at] = w;
    res_h_tbl[at] = h;
    res_count++;
}

static void ensure_presets(void) {
    if (res_count > 0) return;
    /* 4:3 */
    add_preset(640,  480);
    add_preset(800,  600);
    add_preset(960,  720);
    add_preset(1024, 768);
    add_preset(1152, 864);
    add_preset(1280, 960);
    add_preset(1400, 1050);
    add_preset(1600, 1200);
    /* 16:9 */
    add_preset(1280, 720);
    add_preset(1366, 768);
    add_preset(1600, 900);
    add_preset(1920, 1080);
    add_preset(2560, 1440);
    add_preset(3840, 2160);
    /* Desktop native (inserted sorted, de-duped against the list above). */
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(0, &mode) == 0) {
        add_preset(mode.w, mode.h);
    }
}

/* Find the closest preset by total pixel count. Used when the caller's
 * current (w, h) isn't in the list (e.g. custom settings.ini value). */
static int nearest_index(int w, int h) {
    long long want = (long long)w * h;
    int best = 0;
    long long best_diff = (long long)1 << 62;
    for (int i = 0; i < res_count; i++) {
        long long diff = (long long)res_w_tbl[i] * res_h_tbl[i] - want;
        if (diff < 0) diff = -diff;
        if (diff < best_diff) { best_diff = diff; best = i; }
    }
    return best;
}

void pc_settings_cycle_resolution(int* width, int* height, int dir) {
    ensure_presets();
    int cur = -1;
    for (int i = 0; i < res_count; i++) {
        if (res_w_tbl[i] == *width && res_h_tbl[i] == *height) { cur = i; break; }
    }
    if (cur < 0) cur = nearest_index(*width, *height);

    if (dir > 0 && cur < res_count - 1) cur++;
    else if (dir < 0 && cur > 0) cur--;

    *width  = res_w_tbl[cur];
    *height = res_h_tbl[cur];
}

int pc_settings_resolution_count(void) {
    ensure_presets();
    return res_count;
}

void pc_settings_resolution_at(int i, int* width, int* height) {
    ensure_presets();
    if (i < 0) i = 0;
    if (i >= res_count) i = res_count - 1;
    *width = res_w_tbl[i];
    *height = res_h_tbl[i];
}

void pc_settings_apply(void) {
    if (!g_pc_window) return;

#ifdef __ANDROID__
    /* The game always fills the screen; window mode and size are the PC's
     * (changing them took the app out of full screen) */
    pc_gfx_set_vsync(g_pc_settings.vsync);
    printf("[Settings] Applied: vsync=%d msaa=%d\n", g_pc_settings.vsync, g_pc_settings.msaa);
    return;
#endif
    int w = g_pc_settings.window_width;
    int h = g_pc_settings.window_height;

    switch (g_pc_settings.fullscreen) {
        case 1: {
            /* Exclusive fullscreen at the user's chosen resolution. SDL
             * needs the display mode set before transitioning to
             * SDL_WINDOW_FULLSCREEN or it'll just use the desktop mode. */
            SDL_DisplayMode target = { 0 };
            target.w = w;
            target.h = h;
            int display_idx = SDL_GetWindowDisplayIndex(g_pc_window);
            if (display_idx < 0) display_idx = 0;
            SDL_DisplayMode closest;
            if (SDL_GetClosestDisplayMode(display_idx, &target, &closest)) {
                SDL_SetWindowDisplayMode(g_pc_window, &closest);
            }
            SDL_SetWindowFullscreen(g_pc_window, SDL_WINDOW_FULLSCREEN);
            break;
        }
        case 2: {
            /* Borderless window at the user's chosen size, centred. Exit
             * any fullscreen mode first (including FULLSCREEN_DESKTOP)
             * so the resize sticks. */
            SDL_SetWindowFullscreen(g_pc_window, 0);
            SDL_SetWindowBordered(g_pc_window, SDL_FALSE);
            SDL_SetWindowSize(g_pc_window, w, h);
            SDL_SetWindowPosition(g_pc_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            break;
        }
        case 0:
        default: {
            SDL_SetWindowFullscreen(g_pc_window, 0);
            SDL_SetWindowBordered(g_pc_window, SDL_TRUE);
            SDL_SetWindowSize(g_pc_window, w, h);
            SDL_SetWindowPosition(g_pc_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
            break;
        }
    }

    pc_gfx_set_vsync(g_pc_settings.vsync);
    pc_platform_update_window_size();

    printf("[Settings] Applied: %dx%d fullscreen=%d vsync=%d msaa=%d\n",
           g_pc_settings.window_width, g_pc_settings.window_height,
           g_pc_settings.fullscreen, g_pc_settings.vsync, g_pc_settings.msaa);
}

void pc_settings_load(void) {
    FILE* f = fopen(SETTINGS_FILE, "r");
    if (!f) {
        pc_settings_save(); /* the defaults */
        printf("[Settings] Created default %s\n", SETTINGS_FILE);
        return;
    }

    char line[1200]; /* rom_dir can be a long path */
    while (fgets(line, sizeof(line), f)) {
        const char* p = skip_ws(line);

        if (*p == '#' || *p == ';' || *p == '\0' || *p == '\n' || *p == '[')
            continue;

        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = (char*)skip_ws(line);
        trim_end(key);
        char* value = (char*)skip_ws(eq + 1);
        trim_end(value);

        if (*key && *value) {
            apply_setting(key, value);
        }
    }
    fclose(f);
    printf("[Settings] Loaded %s: %dx%d fullscreen=%d vsync=%d msaa=%d preload_textures=%d\n",
           SETTINGS_FILE, g_pc_settings.window_width, g_pc_settings.window_height,
           g_pc_settings.fullscreen, g_pc_settings.vsync, g_pc_settings.msaa,
           g_pc_settings.preload_textures);
}

int pc_seamless_blocks(void) {
    return g_pc_settings.seamless_blocks;
}
