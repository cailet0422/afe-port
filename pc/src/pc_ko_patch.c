/* pc_ko_patch.c - see pc_ko_patch.h */
#include "pc_platform.h"
#include "pc_ko_patch.h"
#include "pc_disc.h"
#include "pc_rom_dir.h"
#include "pc_settings.h"
#include "ko_patch.h"

#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

/* Patches larger than this are not the Korean patch (v0.9.2 is 8 MB) */
#define PATCH_MAX_BYTES (512u << 20)

/* Checked when only the .xdelta was picked, without its manifest.json */
static const struct {
    const char* patch_sha256;
    const char* target_sha256;
    uint64_t target_bytes;
    const char* target_name;
} s_known[] = {
    { "1fd88f400fd93387c0e8221fa84d14581e9440129a607a0889e9fcda93025749",
      "2d22f8f81936168423e90ba269a5b41a07ad5c2ba87196e0a3203b31813fc7c7", 36995072,
      "Doubutsu-no-Mori-e-plus-Korean-v0.9.2.iso" },
};

typedef enum {
    STAGE_READING,
    STAGE_CHECKING,
    STAGE_PATCHING,
    STAGE_VERIFYING,
    STAGE_SAVING,
} Stage;

typedef struct {
    FILE* patch_fp;
    char patch_name[256];
    PCDiscImage* source;
    char out_path[512];
} Job;

static SDL_atomic_t s_state;    /* PCKoPatchState */
static SDL_atomic_t s_stage;    /* Stage */
static SDL_atomic_t s_progress; /* 0-1000 */
static SDL_atomic_t s_finished; /* the thread is done */
static volatile int s_cancel = 0;
static SDL_Thread* s_thread = NULL;
static Job s_job;
/* written before the state turns FAILED or DONE, read after */
static char s_message[96];
static char s_detail[96];

static int fail(const char* message, const char* detail) {
    snprintf(s_message, sizeof(s_message), "%s", message);
    snprintf(s_detail, sizeof(s_detail), "%s", detail != NULL ? detail : "");
    printf("[KO-PATCH] failed: %s %s\n", s_message, s_detail);
    return 0;
}

/* ---- reading the patch ---- */

/* Reads to the end without seeking (the Android picker may hand a pipe) */
static uint8_t* read_all(FILE* fp, size_t* out_len) {
    size_t cap = 1 << 20;
    size_t len = 0;
    uint8_t* buf = (uint8_t*)malloc(cap);

    while (buf != NULL) {
        size_t n;

        if (len == cap) {
            uint8_t* grown;

            if (cap >= PATCH_MAX_BYTES) {
                free(buf);
                return NULL;
            }
            grown = (uint8_t*)realloc(buf, cap * 2);
            if (grown == NULL) {
                free(buf);
                return NULL;
            }
            buf = grown;
            cap *= 2;
        }
        n = fread(buf + len, 1, cap - len, fp);
        len += n;
        if (n == 0) {
            break;
        }
    }
    *out_len = len;
    return buf;
}

/* A file name from the manifest, kept to plain characters */
static void safe_name(const char* in, char* out, size_t n) {
    size_t len = 0;
    const char* p;

    for (p = in; *p != '\0' && len + 1 < n; p++) {
        unsigned char c = (unsigned char)*p;

        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
            (c == '.' && len > 0)) {
            out[len++] = (char)c;
        }
    }
    out[len] = '\0';
    if (len < 5 || SDL_strcasecmp(out + len - 4, ".iso") != 0) {
        snprintf(out, n, "Doubutsu-no-Mori-e-plus-Korean.iso");
    }
}

