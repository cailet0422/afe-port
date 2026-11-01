#include "pc_settings_menu.h"
#include "pc_settings.h"
#include "pc_keybindings.h"
#include "pc_ko.h"
#include "pc_ko_patch.h"
#include "pc_rom_dir.h"
#include "pc_text_draw.h"
#include "pc_update.h"

#include "graph.h"
#include "m_font.h"
#include "m_rcp.h"
#include "main.h" /* SCREEN_WIDTH_F */

#include <stdio.h>
#include <string.h>

/* --- Item IDs (stable across tabs — used by the dispatch switches) --- */
enum {
    ITEM_DISPLAY,
    ITEM_VSYNC,
    ITEM_MSAA,
    ITEM_RES,
    ITEM_TEXTURES,
    ITEM_RESETTI,
    ITEM_NES_ASPECT,
    ITEM_MASTER_VOLUME,
    ITEM_ROM_DIR,
    ITEM_KOREAN,
    ITEM_KO_INSTALL,
    ITEM_BINDINGS,
    ITEM_MINIMAP,
    ITEM_AUTO_UPDATE,
    ITEM_UPDATE_CHECK,
};

/* Per-item static metadata. restart=1 appends " *" and folds into the
 * "requires restart" hint at the bottom of the page. */
typedef struct {
    const char* label;
    int         id;
    int         restart;
} Item;

/* Android always fills the screen, so it has no window mode or size */
static const Item tab_video_items[] = {
#ifndef __ANDROID__
    { "Display",    ITEM_DISPLAY,  0 },
#endif
    { "VSync",      ITEM_VSYNC,    0 },
    { "MSAA",       ITEM_MSAA,     1 },
#ifndef __ANDROID__
    { "Resolution", ITEM_RES,      0 },
#endif
    { "Textures",   ITEM_TEXTURES, 1 },
};

static const Item tab_gameplay_items[] = {
    { "Resetti",    ITEM_RESETTI,    0 },
    { "NES aspect", ITEM_NES_ASPECT, 0 },
    { "Minimap",    ITEM_MINIMAP,    0 },
#if defined(PC_UPDATER) || defined(__ANDROID__)
    /* Android looks when the app starts (Updater.java) */
    { "Updates",    ITEM_AUTO_UPDATE,  0 },
#endif
#if defined(PC_UPDATER) && !defined(__ANDROID__)
    { "Version",    ITEM_UPDATE_CHECK, 0 },
#endif
};

static const Item tab_audio_items[] = {
    { "Master volume", ITEM_MASTER_VOLUME, 0 },
};

/* Opens the key and gamepad bindings page */
static const Item tab_controls_items[] = {
    { "Buttons", ITEM_BINDINGS, 0 },
};

/* The folder is picked with the system's folder dialog (pc_rom_dir.c); the
 * Korean patch is made into a disc of its own (pc_ko_patch.c). Both are read
 * at startup. */
static const Item tab_disc_items[] = {
    { "Folder",        ITEM_ROM_DIR,    1 },
    { "Korean",        ITEM_KOREAN,     1 },
    { "Korean patch",  ITEM_KO_INSTALL, 0 },
};

typedef struct {
    const char* name;
    const Item* items;
    int         count;
} Tab;

#define TAB_ITEMS(a) (a), (int)(sizeof(a) / sizeof((a)[0]))
static const Tab s_tabs[] = {
    { "Video",    TAB_ITEMS(tab_video_items) },
    { "Audio",    TAB_ITEMS(tab_audio_items) },
    { "Gameplay", TAB_ITEMS(tab_gameplay_items) },
    { "Controls", TAB_ITEMS(tab_controls_items) },
    { "Disc",     TAB_ITEMS(tab_disc_items) },
};
#define TAB_COUNT ((int)(sizeof(s_tabs) / sizeof(s_tabs[0])))

/* --- Sub-pages --- */
typedef enum {
    SUB_SETTINGS = 0,
    SUB_CONFIRM_RES = 1,
    SUB_CONFIRM_BACK = 2,
    SUB_BINDINGS = 3,
    SUB_PATCH = 4,
    SUB_UPDATE = 5,
} SubPage;

static int     s_active = 0;
static SubPage s_sub    = SUB_SETTINGS;
static int     s_tab    = 0;
static int     s_sel    = -1; /* start on the tab row */

/* Pending edits — only committed to g_pc_settings on Apply. */
static PCSettings s_pending;
static int        s_pending_dirty = 0;

/* Resolution-change confirmation state. */
#define RES_CONFIRM_MS 15000u
static int    s_res_old_w = 0;
static int    s_res_old_h = 0;
static Uint32 s_res_deadline = 0;
static int    s_res_sel = 1;

/* Back-confirm state (Discard vs Keep editing when Back hit while dirty). */
static int    s_back_sel = 0; /* 0 = Keep editing (safe default), 1 = Discard */

/* Startup snapshot + restart-required flag. Some settings (MSAA, texture
 * pack preload mode) only take effect on process restart. More might appear.*/
static PCSettings s_startup;
static int        s_startup_captured = 0;
static int        s_pending_restart = 0;

/* Bindings page: rows are the PC_BIND_* actions, then Reset and Back;
 * column 0 = keyboard, 1 = gamepad. While capturing, the next key or button
 * becomes the cell's binding. */
#define BIND_ROW_RESET   PC_BIND_COUNT
#define BIND_ROW_BACK    (PC_BIND_COUNT + 1)
#define BIND_ROWS        (PC_BIND_COUNT + 2)
#define BIND_VISIBLE     9
#define BIND_CAPTURE_MS  6000u
static int    s_bind_sel = 0;
static int    s_bind_col = 0;
static int    s_bind_scroll = 0;
static int    s_capture = 0;
static Uint32 s_capture_deadline = 0;

/* Korean patch page: 0 = Restart now, 1 = Later (after it is installed) */
static int s_patch_sel = 0;
static int s_ko_installed = 0;

/* Update page: the chosen button (Update now / Later / Skip, or Restart now /
 * Later); s_update_only: opened on its own, leaving it closes the menu */
static int s_update_sel = 0;
static int s_update_only = 0;

/* --- Dirty + helpers --- */

