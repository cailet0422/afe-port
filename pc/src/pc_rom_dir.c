/* pc_rom_dir.c - see pc_rom_dir.h */
#include "pc_platform.h"
#include "pc_rom_dir.h"
#include "pc_settings.h"

#ifdef _WIN32
#define COBJMACROS
#include <shobjidl.h>
#include <SDL_syswm.h>
#endif
#ifdef __ANDROID__
#include <jni.h>
#include <unistd.h>
#endif

static int s_changed = 0;

FILE* pc_fopen_utf8(const char* path, const char* mode) {
#ifdef _WIN32
    wchar_t wpath[1024];
    wchar_t wmode[8];

    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, (int)(sizeof(wpath) / sizeof(wpath[0]))) == 0 ||
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, (int)(sizeof(wmode) / sizeof(wmode[0]))) == 0) {
        return NULL;
    }
    return _wfopen(wpath, wmode);
#else
    return fopen(path, mode);
#endif
}

#ifdef _WIN32
/* The Explorer folder dialog */
static int pick_windows(char* out, size_t n) {
    HWND owner = NULL;
    Uint32 fullscreen = 0;
    IFileOpenDialog* dlg = NULL;
    HRESULT init;
    int ok = 0;

    if (g_pc_window != NULL) {
        SDL_SysWMinfo info;

        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(g_pc_window, &info)) {
            owner = info.info.win.window;
        }
        /* The dialog can't show over a fullscreen window */
        fullscreen = SDL_GetWindowFlags(g_pc_window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);
        if (fullscreen) {
            SDL_SetWindowFullscreen(g_pc_window, 0);
        }
    }

    init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog,
                                   (void**)&dlg))) {
        FILEOPENDIALOGOPTIONS opts = 0;
        IShellItem* item = NULL;

        IFileOpenDialog_GetOptions(dlg, &opts);
        IFileOpenDialog_SetOptions(dlg, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        IFileOpenDialog_SetTitle(dlg, L"디스크 이미지(.iso/.ciso/.gcm)가 있는 폴더 선택");
        if (SUCCEEDED(IFileOpenDialog_Show(dlg, owner)) && SUCCEEDED(IFileOpenDialog_GetResult(dlg, &item))) {
            PWSTR path = NULL;

            if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path))) {
                ok = WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)n, NULL, NULL) > 0;
                CoTaskMemFree(path);
            }
            IShellItem_Release(item);
        }
        IFileOpenDialog_Release(dlg);
    }
    if (SUCCEEDED(init)) {
        CoUninitialize();
    }

    if (fullscreen) {
        SDL_SetWindowFullscreen(g_pc_window, fullscreen);
    }
    return ok;
}
#endif

#ifdef __ANDROID__
/* MainActivity's static methods, called from the game thread */
typedef struct {
    JNIEnv* env;
    jobject activity;
    jclass cls;
} Jni;

static int jni_begin(Jni* j) {
    j->env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    j->activity = (jobject)SDL_AndroidGetActivity();
    j->cls = NULL;
    if (j->env == NULL || j->activity == NULL) {
        return 0;
    }
    j->cls = (*j->env)->GetObjectClass(j->env, j->activity);
    return j->cls != NULL;
}

static void jni_end(Jni* j) {
    if (j->env == NULL) {
        return;
    }
    if ((*j->env)->ExceptionCheck(j->env)) {
        (*j->env)->ExceptionClear(j->env);
    }
    if (j->cls != NULL) {
        (*j->env)->DeleteLocalRef(j->env, j->cls);
    }
    if (j->activity != NULL) {
        (*j->env)->DeleteLocalRef(j->env, j->activity);
    }
}

static int jni_int(Jni* j, const char* name) {
    jmethodID m = (*j->env)->GetStaticMethodID(j->env, j->cls, name, "()I");
    jint r;

    if (m == NULL) {
        (*j->env)->ExceptionClear(j->env);
        return -1;
    }
    r = (*j->env)->CallStaticIntMethod(j->env, j->cls, m);
    if ((*j->env)->ExceptionCheck(j->env)) {
        (*j->env)->ExceptionClear(j->env);
        return -1;
    }
    return r;
}

static void jni_string(Jni* j, const char* name, char* out, size_t n) {
    jmethodID m = (*j->env)->GetStaticMethodID(j->env, j->cls, name, "()Ljava/lang/String;");
    jstring s;

    out[0] = '\0';
    if (m == NULL) {
        (*j->env)->ExceptionClear(j->env);
        return;
    }
    s = (jstring)(*j->env)->CallStaticObjectMethod(j->env, j->cls, m);
    if ((*j->env)->ExceptionCheck(j->env)) {
        (*j->env)->ExceptionClear(j->env);
        return;
    }
    if (s != NULL) {
        const char* c = (*j->env)->GetStringUTFChars(j->env, s, NULL);

        if (c != NULL) {
            snprintf(out, n, "%s", c);
            (*j->env)->ReleaseStringUTFChars(j->env, s, c);
        }
        (*j->env)->DeleteLocalRef(j->env, s);
    }
}

