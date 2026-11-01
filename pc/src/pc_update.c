/* pc_update.c - see pc_update.h */
#include "pc_update.h"
#include "pc_update_core.h"
#include "pc_platform.h"
#include "pc_settings.h"
#include "pc_pause_menu.h"
#include "pc_typing.h"
#include "pc_input_script.h"
#include "ko_patch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PC_PORT_VERSION
#define PC_PORT_VERSION "0.0.0"
#endif

/* The releases this build updates from, and its file there */
#define RELEASES_URL "https://api.github.com/repos/cailet0422/afe-port/releases?per_page=10"
#define USER_AGENT "DobutsuNoMoriEPlus/" PC_PORT_VERSION " (updater)"
#if defined(PC_UPDATER) && !defined(__ANDROID__)
#if defined(_WIN64)
#define ASSET_SUFFIX "-Windows-x64.zip"
#elif defined(_WIN32)
#define ASSET_SUFFIX "-Windows-x86.zip"
#elif defined(__linux__) && defined(__x86_64__)
#define ASSET_SUFFIX "-Linux-x86_64.zip"
#endif
#endif

#ifdef _WIN32
#define EXE_NAME "DobutsuNoMoriEPlus.exe"
#else
#define EXE_NAME "DobutsuNoMoriEPlus"
#endif

const char* pc_update_version(void) {
    return PC_PORT_VERSION;
}

#ifndef ASSET_SUFFIX

/* A build that does not update itself: from source, Android (Updater.java),
 * or a platform without a release file */
int pc_update_supported(void) { return 0; }
void pc_update_init(void) {}
void pc_update_frame(void) {}
PCUpdateState pc_update_state(void) { return PC_UPDATE_IDLE; }
const char* pc_update_new_version(void) { return ""; }
int pc_update_progress(void) { return 0; }
const char* pc_update_message(void) { return "This build does not update itself"; }
void pc_update_check_now(void) {}
int pc_update_start_download(void) { return 0; }
void pc_update_cancel(void) {}
void pc_update_skip(void) {}
const char* pc_update_restart_path(void) { return NULL; }

#else /* ASSET_SUFFIX */

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#define MAX_LIST (4u << 20)  /* the release list */
#define MAX_SUMS (64u << 10) /* SHA256SUMS.txt */
#define MAX_ZIP (256u << 20)
#define MAX_FILES 64
#define CLEANUP_LIST "update_old.txt" /* the replaced files, removed at the next start */

static SDL_atomic_t s_state;    /* PCUpdateState */
static SDL_atomic_t s_progress; /* 0-1000 */
static SDL_atomic_t s_cancel;
static SDL_Thread* s_thread;
static char s_message[200];
static PcUpdateRelease s_release;
static int s_prompted;             /* the page was opened for this run's find */
static char s_dir[1024];           /* the game's files, with a separator at the end ("" = not known) */
static char s_restart[1200];

int pc_update_supported(void) {
    return s_dir[0] != '\0';
}

PCUpdateState pc_update_state(void) {
    return (PCUpdateState)SDL_AtomicGet(&s_state);
}

const char* pc_update_new_version(void) {
    return s_release.tag;
}

int pc_update_progress(void) {
    return SDL_AtomicGet(&s_progress);
}

const char* pc_update_message(void) {
    return s_message;
}

const char* pc_update_restart_path(void) {
    return s_restart[0] != '\0' ? s_restart : NULL;
}

static void set_state(PCUpdateState state) {
    SDL_AtomicSet(&s_state, (int)state);
}

static void fail(const char* fmt, const char* detail) {
    snprintf(s_message, sizeof(s_message), fmt, detail != NULL ? detail : "");
    printf("[UPDATE] %s\n", s_message);
    set_state(PC_UPDATE_FAILED);
}

static void set_progress(uint64_t done, uint64_t total) {
    if (total > 0) {
        uint64_t p = done * 1000 / total;

        SDL_AtomicSet(&s_progress, (int)(p > 1000 ? 1000 : p));
    }
}

/* =========================================================================
 * Files, with UTF-8 names (the game folder may have any name)
 * ========================================================================= */

#ifdef _WIN32
static int widen(const char* s, wchar_t* out, int n) {
    return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n) > 0;
}