static void recompute_dirty(void) {
    s_pending_dirty =
        (s_pending.fullscreen       != g_pc_settings.fullscreen) ||
        (s_pending.vsync            != g_pc_settings.vsync) ||
        (s_pending.msaa             != g_pc_settings.msaa) ||
        (s_pending.window_width     != g_pc_settings.window_width) ||
        (s_pending.window_height    != g_pc_settings.window_height) ||
        (s_pending.preload_textures != g_pc_settings.preload_textures) ||
        (s_pending.disable_resetti  != g_pc_settings.disable_resetti) ||
        (s_pending.nes_aspect       != g_pc_settings.nes_aspect) ||
        (s_pending.master_volume    != g_pc_settings.master_volume) ||
        (s_pending.minimap          != g_pc_settings.minimap) ||
        (s_pending.auto_update      != g_pc_settings.auto_update) ||
        (s_pending.korean           != g_pc_settings.korean);
}

static void snapshot(void) {
    s_pending = g_pc_settings;
    s_pending_dirty = 0;
}

/* Per-item dispatch: cycle, format, changed. Add cases here when a
 * new setting row is added to any tab. */

static void item_cycle(int id, int dir) {
    switch (id) {
        case ITEM_DISPLAY: {
            int v = s_pending.fullscreen + (dir > 0 ? 1 : 2);
            s_pending.fullscreen = v % 3;
        } break;
        case ITEM_VSYNC:
            s_pending.vsync = !s_pending.vsync;
            break;
        case ITEM_MSAA: {
            static const int steps[] = { 0, 2, 4, 8 };
            int idx = 0;
            for (int i = 0; i < 4; i++) if (s_pending.msaa == steps[i]) { idx = i; break; }
            idx = (idx + (dir > 0 ? 1 : 3)) % 4;
            s_pending.msaa = steps[idx];
        } break;
        case ITEM_RES:
            pc_settings_cycle_resolution(&s_pending.window_width, &s_pending.window_height, dir);
            break;
        case ITEM_TEXTURES: {
            int v = s_pending.preload_textures + (dir > 0 ? 1 : -1);
            if (v < 0) v = 2;
            if (v > 2) v = 0;
            s_pending.preload_textures = v;
        } break;
        case ITEM_RESETTI:
            s_pending.disable_resetti = !s_pending.disable_resetti;
            break;
        case ITEM_NES_ASPECT:
            s_pending.nes_aspect = !s_pending.nes_aspect;
            break;
        case ITEM_MINIMAP:
            s_pending.minimap = !s_pending.minimap;
            break;
        case ITEM_AUTO_UPDATE:
            s_pending.auto_update = !s_pending.auto_update;
            break;
        case ITEM_MASTER_VOLUME: {
            int v = s_pending.master_volume + (dir > 0 ? 10 : -10);
            if (v < 0)   v = 0;
            if (v > 100) v = 100;
            s_pending.master_volume = v;
        } break;
        case ITEM_KOREAN:
            /* only once there is a Korean disc to run */
            if (s_ko_installed) s_pending.korean = !s_pending.korean;
            break;
    }
    recompute_dirty();
}

static void item_format(int id, char* buf, size_t n) {
    switch (id) {
        case ITEM_DISPLAY:
            snprintf(buf, n, "%s",
                s_pending.fullscreen == 0 ? "< Windowed >" :
                s_pending.fullscreen == 1 ? "< Fullscreen >" :
                                            "< Borderless >");
            break;
        case ITEM_VSYNC:
            snprintf(buf, n, "%s", s_pending.vsync ? "< On >" : "< Off >");
            break;
        case ITEM_MSAA:
            if (s_pending.msaa > 0) snprintf(buf, n, "< %dx >", s_pending.msaa);
            else                    snprintf(buf, n, "< Off >");
            break;
        case ITEM_RES:
            snprintf(buf, n, "< %dx%d >", s_pending.window_width, s_pending.window_height);
            break;
        case ITEM_TEXTURES:
            snprintf(buf, n, "%s",
                s_pending.preload_textures == 0 ? "< On Demand >" :
                s_pending.preload_textures == 1 ? "< Preload >"   :
                                                  "< Preload&Cache >");
            break;
        case ITEM_RESETTI:
            /* disable_resetti flips the polarity — show the user-facing side. */
            snprintf(buf, n, "%s", s_pending.disable_resetti ? "< Disabled >" : "< Enabled >");
            break;
        case ITEM_NES_ASPECT:
            snprintf(buf, n, "%s", s_pending.nes_aspect ? "< 4:3 >" : "< Stretch >");
            break;
        case ITEM_MINIMAP:
            snprintf(buf, n, "%s", s_pending.minimap ? "< On >" : "< Off >");
            break;
        case ITEM_AUTO_UPDATE:
            snprintf(buf, n, "%s", s_pending.auto_update ? "< Auto >" : "< Off >");
            break;
        case ITEM_UPDATE_CHECK:
            snprintf(buf, n, "v%s", pc_update_version());
            break;
        case ITEM_MASTER_VOLUME:
            snprintf(buf, n, "< %d%% >", s_pending.master_volume);
            break;
        case ITEM_ROM_DIR:
            pc_rom_dir_describe(buf, n);
            break;
        case ITEM_KOREAN:
            if (s_ko_installed)   snprintf(buf, n, "%s", s_pending.korean ? "< On >" : "< Off >");
            else if (g_pc_ko_mode) snprintf(buf, n, "On (patched disc)");
            else                  snprintf(buf, n, "Not installed");
            break;
        case ITEM_KO_INSTALL:
            snprintf(buf, n, "%s", s_ko_installed ? "Install again" : "Install");
            break;
        case ITEM_BINDINGS:
            snprintf(buf, n, "Change");
            break;
        default:
            buf[0] = '\0';
            break;
    }
}

static int item_changed(int id) {
    switch (id) {
        case ITEM_DISPLAY:    return s_pending.fullscreen       != g_pc_settings.fullscreen;
        case ITEM_VSYNC:      return s_pending.vsync            != g_pc_settings.vsync;
        case ITEM_MSAA:       return s_pending.msaa             != g_pc_settings.msaa;
        case ITEM_RES:        return (s_pending.window_width  != g_pc_settings.window_width) ||
                                     (s_pending.window_height != g_pc_settings.window_height);
        case ITEM_TEXTURES:   return s_pending.preload_textures != g_pc_settings.preload_textures;
        case ITEM_RESETTI:    return s_pending.disable_resetti  != g_pc_settings.disable_resetti;
        case ITEM_NES_ASPECT:    return s_pending.nes_aspect    != g_pc_settings.nes_aspect;
        case ITEM_MINIMAP:       return s_pending.minimap       != g_pc_settings.minimap;
        case ITEM_AUTO_UPDATE:   return s_pending.auto_update   != g_pc_settings.auto_update;
        case ITEM_MASTER_VOLUME: return s_pending.master_volume != g_pc_settings.master_volume;
        case ITEM_KOREAN:        return s_pending.korean        != g_pc_settings.korean;
    }
    return 0;
}