/* MainActivity.pickState(): 1 while the picker is open, 2 = picked, 3 = cancelled */
static int pick_android(void) {
    Jni j;
    int state = 0;

    if (jni_begin(&j) && jni_int(&j, "pickDiscFolder") == 1) {
        do {
            SDL_Delay(50);
            state = jni_int(&j, "pickState");
        } while (state == 1);
    }
    jni_end(&j);
    return state == 2;
}

FILE* pc_rom_dir_android_open(char* name, size_t n) {
    Jni j;
    FILE* fp = NULL;

    if (jni_begin(&j)) {
        int fd = jni_int(&j, "openDiscFd");

        if (fd >= 0) {
            fp = fdopen(fd, "rb");
            if (fp == NULL) {
                close(fd);
            } else if (name != NULL) {
                jni_string(&j, "discFileName", name, n);
            }
        }
    }
    jni_end(&j);
    return fp;
}
#endif

int pc_rom_dir_can_pick(void) {
#if defined(_WIN32) || defined(__ANDROID__)
    return 1;
#else
    return 0;
#endif
}

int pc_rom_dir_pick(void) {
#ifdef _WIN32
    char path[1024];

    if (!pick_windows(path, sizeof(path))) {
        return 0;
    }
    snprintf(g_pc_settings.rom_dir, sizeof(g_pc_settings.rom_dir), "%s", path);
    pc_settings_save();
    s_changed = 1;
    return 1;
#elif defined(__ANDROID__)
    if (!pick_android()) {
        return 0;
    }
    s_changed = 1;
    return 1;
#else
    return 0;
#endif
}

int pc_rom_dir_changed(void) {
    return s_changed;
}

int pc_rom_dir_ask(const char* default_dir) {
    static const SDL_MessageBoxButtonData pick_buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "폴더 선택 / Choose folder" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "종료 / Quit" },
    };
    char msg[3072];
    char where[1200];
    SDL_MessageBoxData box;

    if (g_pc_settings.rom_dir[0] != '\0') {
        snprintf(where, sizeof(where), "%s", g_pc_settings.rom_dir);
    } else {
        snprintf(where, sizeof(where), "%s/rom", default_dir);
    }

    if (pc_rom_dir_can_pick()) {
        snprintf(msg, sizeof(msg),
                 "동물의 숲 e+ 디스크 이미지(.iso/.ciso/.gcm)를 찾지 못했습니다.\n"
                 "이미지가 있는 폴더를 선택해 주세요. 지금 찾아본 곳:\n%s\n\n"
                 "No Doubutsu no Mori e+ disc image (.iso, .ciso or .gcm) was found.\n"
                 "Choose the folder that holds it.",
                 where);
    } else {
        snprintf(msg, sizeof(msg),
                 "동물의 숲 e+ 디스크 이미지(.iso/.ciso/.gcm)를 찾지 못했습니다.\n"
                 "아래 폴더에 넣거나 settings.ini의 rom_dir에 이미지가 있는 폴더를 적어 주세요:\n%s\n\n"
                 "No Doubutsu no Mori e+ disc image (.iso, .ciso or .gcm) was found.\n"
                 "Put it in the folder above, or set rom_dir in settings.ini.",
                 where);
    }
    fprintf(stderr, "%s\n", msg);

    if (!pc_rom_dir_can_pick()) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, PC_WINDOW_TITLE, msg, g_pc_window);
        return 0;
    }

    SDL_zero(box);
    box.flags = SDL_MESSAGEBOX_WARNING;
    box.window = g_pc_window;
    box.title = PC_WINDOW_TITLE;
    box.message = msg;
    box.numbuttons = (int)(sizeof(pick_buttons) / sizeof(pick_buttons[0]));
    box.buttons = pick_buttons;
    for (;;) {
        int button = 0;

        if (SDL_ShowMessageBox(&box, &button) != 0 || button != 1) {
            return 0;
        }
        if (pc_rom_dir_pick()) {
            return 1;
        }
        /* cancelled the picker: ask again */
    }
}

void pc_rom_dir_describe(char* buf, size_t n) {
    char raw[1024];
    char ascii[1024];
    size_t i;
    size_t len = 0;

#ifdef __ANDROID__
    Jni j;

    raw[0] = '\0';
    if (jni_begin(&j)) {
        jni_string(&j, "discFolderName", raw, sizeof(raw));
    }
    jni_end(&j);
    if (raw[0] == '\0') {
        snprintf(buf, n, "app folder/rom");
        return;
    }
#else
    if (g_pc_settings.rom_dir[0] == '\0') {
        snprintf(buf, n, "rom (default)");
        return;
    }
    snprintf(raw, sizeof(raw), "%s", g_pc_settings.rom_dir);
#endif

    /* The menu font is ASCII: one '?' per other character */
    for (i = 0; raw[i] != '\0' && len + 1 < sizeof(ascii); i++) {
        unsigned char c = (unsigned char)raw[i];

        if (c < 0x80) {
            ascii[len++] = (c == '\\') ? '/' : (char)c;
        } else if ((c & 0xC0) != 0x80) {
            ascii[len++] = '?';
        }
    }
    ascii[len] = '\0';

    /* Keep the end of long paths */
    if (len > 14) {
        snprintf(buf, n, "...%s", ascii + len - 11);
    } else {
        snprintf(buf, n, "%s", ascii);
    }
}