static FILE* u8_fopen(const char* path, const char* mode) {
    wchar_t wpath[MAX_PATH * 2], wmode[8];

    if (!widen(path, wpath, MAX_PATH * 2) || !widen(mode, wmode, 8)) {
        return NULL;
    }
    return _wfopen(wpath, wmode);
}

static int u8_exists(const char* path) {
    wchar_t w[MAX_PATH * 2];

    return widen(path, w, MAX_PATH * 2) && GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
}

static int u8_remove(const char* path) {
    wchar_t w[MAX_PATH * 2];

    return widen(path, w, MAX_PATH * 2) && DeleteFileW(w);
}

/* Renaming the running program and its DLL is allowed; overwriting them is not */
static int u8_rename(const char* from, const char* to) {
    wchar_t wf[MAX_PATH * 2], wt[MAX_PATH * 2];

    return widen(from, wf, MAX_PATH * 2) && widen(to, wt, MAX_PATH * 2) && MoveFileExW(wf, wt, 0);
}

static void u8_mkdir(const char* path) {
    wchar_t w[MAX_PATH * 2];

    if (widen(path, w, MAX_PATH * 2)) {
        CreateDirectoryW(w, NULL);
    }
}

static void last_error(char* out, size_t n) {
    snprintf(out, n, "error %lu", (unsigned long)GetLastError());
}
#else
static FILE* u8_fopen(const char* path, const char* mode) {
    return fopen(path, mode);
}

static int u8_exists(const char* path) {
    return access(path, F_OK) == 0;
}

static int u8_remove(const char* path) {
    return remove(path) == 0;
}

static int u8_rename(const char* from, const char* to) {
    return rename(from, to) == 0;
}

static void u8_mkdir(const char* path) {
    mkdir(path, 0755);
}

static void last_error(char* out, size_t n) {
    snprintf(out, n, "%s", strerror(errno));
}
#endif

/* The folder of the running program */
static void find_game_folder(void) {
    char path[sizeof(s_dir)];
    char* slash;

    path[0] = '\0';
#ifdef _WIN32
    {
        wchar_t w[MAX_PATH * 2];
        DWORD n = GetModuleFileNameW(NULL, w, MAX_PATH * 2);

        if (n == 0 || n >= MAX_PATH * 2 ||
            WideCharToMultiByte(CP_UTF8, 0, w, -1, path, (int)sizeof(path), NULL, NULL) <= 0) {
            return;
        }
        for (slash = path; *slash != '\0'; slash++) {
            if (*slash == '\\') *slash = '/';
        }
    }
#else
    {
        ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);

        if (n <= 0) {
            return;
        }
        path[n] = '\0';
    }
#endif
    slash = strrchr(path, '/');
    if (slash == NULL) {
        return;
    }
    slash[1] = '\0';
    snprintf(s_dir, sizeof(s_dir), "%s", path);
}

/* Removes what the last update replaced (the old program can only go once it
 * has ended) */
static void remove_replaced(void) {
    char list[sizeof(s_dir) + 32];
    char line[512];
    char left[MAX_FILES][512];
    int count = 0, i;
    FILE* f;

    snprintf(list, sizeof(list), "%s%s", s_dir, CLEANUP_LIST);
    f = u8_fopen(list, "r");
    if (f == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        char path[sizeof(s_dir) + 512];
        size_t n = strcspn(line, "\r\n");

        line[n] = '\0';
        if (n == 0 || strstr(line, "..") != NULL || n < 4 || strcmp(line + n - 4, ".old") != 0) {
            continue; /* only the .old files this wrote */
        }
        snprintf(path, sizeof(path), "%s%s", s_dir, line);
        if (u8_exists(path) && !u8_remove(path) && count < MAX_FILES) {
            snprintf(left[count++], sizeof(left[0]), "%s", line);
        }
    }
    fclose(f);
    if (count == 0) {
        u8_remove(list);
        return;
    }
    f = u8_fopen(list, "w");
    if (f != NULL) {
        for (i = 0; i < count; i++) {
            fprintf(f, "%s\n", left[i]);
        }
        fclose(f);
    }
}

/* =========================================================================
 * HTTPS GET into memory
 * ========================================================================= */

typedef struct {
    uint8_t* data;
    size_t len;
} Buffer;

#ifdef _WIN32
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

