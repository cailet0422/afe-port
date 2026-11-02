/* pc_texture_pack.c - Dolphin-style HD texture packs, see pc_texture_pack.h
 *
 * Names: tex1_{W}x{H}[_m]_{hash}[_{tlut hash}|_$]_{format}.dds|.png. The
 * hashes are Dolphin's XXH64 (seed 0) of the texture's data as the GameCube
 * has it (big-endian, in tiles); for paletted textures the palette hash covers
 * only the entries the texture uses (min..max index), big-endian.
 *
 * texture_pack/ holds zips (read in place: each texture's local header and
 * deflated bytes are read when it is needed) and folders of files. The index
 * of every name is built on the first frame (once the Korean patch is known,
 * see pc_texture_pack.h), a thread reads and decodes what the game asks for,
 * and the game thread uploads it between frames. */
#include "pc_texture_pack.h"
#include "pc_gx_internal.h"
#include "pc_settings.h"
#include "pc_rom_dir.h"
#include "pc_ko.h"
#include "ko_patch.h" /* ko_inflate */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define BCDEC_IMPLEMENTATION
#include "../lib/bcdec/bcdec.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "../lib/stb/stb_image.h"

#ifdef _WIN32
#include <windows.h>
#undef near
#undef far
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern int g_pc_deterministic;
extern SDL_Window* g_pc_window;
int pc_input_script_active(void);

/* --- XXHash64 (seed=0, matches Dolphin's GetHash64) --- */

typedef unsigned long long xxh_u64;
typedef unsigned int xxh_u32;

#define XXH_PRIME64_1 0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2 0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3 0x165667B19E3779F9ULL
#define XXH_PRIME64_4 0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5 0x27D4EB2F165667C5ULL

static inline xxh_u64 xxh_read64(const void* p) {
    xxh_u64 val;
    memcpy(&val, p, 8);
    return val; /* LE platform = native read */
}

static inline xxh_u32 xxh_read32(const void* p) {
    xxh_u32 val;
    memcpy(&val, p, 4);
    return val;
}

static inline xxh_u64 xxh_rotl64(xxh_u64 x, int r) {
    return (x << r) | (x >> (64 - r));
}

static inline xxh_u64 xxh_round(xxh_u64 acc, xxh_u64 input) {
    acc += input * XXH_PRIME64_2;
    acc = xxh_rotl64(acc, 31);
    acc *= XXH_PRIME64_1;
    return acc;
}

static inline xxh_u64 xxh_merge_round(xxh_u64 acc, xxh_u64 val) {
    val = xxh_round(0, val);
    acc ^= val;
    acc = acc * XXH_PRIME64_1 + XXH_PRIME64_4;
    return acc;
}

