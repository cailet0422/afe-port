/* pc_rom_dir.h - where the disc image comes from, and letting the player choose it
 *
 * Windows and Linux keep the folder in settings.ini (rom_dir). On Android a
 * folder outside the app's own directory can only be reached through the
 * system folder picker, so the chosen folder lives in the app's preferences
 * (MainActivity.java) and the image is opened through it. */
#ifndef PC_ROM_DIR_H
#define PC_ROM_DIR_H

#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 if this platform has a folder picker (Windows, Android) */
int pc_rom_dir_can_pick(void);

/* Shows the folder picker and stores the choice (settings.ini or the app's
 * preferences). Blocks until the player picks or cancels; 1 = picked. */
int pc_rom_dir_pick(void);

/* 1 once a folder was picked in this run (the game reads the disc at startup) */
int pc_rom_dir_changed(void);

/* Startup: no disc image was found. Explains, offers the picker and returns
 * 1 to look again, or 0 to quit. */
int pc_rom_dir_ask(const char* default_dir);

/* The chosen folder's name, shortened, as UTF-8 for the settings menu */
void pc_rom_dir_describe(char* buf, size_t n);

/* fopen for UTF-8 paths (Windows paths can hold any characters) */
FILE* pc_fopen_utf8(const char* path, const char* mode);

#ifdef __ANDROID__
/* Opens the first disc image in the folder chosen with the picker; NULL if
 * none was chosen or it holds no image */
FILE* pc_rom_dir_android_open(char* name, size_t n);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PC_ROM_DIR_H */
