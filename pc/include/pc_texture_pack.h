/* pc_texture_pack.h - Dolphin-style HD texture packs
 *
 * A pack replaces the game's textures by the hash of their data: files named
 * tex1_{W}x{H}_{hash}[_{tlut hash}|_$]_{format}.dds (or .png), as Dolphin's
 * "Load Custom Textures" reads them. The game reads them from texture_pack/
 * next to it (the app's folder on Android):
 *   - zip files as they are downloaded (texture_pack/*.zip), read in place:
 *     nothing is unpacked, so a 160 MB zip stays 160 MB on disk;
 *   - or the files unpacked into folders below texture_pack/, as for Dolphin.
 * Settings > HD textures installs a zip picked with the system's file picker
 * (Windows, Android; it is copied into texture_pack/), turns the pack on and
 * off and shows where it comes from.
 *
 * Textures load on a thread while playing (the original shows until the HD
 * one is ready), or all at start with preload_textures. BC7/BC1/BC3 DDS go
 * to the GPU as they are when it can take them; otherwise (most phones) they
 * are decoded to RGBA on the thread, at half size with hd_quality = 1.
 *
 * Because textures are matched by the hash of their exact data, a texture the
 * Korean patch changed (its logo, name plates, Hangul font and UI text) never
 * matches a pack made for the original game: it is drawn as the patch made it.
 * The pack's Localisation/ folders (other languages' text) are also left out
 * while the game runs in Korean. docs/texture_pack.md */
#ifndef PC_TEXTURE_PACK_H
#define PC_TEXTURE_PACK_H

#include "pc_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PC_TEXPACK_DIR "texture_pack"

void pc_texture_pack_init(void);
void pc_texture_pack_preload_all(void);
void pc_texture_pack_shutdown(void);

/* GL texture for an HD replacement, or 0. *out_index gets the pack's entry
 * (for pc_texture_pack_touch) or -1. When the replacement is still loading
 * on the thread, 0 and *pending_key gets a key that pc_gx_texture_hd_ready()
 * is called with once it is there (0 otherwise). */
GLuint pc_texture_pack_lookup(const void* data, int data_size, int w, int h, unsigned int fmt, const void* tlut_data,
                              int tlut_entries, int tlut_is_be, int* out_w, int* out_h, int* out_index,
                              unsigned long long* pending_key);

/* The game drew entry index again (the least recently used are freed first) */
void pc_texture_pack_touch(int index);

/* A pack is installed and turned on */
int pc_texture_pack_active(void);

/* Once a frame on the game thread (pc_vi.c): uploads what the thread loaded,
 * frees the least recently used textures over the memory budget */
void pc_texture_pack_frame(void);

/* Turns the pack on or off at once (hd_textures in settings.ini) */
void pc_texture_pack_set_enabled(int on);

/* Reads texture_pack/ again (after files were added or removed) */
void pc_texture_pack_reload(void);

/* --- For the settings page ------------------------------------------- */

#define PC_TEXPACK_MAX_SOURCES 16

typedef struct {
    char name[128];   /* "ACHD V25.zip", or a folder's name */
    int is_zip;
    int entries;      /* textures it has */
    long long bytes;  /* file size (zip) */
    char title[96];   /* from its Readme.txt: "Animal Crossing HD Texture Pack" */
    char version[64]; /* "Version 25 (Oct 11th 2026)" */
} PCTexPackSource;

typedef struct {
    int sources;
    PCTexPackSource source[PC_TEXPACK_MAX_SOURCES];
    int entries;          /* textures in all of them */
    int skipped_korean;   /* left out while running in Korean (Localisation/) */
    int loaded;           /* on the GPU now */
    int replaced;         /* game textures that matched since the start */
    int loading;          /* waiting for the thread */
    long long gpu_bytes;  /* memory of the loaded ones */
    long long budget;     /* before the least recently used are freed */
    int gpu_bc7;          /* the GPU takes BC7 as it is */
} PCTexPackInfo;

void pc_texture_pack_info(PCTexPackInfo* out);

/* Installing a zip: picks it (blocks while the picker is open; 0 if
 * cancelled or there is no picker), then copies it on a thread */
typedef enum {
    PC_TEXPACK_INSTALL_IDLE,
    PC_TEXPACK_INSTALL_RUNNING,
    PC_TEXPACK_INSTALL_DONE,
    PC_TEXPACK_INSTALL_FAILED,
} PCTexPackInstallState;

int pc_texture_pack_install_start(void);
void pc_texture_pack_install_poll(void);
PCTexPackInstallState pc_texture_pack_install_state(void);
int pc_texture_pack_install_progress(void); /* 0-1000 */
const char* pc_texture_pack_install_message(void);
void pc_texture_pack_install_cancel(void);
void pc_texture_pack_install_reset(void);

/* Deletes texture_pack/<name> (a zip the game installed); 1 if it did */
int pc_texture_pack_remove(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* PC_TEXTURE_PACK_H */
