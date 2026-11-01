/* pc_update.h - updates the game from the project's GitHub releases
 *
 * Release builds (PC_UPDATER: Windows and Linux builds without assets, see
 * pc/CMakeLists.txt) look at github.com/cailet0422/afe-port/releases when they
 * start, unless settings.ini auto_update = 0 (Settings > Gameplay > Updates).
 * When there is a newer version for this platform, the pause menu opens on
 * the update page: Update now downloads the release's zip on a thread (checked
 * against the release's SHA256SUMS.txt and the zip's CRCs), puts its files in
 * place of the old ones (the old ones are kept as *.old until the next start)
 * and offers to restart. Saves, the disc image, the Korean disc and the
 * settings are not in the zip and stay as they are. "Skip this version" does
 * not ask about that version again (update_skip).
 *
 * The Android build does the same in Java before it matters to the game
 * (Updater.java: the APK and the system's installer). */
#ifndef PC_UPDATE_H
#define PC_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_UPDATE_IDLE,        /* not checked */
    PC_UPDATE_CHECKING,
    PC_UPDATE_CURRENT,     /* no newer version */
    PC_UPDATE_AVAILABLE,   /* pc_update_new_version() */
    PC_UPDATE_DOWNLOADING, /* pc_update_progress() */
    PC_UPDATE_INSTALLED,   /* takes effect on the next start */
    PC_UPDATE_FAILED,      /* pc_update_message() */
} PCUpdateState;

/* This build's version ("0.1.7", pc/VERSION) */
const char* pc_update_version(void);

/* Whether this build can update itself (a release build of a platform the
 * releases have a file for) */
int pc_update_supported(void);

/* At startup: removes the files the last update replaced and starts the
 * check if settings allow it */
void pc_update_init(void);

/* Every frame on the game thread: opens the update page once a check finds
 * a version that was not skipped */
void pc_update_frame(void);

PCUpdateState pc_update_state(void);
const char* pc_update_new_version(void); /* "v0.1.8" */
int pc_update_progress(void);            /* 0-1000 while downloading */
const char* pc_update_message(void);     /* what went wrong, or what is happening */

void pc_update_check_now(void);
int pc_update_start_download(void); /* from PC_UPDATE_AVAILABLE; 0 if it cannot */
void pc_update_cancel(void);        /* a running download */
void pc_update_skip(void);          /* don't ask about this version again */

/* The program to start after an installed update (NULL: the running one) */
const char* pc_update_restart_path(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_UPDATE_H */