/* For items flagged restart=1. Does the live g_pc_settings value differ
 * from what the process booted with? Used after Apply to decide whether
 * the "restart required" banner should show. */
static int item_differs_from_startup(int id) {
    switch (id) {
        case ITEM_MSAA:     return g_pc_settings.msaa             != s_startup.msaa;
        case ITEM_TEXTURES: return g_pc_settings.preload_textures != s_startup.preload_textures;
        case ITEM_ROM_DIR:  return pc_rom_dir_changed();
        case ITEM_KOREAN:   return g_pc_settings.korean != s_startup.korean ||
                                   strcmp(g_pc_settings.ko_image, s_startup.ko_image) != 0;
    }
    return 0;
}

static void recompute_restart_needed(void) {
    s_pending_restart = 0;
    for (int t = 0; t < TAB_COUNT; t++) {
        const Tab* tab = &s_tabs[t];
        for (int i = 0; i < tab->count; i++) {
            if (tab->items[i].restart && item_differs_from_startup(tab->items[i].id)) {
                s_pending_restart = 1;
                return;
            }
        }
    }
}

/* --- Nav helpers --- */

static int cur_item_count(void) { return s_tabs[s_tab].count; }
static int idx_apply(void)      { return cur_item_count(); }
static int idx_back(void)       { return cur_item_count() + 1; }
static int idx_last(void)       { return cur_item_count() + 1; }

/* --- Apply / res-confirm finalisers --- */

static void res_confirm_keep(void) {
    pc_settings_save();
    printf("[SETTINGS] resolution kept: %dx%d\n",
           g_pc_settings.window_width, g_pc_settings.window_height);
}

static void res_confirm_revert(void) {
    g_pc_settings.window_width  = s_res_old_w;
    g_pc_settings.window_height = s_res_old_h;
    s_pending.window_width      = s_res_old_w;
    s_pending.window_height     = s_res_old_h;
    pc_settings_apply();
    pc_settings_save();
    recompute_dirty();
    printf("[SETTINGS] resolution reverted to %dx%d\n", s_res_old_w, s_res_old_h);
}

static void res_confirm_finish(void) {
    if (s_res_sel == 0) res_confirm_keep();
    else                res_confirm_revert();
    s_res_deadline = 0;
    s_sub = SUB_SETTINGS;
}

