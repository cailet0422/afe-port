/* pc_android.c - Android startup (see docs/pc_port/android.md)
 *
 * The game reads and writes everything relative to its working directory
 * (settings.ini, rom/, save/, shaders/, the REL overlay table). On Android
 * that directory is the app's folder on shared storage,
 * Android/data/<package>/files, which a computer can reach over USB; the
 * files that ship in the APK's assets are copied there at startup. */
#include "pc_platform.h"

#ifdef __ANDROID__
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* const asset_files[] = {
    "shaders/default.vert",
    "shaders/default.frag",
    "DobutsuNoMoriEPlus.relmap",
};

static void copy_asset(const char* name) {
    SDL_RWops* in = SDL_RWFromFile(name, "rb"); /* relative: from the APK */
    FILE* out;
    char buf[16384];
    size_t n;

    if (in == NULL) {
        printf("[ANDROID] missing asset %s: %s\n", name, SDL_GetError());
        return;
    }
    out = fopen(name, "wb");
    if (out == NULL) {
        printf("[ANDROID] cannot write %s: %s\n", name, strerror(errno));
        SDL_RWclose(in);
        return;
    }
    while ((n = SDL_RWread(in, buf, 1, sizeof(buf))) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(out);
    SDL_RWclose(in);
}

void pc_android_prepare(void) {
    const char* dir = SDL_AndroidGetExternalStoragePath();
    size_t i;

    if (dir == NULL || chdir(dir) != 0) {
        dir = SDL_AndroidGetInternalStoragePath();
        if (dir == NULL || chdir(dir) != 0) {
            return;
        }
    }
    /* stdout/stderr go nowhere on Android */
    freopen("log.txt", "w", stdout);
    dup2(fileno(stdout), fileno(stderr));
    printf("[ANDROID] data folder: %s\n", dir);

    mkdir("rom", 0755);
    mkdir("save", 0755);
    mkdir("shaders", 0755);
    for (i = 0; i < sizeof(asset_files) / sizeof(asset_files[0]); i++) {
        copy_asset(asset_files[i]);
    }

    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    /* The back button opens the pause menu instead of closing the app, and
     * touches are only the on-screen pad's (pc_touch.c), not mouse clicks */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
}
#endif
