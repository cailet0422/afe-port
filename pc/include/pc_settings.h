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
    int disable_resetti;  /* 0=normal (Resetti appears on reset), 1=disable reset penalty */
    int nes_aspect;       /* NES emulator aspect: 0=fullscreen stretch, 1=4:3 pillarbox (default) */
    int master_volume;    /* Applied at the PC audio output, 0-100 (default 100) */
    int text_input;       /* 0 = built-in Hangul input from keys, 1 = the system's input method */
    int touch_controls;   /* 0 = off, 1 = on-screen pad for touch screens */
    int minimap;          /* 0 = off, 1 = the town's map in the bottom left corner outside (pc_minimap.c) */
    char rom_dir[1024];   /* folder holding the disc image, or the image itself (UTF-8); "" = rom/ by the game */
    int korean;           /* 1 = run the Korean disc made in the settings menu (ko_image) */
    char ko_image[1024];  /* that disc (UTF-8), written by the patch installer (pc_ko_patch.c) */
    int auto_update;      /* 1 = look for a new version at start (pc_update.c, Android: Updater.java) */
    char update_skip[32]; /* a version not to ask about again ("v0.1.8") */
} PCSettings;

extern PCSettings g_pc_settings;

void pc_settings_load(void);
void pc_settings_save(void);
void pc_settings_apply(void);
void pc_settings_cycle_resolution(int* width, int* height, int dir);

#ifdef __cplusplus
}
#endif

#endif /* PC_SETTINGS_H */
