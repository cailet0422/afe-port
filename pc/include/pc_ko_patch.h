/* pc_ko_patch.h - installing the community Korean patch from the settings menu
 *
 * The player picks the patch as downloaded (the zip, or the .xdelta in it)
 * with the system file picker on Windows and Android, or puts it in patch/
 * next to the game elsewhere. A thread decodes it against the player's own
 * disc image (the one the game would start from, pc_disc_open_original) into
 * korean/<name>.iso, checks it against the patch's manifest.json and, once
 * the menu polls the result, records it in settings.ini (korean = 1,
 * ko_image), so the next start runs in Korean. docs/korean_patch.md */
#ifndef PC_KO_PATCH_H
#define PC_KO_PATCH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_KO_PATCH_IDLE,
    PC_KO_PATCH_RUNNING,
    PC_KO_PATCH_DONE,
    PC_KO_PATCH_FAILED,
} PCKoPatchState;

/* Folder the made disc goes into, and where patches are looked for without a picker */
#define PC_KO_PATCH_OUT_DIR "korean"
#define PC_KO_PATCH_IN_DIR "patch"

/* Picks the patch and starts installing it. Returns 0 if the player
 * cancelled the picker (state stays IDLE); otherwise the state is RUNNING,
 * or FAILED with a message. Blocks while a picker is open. */
int pc_ko_patch_start(void);

/* Call every frame while the result is shown: finishes up after the thread */
void pc_ko_patch_poll(void);

PCKoPatchState pc_ko_patch_state(void);
/* 0-1000 */
int pc_ko_patch_progress(void);
/* What it is doing, or why it failed (two lines at most, ASCII) */
const char* pc_ko_patch_message(void);
const char* pc_ko_patch_detail(void);
void pc_ko_patch_cancel(void);
/* Back to IDLE once the result has been shown */
void pc_ko_patch_reset(void);

/* 1 if settings.ini names a Korean disc made here and it is still there */
int pc_ko_patch_installed(void);

#ifdef __cplusplus
}
#endif

#endif