static xxh_u64 xxhash64(const void* input, int len) {
    const unsigned char* p = (const unsigned char*)input;
    const unsigned char* end = p + len;
    xxh_u64 h64;

    if (len >= 32) {
        const unsigned char* limit = end - 32;
        xxh_u64 v1 = 0 + XXH_PRIME64_1 + XXH_PRIME64_2; /* seed=0 */
        xxh_u64 v2 = 0 + XXH_PRIME64_2;
        xxh_u64 v3 = 0;
        xxh_u64 v4 = 0 - XXH_PRIME64_1;

        do {
            v1 = xxh_round(v1, xxh_read64(p)); p += 8;
            v2 = xxh_round(v2, xxh_read64(p)); p += 8;
            v3 = xxh_round(v3, xxh_read64(p)); p += 8;
            v4 = xxh_round(v4, xxh_read64(p)); p += 8;
        } while (p <= limit);

        h64 = xxh_rotl64(v1, 1) + xxh_rotl64(v2, 7) + xxh_rotl64(v3, 12) + xxh_rotl64(v4, 18);
        h64 = xxh_merge_round(h64, v1);
        h64 = xxh_merge_round(h64, v2);
        h64 = xxh_merge_round(h64, v3);
        h64 = xxh_merge_round(h64, v4);
    } else {
        h64 = 0 + XXH_PRIME64_5; /* seed + PRIME5 */
    }

    h64 += (xxh_u64)len;
    while (p + 8 <= end) {
        xxh_u64 k1 = xxh_round(0, xxh_read64(p));
        h64 ^= k1;
        h64 = xxh_rotl64(h64, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
        p += 8;
    }
    if (p + 4 <= end) {
        h64 ^= (xxh_u64)xxh_read32(p) * XXH_PRIME64_1;
        h64 = xxh_rotl64(h64, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
        p += 4;
    }
    while (p < end) {
        h64 ^= (xxh_u64)(*p) * XXH_PRIME64_5;
        h64 = xxh_rotl64(h64, 11) * XXH_PRIME64_1;
        p++;
    }
    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME64_3;
    h64 ^= h64 >> 32;
    return h64;
}

/* --- GL formats --- */

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C
#endif

static int g_has_bc7 = 0;
static int g_has_s3tc = 0;

/* --- The index --- */

enum {
    ST_NONE,
    ST_QUEUED,
    ST_LOADED,
    ST_FAILED,
};

typedef struct {
    xxh_u64 data_hash;
    xxh_u64 tlut_hash;
    xxh_u32 fmt;
    unsigned short w, h;
    unsigned char wildcard; /* tlut hash "$": any palette */
    unsigned char state;    /* ST_* (game thread only) */
    unsigned char is_png;
    signed char source;     /* index into s_sources */
    xxh_u32 path_off;       /* loose file: path in the string pool */
    xxh_u32 local_off;      /* zip: local header */
    xxh_u32 comp_size;
    xxh_u32 size;
    unsigned short method;  /* zip: 0 stored, 8 deflated */
    GLuint gl;
    unsigned short tex_w, tex_h;
    xxh_u32 bytes;
    xxh_u32 last_used;      /* frame */
} Entry;

typedef struct {
    PCTexPackSource info;
    char path[512]; /* zip file, or "" for folders */
} Source;

static Entry* s_entries = NULL;
static int s_count = 0, s_cap = 0;
static int* s_map = NULL; /* open addressing, -1 = empty */
static int s_map_mask = 0;
static char* s_pool = NULL;
static size_t s_pool_len = 0, s_pool_cap = 0;
static Source s_sources[PC_TEXPACK_MAX_SOURCES];
static int s_source_count = 0;
static int s_skipped_korean = 0;

static int s_indexed = 0;    /* the index is built (first frame) */
static int s_enabled = 0;    /* hd_textures */
static int s_async = 0;      /* load on the thread */
static int s_preloaded = 0;
static int s_replaced = 0;
static int s_loaded = 0;
static long long s_gpu_bytes = 0;
static long long s_budget = 0;
static xxh_u32 s_frame = 1;

/* --- String pool, entries, map --- */

static xxh_u32 pool_add(const char* s) {
    size_t n = strlen(s) + 1;
    xxh_u32 off;

    if (s_pool_len + n > s_pool_cap) {
        size_t cap = s_pool_cap ? s_pool_cap * 2 : 65536;

        while (cap < s_pool_len + n) cap *= 2;
        char* p = (char*)realloc(s_pool, cap);
        if (p == NULL) return 0;
        s_pool = p;
        s_pool_cap = cap;
    }
    off = (xxh_u32)s_pool_len;
    memcpy(s_pool + s_pool_len, s, n);
    s_pool_len += n;
    return off;
}

static xxh_u32 key_slot(xxh_u64 data_hash, xxh_u64 tlut_hash, xxh_u32 fmt, xxh_u32 w, xxh_u32 h, int wildcard) {
    xxh_u64 k = data_hash ^ (tlut_hash * 0x9E3779B97F4A7C15ULL);

    k ^= (xxh_u64)fmt * 0x517CC1B727220A95ULL;
    k ^= (xxh_u64)w * 0x6C62272E07BB0142ULL;
    k ^= (xxh_u64)h * 0x165667B19E3779F9ULL;
    k ^= (xxh_u64)wildcard * 0x85EBCA77C2B2AE63ULL;
    k ^= k >> 29;
    return (xxh_u32)k;
}

static int map_find(xxh_u64 data_hash, xxh_u64 tlut_hash, xxh_u32 fmt, xxh_u32 w, xxh_u32 h, int wildcard) {
    if (s_map == NULL) return -1;
    xxh_u32 i = key_slot(data_hash, tlut_hash, fmt, w, h, wildcard) & s_map_mask;
    for (;;) {
        int e = s_map[i];

        if (e < 0) return -1;
        const Entry* en = &s_entries[e];
        if (en->data_hash == data_hash && en->tlut_hash == tlut_hash && en->fmt == fmt && en->w == w &&
            en->h == h && en->wildcard == wildcard) {
            return e;
        }
        i = (i + 1) & s_map_mask;
    }
}

static void map_build(void) {
    int size = 1024;

    while (size < s_count * 2) size *= 2;
    free(s_map);
    s_map = (int*)malloc(sizeof(int) * size);
    if (s_map == NULL) return;
    memset(s_map, 0xFF, sizeof(int) * size);
    s_map_mask = size - 1;
    for (int e = 0; e < s_count; e++) {
        const Entry* en = &s_entries[e];
        xxh_u32 i;

        if (map_find(en->data_hash, en->tlut_hash, en->fmt, en->w, en->h, en->wildcard) >= 0) {
            continue; /* the same texture again: the first one (folders before zips) stays */
        }
        i = key_slot(en->data_hash, en->tlut_hash, en->fmt, en->w, en->h, en->wildcard) & s_map_mask;
        while (s_map[i] >= 0) i = (i + 1) & s_map_mask;
        s_map[i] = e;
    }
}

static Entry* entry_new(void) {
    if (s_count == s_cap) {
        int cap = s_cap ? s_cap * 2 : 4096;
        Entry* p = (Entry*)realloc(s_entries, sizeof(Entry) * cap);

        if (p == NULL) return NULL;
        s_entries = p;
        s_cap = cap;
    }
    Entry* e = &s_entries[s_count];
    memset(e, 0, sizeof(*e));
    return e;
}

/* --- Names --- */

static int parse_hex64(const char* s, xxh_u64* out) {
    *out = 0;
    for (int i = 0; i < 16; i++) {
        char c = s[i];
        xxh_u64 nibble;

        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'a' && c <= 'f') nibble = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') nibble = 10 + c - 'A';
        else return 0;
        *out = (*out << 4) | nibble;
    }
    return 1;
}

/* tex1_WxH[_m]_HASH[_TLUT|_$]_FMT.dds|png (strict: "_8(1).dds" or "-export" are not Dolphin's) */
static int parse_name(const char* base, Entry* e) {
    const char* p;
    char* end;
    unsigned long w, h, fmt;

    if (strncmp(base, "tex1_", 5) != 0) return 0;
    p = base + 5;
    w = strtoul(p, &end, 10);
    if (end == p || *end != 'x') return 0;
    p = end + 1;
    h = strtoul(p, &end, 10);
    if (end == p || *end != '_') return 0;
    p = end + 1;
    if (p[0] == 'm' && p[1] == '_') p += 2;
    if (!parse_hex64(p, &e->data_hash) || p[16] != '_') return 0;
    p += 17;
    e->tlut_hash = 0;
    e->wildcard = 0;
    if (p[0] == '$' && p[1] == '_') {
        e->wildcard = 1;
        p += 2;
    } else if (strlen(p) > 17 && p[16] == '_' && parse_hex64(p, &e->tlut_hash)) {
        p += 17;
    }
    fmt = strtoul(p, &end, 10);
    if (end == p) return 0;
#ifdef _WIN32
    if (_stricmp(end, ".dds") == 0) e->is_png = 0;
    else if (_stricmp(end, ".png") == 0) e->is_png = 1;
#else
    if (strcasecmp(end, ".dds") == 0) e->is_png = 0;
    else if (strcasecmp(end, ".png") == 0) e->is_png = 1;
#endif
    else return 0;
    if (w == 0 || h == 0 || w > 1024 || h > 1024) return 0;
    e->w = (unsigned short)w;
    e->h = (unsigned short)h;
    e->fmt = (xxh_u32)fmt;
    return 1;
}

static int path_has_localisation(const char* path) {
    char low[512];
    size_t i;

    for (i = 0; path[i] && i < sizeof(low) - 1; i++) low[i] = (char)tolower((unsigned char)path[i]);
    low[i] = '\0';
    return strstr(low, "localisation/") != NULL || strstr(low, "localization/") != NULL ||
           strstr(low, "localisation\\") != NULL;
}

/* Adds a file of a pack; 1 if it is a texture */
static int add_name(const char* path, int source, xxh_u32 local_off, xxh_u32 comp, xxh_u32 size, int method) {
    const char* base = path + strlen(path);
    Entry tmp;

    while (base > path && base[-1] != '/' && base[-1] != '\\') base--;
    memset(&tmp, 0, sizeof(tmp));
    if (!parse_name(base, &tmp)) return 0;
    if (g_pc_ko_mode && path_has_localisation(path)) {
        s_skipped_korean++; /* other languages' text, never over the Korean patch */
        return 0;
    }
    Entry* e = entry_new();
    if (e == NULL) return 0;
    *e = tmp;
    e->source = (signed char)source;
    e->local_off = local_off;
    e->comp_size = comp;
    e->size = size;
    e->method = (unsigned short)method;
    e->path_off = source < 0 || s_sources[source].path[0] == '\0' ? pool_add(path) : 0;
    s_count++;
    return 1;
}

/* --- Zip --- */

static unsigned rd16(const unsigned char* p) { return p[0] | (p[1] << 8); }
static xxh_u32 rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((xxh_u32)p[3] << 24); }