/* WinHTTP: in every Windows, follows GitHub's redirect to its file server */
static int http_get(const char* url, int api, size_t max, uint64_t expected, Buffer* out, char* err, size_t errn) {
    wchar_t wurl[2048], host[256], path[2048], query[1024], agent[128];
    URL_COMPONENTS uc;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    DWORD status = 0, size = sizeof(status);
    const wchar_t* headers = api ? L"Accept: application/vnd.github+json\r\n" : L"Accept: */*\r\n";
    size_t capacity = 0;
    int ok = 0;

    out->data = NULL;
    out->len = 0;
    if (!widen(url, wurl, 2048)) {
        snprintf(err, errn, "bad address");
        return 0;
    }
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = query;
    uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS ||
        wcslen(path) + wcslen(query) >= 2048) {
        snprintf(err, errn, "bad address");
        return 0;
    }
    wcscat(path, query); /* "?per_page=10" */
    widen(USER_AGENT, agent, 128);

    session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        last_error(err, errn);
        return 0;
    }
    /* TLS 1.2 and 1.3 (Windows 7 needs to be told about 1.2; older ones know no 1.3) */
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
    }
    WinHttpSetTimeouts(session, 20000, 20000, 30000, 60000);
    connection = WinHttpConnect(session, host, uc.nPort, 0);
    request = connection != NULL ? WinHttpOpenRequest(connection, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                 : NULL;
    if (request == NULL || !WinHttpSendRequest(request, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        last_error(err, errn);
        goto done;
    }
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        snprintf(err, errn, "HTTP %lu", (unsigned long)status);
        goto done;
    }
    for (;;) {
        DWORD avail = 0, got = 0;

        if (SDL_AtomicGet(&s_cancel)) {
            snprintf(err, errn, "cancelled");
            goto done;
        }
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            last_error(err, errn);
            goto done;
        }
        if (avail == 0) {
            break;
        }
        if (out->len + avail > max) {
            snprintf(err, errn, "the file is too big");
            goto done;
        }
        if (out->len + avail > capacity) {
            size_t want = capacity == 0 ? 256 * 1024 : capacity * 2;
            uint8_t* grown;

            while (want < out->len + avail) want *= 2;
            grown = (uint8_t*)realloc(out->data, want);
            if (grown == NULL) {
                snprintf(err, errn, "out of memory");
                goto done;
            }
            out->data = grown;
            capacity = want;
        }
        if (!WinHttpReadData(request, out->data + out->len, avail, &got)) {
            last_error(err, errn);
            goto done;
        }
        out->len += got;
        set_progress(out->len, expected);
    }
    ok = 1;

done:
    if (request != NULL) WinHttpCloseHandle(request);
    if (connection != NULL) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (!ok) {
        free(out->data);
        out->data = NULL;
        out->len = 0;
    }
    return ok;
}
#else
/* curl (or wget) into a temporary file: Linux has no HTTPS of its own that
 * every system carries, and one of the two nearly always is there */
static int run_download(const char* url, int api, const char* file, uint64_t expected, char* err, size_t errn) {
    const char* accept = api ? "Accept: application/vnd.github+json" : "Accept: */*";
    char* curl_argv[] = { "curl", "-fsSL", "--proto", "=https", "--proto-redir", "=https", "--connect-timeout", "20",
                          "--retry", "2", "-A", USER_AGENT, "-H", (char*)accept, "-o", (char*)file, (char*)url, NULL };
    char* wget_argv[] = { "wget", "-q", "-T", "30", "-t", "3", "-U", USER_AGENT, "--header", (char*)accept,
                          "-O", (char*)file, (char*)url, NULL };
    const char* tool = "curl";
    pid_t pid;
    int status = 0;

    if (posix_spawnp(&pid, "curl", NULL, NULL, curl_argv, environ) != 0) {
        tool = "wget";
        if (posix_spawnp(&pid, "wget", NULL, NULL, wget_argv, environ) != 0) {
            snprintf(err, errn, "needs curl or wget");
            return 0;
        }
    }
    for (;;) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        struct stat st;

        if (r == pid) {
            break;
        }
        if (r < 0 && errno != EINTR) {
            snprintf(err, errn, "%s did not run", tool);
            return 0;
        }
        if (SDL_AtomicGet(&s_cancel)) {
            kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
            snprintf(err, errn, "cancelled");
            return 0;
        }
        if (expected > 0 && stat(file, &st) == 0) {
            set_progress((uint64_t)st.st_size, expected);
        }
        SDL_Delay(100);
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(err, errn, "%s failed (%d)", tool, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        return 0;
    }
    return 1;
}