static void apply_pending(void) {
    if (!s_pending_dirty) return;

    int res_changed = (s_pending.window_width  != g_pc_settings.window_width) ||
                      (s_pending.window_height != g_pc_settings.window_height);
    if (res_changed) {
        s_res_old_w = g_pc_settings.window_width;
        s_res_old_h = g_pc_settings.window_height;
    }

    g_pc_settings = s_pending;
    pc_settings_apply();
    s_pending_dirty = 0;
    recompute_restart_needed();

    if (res_changed) {
        s_sub = SUB_CONFIRM_RES;
        s_res_sel = 1;
        s_res_deadline = SDL_GetTicks() + RES_CONFIRM_MS;
        printf("[SETTINGS] resolution changed, awaiting confirmation (15s)\n");
    } else {
        pc_settings_save();
        printf("[SETTINGS] applied\n");
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void pc_settings_menu_enter(void) {
    /* First-time entry captures what the process actually booted with.
     * Anything that changes from here and can't live-apply gets flagged
     * as "restart required". */
    if (!s_startup_captured) {
        s_startup = g_pc_settings;
        s_startup_captured = 1;
    }
    snapshot();
    s_tab = 0;
    s_sel = -1;           /* start cursor on the tab row */
    s_sub = SUB_SETTINGS;
    s_res_deadline = 0;
    s_capture = 0;
    s_ko_installed = pc_ko_patch_installed();
    s_active = 1;
}

void pc_settings_menu_enter_update(void) {
    pc_settings_menu_enter();
    s_sub = SUB_UPDATE;
    s_update_sel = 0;
    s_update_only = 1;
}

int pc_settings_menu_active(void) {
    return s_active;
}

/* --- Update page --- */

/* Buttons on the update page now */
static int update_buttons(void) {
    switch (pc_update_state()) {
        case PC_UPDATE_AVAILABLE: return 3; /* Update now, Later, Skip this version */
        case PC_UPDATE_INSTALLED: return 2; /* Restart now, Later */
        case PC_UPDATE_DOWNLOADING:
        case PC_UPDATE_CHECKING:  return 0;
        default:                  return 1; /* OK */
    }
}

/* Leaving the page; 0 if that closes the menu */
static int update_close(void) {
    if (s_update_only) {
        s_update_only = 0;
        s_active = 0;
        return 0;
    }
    s_sub = SUB_SETTINGS;
    return 1;
}

static int update_confirm(void) {
    PCUpdateState st = pc_update_state();

    if (st == PC_UPDATE_AVAILABLE) {
        if (s_update_sel == 0) {
            pc_update_start_download();
            return 1;
        }
        if (s_update_sel == 2) {
            pc_update_skip();
            memcpy(s_pending.update_skip, g_pc_settings.update_skip, sizeof(s_pending.update_skip));
        }
        return update_close();
    }
    if (st == PC_UPDATE_INSTALLED) {
        if (s_update_sel == 0) {
            pc_request_restart(); /* the new program (pc_update_restart_path) */
            return 1;
        }
        return update_close();
    }
    if (st == PC_UPDATE_DOWNLOADING || st == PC_UPDATE_CHECKING) {
        return 1;
    }
    return update_close();
}

static int update_cancel(void) {
    if (pc_update_state() == PC_UPDATE_DOWNLOADING) {
        pc_update_cancel(); /* back to Update now / Later / Skip */
        return 1;
    }
    return update_close();
}

/* --- Bindings page --- */

static void bind_move(int dir) {
    s_bind_sel += dir;
    if (s_bind_sel < 0) s_bind_sel = 0;
    if (s_bind_sel > BIND_ROWS - 1) s_bind_sel = BIND_ROWS - 1;
    if (s_bind_sel < s_bind_scroll) s_bind_scroll = s_bind_sel;
    if (s_bind_sel >= s_bind_scroll + BIND_VISIBLE) s_bind_scroll = s_bind_sel - BIND_VISIBLE + 1;
}

static void bind_confirm(void) {
    if (s_bind_sel < PC_BIND_COUNT) {
        s_capture = 1;
        s_capture_deadline = SDL_GetTicks() + BIND_CAPTURE_MS;
    } else if (s_bind_sel == BIND_ROW_RESET) {
        pc_keybindings_reset();
        pc_keybindings_save();
    } else {
        s_sub = SUB_SETTINGS;
    }
}

static void bind_captured(void) {
    s_capture = 0;
    pc_keybindings_save();
}

int pc_settings_menu_handle_event(const SDL_Event* e) {
    int action = s_bind_sel;

    if (!s_active || s_sub != SUB_BINDINGS || !s_capture) return 0;

    switch (e->type) {
        case SDL_KEYDOWN:
            if (e->key.repeat) break;
            if (e->key.keysym.sym == SDLK_ESCAPE || e->key.keysym.sym == SDLK_AC_BACK) {
                s_capture = 0;
            } else if (s_bind_col == 0 && e->key.keysym.scancode != SDL_SCANCODE_UNKNOWN) {
                /* (the touch pad's menu keys carry no scancode) */
                pc_keybindings_set_key(action, (PCInputCode)e->key.keysym.scancode);
                bind_captured();
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (s_bind_col == 0 && e->button.button >= SDL_BUTTON_LEFT && e->button.button <= SDL_BUTTON_MIDDLE) {
                pc_keybindings_set_key(action, PC_INPUT_MOUSE_BIT | e->button.button);
                bind_captured();
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            if (s_bind_col == 1) {
                pc_keybindings_set_pad(action, (PCPadInput)e->cbutton.button);
                bind_captured();
            } else {
                s_capture = 0; /* waiting for a key: a pad button gives up */
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (s_bind_col == 1 && e->caxis.value > 16384 &&
                (e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
                pc_keybindings_set_pad(action, e->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? PC_PAD_TRIGGER_LEFT
                                                                                              : PC_PAD_TRIGGER_RIGHT);
                bind_captured();
            }
            break;
        default:
            break;
    }
    return 1;
}

int pc_settings_menu_capturing(void) {
    return s_active && s_sub == SUB_BINDINGS && s_capture;
}

/* --- Korean patch page --- */

static void patch_close(void) {
    pc_ko_patch_reset();
    s_sub = SUB_SETTINGS;
}

static void patch_confirm(void) {
    switch (pc_ko_patch_state()) {
        case PC_KO_PATCH_RUNNING:
            break;
        case PC_KO_PATCH_DONE:
            if (s_patch_sel == 0) {
                pc_request_restart();
            } else {
                patch_close();
            }
            break;
        default:
            patch_close();
            break;
    }
}

static void patch_cancel(void) {
    if (pc_ko_patch_state() == PC_KO_PATCH_RUNNING) {
        pc_ko_patch_cancel();
    } else {
        patch_close();
    }
}

/* --- Navigation --- */

int pc_settings_menu_nav_up(void) {
    if (!s_active) return 0;
    if (s_sub == SUB_BINDINGS) {
        if (!s_capture) bind_move(-1);
        return 1;
    }
    if (s_sub != SUB_SETTINGS) return 1;
    /* Clamp at the top — no wrap. */
    if (s_sel == 0)      s_sel = -1;            /* first item → tab row */
    else if (s_sel > 0)  s_sel--;
    return 1;
}

int pc_settings_menu_nav_down(void) {
    if (!s_active) return 0;
    if (s_sub == SUB_BINDINGS) {
        if (!s_capture) bind_move(+1);
        return 1;
    }
    if (s_sub != SUB_SETTINGS) return 1;
    /* Clamp at the bottom — no wrap. */
    if (s_sel == -1)             s_sel = 0;     /* tab row → first item */
    else if (s_sel < idx_last()) s_sel++;
    return 1;
}

int pc_settings_menu_nav_left(void) {
    if (!s_active) return 0;
    if (s_sub == SUB_BINDINGS) {
        if (!s_capture && s_bind_sel < PC_BIND_COUNT) s_bind_col = !s_bind_col;
        return 1;
    }
    if (s_sub == SUB_PATCH) {
        s_patch_sel = !s_patch_sel;
        return 1;
    }
    if (s_sub == SUB_UPDATE) {
        int n = update_buttons();

        if (n > 0) s_update_sel = (s_update_sel + n - 1) % n;
        return 1;
    }
    if (s_sub == SUB_CONFIRM_RES) {
        s_res_sel = (s_res_sel + 1) % 2;
        return 1;
    }
    if (s_sub == SUB_CONFIRM_BACK) {
        s_back_sel = (s_back_sel + 1) % 2;
        return 1;
    }
    if (s_sel == -1) {
        /* Clamp at the first tab — no wrap. */
        if (s_tab > 0) s_tab--;
    } else if (s_sel < cur_item_count()) {
        item_cycle(s_tabs[s_tab].items[s_sel].id, -1);
    }
    return 1;
}

int pc_settings_menu_nav_right(void) {
    if (!s_active) return 0;
    if (s_sub == SUB_BINDINGS) {
        if (!s_capture && s_bind_sel < PC_BIND_COUNT) s_bind_col = !s_bind_col;
        return 1;
    }
    if (s_sub == SUB_PATCH) {
        s_patch_sel = !s_patch_sel;
        return 1;
    }
    if (s_sub == SUB_UPDATE) {
        int n = update_buttons();

        if (n > 0) s_update_sel = (s_update_sel + 1) % n;
        return 1;
    }
    if (s_sub == SUB_CONFIRM_RES) {
        s_res_sel = (s_res_sel + 1) % 2;
        return 1;
    }
    if (s_sub == SUB_CONFIRM_BACK) {
        s_back_sel = (s_back_sel + 1) % 2;
        return 1;
    }
    if (s_sel == -1) {
        /* Clamp at the last tab — no wrap. */
        if (s_tab < TAB_COUNT - 1) s_tab++;
    } else if (s_sel < cur_item_count()) {
        item_cycle(s_tabs[s_tab].items[s_sel].id, +1);
    }
    return 1;
}

int pc_settings_menu_confirm(void) {
    if (!s_active) return 0;
    if (s_sub == SUB_BINDINGS) {
        if (!s_capture) bind_confirm();
        return 1;
    }
    if (s_sub == SUB_PATCH) {
        patch_confirm();
        return 1;
    }
    if (s_sub == SUB_UPDATE) {
        return update_confirm();
    }
    if (s_sub == SUB_CONFIRM_RES) {
        res_confirm_finish();
        return 1;
    }
    if (s_sub == SUB_CONFIRM_BACK) {
        if (s_back_sel == 1) {
            /* Discard — close the menu; pending edits drop on the floor. */
            s_active = 0;
            return 0;
        }
        s_sub = SUB_SETTINGS;
        return 1;
    }
    if (s_sel == -1) {
        /* Confirm on the tab row — just hop into the first item. */
        s_sel = 0;
    } else if (s_sel < cur_item_count() && s_tabs[s_tab].items[s_sel].id == ITEM_ROM_DIR) {
        /* Saved right away; keep Apply from writing back the old folder */
        if (pc_rom_dir_pick()) {
            memcpy(s_pending.rom_dir, g_pc_settings.rom_dir, sizeof(s_pending.rom_dir));
            recompute_restart_needed();
        }
    } else if (s_sel < cur_item_count() && s_tabs[s_tab].items[s_sel].id == ITEM_BINDINGS) {
        s_sub = SUB_BINDINGS;
        s_bind_sel = 0;
        s_bind_col = 0;
        s_bind_scroll = 0;
        s_capture = 0;
    } else if (s_sel < cur_item_count() && s_tabs[s_tab].items[s_sel].id == ITEM_UPDATE_CHECK) {
        pc_update_check_now();
        s_sub = SUB_UPDATE;
        s_update_sel = 0;
        s_update_only = 0;
    } else if (s_sel < cur_item_count() && s_tabs[s_tab].items[s_sel].id == ITEM_KO_INSTALL) {
        /* picks the file (blocks while the picker is open), then runs on a thread */
        if (pc_ko_patch_start()) {
            s_sub = SUB_PATCH;
            s_patch_sel = 0;
        }
    } else if (s_sel < cur_item_count()) {
        item_cycle(s_tabs[s_tab].items[s_sel].id, +1);
    } else if (s_sel == idx_apply()) {
        apply_pending();
    } else if (s_sel == idx_back()) {
        if (s_pending_dirty) {
            s_sub = SUB_CONFIRM_BACK;
            s_back_sel = 0;
            return 1;
        }
        s_active = 0;
        return 0;
    }
    return 1;
}

int pc_settings_menu_cancel(void) {
    if (!s_active) return 0;
    if (s_sub == SUB_BINDINGS) {
        if (s_capture) s_capture = 0;
        else           s_sub = SUB_SETTINGS;
        return 1;
    }
    if (s_sub == SUB_PATCH) {
        patch_cancel();
        return 1;
    }
    if (s_sub == SUB_UPDATE) {
        return update_cancel();
    }
    if (s_sub == SUB_CONFIRM_RES) {
        s_res_sel = 1; /* safer default on Esc/B */
        res_confirm_finish();
        return 1;
    }
    if (s_sub == SUB_CONFIRM_BACK) {
        /* Esc = cancel the confirm — snap back to the settings page. */
        s_sub = SUB_SETTINGS;
        return 1;
    }
    if (s_pending_dirty) {
        s_sub = SUB_CONFIRM_BACK;
        s_back_sel = 0;
        return 1;
    }
    s_active = 0;
    return 0;
}

void pc_settings_menu_tick(void) {
    if (!s_active) return;
    if (s_sub == SUB_UPDATE) {
        static PCUpdateState last = PC_UPDATE_IDLE;
        PCUpdateState st = pc_update_state();

        if (st != last) {
            s_update_sel = 0; /* new buttons: start on the first */
            last = st;
        }
    }
    if (s_sub == SUB_BINDINGS && s_capture && SDL_GetTicks() >= s_capture_deadline) {
        s_capture = 0;
    }
    if (s_sub == SUB_PATCH) {
        pc_ko_patch_poll();
        if (pc_ko_patch_state() == PC_KO_PATCH_DONE) {
            /* installed and saved: the next start runs it */
            s_pending.korean = g_pc_settings.korean;
            memcpy(s_pending.ko_image, g_pc_settings.ko_image, sizeof(s_pending.ko_image));
            s_ko_installed = 1;
            recompute_dirty();
            recompute_restart_needed();
        }
    }
    if (s_sub == SUB_CONFIRM_RES && s_res_deadline != 0) {
        if (SDL_GetTicks() >= s_res_deadline) {
            s_res_sel = 1;
            res_confirm_finish();
        }
    }
}

/* =========================================================================
 * Drawing
 * ========================================================================= */

static void draw_dim_rect(GRAPH* graph) {
    Gfx* gfx;
    OPEN_DISP(graph);
    gfx = NOW_FONT_DISP;
    gDPNoOpTag(gfx++, PC_NOOP_WIDESCREEN_STRETCH);
    gDPPipeSync(gfx++);
    gDPSetOtherMode(gfx++,
        G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT |
        G_TF_POINT | G_TT_NONE | G_TL_TILE | G_TD_CLAMP |
        G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
        G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, 0, 0, 0, 180);
    gfx = gfx_gSPTextureRectangle1(gfx,
        0, 0, 320 << 2, 240 << 2, 0, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    gDPNoOpTag(gfx++, PC_NOOP_WIDESCREEN_STRETCH_OFF);
    SET_FONT_DISP(gfx);
    CLOSE_DISP(graph);
}

/* Selected row gets a small scale bump — the font atlas has no bold
 * variant, so this is how we highlight the cursor position. */
#define SETTINGS_SCALE_SELECTED 1.15f

static void draw_centered(struct game_s* game, const char* s, f32 y,
                          int r, int g, int b, int a, f32 scale) {
    f32 w = (f32)pc_text_width(s) * scale;
    f32 x = (SCREEN_WIDTH_F - w) * 0.5f;
    pc_text_draw(game, s, x, y, r, g, b, a, scale);
}

static void draw_left(struct game_s* game, const char* s, f32 x, f32 y,
                      int r, int g, int b, int a, f32 scale) {
    pc_text_draw(game, s, x, y, r, g, b, a, scale);
}

static void row_colors(int selected, int* r, int* g, int* b, int* a) {
    if (selected) { *r = 255; *g = 235; *b = 120; *a = 255; }
    else          { *r = 200; *g = 200; *b = 200; *a = 200; }
}

static void value_colors(int selected, int changed, int* r, int* g, int* b, int* a) {
    if (changed) {
        *r = 255; *g = 215; *b = 90;
        *a = selected ? 255 : 200;
    } else {
        row_colors(selected, r, g, b, a);
    }
}

/* Tab row rendering: tabs are evenly spaced around the screen centre.
 * Active tab is highlighted; when the nav cursor is on the tab row, the
 * active tab also gets the bracketed "> name <" decoration. */
static void draw_tab_row(struct game_s* game, f32 y) {
    int on_tab_row = (s_sel == -1);

    /* Pre-measure widths and total horizontal extent so we can centre. */
    int widths[TAB_COUNT];
    int total = 0;
    const int gap_px = 18;
    for (int t = 0; t < TAB_COUNT; t++) {
        widths[t] = pc_text_width(s_tabs[t].name);
        total += widths[t];
    }
    total += gap_px * (TAB_COUNT - 1);

    f32 x = (SCREEN_WIDTH_F - (f32)total) * 0.5f;
    for (int t = 0; t < TAB_COUNT; t++) {
        const char* name = s_tabs[t].name;
        int active = (t == s_tab);
        int r, g, b, a;
        if (active && on_tab_row) { r = 255; g = 235; b = 120; a = 255; }
        else if (active)          { r = 255; g = 255; b = 255; a = 230; }
        else                      { r = 160; g = 160; b = 160; a = 180; }

        /* Active tab + cursor-on-tab-row gets the full bump; active but
         * cursor-off-tab-row stays normal size (the white color already
         * marks it). Inactive tabs render normal too. */
        f32 s = (active && on_tab_row) ? SETTINGS_SCALE_SELECTED : 1.0f;
        pc_text_draw(game, name, x, y, r, g, b, a, s);
        x += (f32)widths[t] + (f32)gap_px;
    }
}

/* What Enter does on the selected row, drawn at the bottom */
static const char* item_hint(int id) {
    switch (id) {
        case ITEM_ROM_DIR:
            return pc_rom_dir_can_pick() ? "Enter: pick the folder" : "Set rom_dir in settings.ini";
        case ITEM_KOREAN:
            return s_ko_installed ? "Plays the Korean disc from the next start" : "Install the Korean patch first";
        case ITEM_KO_INSTALL:
            return pc_rom_dir_can_pick() ? "Enter: choose the patch zip or .xdelta"
                                         : "Enter: install from the " PC_KO_PATCH_IN_DIR " folder";
        case ITEM_BINDINGS:
            return "Enter: keyboard keys and gamepad buttons";
        case ITEM_MINIMAP:
            return "Town map in the corner outside; M opens the map";
        case ITEM_AUTO_UPDATE:
            return "Looks for a new version when the game starts";
        case ITEM_UPDATE_CHECK:
            return "Enter: look for a new version now";
    }
    return NULL;
}

static void draw_settings_page(struct game_s* game) {
    char buf[64];
    const char* hint = NULL;
    int r, g, b, a;
    f32 lx = 70.0f;
    f32 vx = 200.0f;
    f32 y_tab = 50.0f;
    f32 y0    = 78.0f;
    f32 line_h = 16.0f;

    draw_centered(game, "- Settings -", 30.0f, 255, 255, 255, 255, 1.0f);
    draw_tab_row(game, y_tab);

    const Tab* tab = &s_tabs[s_tab];
    for (int i = 0; i < tab->count; i++) {
        const Item* it = &tab->items[i];
        int selected = (s_sel == i);

        char value_buf[64];
        item_format(it->id, value_buf, sizeof(value_buf));

        int changed = item_changed(it->id);
        f32 y = y0 + i * line_h;

        f32 s = selected ? SETTINGS_SCALE_SELECTED : 1.0f;
        row_colors(selected, &r, &g, &b, &a);
        draw_left(game, it->label, lx, y, r, g, b, a, s);
        value_colors(selected, changed, &r, &g, &b, &a);
        draw_left(game, value_buf, vx, y, r, g, b, a, s);

        if (selected) hint = item_hint(it->id);
    }

    /* Anchor Apply/Back to the tallest tab's footprint so they don't
     * shift up when the user moves to a tab with fewer items. */
    int max_items = 0;
    for (int t = 0; t < TAB_COUNT; t++) {
        if (s_tabs[t].count > max_items) max_items = s_tabs[t].count;
    }

    /* Apply — coloured green when there's something to apply. */
    f32 apy = y0 + max_items * line_h + 10.0f;
    int sel_apply = (s_sel == idx_apply());
    if (sel_apply && s_pending_dirty) {
        r = 120; g = 255; b = 140; a = 255;
    } else if (sel_apply) {
        r = 160; g = 160; b = 160; a = 220;
    } else if (s_pending_dirty) {
        r = 120; g = 220; b = 140; a = 200;
    } else {
        r = 120; g = 120; b = 120; a = 160;
    }
    draw_centered(game, "Apply", apy, r, g, b, a,
                  sel_apply ? SETTINGS_SCALE_SELECTED : 1.0f);

    /* Back */
    f32 bky = apy + line_h;
    int sel_back = (s_sel == idx_back());
    row_colors(sel_back, &r, &g, &b, &a);
    draw_centered(game, "Back", bky, r, g, b, a,
                  sel_back ? SETTINGS_SCALE_SELECTED : 1.0f);

    /* Persistent "restart required" banner. Stays up until the process
     * actually restarts — survives closing and re-opening the menu. Sits
     * below Back so it never competes with the row cursor. */
    if (s_pending_restart) {
        draw_centered(game, "Restart the game to apply all changes",
                      bky + line_h + 6.0f, 255, 195, 85, 230, 1.0f);
    }
    if (hint != NULL) {
        draw_centered(game, hint, bky + 2.0f * line_h + 8.0f, 170, 170, 170, 200, 1.0f);
    }

    (void)buf;
    (void)bky;
}

static void draw_res_confirm_page(struct game_s* game) {
    int r, g, b, a;
    char buf[48];

    draw_centered(game, "- Keep this resolution? -", 70.0f, 255, 255, 255, 255, 1.0f);

    snprintf(buf, sizeof(buf), "%dx%d",
             g_pc_settings.window_width, g_pc_settings.window_height);
    draw_centered(game, buf, 95.0f, 230, 230, 230, 255, 1.0f);

    Uint32 now = SDL_GetTicks();
    Uint32 ms_left = (now < s_res_deadline) ? s_res_deadline - now : 0;
    int secs = (int)((ms_left + 999) / 1000);
    snprintf(buf, sizeof(buf), "Reverting in %d...", secs);
    draw_centered(game, buf, 125.0f, 230, 200, 110, 255, 1.0f);

    f32 y = 160.0f;
    f32 gap = 70.0f;
    int lw_keep = pc_text_width("Keep");
    f32 cx = SCREEN_WIDTH_F * 0.5f;
    f32 keep_x   = cx - gap - (f32)lw_keep;
    f32 revert_x = cx + gap;

    row_colors(s_res_sel == 0, &r, &g, &b, &a);
    draw_left(game, "Keep",   keep_x,   y, r, g, b, a,
              s_res_sel == 0 ? SETTINGS_SCALE_SELECTED : 1.0f);
    row_colors(s_res_sel == 1, &r, &g, &b, &a);
    draw_left(game, "Revert", revert_x, y, r, g, b, a,
              s_res_sel == 1 ? SETTINGS_SCALE_SELECTED : 1.0f);
}

/* Discard-changes confirmation shown when the user tries to Back out
 * while s_pending_dirty. Mirrors the res-confirm layout but with no
 * auto-timer (the user must pick). */
static void draw_back_confirm_page(struct game_s* game) {
    int r, g, b, a;

    draw_centered(game, "- Discard changes? -", 80.0f, 255, 255, 255, 255, 1.0f);
    draw_centered(game, "You have unapplied changes.",
                  115.0f, 230, 230, 230, 255, 1.0f);

    f32 y = 160.0f;
    f32 gap = 70.0f;
    int lw_keep = pc_text_width("Keep editing");
    f32 cx = SCREEN_WIDTH_F * 0.5f;
    f32 keep_x    = cx - gap - (f32)lw_keep;
    f32 discard_x = cx + gap;

    row_colors(s_back_sel == 0, &r, &g, &b, &a);
    draw_left(game, "Keep editing", keep_x,    y, r, g, b, a,
              s_back_sel == 0 ? SETTINGS_SCALE_SELECTED : 1.0f);
    row_colors(s_back_sel == 1, &r, &g, &b, &a);
    draw_left(game, "Discard",      discard_x, y, r, g, b, a,
              s_back_sel == 1 ? SETTINGS_SCALE_SELECTED : 1.0f);
}

static void draw_bindings_page(struct game_s* game) {
    char buf[64];
    int r, g, b, a;
    const f32 lx = 34.0f;
    const f32 col_x[2] = { 132.0f, 228.0f };
    const f32 y0 = 62.0f;
    const f32 line_h = 16.0f;

    draw_centered(game, "- Controls -", 22.0f, 255, 255, 255, 255, 1.0f);
    draw_left(game, "Keyboard", col_x[0], 42.0f, 150, 150, 150, 220, 1.0f);
    draw_left(game, "Gamepad", col_x[1], 42.0f, 150, 150, 150, 220, 1.0f);

    for (int row = s_bind_scroll; row < s_bind_scroll + BIND_VISIBLE && row < BIND_ROWS; row++) {
        f32 y = y0 + (f32)(row - s_bind_scroll) * line_h;
        int selected = (row == s_bind_sel);

        if (row >= PC_BIND_COUNT) {
            row_colors(selected, &r, &g, &b, &a);
            draw_centered(game, row == BIND_ROW_RESET ? "Reset to defaults" : "Back", y, r, g, b, a,
                          selected ? SETTINGS_SCALE_SELECTED : 1.0f);
            continue;
        }

        row_colors(selected, &r, &g, &b, &a);
        draw_left(game, pc_keybindings_action_name(row), lx, y, r, g, b, a, 1.0f);
        for (int col = 0; col < 2; col++) {
            int cell = selected && s_bind_col == col;

            if (col == 0) pc_keybindings_key_label(row, buf, sizeof(buf));
            else          pc_keybindings_pad_label(row, buf, sizeof(buf));

            if (cell && s_capture) {
                /* blinks while waiting */
                snprintf(buf, sizeof(buf), "%s", (SDL_GetTicks() / 400) & 1 ? "..." : "");
                r = 255; g = 170; b = 80; a = 255;
            } else if (cell) {
                r = 255; g = 235; b = 120; a = 255;
            } else if (col == 1 && row >= PC_BIND_STICK_UP && row <= PC_BIND_CSTICK_RIGHT &&
                       strcmp(buf, "-") == 0) {
                /* no button: the analog stick still does it */
                snprintf(buf, sizeof(buf), "%s", row <= PC_BIND_STICK_RIGHT ? "L stick" : "R stick");
                r = 140; g = 140; b = 140; a = 180;
            } else {
                r = 200; g = 200; b = 200; a = 200;
            }
            draw_left(game, buf, col_x[col], y, r, g, b, a, cell ? SETTINGS_SCALE_SELECTED : 1.0f);
        }
    }

    /* more rows above / below */
    if (s_bind_scroll > 0) {
        draw_left(game, "...", 290.0f, y0, 150, 150, 150, 200, 1.0f);
    }
    if (s_bind_scroll + BIND_VISIBLE < BIND_ROWS) {
        draw_left(game, "...", 290.0f, y0 + (BIND_VISIBLE - 1) * line_h, 150, 150, 150, 200, 1.0f);
    }

    if (s_capture) {
        Uint32 now = SDL_GetTicks();
        int secs = (int)((s_capture_deadline > now ? s_capture_deadline - now : 0) + 999) / 1000;

        snprintf(buf, sizeof(buf), "%s for %s (%d)", s_bind_col == 0 ? "Press a key" : "Press a button",
                 pc_keybindings_action_name(s_bind_sel), secs);
        draw_centered(game, buf, 208.0f, 255, 200, 110, 255, 1.0f);
        draw_centered(game, "Esc: cancel", 224.0f, 170, 170, 170, 200, 1.0f);
    } else if (s_bind_sel < PC_BIND_COUNT) {
        draw_centered(game, "Enter: change   Left, Right: keyboard or pad", 216.0f, 170, 170, 170, 200, 1.0f);
    }
}

/* A filled rectangle in 320x240 screen space */
static void draw_rect(GRAPH* graph, int x, int y, int w, int h, int r, int g, int b, int a) {
    Gfx* gfx;

    if (w <= 0 || h <= 0) return;
    OPEN_DISP(graph);
    gfx = NOW_FONT_DISP;
    gDPPipeSync(gfx++);
    gDPSetOtherMode(gfx++,
        G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT |
        G_TF_POINT | G_TT_NONE | G_TL_TILE | G_TD_CLAMP |
        G_TP_NONE | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
        G_AC_NONE | G_ZS_PRIM | G_RM_XLU_SURF | G_RM_XLU_SURF2);
    gDPSetCombineMode(gfx++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gfx++, 0, 0, r, g, b, a);
    gfx = gfx_gSPTextureRectangle1(gfx, x << 2, y << 2, (x + w) << 2, (y + h) << 2, 0, 0, 0, 0, 0);
    gDPPipeSync(gfx++);
    SET_FONT_DISP(gfx);
    CLOSE_DISP(graph);
}

static void draw_patch_page(struct game_s* game) {
    PCKoPatchState state = pc_ko_patch_state();
    int r, g, b, a;

    draw_centered(game, "- Korean patch -", 60.0f, 255, 255, 255, 255, 1.0f);

    if (state == PC_KO_PATCH_RUNNING) {
        char buf[16];
        int permille = pc_ko_patch_progress();

        draw_centered(game, pc_ko_patch_message(), 98.0f, 230, 230, 230, 255, 1.0f);
        draw_rect(game->graph, 60, 124, 200, 8, 110, 110, 110, 255);
        draw_rect(game->graph, 60, 124, 200 * permille / 1000, 8, 120, 220, 140, 255);
        snprintf(buf, sizeof(buf), "%d%%", permille / 10);
        draw_centered(game, buf, 138.0f, 200, 200, 200, 230, 1.0f);
        draw_centered(game, "Esc: cancel", 184.0f, 170, 170, 170, 200, 1.0f);
        return;
    }

    if (state == PC_KO_PATCH_DONE) {
        r = 120; g = 230; b = 140;
    } else {
        r = 255; g = 180; b = 90;
    }
    draw_centered(game, pc_ko_patch_message(), 98.0f, r, g, b, 255, 1.0f);
    draw_centered(game, pc_ko_patch_detail(), 116.0f, 230, 230, 230, 255, 1.0f);

    if (state == PC_KO_PATCH_DONE) {
        f32 y = 160.0f;
        f32 gap = 40.0f;
        int lw = pc_text_width("Restart now");
        f32 cx = SCREEN_WIDTH_F * 0.5f;

        row_colors(s_patch_sel == 0, &r, &g, &b, &a);
        draw_left(game, "Restart now", cx - gap - (f32)lw, y, r, g, b, a,
                  s_patch_sel == 0 ? SETTINGS_SCALE_SELECTED : 1.0f);
        row_colors(s_patch_sel == 1, &r, &g, &b, &a);
        draw_left(game, "Later", cx + gap, y, r, g, b, a, s_patch_sel == 1 ? SETTINGS_SCALE_SELECTED : 1.0f);
        draw_centered(game, "Progress since the last save is lost", 190.0f, 170, 170, 170, 200, 1.0f);
    } else {
        row_colors(1, &r, &g, &b, &a);
        draw_centered(game, "OK", 160.0f, r, g, b, a, SETTINGS_SCALE_SELECTED);
    }
}

static void draw_buttons(struct game_s* game, const char* const* names, int n, int sel, f32 y) {
    f32 widths[3], total = 0.0f, x;
    const f32 gap = 28.0f;
    int i, r, g, b, a;

    for (i = 0; i < n; i++) {
        widths[i] = (f32)pc_text_width(names[i]);
        total += widths[i] + (i > 0 ? gap : 0.0f);
    }
    x = (SCREEN_WIDTH_F - total) * 0.5f;
    for (i = 0; i < n; i++) {
        row_colors(i == sel, &r, &g, &b, &a);
        draw_left(game, names[i], x, y, r, g, b, a, i == sel ? SETTINGS_SCALE_SELECTED : 1.0f);
        x += widths[i] + gap;
    }
}

static void draw_update_page(struct game_s* game) {
    static const char* const available[] = { "Update now", "Later", "Skip this version" };
    static const char* const installed[] = { "Restart now", "Later" };
    static const char* const ok[] = { "OK" };
    PCUpdateState st = pc_update_state();
    char buf[96];

    draw_centered(game, "- Update -", 60.0f, 255, 255, 255, 255, 1.0f);
    switch (st) {
        case PC_UPDATE_AVAILABLE:
            snprintf(buf, sizeof(buf), "Version %s is out (you have v%s)", pc_update_new_version(), pc_update_version());
            draw_centered(game, buf, 92.0f, 120, 230, 140, 255, 1.0f);
            draw_centered(game, "Saves, settings and the disc stay as they are", 112.0f, 230, 230, 230, 255, 1.0f);
            draw_buttons(game, available, 3, s_update_sel, 150.0f);
            break;
        case PC_UPDATE_DOWNLOADING: {
            int permille = pc_update_progress();

            draw_centered(game, pc_update_message(), 98.0f, 230, 230, 230, 255, 1.0f);
            draw_rect(game->graph, 60, 124, 200, 8, 110, 110, 110, 255);
            draw_rect(game->graph, 60, 124, 200 * permille / 1000, 8, 120, 220, 140, 255);
            snprintf(buf, sizeof(buf), "%d%%", permille / 10);
            draw_centered(game, buf, 138.0f, 200, 200, 200, 230, 1.0f);
            draw_centered(game, "Esc: cancel", 184.0f, 170, 170, 170, 200, 1.0f);
            break;
        }
        case PC_UPDATE_INSTALLED:
            draw_centered(game, pc_update_message(), 98.0f, 120, 230, 140, 255, 1.0f);
            draw_centered(game, "It runs from the next start of the game", 116.0f, 230, 230, 230, 255, 1.0f);
            draw_buttons(game, installed, 2, s_update_sel, 160.0f);
            draw_centered(game, "Progress since the last save is lost", 190.0f, 170, 170, 170, 200, 1.0f);
            break;
        case PC_UPDATE_CHECKING:
            draw_centered(game, pc_update_message(), 98.0f, 230, 230, 230, 255, 1.0f);
            break;
        case PC_UPDATE_FAILED:
            draw_centered(game, pc_update_message(), 98.0f, 255, 180, 90, 255, 1.0f);
            draw_centered(game, "github.com/cailet0422/afe-port/releases", 116.0f, 230, 230, 230, 255, 1.0f);
            draw_buttons(game, ok, 1, 0, 160.0f);
            break;
        default:
            draw_centered(game, pc_update_message(), 98.0f, 230, 230, 230, 255, 1.0f);
            draw_buttons(game, ok, 1, 0, 160.0f);
            break;
    }
}

void pc_settings_menu_draw(struct game_s* game, int with_dim_backdrop) {
    if (!s_active || !game || !game->graph) return;
    if (with_dim_backdrop) draw_dim_rect(game->graph);
    if (s_sub == SUB_SETTINGS)          draw_settings_page(game);
    else if (s_sub == SUB_CONFIRM_RES)  draw_res_confirm_page(game);
    else if (s_sub == SUB_BINDINGS)     draw_bindings_page(game);
    else if (s_sub == SUB_PATCH)        draw_patch_page(game);
    else if (s_sub == SUB_UPDATE)       draw_update_page(game);
    else                                draw_back_confirm_page(game);
}