/* Reads the zip's central directory: calls fn for each file. 0 if it is not a zip */
typedef void (*ZipFn)(void* user, const char* name, xxh_u32 local_off, xxh_u32 comp, xxh_u32 size, int method);

static int zip_walk(FILE* f, ZipFn fn, void* user) {
    unsigned char tail[65557];
    long size, start;
    size_t got;
    long eocd = -1;

    if (fseek(f, 0, SEEK_END) != 0) return 0;
    size = ftell(f);
    if (size < 22) return 0;
    start = size > (long)sizeof(tail) ? size - (long)sizeof(tail) : 0;
    fseek(f, start, SEEK_SET);
    got = fread(tail, 1, (size_t)(size - start), f);
    for (long i = (long)got - 22; i >= 0; i--) {
        if (rd32(tail + i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) return 0;
    xxh_u32 cd_count = rd16(tail + eocd + 10);
    xxh_u32 cd_size = rd32(tail + eocd + 12);
    xxh_u32 cd_off = rd32(tail + eocd + 16);
    if ((long)cd_off + (long)cd_size > size) return 0;

    unsigned char* cd = (unsigned char*)malloc(cd_size ? cd_size : 1);
    if (cd == NULL) return 0;
    fseek(f, (long)cd_off, SEEK_SET);
    if (fread(cd, 1, cd_size, f) != cd_size) {
        free(cd);
        return 0;
    }
    xxh_u32 pos = 0;
    for (xxh_u32 i = 0; i < cd_count && pos + 46 <= cd_size; i++) {
        const unsigned char* h = cd + pos;
        char name[512];

        if (rd32(h) != 0x02014b50) break;
        unsigned method = rd16(h + 10);
        xxh_u32 comp = rd32(h + 20);
        xxh_u32 usize = rd32(h + 24);
        unsigned nlen = rd16(h + 28), xlen = rd16(h + 30), clen = rd16(h + 32);
        xxh_u32 local = rd32(h + 42);
        if (pos + 46 + nlen > cd_size) break;
        size_t n = nlen < sizeof(name) - 1 ? nlen : sizeof(name) - 1;
        memcpy(name, h + 46, n);
        name[n] = '\0';
        fn(user, name, local, comp, usize, (int)method);
        pos += 46 + nlen + xlen + clen;
    }
    free(cd);
    return 1;
}

typedef struct {
    int source;
    int textures;
    xxh_u32 readme_off, readme_comp, readme_size;
    int readme_method;
} ZipScan;

static void zip_index_fn(void* user, const char* name, xxh_u32 local_off, xxh_u32 comp, xxh_u32 size, int method) {
    ZipScan* z = (ZipScan*)user;
    const char* base = name + strlen(name);

    while (base > name && base[-1] != '/') base--;
#ifdef _WIN32
    if (_stricmp(base, "readme.txt") == 0 && z->readme_size == 0) {
#else
    if (strcasecmp(base, "readme.txt") == 0 && z->readme_size == 0) {
#endif
        z->readme_off = local_off;
        z->readme_comp = comp;
        z->readme_size = size;
        z->readme_method = method;
        return;
    }
    if ((method == 0 || method == 8) && add_name(name, z->source, local_off, comp, size, method)) {
        z->textures++;
    }
}

/* The bytes of one file in a zip (malloc'd) */
static unsigned char* zip_read(FILE* f, xxh_u32 local_off, xxh_u32 comp, xxh_u32 size, int method) {
    unsigned char lh[30];
    unsigned char* src;
    unsigned char* out;

    if (fseek(f, (long)local_off, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30 || rd32(lh) != 0x04034b50) {
        return NULL;
    }
    if (fseek(f, (long)(local_off + 30 + rd16(lh + 26) + rd16(lh + 28)), SEEK_SET) != 0) return NULL;
    src = (unsigned char*)malloc(comp ? comp : 1);
    if (src == NULL) return NULL;
    if (fread(src, 1, comp, f) != comp) {
        free(src);
        return NULL;
    }
    if (method == 0) {
        return src;
    }
    out = (unsigned char*)malloc(size ? size : 1);
    if (out == NULL || !ko_inflate(src, comp, out, size)) {
        free(out);
        out = NULL;
    }
    free(src);
    return out;
}

/* "--Animal Crossing HD Texture Pack--" / "Version 25 (Oct 11th 2026)" */
static void read_readme(FILE* f, const ZipScan* z, PCTexPackSource* info) {
    unsigned char* text;
    char line[128];
    int n = 0;

    if (z->readme_size == 0 || z->readme_size > 1 << 20) return;
    text = zip_read(f, z->readme_off, z->readme_comp, z->readme_size, z->readme_method);
    if (text == NULL) return;
    const char* p = (const char*)text;
    const char* end = p + z->readme_size;
    while (p < end && n < 2) {
        size_t len = 0;

        while (p + len < end && p[len] != '\n' && p[len] != '\r') len++;
        if (len > 0) {
            size_t k = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            char* s = line;

            memcpy(line, p, k);
            line[k] = '\0';
            while (*s == '-' || *s == ' ') s++;
            for (char* t = s + strlen(s); t > s && (t[-1] == '-' || t[-1] == ' ');) *--t = '\0';
            if (n == 0) snprintf(info->title, sizeof(info->title), "%s", s);
            else snprintf(info->version, sizeof(info->version), "%s", s);
            n++;
        }
        p += len;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
    }
    free(text);
}

static void index_zip(const char* path, const char* name, long long bytes) {
    FILE* f;
    ZipScan z;
    int src = s_source_count;

    if (src >= PC_TEXPACK_MAX_SOURCES) return;
    f = pc_fopen_utf8(path, "rb");
    if (f == NULL) return;
    memset(&s_sources[src], 0, sizeof(Source));
    snprintf(s_sources[src].path, sizeof(s_sources[src].path), "%s", path);
    snprintf(s_sources[src].info.name, sizeof(s_sources[src].info.name), "%s", name);
    s_sources[src].info.is_zip = 1;
    s_sources[src].info.bytes = bytes;
    s_source_count++;
    memset(&z, 0, sizeof(z));
    z.source = src;
    if (zip_walk(f, zip_index_fn, &z)) {
        read_readme(f, &z, &s_sources[src].info);
    }
    fclose(f);
    s_sources[src].info.entries = z.textures;
    if (z.textures == 0) {
        s_source_count--; /* not a texture pack */
    }
}

/* --- Folders --- */

static int s_folder_textures;

static int ends_with_zip(const char* name) {
    size_t n = strlen(name);
#ifdef _WIN32
    return n > 4 && _stricmp(name + n - 4, ".zip") == 0;
#else
    return n > 4 && strcasecmp(name + n - 4, ".zip") == 0;
#endif
}

#ifdef _WIN32
static void scan_dir(const char* dir, int depth, int zips) {
    char pattern[600];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        char full[600];

        if (fd.cFileName[0] == '.') continue;
        snprintf(full, sizeof(full), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!zips && depth < 8) scan_dir(full, depth + 1, 0);
        } else if (zips) {
            if (depth == 0 && ends_with_zip(fd.cFileName)) {
                index_zip(full, fd.cFileName, ((long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
            }
        } else if (add_name(full, -1, 0, 0, 0, 0)) {
            s_folder_textures++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}
#else
static void scan_dir(const char* dir, int depth, int zips) {
    DIR* d = opendir(dir);
    struct dirent* ent;

    if (d == NULL) return;
    while ((ent = readdir(d)) != NULL) {
        char full[600];
        struct stat st;

        if (ent->d_name[0] == '.') continue;
        snprintf(full, sizeof(full), "%s/%s", dir, ent->d_name);
        if (stat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!zips && depth < 8) scan_dir(full, depth + 1, 0);
        } else if (zips) {
            if (depth == 0 && ends_with_zip(ent->d_name)) {
                index_zip(full, ent->d_name, (long long)st.st_size);
            }
        } else if (add_name(full, -1, 0, 0, 0, 0)) {
            s_folder_textures++;
        }
    }
    closedir(d);
}
#endif

/* --- Decoding --- */

typedef struct {
    int w, h;
    GLenum gl_internal; /* 0: RGBA8 */
    int data_size;
    unsigned char* data;
} Image;

static void image_free(Image* im) {
    free(im->data);
    im->data = NULL;
}

/* RGBA to half size (box filter) */
static void image_halve(Image* im) {
    int w = im->w / 2, h = im->h / 2;
    unsigned char* out;

    if (im->gl_internal != 0 || w < 1 || h < 1) return;
    out = (unsigned char*)malloc((size_t)w * h * 4);
    if (out == NULL) return;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned char* a = im->data + ((size_t)(y * 2) * im->w + x * 2) * 4;
            const unsigned char* b = a + (size_t)im->w * 4;
            for (int c = 0; c < 4; c++) {
                out[((size_t)y * w + x) * 4 + c] = (unsigned char)((a[c] + a[4 + c] + b[c] + b[4 + c] + 2) / 4);
            }
        }
    }
    free(im->data);
    im->data = out;
    im->w = w;
    im->h = h;
    im->data_size = w * h * 4;
}

enum { BC_NONE, BC1, BC3, BC7 };

/* Decodes a block-compressed image to RGBA */
static int bc_to_rgba(Image* im, int kind, const unsigned char* src) {
    int bw = (im->w + 3) / 4, bh = (im->h + 3) / 4;
    unsigned char* out = (unsigned char*)malloc((size_t)im->w * im->h * 4);
    unsigned char block[4 * 4 * 4];

    if (out == NULL) return 0;
    for (int by = 0; by < bh; by++) {
        for (int bx = 0; bx < bw; bx++) {
            switch (kind) {
                case BC1: bcdec_bc1(src, block, 16); src += 8; break;
                case BC3: bcdec_bc3(src, block, 16); src += 16; break;
                default: bcdec_bc7(src, block, 16); src += 16; break;
            }
            for (int y = 0; y < 4 && by * 4 + y < im->h; y++) {
                int n = im->w - bx * 4 < 4 ? im->w - bx * 4 : 4;
                memcpy(out + ((size_t)(by * 4 + y) * im->w + bx * 4) * 4, block + y * 16, (size_t)n * 4);
            }
        }
    }
    im->data = out;
    im->gl_internal = 0;
    im->data_size = im->w * im->h * 4;
    return 1;
}

static int decode_dds(const unsigned char* buf, size_t len, Image* im, int half) {
    xxh_u32 flags, fourcc, dxgi = 0;
    size_t off = 128;
    int kind = BC_NONE, bgra = 0, block = 0;

    if (len < 128 || rd32(buf) != 0x20534444) return 0;
    im->h = (int)rd32(buf + 12);
    im->w = (int)rd32(buf + 16);
    if (im->w <= 0 || im->h <= 0 || im->w > 8192 || im->h > 8192) return 0;
    flags = rd32(buf + 80);
    fourcc = rd32(buf + 84);
    if ((flags & 4) && fourcc == 0x30315844) { /* "DX10" */
        if (len < 148) return 0;
        dxgi = rd32(buf + 128);
        off = 148;
        switch (dxgi) {
            case 98: case 99: kind = BC7; break;
            case 71: case 72: kind = BC1; break;
            case 77: case 78: kind = BC3; break;
            case 28: case 29: break;
            case 87: case 91: bgra = 1; break;
            default: return 0;
        }
    } else if (flags & 4) {
        if (fourcc == 0x31545844) kind = BC1;      /* DXT1 */
        else if (fourcc == 0x35545844) kind = BC3; /* DXT5 */
        else return 0;
    } else {
        if (rd32(buf + 88) != 32) return 0;
        bgra = rd32(buf + 92) == 0x00FF0000; /* red mask */
    }
    if (kind != BC_NONE) {
        block = kind == BC1 ? 8 : 16;
        size_t need = (size_t)((im->w + 3) / 4) * ((im->h + 3) / 4) * block;
        int gpu = (kind == BC7 && g_has_bc7) || (kind != BC7 && g_has_s3tc);

        if (off + need > len) return 0;
        if (gpu && !half) {
            im->data = (unsigned char*)malloc(need);
            if (im->data == NULL) return 0;
            memcpy(im->data, buf + off, need);
            im->data_size = (int)need;
            im->gl_internal = kind == BC7 ? GL_COMPRESSED_RGBA_BPTC_UNORM
                            : kind == BC1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                                          : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
            return 1;
        }
        if (!bc_to_rgba(im, kind, buf + off)) return 0;
    } else {
        size_t need = (size_t)im->w * im->h * 4;

        if (off + need > len) return 0;
        im->data = (unsigned char*)malloc(need);
        if (im->data == NULL) return 0;
        memcpy(im->data, buf + off, need);
        if (bgra) {
            for (size_t i = 0; i < need; i += 4) {
                unsigned char t = im->data[i];
                im->data[i] = im->data[i + 2];
                im->data[i + 2] = t;
            }
        }
        im->gl_internal = 0;
        im->data_size = (int)need;
    }
    if (half) image_halve(im);
    return 1;
}

static int decode_png(const unsigned char* buf, size_t len, Image* im, int half) {
    int comp;
    unsigned char* px = stbi_load_from_memory(buf, (int)len, &im->w, &im->h, &comp, 4);

    if (px == NULL) return 0;
    im->data = (unsigned char*)malloc((size_t)im->w * im->h * 4);
    if (im->data == NULL) {
        stbi_image_free(px);
        return 0;
    }
    memcpy(im->data, px, (size_t)im->w * im->h * 4);
    stbi_image_free(px);
    im->gl_internal = 0;
    im->data_size = im->w * im->h * 4;
    if (half) image_halve(im);
    return 1;
}

static unsigned char* read_file(const char* path, size_t* len) {
    FILE* f = pc_fopen_utf8(path, "rb");
    unsigned char* buf;
    long n;

    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (64 << 20)) {
        fclose(f);
        return NULL;
    }
    buf = (unsigned char*)malloc((size_t)n);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

/* Reads and decodes one entry (any thread). zip_files: a FILE* per source
 * kept open by the caller */
static int load_entry(const Entry* e, FILE** zip_files, Image* im) {
    unsigned char* buf = NULL;
    size_t len = 0;
    int ok, half = g_pc_settings.hd_quality == 1;

    memset(im, 0, sizeof(*im));
    if (e->source >= 0 && s_sources[e->source].path[0] != '\0') {
        FILE** f = &zip_files[(int)e->source];

        if (*f == NULL) *f = pc_fopen_utf8(s_sources[e->source].path, "rb");
        if (*f == NULL) return 0;
        buf = zip_read(*f, e->local_off, e->comp_size, e->size, e->method);
        len = e->size;
    } else {
        buf = read_file(s_pool + e->path_off, &len);
    }
    if (buf == NULL) return 0;
    ok = e->is_png ? decode_png(buf, len, im, half) : decode_dds(buf, len, im, half);
    free(buf);
    return ok;
}

/* --- GL --- */

static GLuint upload(Image* im) {
    GLuint tex;

    /* an error the game's drawing left must not count as this upload's */
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {
    }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    if (im->gl_internal != 0) {
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, im->gl_internal, im->w, im->h, 0, im->data_size, im->data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    } else {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im->w, im->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im->data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &tex);
        return 0;
    }
    return tex;
}

static void entry_loaded(int idx, Image* im) {
    Entry* e = &s_entries[idx];

    e->gl = upload(im);
    if (e->gl == 0) {
        e->state = ST_FAILED;
        return;
    }
    e->state = ST_LOADED;
    e->tex_w = (unsigned short)im->w;
    e->tex_h = (unsigned short)im->h;
    e->bytes = (xxh_u32)(im->gl_internal != 0 ? im->data_size : im->data_size + im->data_size / 3);
    e->last_used = s_frame;
    s_gpu_bytes += e->bytes;
    s_loaded++;
}

static void entry_unload(Entry* e) {
    if (e->state == ST_LOADED && e->gl != 0) {
        pc_gx_texture_forget(e->gl);
        glDeleteTextures(1, &e->gl);
        s_gpu_bytes -= e->bytes;
        s_loaded--;
    }
    e->gl = 0;
    e->state = ST_NONE;
}

/* --- The loading thread --- */

#define QUEUE_SIZE 8192

typedef struct Done {
    int idx;
    Image im;
    int ok;
    struct Done* next;
} Done;

static SDL_Thread* s_thread = NULL;
static SDL_mutex* s_mutex = NULL;
static SDL_cond* s_cond = NULL;
static int s_queue[QUEUE_SIZE];
static int s_q_head = 0, s_q_tail = 0;
static int s_stop = 0;
static Done* s_done = NULL;
static int s_pending = 0;

static int worker(void* unused) {
    FILE* zips[PC_TEXPACK_MAX_SOURCES] = { 0 };

    (void)unused;
    SDL_LockMutex(s_mutex);
    while (!s_stop) {
        if (s_q_head == s_q_tail) {
            SDL_CondWait(s_cond, s_mutex);
            continue;
        }
        int idx = s_queue[s_q_head];
        s_q_head = (s_q_head + 1) % QUEUE_SIZE;
        SDL_UnlockMutex(s_mutex);

        Done* d = (Done*)calloc(1, sizeof(Done));
        if (d != NULL) {
            d->idx = idx;
            d->ok = load_entry(&s_entries[idx], zips, &d->im);
        }

        SDL_LockMutex(s_mutex);
        if (d != NULL) {
            d->next = s_done;
            s_done = d;
        }
    }
    SDL_UnlockMutex(s_mutex);
    for (int i = 0; i < PC_TEXPACK_MAX_SOURCES; i++) {
        if (zips[i] != NULL) fclose(zips[i]);
    }
    return 0;
}

static void thread_start(void) {
    if (s_thread != NULL || !s_async) return;
    if (s_mutex == NULL) s_mutex = SDL_CreateMutex();
    if (s_cond == NULL) s_cond = SDL_CreateCond();
    s_stop = 0;
    s_q_head = s_q_tail = 0;
    s_thread = SDL_CreateThread(worker, "texpack", NULL);
}

static void thread_stop(void) {
    if (s_thread == NULL) return;
    SDL_LockMutex(s_mutex);
    s_stop = 1;
    SDL_CondSignal(s_cond);
    SDL_UnlockMutex(s_mutex);
    SDL_WaitThread(s_thread, NULL);
    s_thread = NULL;
    while (s_done != NULL) {
        Done* d = s_done;

        s_done = d->next;
        image_free(&d->im);
        free(d);
    }
    s_pending = 0;
}

static int enqueue(int idx) {
    int next = (s_q_tail + 1) % QUEUE_SIZE;

    SDL_LockMutex(s_mutex);
    if (next == s_q_head) {
        SDL_UnlockMutex(s_mutex);
        return 0;
    }
    s_queue[s_q_tail] = idx;
    s_q_tail = next;
    SDL_CondSignal(s_cond);
    SDL_UnlockMutex(s_mutex);
    s_pending++;
    return 1;
}

/* --- Index --- */

static void free_index(void) {
    thread_stop();
    for (int i = 0; i < s_count; i++) entry_unload(&s_entries[i]);
    free(s_entries);
    free(s_map);
    free(s_pool);
    s_entries = NULL;
    s_map = NULL;
    s_pool = NULL;
    s_count = s_cap = 0;
    s_pool_len = s_pool_cap = 0;
    s_source_count = 0;
    s_skipped_korean = 0;
    s_loaded = 0;
    s_gpu_bytes = 0;
    s_indexed = 0;
}

static void check_gl(void) {
    GLint n = 0;

    g_has_bc7 = g_has_s3tc = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
        const char* ext = (const char*)glGetStringi(GL_EXTENSIONS, i);

        if (ext == NULL) continue;
        if (strcmp(ext, "GL_ARB_texture_compression_bptc") == 0 || strcmp(ext, "GL_EXT_texture_compression_bptc") == 0)
            g_has_bc7 = 1;
        if (strcmp(ext, "GL_EXT_texture_compression_s3tc") == 0) g_has_s3tc = 1;
    }
}

static void preload_all(void);

static void build_index(void) {
    Uint32 t0 = SDL_GetTicks();

    s_indexed = 1;
    s_folder_textures = 0;
    /* folders first (their textures win over a zip's), then the zips */
    scan_dir(PC_TEXPACK_DIR, 0, 0);
    if (s_folder_textures > 0 && s_source_count < PC_TEXPACK_MAX_SOURCES) {
        Source* s = &s_sources[s_source_count++];

        memset(s, 0, sizeof(*s));
        snprintf(s->info.name, sizeof(s->info.name), "%s/", PC_TEXPACK_DIR);
        s->info.entries = s_folder_textures;
    }
    scan_dir(PC_TEXPACK_DIR, 0, 1);
    map_build();

    s_enabled = g_pc_settings.hd_textures && s_count > 0;
    /* the same screens every run for tests: no thread */
    s_async = !g_pc_deterministic && !pc_input_script_active() && !g_pc_settings.preload_textures;
#ifdef __ANDROID__
    s_budget = 320LL << 20;
#else
    s_budget = g_has_bc7 && g_pc_settings.hd_quality == 0 ? 1536LL << 20 : 1024LL << 20;
#endif
    printf("[TexturePack] %d textures in %d source(s), %d left out for Korean (BC7:%s S3TC:%s, %u ms)\n", s_count,
           s_source_count, s_skipped_korean, g_has_bc7 ? "yes" : "no", g_has_s3tc ? "yes" : "no",
           SDL_GetTicks() - t0);
    if (s_count > 0) {
        thread_start();
        if (g_pc_settings.preload_textures && s_enabled) {
            preload_all();
        }
    }
}

static void ensure_index(void) {
    if (!s_indexed) build_index();
}

/* --- Public --- */

void pc_texture_pack_init(void) {
    check_gl();
    s_replaced = 0;
}

void pc_texture_pack_preload_all(void) {
    /* done with the index, on the first frame (see build_index) */
}

static void preload_all(void) {
    FILE* zips[PC_TEXPACK_MAX_SOURCES] = { 0 };
    Uint32 t0 = SDL_GetTicks();
    int done = 0;

    for (int i = 0; i < s_count; i++) {
        Entry* e = &s_entries[i];
        Image im;

        if (e->state != ST_NONE) continue;
        if (load_entry(e, zips, &im)) {
            entry_loaded(i, &im);
            image_free(&im);
        } else {
            e->state = ST_FAILED;
        }
        if (g_pc_window != NULL && (++done % 200) == 0) {
            char title[128];

            snprintf(title, sizeof(title), "Loading HD textures... %d/%d", i, s_count);
            SDL_SetWindowTitle(g_pc_window, title);
            SDL_PumpEvents();
        }
    }
    for (int i = 0; i < PC_TEXPACK_MAX_SOURCES; i++) {
        if (zips[i] != NULL) fclose(zips[i]);
    }
    s_preloaded = 1;
    printf("[TexturePack] Preloaded %d textures (%lld MB) in %u ms\n", s_loaded, s_gpu_bytes >> 20,
           SDL_GetTicks() - t0);
}

void pc_texture_pack_shutdown(void) {
    if (s_indexed) {
        printf("[TexturePack] %d game textures replaced, %d on the GPU (%lld MB)\n", s_replaced, s_loaded,
               s_gpu_bytes >> 20);
    }
    free_index();
}

int pc_texture_pack_active(void) {
    ensure_index();
    return s_enabled;
}

void pc_texture_pack_set_enabled(int on) {
    ensure_index();
    g_pc_settings.hd_textures = on ? 1 : 0;
    s_enabled = on && s_count > 0;
    /* every texture decides again: the originals or the pack's */
    pc_gx_texture_cache_invalidate();
}

void pc_texture_pack_reload(void) {
    free_index();
    pc_gx_texture_cache_invalidate();
    s_replaced = 0;
    build_index();
}

void pc_texture_pack_touch(int index) {
    if (index >= 0 && index < s_count) s_entries[index].last_used = s_frame;
}

/* Frees the least recently used textures down to 85% of the budget */
static void evict(void) {
    int* order;
    int n = 0;

    if (s_preloaded || s_gpu_bytes <= s_budget) return;
    order = (int*)malloc(sizeof(int) * (s_loaded > 0 ? s_loaded : 1));
    if (order == NULL) return;
    for (int i = 0; i < s_count && n < s_loaded; i++) {
        if (s_entries[i].state == ST_LOADED) order[n++] = i;
    }
    /* oldest first (insertion sort on a mostly sorted list is fine for the
     * few thousand there can be) */
    for (int i = 1; i < n; i++) {
        int v = order[i], j = i - 1;
        xxh_u32 lv = s_entries[v].last_used;

        while (j >= 0 && s_entries[order[j]].last_used > lv) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
    for (int i = 0; i < n && s_gpu_bytes > s_budget * 85 / 100; i++) {
        Entry* e = &s_entries[order[i]];

        if (s_frame - e->last_used < 3) break; /* in use */
        entry_unload(e);
    }
    free(order);
}

void pc_texture_pack_frame(void) {
    Done* list;

    s_frame++;
    ensure_index();
    if (s_thread == NULL) return;
    SDL_LockMutex(s_mutex);
    list = s_done;
    s_done = NULL;
    SDL_UnlockMutex(s_mutex);
    while (list != NULL) {
        Done* d = list;
        Entry* e = &s_entries[d->idx];

        list = d->next;
        s_pending--;
        if (e->state == ST_QUEUED) {
            if (d->ok) {
                entry_loaded(d->idx, &d->im);
            } else {
                e->state = ST_FAILED;
            }
            /* the original that stood in for it goes */
            pc_gx_texture_hd_ready((unsigned long long)d->idx + 1);
        }
        image_free(&d->im);
        free(d);
    }
    evict();
}

static int gc_texture_data_size(int w, int h, unsigned int fmt) {
    int bw, bh, block_bytes;

    switch (fmt) {
        case 0: case 8: case 14: bw = 8; bh = 8; block_bytes = 32; break; /* I4, C4, CMPR */
        case 1: case 2: case 9: bw = 8; bh = 4; block_bytes = 32; break;  /* I8, IA4, C8 */
        case 3: case 4: case 5: bw = 4; bh = 4; block_bytes = 32; break;  /* IA8, RGB565, RGB5A3 */
        case 6: bw = 4; bh = 4; block_bytes = 64; break;                  /* RGBA8 */
        default: bw = 8; bh = 4; block_bytes = 32; break;
    }
    return ((w + bw - 1) / bw) * ((h + bh - 1) / bh) * block_bytes;
}

GLuint pc_texture_pack_lookup(const void* data, int data_size, int w, int h, unsigned int fmt, const void* tlut_data,
                              int tlut_entries, int tlut_is_be, int* out_w, int* out_h, int* out_index,
                              unsigned long long* pending_key) {
    int hash_size, idx;
    xxh_u64 data_hash, tlut_hash = 0;

    if (out_index) *out_index = -1;
    if (pending_key) *pending_key = 0;
    if (!pc_texture_pack_active() || data == NULL || data_size <= 0) return 0;

    hash_size = gc_texture_data_size(w, h, fmt);
    if (hash_size > data_size) hash_size = data_size;
    data_hash = xxhash64(data, hash_size);

    /* paletted: only the entries the texture uses, big-endian */
    if (tlut_data != NULL && tlut_entries > 0) {
        const unsigned char* tex = (const unsigned char*)data;
        unsigned int lo = 0xFFFF, hi = 0;

        if (tlut_entries <= 16) {
            for (int i = 0; i < hash_size; i++) {
                unsigned int a = tex[i] & 0xF, b = tex[i] >> 4;
                if (a < lo) lo = a;
                if (b < lo) lo = b;
                if (a > hi) hi = a;
                if (b > hi) hi = b;
            }
        } else if (tlut_entries <= 256) {
            for (int i = 0; i < hash_size; i++) {
                if (tex[i] < lo) lo = tex[i];
                if (tex[i] > hi) hi = tex[i];
            }
        } else {
            for (int i = 0; i + 1 < hash_size; i += 2) {
                unsigned int v = ((unsigned int)tex[i] << 8 | tex[i + 1]) & 0x3FFF;
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        }
        if (lo > hi) lo = hi = 0;
        int off = (int)lo * 2, bytes = (int)(hi + 1 - lo) * 2;
        if (off + bytes > tlut_entries * 2) bytes = tlut_entries * 2 - off;
        if (bytes <= 0) {
            off = 0;
            bytes = tlut_entries * 2;
        }
        const unsigned char* src = (const unsigned char*)tlut_data + off;
        if (tlut_is_be) {
            tlut_hash = xxhash64(src, bytes);
        } else {
            unsigned char tmp[16384 * 2];

            if (bytes > (int)sizeof(tmp)) return 0;
            for (int i = 0; i + 1 < bytes; i += 2) {
                tmp[i] = src[i + 1];
                tmp[i + 1] = src[i];
            }
            tlut_hash = xxhash64(tmp, bytes);
        }
    }

    idx = map_find(data_hash, tlut_hash, fmt, (xxh_u32)w, (xxh_u32)h, 0);
    if (idx < 0 && tlut_data != NULL) {
        idx = map_find(data_hash, 0, fmt, (xxh_u32)w, (xxh_u32)h, 1);
    }
    if (idx < 0) return 0;

    Entry* e = &s_entries[idx];
    if (e->state == ST_NONE) {
        s_replaced++;
        if (s_async && enqueue(idx)) {
            e->state = ST_QUEUED;
        } else {
            FILE* zips[PC_TEXPACK_MAX_SOURCES] = { 0 };
            Image im;

            if (load_entry(e, zips, &im)) {
                entry_loaded(idx, &im);
                image_free(&im);
            } else {
                e->state = ST_FAILED;
            }
            for (int i = 0; i < PC_TEXPACK_MAX_SOURCES; i++) {
                if (zips[i] != NULL) fclose(zips[i]);
            }
            if (g_pc_verbose && e->state == ST_LOADED) {
                printf("[TexturePack] %dx%d fmt %u -> %dx%d\n", w, h, fmt, e->tex_w, e->tex_h);
            }
        }
    }
    if (e->state == ST_QUEUED) {
        if (pending_key) *pending_key = (unsigned long long)idx + 1;
        return 0;
    }
    if (e->state != ST_LOADED) return 0;
    e->last_used = s_frame;
    if (out_w) *out_w = e->tex_w;
    if (out_h) *out_h = e->tex_h;
    if (out_index) *out_index = idx;
    return e->gl;
}

void pc_texture_pack_info(PCTexPackInfo* out) {
    memset(out, 0, sizeof(*out));
    ensure_index();
    out->sources = s_source_count;
    for (int i = 0; i < s_source_count; i++) out->source[i] = s_sources[i].info;
    out->entries = s_count;
    out->skipped_korean = s_skipped_korean;
    out->loaded = s_loaded;
    out->replaced = s_replaced;
    out->loading = s_pending;
    out->gpu_bytes = s_gpu_bytes;
    out->budget = s_preloaded ? 0 : s_budget;
    out->gpu_bc7 = g_has_bc7;
}

/* --- Installing --- */

static SDL_Thread* s_inst_thread = NULL;
static volatile int s_inst_state = PC_TEXPACK_INSTALL_IDLE;
static volatile int s_inst_progress = 0;
static volatile int s_inst_cancel = 0;
static char s_inst_message[256];
static FILE* s_inst_src = NULL;
static char s_inst_name[160];
static long long s_inst_size = 0;

static void make_dir(const char* path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

static int count_textures_fn_count;
static void count_fn(void* user, const char* name, xxh_u32 a, xxh_u32 b, xxh_u32 c, int d) {
    const char* base = name + strlen(name);
    Entry tmp;

    (void)user; (void)a; (void)b; (void)c; (void)d;
    while (base > name && base[-1] != '/') base--;
    if (parse_name(base, &tmp)) count_textures_fn_count++;
}

static int install_thread(void* unused) {
    char part[700], final_path[700];
    unsigned char* buf = (unsigned char*)malloc(1 << 20);
    long long done = 0;
    FILE* out;
    int ok = 1;

    (void)unused;
    make_dir(PC_TEXPACK_DIR);
    snprintf(final_path, sizeof(final_path), "%s/%s", PC_TEXPACK_DIR, s_inst_name);
    snprintf(part, sizeof(part), "%s.part", final_path);
    out = pc_fopen_utf8(part, "wb");
    if (out == NULL || buf == NULL) {
        snprintf(s_inst_message, sizeof(s_inst_message), "Cannot write %s", part);
        s_inst_state = PC_TEXPACK_INSTALL_FAILED;
        free(buf);
        if (out) fclose(out);
        fclose(s_inst_src);
        return 0;
    }
    for (;;) {
        size_t n = fread(buf, 1, 1 << 20, s_inst_src);

        if (n == 0) break;
        if (fwrite(buf, 1, n, out) != n) {
            ok = 0;
            snprintf(s_inst_message, sizeof(s_inst_message), "Writing failed (disk full?)");
            break;
        }
        done += (long long)n;
        if (s_inst_size > 0) s_inst_progress = (int)(done * 950 / s_inst_size);
        if (s_inst_cancel) {
            ok = 0;
            snprintf(s_inst_message, sizeof(s_inst_message), "Cancelled");
            break;
        }
    }
    free(buf);
    fclose(s_inst_src);
    s_inst_src = NULL;
    fclose(out);
    if (ok) {
        FILE* f = pc_fopen_utf8(part, "rb");

        count_textures_fn_count = 0;
        if (f == NULL || !zip_walk(f, count_fn, NULL)) {
            ok = 0;
            snprintf(s_inst_message, sizeof(s_inst_message), "This is not a zip file");
        } else if (count_textures_fn_count == 0) {
            ok = 0;
            snprintf(s_inst_message, sizeof(s_inst_message), "No Dolphin textures (tex1_*.dds/.png) in this zip");
        }
        if (f) fclose(f);
    }
    if (!ok) {
        remove(part);
        s_inst_state = PC_TEXPACK_INSTALL_FAILED;
        return 0;
    }
    remove(final_path);
    if (rename(part, final_path) != 0) {
        remove(part);
        snprintf(s_inst_message, sizeof(s_inst_message), "Cannot rename to %s", final_path);
        s_inst_state = PC_TEXPACK_INSTALL_FAILED;
        return 0;
    }
    snprintf(s_inst_message, sizeof(s_inst_message), "%d textures", count_textures_fn_count);
    s_inst_progress = 1000;
    s_inst_state = PC_TEXPACK_INSTALL_DONE;
    return 0;
}

int pc_texture_pack_install_start(void) {
    char name[256];
    FILE* f;
    char* p;

    if (s_inst_state == PC_TEXPACK_INSTALL_RUNNING) return 1;
    f = pc_pick_file(PC_PICK_TEXTURE_PACK, name, sizeof(name));
    if (f == NULL) return 0;
    /* a plain file name in texture_pack/ */
    for (p = name; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' ||
            *p == '|') {
            *p = '_';
        }
    }
    if (name[0] == '\0') snprintf(name, sizeof(name), "texture_pack.zip");
    if (!ends_with_zip(name)) {
        strncat(name, ".zip", sizeof(name) - strlen(name) - 1);
    }
    snprintf(s_inst_name, sizeof(s_inst_name), "%s", name);
    fseek(f, 0, SEEK_END);
    s_inst_size = (long long)ftell(f);
    fseek(f, 0, SEEK_SET);
    s_inst_src = f;
    s_inst_cancel = 0;
    s_inst_progress = 0;
    snprintf(s_inst_message, sizeof(s_inst_message), "Copying %s", name);
    s_inst_state = PC_TEXPACK_INSTALL_RUNNING;
    thread_stop(); /* nothing reads texture_pack/ meanwhile */
    s_inst_thread = SDL_CreateThread(install_thread, "texinstall", NULL);
    if (s_inst_thread == NULL) {
        fclose(f);
        s_inst_state = PC_TEXPACK_INSTALL_FAILED;
        snprintf(s_inst_message, sizeof(s_inst_message), "Cannot start the copy");
    }
    return 1;
}

void pc_texture_pack_install_poll(void) {
    if (s_inst_thread != NULL && s_inst_state != PC_TEXPACK_INSTALL_RUNNING) {
        SDL_WaitThread(s_inst_thread, NULL);
        s_inst_thread = NULL;
        if (s_inst_state == PC_TEXPACK_INSTALL_DONE) {
            g_pc_settings.hd_textures = 1;
            pc_settings_save();
        }
        pc_texture_pack_reload();
    }
}

PCTexPackInstallState pc_texture_pack_install_state(void) {
    return (PCTexPackInstallState)s_inst_state;
}

int pc_texture_pack_install_progress(void) {
    return s_inst_progress;
}

const char* pc_texture_pack_install_message(void) {
    return s_inst_message;
}

void pc_texture_pack_install_cancel(void) {
    s_inst_cancel = 1;
}

void pc_texture_pack_install_reset(void) {
    if (s_inst_state != PC_TEXPACK_INSTALL_RUNNING) s_inst_state = PC_TEXPACK_INSTALL_IDLE;
}

int pc_texture_pack_remove(const char* name) {
    char path[700];
    int ok;

    if (name == NULL || name[0] == '\0' || strchr(name, '/') != NULL || strchr(name, '\\') != NULL ||
        !ends_with_zip(name)) {
        return 0;
    }
    free_index(); /* closes the zip */
    snprintf(path, sizeof(path), "%s/%s", PC_TEXPACK_DIR, name);
    ok = remove(path) == 0;
    pc_gx_texture_cache_invalidate();
    build_index();
    return ok;
}