static int http_get(const char* url, int api, size_t max, uint64_t expected, Buffer* out, char* err, size_t errn) {
    const char* tmp = getenv("TMPDIR");
    char file[512];
    FILE* f;
    long size;
    int fd, ok = 0;

    out->data = NULL;
    out->len = 0;
    snprintf(file, sizeof(file), "%s/afe-update-XXXXXX", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
    fd = mkstemp(file);
    if (fd < 0) {
        last_error(err, errn);
        return 0;
    }
    close(fd);
    if (run_download(url, api, file, expected, err, errn) && (f = fopen(file, "rb")) != NULL) {
        if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0 && (size_t)size <= max && fseek(f, 0, SEEK_SET) == 0) {
            out->data = (uint8_t*)malloc(size > 0 ? (size_t)size : 1);
            out->len = (size_t)size;
            ok = out->data != NULL && fread(out->data, 1, out->len, f) == out->len;
            if (!ok) {
                snprintf(err, errn, "cannot read the download");
            }
        } else {
            snprintf(err, errn, "the file is too big");
        }
        fclose(f);
    }
    remove(file);
    if (!ok) {
        free(out->data);
        out->data = NULL;
        out->len = 0;
    }
    return ok;
}
#endif

/* =========================================================================
 * Putting the new files in place
 * ========================================================================= */

typedef struct {
    char rel[MAX_FILES][256];
    int count;
    int has_program;
    char err[160];
} Install;

static void make_parents(const char* rel) {
    char path[sizeof(s_dir) + 256];
    char* p;

    snprintf(path, sizeof(path), "%s%s", s_dir, rel);
    for (p = path + strlen(s_dir); *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            u8_mkdir(path);
            *p = '/';
        }
    }
}

