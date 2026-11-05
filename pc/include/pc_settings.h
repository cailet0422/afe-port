#ifndef PC_SETTINGS_H
#define PC_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int window_width;
    int window_height;
    int fullscreen;       /* 0=windowed, 1=fullscreen, 2=borderless */
    int vsync;            /* 0=off, 1=on */
    int msaa;             /* 0=off, 2/4/8=samples */
    int preload_textures; /* 0=off (load on demand), 1=on (load all at startup), 2=on + cache file */
    int disable_resetti;  /* 1 = Mr. Resetti never comes (default: the port saves by itself), 0 = as on the GameCube */
    int autosave;         /* 1 = save by itself (pc_autosave.c): between scenes, every minute, on quitting */
    int autosave_minutes; /* how often outdoors, 1-60 (settings.ini autosave_every; 1 by default) */
    int nes_aspect;       /* NES emulator aspect: 0=fullscreen stretch, 1=4:3 pillarbox (default) */
    int master_volume;    /* Applied at the PC audio output, 0-100 (default 100) */
    int text_input;       /* 0 = built-in Hangul input from keys, 1 = the system's input method */
    int touch_controls;   /* 0 = off, 1 = on-screen pad for touch screens */
    int minimap;          /* 0 = off, 1 = the town's map in the bottom left corner outside (pc_minimap.c) */
    int seamless_blocks;  /* 1 = outside, walk on into the next block (acre) with the camera following, with
                             no stop at its edge (read by afe-decomp through pc_seamless_blocks) */
    char rom_dir[1024];   /* folder holding the disc image, or the image itself (UTF-8); "" = rom/ by the game */
    int korean;           /* 1 = run the Korean disc made in the settings menu (ko_image) */
    char ko_image[1024];  /* that disc (UTF-8), written by the patch installer (pc_ko_patch.c) */
    int auto_update;      /* 1 = look for a new version at start (pc_update.c, Android: Updater.java) */
    char update_skip[32]; /* a version not to ask about again ("v0.1.8") */
    int ui_language;      /* menus (pc_gui): 0 = automatic, 1 = Korean, 2 = English */
    int ui_scale;         /* menu size in percent, 0 = automatic (from the window and the screen) */
    int hd_textures;      /* 1 = use the installed HD texture pack (pc_texture_pack.c) */
    int hd_quality;       /* 0 = the pack's full size, 1 = half (less memory; phones) */
    int specialized_shaders; /* 1 = a GX shader made for each configuration (pc_gx_tev.c), 0 = the uber shader only */
    int mp_port;          /* multiplayer: the UDP port a hosted room listens on (pc_mp.c) */
    char mp_name[32];     /* multiplayer: the name others see (UTF-8), "" = the player's own */
} PCSettings;

extern PCSettings g_pc_settings;

void pc_settings_load(void);
/* Seamless block crossing is on (read by the game's field, camera and player code) */
int pc_seamless_blocks(void);
void pc_settings_save(void);
void pc_settings_apply(void);
void pc_settings_cycle_resolution(int* width, int* height, int dir);
/* The window sizes the settings offer, smallest first */
int pc_settings_resolution_count(void);
void pc_settings_resolution_at(int i, int* width, int* height);

#ifdef __cplusplus
}
#endif

#endif /* PC_SETTINGS_H */
