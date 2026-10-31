/* pc_android.c - Android startup (see docs/android.md)
 *
 * The game reads and writes everything relative to its working directory
 * (settings.ini, rom/, save/, shaders/, the REL overlay table). On Android
 * that directory is the app's folder on shared storage,
 * Android/data/<package>/files, which a computer can reach over USB; the
 * files that ship in the APK's assets are copied there at startup.
 *
 * A crash writes crash.txt there (the signal, where it happened and the call
 * stack); the next start shows it with the end of log.txt so the player can
 * copy it into a bug report (MainActivity.showLastCrash). */
#include "pc_platform.h"

#ifdef __ANDROID__
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>

static const char* const asset_files[] = {
    "shaders/default.vert",
    "shaders/default.frag",
    PC_RELMAP_NAME, /* DobutsuNoMoriEPlus-<ABI>.relmap */
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

/* --- Crash report --- */
static const int crash_signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP };
static struct sigaction crash_old_actions[sizeof(crash_signals) / sizeof(crash_signals[0])];

static void crash_write(int fd, const char* s) {
    ssize_t r = write(fd, s, strlen(s));
    (void)r;
}

/* "label address library+offset (symbol)" */
static void crash_write_addr(int fd, const char* label, uintptr_t addr) {
    Dl_info info;
    char line[512];

    if (addr != 0 && dladdr((void*)addr, &info) != 0 && info.dli_fname != NULL) {
        const char* lib = strrchr(info.dli_fname, '/');

        snprintf(line, sizeof(line), "%s %p %s+0x%lx (%s)\n", label, (void*)addr,
                 lib != NULL ? lib + 1 : info.dli_fname,
                 (unsigned long)(addr - (uintptr_t)info.dli_fbase),
                 info.dli_sname != NULL ? info.dli_sname : "?");
    } else {
        snprintf(line, sizeof(line), "%s %p\n", label, (void*)addr);
    }
    crash_write(fd, line);
}

typedef struct {
    int fd;
    int n;
} CrashTrace;

static _Unwind_Reason_Code crash_trace_frame(struct _Unwind_Context* ctx, void* arg) {
    CrashTrace* trace = (CrashTrace*)arg;
    uintptr_t pc = (uintptr_t)_Unwind_GetIP(ctx);
    char label[16];

    if (pc == 0 || trace->n >= 48) {
        return _URC_END_OF_STACK;
    }
    snprintf(label, sizeof(label), "#%02d", trace->n++);
    crash_write_addr(trace->fd, label, pc);
    return _URC_NO_REASON;
}

void pc_android_crash(int sig, siginfo_t* info, void* ucontext) {
    static volatile sig_atomic_t reported = 0;
    const ucontext_t* uc = (const ucontext_t*)ucontext;
    size_t i;

    if (!reported) {
        int fd = open("crash.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

        reported = 1;
        if (fd >= 0) {
            char line[256];
            CrashTrace trace = { fd, 0 };

            snprintf(line, sizeof(line), "signal %d (%s), code %d, fault address %p\n", sig,
                     sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL" :
                     sig == SIGFPE ? "SIGFPE" : sig == SIGABRT ? "SIGABRT" : sig == SIGTRAP ? "SIGTRAP" : "?",
                     info != NULL ? info->si_code : 0, info != NULL ? info->si_addr : NULL);
            crash_write(fd, line);
            if (uc != NULL) {
#if defined(__aarch64__)
                crash_write_addr(fd, "pc", (uintptr_t)uc->uc_mcontext.pc);
                crash_write_addr(fd, "lr", (uintptr_t)uc->uc_mcontext.regs[30]);
#elif defined(__arm__)
                crash_write_addr(fd, "pc", (uintptr_t)uc->uc_mcontext.arm_pc);
                crash_write_addr(fd, "lr", (uintptr_t)uc->uc_mcontext.arm_lr);
#elif defined(__x86_64__)
                crash_write_addr(fd, "pc", (uintptr_t)uc->uc_mcontext.gregs[REG_RIP]);
#elif defined(__i386__)
                crash_write_addr(fd, "pc", (uintptr_t)uc->uc_mcontext.gregs[REG_EIP]);
#endif
            }
            crash_write(fd, "backtrace:\n");
            _Unwind_Backtrace(crash_trace_frame, &trace);
            close(fd);
        }
    }

    /* Hand the signal to the handler that was there before (the system's
     * crash reporter). A fault happens again when this returns; a sent
     * signal (abort) is raised again, to be delivered once this returns. */
    for (i = 0; i < sizeof(crash_signals) / sizeof(crash_signals[0]); i++) {
        if (crash_signals[i] == sig) {
            sigaction(sig, &crash_old_actions[i], NULL);
            break;
        }
    }
    if (i == sizeof(crash_signals) / sizeof(crash_signals[0])) {
        signal(sig, SIG_DFL);
    }
    if (info == NULL || info->si_code <= 0) {
        raise(sig);
    }
}

static void crash_report_init(void) {
    struct sigaction sa;
    size_t i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = pc_android_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (i = 0; i < sizeof(crash_signals) / sizeof(crash_signals[0]); i++) {
        sigaction(crash_signals[i], &sa, &crash_old_actions[i]);
    }
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
    /* stdout/stderr go nowhere on Android. Line-buffered, so a crash keeps
     * the lines before it */
    freopen("log.txt", "w", stdout);
    setvbuf(stdout, NULL, _IOLBF, 0);
    dup2(fileno(stdout), fileno(stderr));
    printf("[ANDROID] data folder: %s\n", dir);
    crash_report_init();

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