/* Each file of the zip next to the one it replaces, as <name>.new */
static int stage_file(void* user, const char* name, const uint8_t* data, size_t size) {
    Install* in = (Install*)user;
    char rel[256], path[sizeof(s_dir) + 300];
    FILE* f;
    int ok;

    if (!pc_update_entry_path(name, rel, sizeof(rel))) {
        snprintf(in->err, sizeof(in->err), "unexpected file in the zip");
        return 0;
    }
    if (in->count >= MAX_FILES) {
        snprintf(in->err, sizeof(in->err), "too many files in the zip");
        return 0;
    }
    if (strcmp(rel, EXE_NAME) == 0) {
        in->has_program = 1;
    }
    make_parents(rel);
    snprintf(path, sizeof(path), "%s%s.new", s_dir, rel);
    f = u8_fopen(path, "wb");
    if (f == NULL) {
        char why[64];

        last_error(why, sizeof(why));
        snprintf(in->err, sizeof(in->err), "cannot write to the game folder (%s)", why);
        return 0;
    }
    ok = fwrite(data, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    snprintf(in->rel[in->count++], sizeof(in->rel[0]), "%s", rel);
    if (!ok) {
        snprintf(in->err, sizeof(in->err), "cannot write to the game folder (disk full?)");
    }
    return ok;
}

static void remove_staged(const Install* in) {
    char path[sizeof(s_dir) + 300];
    int i;

    for (i = 0; i < in->count; i++) {
        snprintf(path, sizeof(path), "%s%s.new", s_dir, in->rel[i]);
        u8_remove(path);
    }
}

/* Swaps the staged files in: the current one becomes <name>.old (listed for
 * removal at the next start), the new one takes its name. Undone on failure. */
static int commit(Install* in) {
    char old[MAX_FILES][300];
    int moved[MAX_FILES];
    char target[sizeof(s_dir) + 300], staged[sizeof(s_dir) + 310], path[sizeof(s_dir) + 300];
    FILE* list;
    int i, k;

    memset(moved, 0, sizeof(moved));
    for (i = 0; i < in->count; i++) {
        snprintf(target, sizeof(target), "%s%s", s_dir, in->rel[i]);
        snprintf(staged, sizeof(staged), "%s.new", target);
        old[i][0] = '\0';
        if (u8_exists(target)) {
            /* a .old the last update left that is still in use: another name */
            for (k = 0; k < 10; k++) {
                if (k == 0) snprintf(old[i], sizeof(old[i]), "%s.old", in->rel[i]);
                else snprintf(old[i], sizeof(old[i]), "%s.%d.old", in->rel[i], k);
                snprintf(path, sizeof(path), "%s%s", s_dir, old[i]);
                if (!u8_exists(path) || u8_remove(path)) {
                    break;
                }
            }
            if (k == 10 || !u8_rename(target, path)) {
                last_error(in->err, sizeof(in->err));
                goto undo;
            }
            moved[i] = 1;
        }
        if (!u8_rename(staged, target)) {
            last_error(in->err, sizeof(in->err));
            goto undo;
        }
        moved[i] |= 2;
#ifndef _WIN32
        chmod(target, strcmp(in->rel[i], EXE_NAME) == 0 ? 0755 : 0644);
#endif
    }

    snprintf(path, sizeof(path), "%s%s", s_dir, CLEANUP_LIST);
    list = u8_fopen(path, "a");
    if (list != NULL) {
        for (i = 0; i < in->count; i++) {
            if (old[i][0] != '\0') {
                fprintf(list, "%s\n", old[i]);
            }
        }
        fclose(list);
    }
    return 1;

undo:
    for (i = in->count - 1; i >= 0; i--) {
        snprintf(target, sizeof(target), "%s%s", s_dir, in->rel[i]);
        if (moved[i] & 2) {
            snprintf(staged, sizeof(staged), "%s.new", target);
            u8_rename(target, staged);
        }
        if (moved[i] & 1) {
            snprintf(path, sizeof(path), "%s%s", s_dir, old[i]);
            u8_rename(path, target);
        }
    }
    remove_staged(in);
    return 0;
}

static int install(const uint8_t* zip, size_t len) {
    static Install in; /* large; one install at a time */
    int files;

    memset(&in, 0, sizeof(in));
    files = pc_update_zip_foreach(zip, len, stage_file, &in);
    if (files <= 0 || !in.has_program) {
        remove_staged(&in);
        fail("Could not install: %s", in.err[0] != '\0' ? in.err : "the zip is not this game's");
        return 0;
    }
    if (!commit(&in)) {
        fail("Could not replace the game's files: %s", in.err);
        return 0;
    }
    snprintf(s_restart, sizeof(s_restart), "%s%s", s_dir, EXE_NAME);
    printf("[UPDATE] installed %d files of %s\n", files, s_release.tag);
    return 1;
}

/* =========================================================================
 * The threads
 * ========================================================================= */

static int check_thread(void* arg) {
    char err[160] = "";
    Buffer list;
    PcUpdateRelease found;
    int r;

    (void)arg;
    if (!http_get(RELEASES_URL, 1, MAX_LIST, 0, &list, err, sizeof(err))) {
        fail("Could not look for updates (%s)", err);
        return 0;
    }
    r = pc_update_pick_release((const char*)list.data, list.len, PC_PORT_VERSION, ASSET_SUFFIX, &found);
    free(list.data);
    if (r < 0) {
        fail("Could not read the list of releases%s", "");
        return 0;
    }
    if (r == 0) {
        snprintf(s_message, sizeof(s_message), "v%s is the newest version", PC_PORT_VERSION);
        printf("[UPDATE] %s\n", s_message);
        set_state(PC_UPDATE_CURRENT);
        return 0;
    }
    s_release = found;
    snprintf(s_message, sizeof(s_message), "%s is available", found.tag);
    printf("[UPDATE] %s: %s\n", s_message, found.asset_name);
    set_state(PC_UPDATE_AVAILABLE);
    return 0;
}

static int download_thread(void* arg) {
    char err[160] = "", want[65] = "", got[65];
    Buffer sums = { NULL, 0 }, zip;
    KoSha256 sha;
    uint8_t digest[32];

    (void)arg;
    if (s_release.sums_url[0] != '\0') {
        if (!http_get(s_release.sums_url, 0, MAX_SUMS, 0, &sums, err, sizeof(err))) {
            fail("Could not download the checksums (%s)", err);
            return 0;
        }
        if (!pc_update_find_sum((const char*)sums.data, sums.len, s_release.asset_name, want)) {
            want[0] = '\0';
        }
        free(sums.data);
    }
    if (!http_get(s_release.asset_url, 0, MAX_ZIP, s_release.asset_size, &zip, err, sizeof(err))) {
        if (SDL_AtomicGet(&s_cancel)) {
            snprintf(s_message, sizeof(s_message), "%s is available", s_release.tag);
            set_state(PC_UPDATE_AVAILABLE);
        } else {
            fail("Could not download the update (%s)", err);
        }
        return 0;
    }
    SDL_AtomicSet(&s_progress, 1000);
    if (s_release.asset_size > 0 && zip.len != s_release.asset_size) {
        free(zip.data);
        fail("The download is incomplete%s", "");
        return 0;
    }
    if (want[0] != '\0') {
        ko_sha256_init(&sha);
        ko_sha256_update(&sha, zip.data, zip.len);
        ko_sha256_final(&sha, digest);
        ko_sha256_hex(digest, got);
        if (strcmp(got, want) != 0) {
            free(zip.data);
            fail("The download is damaged (SHA-256 differs)%s", "");
            return 0;
        }
    } else {
        printf("[UPDATE] no SHA-256 for %s; checking the zip's CRCs only\n", s_release.asset_name);
    }
    if (install(zip.data, zip.len)) {
        snprintf(s_message, sizeof(s_message), "%s is installed", s_release.tag);
        set_state(PC_UPDATE_INSTALLED);
    }
    free(zip.data);
    return 0;
}

static int busy(void) {
    PCUpdateState st = pc_update_state();

    return st == PC_UPDATE_CHECKING || st == PC_UPDATE_DOWNLOADING;
}

static void start_thread(SDL_ThreadFunction fn, const char* name) {
    if (s_thread != NULL) {
        SDL_WaitThread(s_thread, NULL); /* it has finished: busy() was false */
        s_thread = NULL;
    }
    SDL_AtomicSet(&s_cancel, 0);
    s_thread = SDL_CreateThread(fn, name, NULL);
    if (s_thread == NULL) {
        fail("Could not start (%s)", SDL_GetError());
    }
}

void pc_update_check_now(void) {
    if (!pc_update_supported() || busy() || pc_update_state() == PC_UPDATE_INSTALLED) {
        return;
    }
    s_prompted = 1; /* asked from the menu: the menu shows the answer */
    snprintf(s_message, sizeof(s_message), "Looking for a new version...");
    set_state(PC_UPDATE_CHECKING);
    start_thread(check_thread, "update_check");
}

int pc_update_start_download(void) {
    if (pc_update_state() != PC_UPDATE_AVAILABLE) {
        return 0;
    }
    SDL_AtomicSet(&s_progress, 0);
    snprintf(s_message, sizeof(s_message), "Downloading %s...", s_release.tag);
    set_state(PC_UPDATE_DOWNLOADING);
    start_thread(download_thread, "update_download");
    return 1;
}

void pc_update_cancel(void) {
    SDL_AtomicSet(&s_cancel, 1);
}

void pc_update_skip(void) {
    snprintf(g_pc_settings.update_skip, sizeof(g_pc_settings.update_skip), "%s", s_release.tag);
    pc_settings_save();
}

void pc_update_init(void) {
    find_game_folder();
    if (s_dir[0] == '\0') {
        return;
    }
    remove_replaced();
    if (!g_pc_settings.auto_update || g_pc_deterministic || pc_input_script_active()) {
        return; /* scripted runs must not change */
    }
    snprintf(s_message, sizeof(s_message), "Looking for a new version...");
    set_state(PC_UPDATE_CHECKING);
    start_thread(check_thread, "update_check");
}

extern int pc_matrix_ready(void); /* sys_matrix.c */

void pc_update_frame(void) {
    if (s_prompted || pc_update_state() != PC_UPDATE_AVAILABLE) {
        return;
    }
    if (strcmp(g_pc_settings.update_skip, s_release.tag) == 0) {
        s_prompted = 1;
        printf("[UPDATE] %s skipped (update_skip)\n", s_release.tag);
        return;
    }
    /* when the pause menu can open (not on the title's menu, in a Famicom
     * game or while typing) */
    if (g_pc_paused || g_pc_title_main_menu_visible || g_pc_nes_active || g_pc_editor_active || !pc_matrix_ready()) {
        return;
    }
    s_prompted = 1;
    pc_pause_menu_open_update();
}

#endif /* ASSET_SUFFIX */