/* (errno is unusable here: pc_mwcc_compat.h drops the macro) */
static int make_dir(const char* path) {
    struct stat st;

    if (stat(path, &st) != 0) {
#ifdef _WIN32
        _mkdir(path);
#else
        mkdir(path, 0755);
#endif
    }
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ---- decoding ---- */

typedef struct {
    FILE* fp;
    KoSha256 sha;
} Target;

static int read_source(void* user, uint64_t offset, uint8_t* dst, size_t len) {
    if (offset + len > 0xFFFFFFFFull) {
        return 0;
    }
    return pc_disc_image_read((PCDiscImage*)user, (u32)offset, dst, (u32)len);
}

static int write_target(void* user, const uint8_t* src, size_t len) {
    Target* t = (Target*)user;

    ko_sha256_update(&t->sha, src, len);
    return fwrite(src, 1, len, t->fp) == len;
}

static int read_target(void* user, uint64_t offset, uint8_t* dst, size_t len) {
    Target* t = (Target*)user;
    int ok;

    fflush(t->fp);
    ok = fseek(t->fp, (long)offset, SEEK_SET) == 0 && fread(dst, 1, len, t->fp) == len;
    return fseek(t->fp, 0, SEEK_END) == 0 && ok;
}

static void progress(void* user, uint64_t done, uint64_t total) {
    (void)user;
    SDL_AtomicSet(&s_progress, total > 0 ? (int)(done * 1000 / total) : 0);
}

static int vcdiff_failed(int err) {
    switch (err) {
        case KO_VCDIFF_BAD_PATCH:
            return fail("The patch file is damaged", "or is not an xdelta patch");
        case KO_VCDIFF_UNSUPPORTED:
            return fail("This patch format is not supported", "(compressed xdelta)");
        case KO_VCDIFF_SOURCE_MISMATCH:
            return fail("Your disc image is not the one", "the patch is for (Japanese Rev 1)");
        case KO_VCDIFF_READ_ERROR:
            return fail("Could not read your disc image", NULL);
        case KO_VCDIFF_WRITE_ERROR:
            return fail("Could not write the new disc", "Is there enough free space?");
        case KO_VCDIFF_NO_MEMORY:
            return fail("Out of memory", NULL);
        case KO_VCDIFF_CANCELLED:
            return fail("Cancelled", NULL);
        default:
            return fail("The patch could not be applied", NULL);
    }
}

static int install(Job* job) {
    size_t file_len = 0;
    uint8_t* file;
    uint8_t* patch = NULL;
    size_t patch_len = 0;
    char* manifest = NULL;
    char want_sha[80] = "";
    uint64_t want_bytes = 0;
    char out_name[256] = "";
    char part_path[600];
    uint8_t digest[32];
    char hex[65];
    Target target;
    KoVcdiffIo io;
    uint64_t total = 0;
    int err;
    size_t i;

    SDL_AtomicSet(&s_stage, STAGE_READING);
    file = read_all(job->patch_fp, &file_len);
    fclose(job->patch_fp);
    job->patch_fp = NULL;
    if (file == NULL) {
        return fail("Could not read the patch file", NULL);
    }

    /* The zip as downloaded, or the .xdelta from it */
    if (file_len >= 4 && memcmp(file, "PK\3\4", 4) == 0) {
        size_t manifest_len = 0;
        uint8_t* m;

        patch = ko_zip_extract(file, file_len, ".xdelta", &patch_len, NULL, 0);
        m = ko_zip_extract(file, file_len, "manifest.json", &manifest_len, NULL, 0);
        free(file);
        if (patch == NULL) {
            free(m);
            return fail("The zip holds no .xdelta patch", "(or it is damaged)");
        }
        if (m != NULL) {
            manifest = (char*)realloc(m, manifest_len + 1);
            if (manifest == NULL) {
                free(m);
            } else {
                manifest[manifest_len] = '\0';
            }
        }
    } else {
        patch = file;
        patch_len = file_len;
    }
    if (patch_len < 4 || patch[0] != 0xD6 || patch[1] != 0xC3 || patch[2] != 0xC4) {
        free(patch);
        free(manifest);
        return fail("This is not the Korean patch", "Pick the downloaded zip or its .xdelta");
    }

    /* What the result must be: the manifest's word, or a known release */
    SDL_AtomicSet(&s_stage, STAGE_CHECKING);
    {
        KoSha256 sha;

        ko_sha256_init(&sha);
        ko_sha256_update(&sha, patch, patch_len);
        ko_sha256_final(&sha, digest);
        ko_sha256_hex(digest, hex);
    }
    if (manifest != NULL) {
        char patch_sha[80];
        char name[256];

        if (ko_json_string(manifest, "patch_sha256", patch_sha, sizeof(patch_sha)) &&
            SDL_strcasecmp(patch_sha, hex) != 0) {
            free(patch);
            free(manifest);
            return fail("The patch file is damaged", "(it differs from its manifest)");
        }
        ko_json_string(manifest, "target_sha256", want_sha, sizeof(want_sha));
        ko_json_u64(manifest, "target_bytes", &want_bytes);
        if (ko_json_string(manifest, "target_filename", name, sizeof(name))) {
            safe_name(name, out_name, sizeof(out_name));
        }
        if (ko_json_string(manifest, "version", name, sizeof(name))) {
            printf("[KO-PATCH] %s, version %s\n", job->patch_name, name);
        }
        free(manifest);
    } else {
        for (i = 0; i < sizeof(s_known) / sizeof(s_known[0]); i++) {
            if (strcmp(s_known[i].patch_sha256, hex) == 0) {
                snprintf(want_sha, sizeof(want_sha), "%s", s_known[i].target_sha256);
                want_bytes = s_known[i].target_bytes;
                snprintf(out_name, sizeof(out_name), "%s", s_known[i].target_name);
            }
        }
        if (want_sha[0] == '\0') {
            printf("[KO-PATCH] %s: no manifest, relying on the patch's own checksums\n", job->patch_name);
        }
    }
    if (out_name[0] == '\0') {
        safe_name("", out_name, sizeof(out_name));
    }
    if (ko_vcdiff_target_size(patch, patch_len, &total) != KO_VCDIFF_OK) {
        free(patch);
        return vcdiff_failed(KO_VCDIFF_BAD_PATCH);
    }
    if (want_bytes != 0 && total != want_bytes) {
        free(patch);
        return fail("The patch file is damaged", "(its size differs from the manifest)");
    }

    /* korean/<name>.iso.part, renamed once it checks out */
    snprintf(job->out_path, sizeof(job->out_path), "%s/%s", PC_KO_PATCH_OUT_DIR, out_name);
    if (pc_disc_is_korean_image() && SDL_strcasecmp(job->out_path, g_pc_settings.ko_image) == 0) {
        /* the game is running from it (and Windows can't replace an open file) */
        free(patch);
        return fail("This version is already installed", "and the game is running from it");
    }
    if (!make_dir(PC_KO_PATCH_OUT_DIR)) {
        free(patch);
        return fail("Could not make the folder", "for the new disc (" PC_KO_PATCH_OUT_DIR ")");
    }
    snprintf(part_path, sizeof(part_path), "%s.part", job->out_path);
    target.fp = fopen(part_path, "w+b");
    if (target.fp == NULL) {
        free(patch);
        return fail("Could not write the new disc", "in the " PC_KO_PATCH_OUT_DIR " folder");
    }
    ko_sha256_init(&target.sha);

    SDL_AtomicSet(&s_stage, STAGE_PATCHING);
    memset(&io, 0, sizeof(io));
    io.read_source = read_source;
    io.source_user = job->source;
    io.write_target = write_target;
    io.read_target = read_target;
    io.target_user = &target;
    io.progress = progress;
    io.cancel = &s_cancel;
    err = ko_vcdiff_apply(patch, patch_len, &io);
    free(patch);
    if (fclose(target.fp) != 0 && err == KO_VCDIFF_OK) {
        err = KO_VCDIFF_WRITE_ERROR;
    }
    if (err != KO_VCDIFF_OK) {
        remove(part_path);
        return vcdiff_failed(err);
    }

    SDL_AtomicSet(&s_stage, STAGE_VERIFYING);
    ko_sha256_final(&target.sha, digest);
    ko_sha256_hex(digest, hex);
    if (want_sha[0] != '\0' && SDL_strcasecmp(want_sha, hex) != 0) {
        remove(part_path);
        printf("[KO-PATCH] result %s, manifest %s\n", hex, want_sha);
        return fail("The new disc does not match the patch", "Is your disc the Japanese Rev 1?");
    }

    SDL_AtomicSet(&s_stage, STAGE_SAVING);
    remove(job->out_path);
    if (rename(part_path, job->out_path) != 0) {
        remove(part_path);
        return fail("Could not save the new disc", "in the " PC_KO_PATCH_OUT_DIR " folder");
    }
    printf("[KO-PATCH] wrote %s (sha256 %s)\n", job->out_path, hex);
    return 1;
}

static int SDLCALL worker(void* arg) {
    Job* job = (Job*)arg;
    int ok = install(job);

    if (job->patch_fp != NULL) {
        fclose(job->patch_fp);
        job->patch_fp = NULL;
    }
    pc_disc_image_close(job->source);
    job->source = NULL;
    SDL_AtomicSet(&s_state, ok ? PC_KO_PATCH_DONE : PC_KO_PATCH_FAILED);
    SDL_AtomicSet(&s_finished, 1);
    return ok;
}

/* ---- picking ---- */

static int ends_with_ci(const char* s, const char* suffix) {
    size_t a = strlen(s);
    size_t b = strlen(suffix);

    return a >= b && SDL_strcasecmp(s + a - b, suffix) == 0;
}

/* Without a picker: the first .xdelta, else the first .zip, in patch/ */
static FILE* find_patch(char* name, size_t n) {
    char zip[256] = "";
    char path[600];
    DIR* dir = opendir(PC_KO_PATCH_IN_DIR);
    struct dirent* ent;

    name[0] = '\0';
    if (dir == NULL) {
        return NULL;
    }
    while ((ent = readdir(dir)) != NULL) {
        if (ends_with_ci(ent->d_name, ".xdelta")) {
            snprintf(name, n, "%s", ent->d_name);
            break;
        }
        if (zip[0] == '\0' && ends_with_ci(ent->d_name, ".zip")) {
            snprintf(zip, sizeof(zip), "%s", ent->d_name);
        }
    }
    closedir(dir);
    if (name[0] == '\0') {
        snprintf(name, n, "%s", zip);
    }
    if (name[0] == '\0') {
        return NULL;
    }
    snprintf(path, sizeof(path), "%s/%s", PC_KO_PATCH_IN_DIR, name);
    return fopen(path, "rb");
}

/* ---- public ---- */

int pc_ko_patch_start(void) {
    char name[256];
    char source[1100];
    FILE* fp;

    if (SDL_AtomicGet(&s_state) == PC_KO_PATCH_RUNNING) {
        return 1;
    }
    pc_ko_patch_reset();

    if (pc_rom_dir_can_pick()) {
        fp = pc_pick_patch_file(name, sizeof(name));
        if (fp == NULL) {
            return 0; /* cancelled */
        }
    } else {
        fp = find_patch(name, sizeof(name));
        if (fp == NULL) {
            fail("No patch file in the " PC_KO_PATCH_IN_DIR " folder", "Put the downloaded zip there");
            SDL_AtomicSet(&s_state, PC_KO_PATCH_FAILED);
            return 1;
        }
    }

    memset(&s_job, 0, sizeof(s_job));
    s_job.patch_fp = fp;
    snprintf(s_job.patch_name, sizeof(s_job.patch_name), "%s", name);

    /* The original disc, opened apart from the game's own handle */
    s_job.source = pc_disc_open_original(source, sizeof(source));
    if (s_job.source == NULL) {
        fclose(fp);
        fail("Your original disc image was not found", "Choose its folder under Disc > Folder");
        SDL_AtomicSet(&s_state, PC_KO_PATCH_FAILED);
        return 1;
    }
    {
        u8 id[8];

        if (!pc_disc_image_read(s_job.source, 0, id, sizeof(id)) || memcmp(id, "GAEJ01", 6) != 0 || id[7] != 1) {
            pc_disc_image_close(s_job.source);
            fclose(fp);
            fail("The patch needs Doubutsu no Mori e+", "Japan, Rev 1 (GAEJ01)");
            SDL_AtomicSet(&s_state, PC_KO_PATCH_FAILED);
            return 1;
        }
    }
    printf("[KO-PATCH] applying %s to %s\n", name, source);

    s_cancel = 0;
    SDL_AtomicSet(&s_progress, 0);
    SDL_AtomicSet(&s_stage, STAGE_READING);
    SDL_AtomicSet(&s_finished, 0);
    SDL_AtomicSet(&s_state, PC_KO_PATCH_RUNNING);
    s_thread = SDL_CreateThread(worker, "ko_patch", &s_job);
    if (s_thread == NULL) {
        pc_disc_image_close(s_job.source);
        fclose(fp);
        fail("Could not start the patcher", NULL);
        SDL_AtomicSet(&s_state, PC_KO_PATCH_FAILED);
    }
    return 1;
}

void pc_ko_patch_poll(void) {
    if (s_thread == NULL || !SDL_AtomicGet(&s_finished)) {
        return;
    }
    SDL_WaitThread(s_thread, NULL);
    s_thread = NULL;
    if (SDL_AtomicGet(&s_state) == PC_KO_PATCH_DONE) {
        /* run it from the next start */
        g_pc_settings.korean = 1;
        snprintf(g_pc_settings.ko_image, sizeof(g_pc_settings.ko_image), "%s", s_job.out_path);
        pc_settings_save();
        snprintf(s_message, sizeof(s_message), "The Korean patch is installed.");
        snprintf(s_detail, sizeof(s_detail), "Restart the game to play in Korean.");
    }
}

PCKoPatchState pc_ko_patch_state(void) {
    return (PCKoPatchState)SDL_AtomicGet(&s_state);
}

int pc_ko_patch_progress(void) {
    return SDL_AtomicGet(&s_progress);
}

const char* pc_ko_patch_message(void) {
    static const char* const stages[] = {
        "Reading the patch...",
        "Checking the patch...",
        "Patching your disc image...",
        "Checking the new disc...",
        "Saving...",
    };
    int stage;

    if (SDL_AtomicGet(&s_state) != PC_KO_PATCH_RUNNING || s_thread == NULL) {
        return s_message;
    }
    stage = SDL_AtomicGet(&s_stage);
    return stages[stage >= 0 && stage <= STAGE_SAVING ? stage : 0];
}

const char* pc_ko_patch_detail(void) {
    if (SDL_AtomicGet(&s_state) == PC_KO_PATCH_RUNNING) {
        return "";
    }
    return s_detail;
}

void pc_ko_patch_cancel(void) {
    s_cancel = 1;
}

void pc_ko_patch_reset(void) {
    if (s_thread != NULL) {
        return; /* still running */
    }
    s_message[0] = '\0';
    s_detail[0] = '\0';
    SDL_AtomicSet(&s_progress, 0);
    SDL_AtomicSet(&s_state, PC_KO_PATCH_IDLE);
}

int pc_ko_patch_installed(void) {
    FILE* fp;

    if (g_pc_settings.ko_image[0] == '\0') {
        return 0;
    }
    fp = pc_fopen_utf8(g_pc_settings.ko_image, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}
