/* pc_update_core.c - see pc_update_core.h */
#include "pc_update_core.h"
#include "ko_patch.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Versions
 * ========================================================================= */

#define VERSION_PARTS 4

/* 1 if s is a version: numbers and the "is a test build" flag */
static int parse_version(const char* s, long parts[VERSION_PARTS], int* prerelease) {
    int i;

    for (i = 0; i < VERSION_PARTS; i++) {
        parts[i] = 0;
    }
    *prerelease = 0;
    if (s == NULL) {
        return 0;
    }
    if (*s == 'v' || *s == 'V') {
        s++;
    }
    if (!isdigit((unsigned char)*s)) {
        return 0;
    }
    for (i = 0; i < VERSION_PARTS; i++) {
        char* end;

        parts[i] = strtol(s, &end, 10);
        s = end;
        if (*s != '.' || !isdigit((unsigned char)s[1])) {
            break;
        }
        s++;
    }
    *prerelease = *s == '-';
    return 1;
}

int pc_update_version_compare(const char* a, const char* b) {
    long pa[VERSION_PARTS], pb[VERSION_PARTS];
    int ra, rb;
    int va = parse_version(a, pa, &ra);
    int vb = parse_version(b, pb, &rb);
    int i;

    if (!va || !vb) {
        return va - vb;
    }
    for (i = 0; i < VERSION_PARTS; i++) {
        if (pa[i] != pb[i]) {
            return pa[i] < pb[i] ? -1 : 1;
        }
    }
    return rb - ra; /* a test build comes before its release */
}

/* =========================================================================
 * JSON: just enough to walk GitHub's release list
 * ========================================================================= */

typedef struct {
    const char* p;
    const char* end;
    int depth;
} Json;

#define JSON_MAX_DEPTH 64

static void json_ws(Json* j) {
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) {
        j->p++;
    }
}

static int json_peek(Json* j) {
    json_ws(j);
    return j->p < j->end ? (unsigned char)*j->p : -1;
}

static int json_expect(Json* j, char c) {
    if (json_peek(j) != (unsigned char)c) {
        return 0;
    }
    j->p++;
    return 1;
}

static void put_utf8(char* out, size_t n, size_t* len, unsigned cp) {
    char buf[4];
    size_t k = 0, i;

    if (cp < 0x80) {
        buf[k++] = (char)cp;
    } else if (cp < 0x800) {
        buf[k++] = (char)(0xC0 | (cp >> 6));
        buf[k++] = (char)(0x80 | (cp & 0x3F));
    } else {
        buf[k++] = (char)(0xE0 | (cp >> 12));
        buf[k++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[k++] = (char)(0x80 | (cp & 0x3F));
    }
    for (i = 0; i < k; i++) {
        if (out != NULL && *len + 1 < n) {
            out[*len] = buf[i];
        }
        (*len)++;
    }
}

/* A string into out (may be NULL: skipped); 0 if it is not one. A string
 * longer than out is cut. */
static int json_string(Json* j, char* out, size_t n) {
    size_t len = 0;

    if (!json_expect(j, '"')) {
        return 0;
    }
    while (j->p < j->end && *j->p != '"') {
        unsigned c = (unsigned char)*j->p++;

        if (c == '\\') {
            if (j->p >= j->end) {
                return 0;
            }
            c = (unsigned char)*j->p++;
            switch (c) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    unsigned cp = 0;
                    int i;

                    if (j->end - j->p < 4) {
                        return 0;
                    }
                    for (i = 0; i < 4; i++) {
                        int h = j->p[i];

                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        else return 0;
                    }
                    j->p += 4;
                    put_utf8(out, n, &len, cp >= 0xD800 && cp < 0xE000 ? '?' : cp);
                    continue;
                }
                default: break; /* \" \\ \/ */
            }
        }
        if (out != NULL && len + 1 < n) {
            out[len] = (char)c;
        }
        len++;
    }
    if (j->p >= j->end) {
        return 0;
    }
    j->p++;
    if (out != NULL && n > 0) {
        out[len < n ? len : n - 1] = '\0';
    }
    return 1;
}

static int json_skip(Json* j);

/* Calls key_fn for each member; key_fn reads the value (or returns 0 to
 * have it skipped) */
typedef int (*JsonMemberFn)(Json* j, const char* key, void* user);

static int json_object(Json* j, JsonMemberFn fn, void* user) {
    if (!json_expect(j, '{') || ++j->depth > JSON_MAX_DEPTH) {
        return 0;
    }
    if (json_peek(j) == '}') {
        j->p++;
        j->depth--;
        return 1;
    }
    for (;;) {
        char key[64];
        int taken;

        if (!json_string(j, key, sizeof(key)) || !json_expect(j, ':')) {
            return 0;
        }
        taken = fn != NULL ? fn(j, key, user) : 0;
        if (taken < 0 || (taken == 0 && !json_skip(j))) {
            return 0;
        }
        if (json_peek(j) == ',') {
            j->p++;
            continue;
        }
        if (!json_expect(j, '}')) {
            return 0;
        }
        j->depth--;
        return 1;
    }
}

typedef int (*JsonElementFn)(Json* j, void* user);

static int json_array(Json* j, JsonElementFn fn, void* user) {
    if (!json_expect(j, '[') || ++j->depth > JSON_MAX_DEPTH) {
        return 0;
    }
    if (json_peek(j) == ']') {
        j->p++;
        j->depth--;
        return 1;
    }
    for (;;) {
        if (fn != NULL ? !fn(j, user) : !json_skip(j)) {
            return 0;
        }
        if (json_peek(j) == ',') {
            j->p++;
            continue;
        }
        if (!json_expect(j, ']')) {
            return 0;
        }
        j->depth--;
        return 1;
    }
}

static int json_literal(Json* j, const char* word) {
    size_t n = strlen(word);

    json_ws(j);
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, word, n) != 0) {
        return 0;
    }
    j->p += n;
    return 1;
}

/* true / false; 0 if neither */
static int json_bool(Json* j, int* out) {
    if (json_literal(j, "true")) {
        *out = 1;
        return 1;
    }
    if (json_literal(j, "false")) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int json_number(Json* j, double* out) {
    char buf[40];
    size_t n = 0;

    json_ws(j);
    while (j->p < j->end && n + 1 < sizeof(buf) && strchr("+-0123456789.eE", *j->p) != NULL) {
        buf[n++] = *j->p++;
    }
    buf[n] = '\0';
    if (n == 0) {
        return 0;
    }
    if (out != NULL) {
        *out = strtod(buf, NULL);
    }
    return 1;
}

static int json_skip(Json* j) {
    switch (json_peek(j)) {
        case '"': return json_string(j, NULL, 0);
        case '{': return json_object(j, NULL, NULL);
        case '[': return json_array(j, NULL, NULL);
        case 't': return json_literal(j, "true");
        case 'f': return json_literal(j, "false");
        case 'n': return json_literal(j, "null");
        default:  return json_number(j, NULL);
    }
}

/* A string member, or null as "" */
static int json_string_or_null(Json* j, char* out, size_t n) {
    if (json_peek(j) == 'n') {
        out[0] = '\0';
        return json_literal(j, "null");
    }
    return json_string(j, out, n);
}

/* --- the release list ------------------------------------------------- */

typedef struct {
    char name[128];
    char url[512];
    double size;
} Asset;

typedef struct {
    const char* suffix;
    char tag[32];
    char page_url[256];
    int draft;
    int has_asset;
    Asset asset;
    char sums_url[512];
} Release;

typedef struct {
    const char* current;
    const char* suffix;
    PcUpdateRelease* best;
    int found;
} Pick;

static int ends_with(const char* s, const char* suffix) {
    size_t a = strlen(s), b = strlen(suffix);

    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static int asset_member(Json* j, const char* key, void* user) {
    Asset* a = (Asset*)user;

    if (strcmp(key, "name") == 0) {
        return json_string_or_null(j, a->name, sizeof(a->name)) ? 1 : -1;
    }
    if (strcmp(key, "browser_download_url") == 0) {
        return json_string_or_null(j, a->url, sizeof(a->url)) ? 1 : -1;
    }
    if (strcmp(key, "size") == 0) {
        return json_number(j, &a->size) ? 1 : -1;
    }
    return 0;
}

static int asset_element(Json* j, void* user) {
    Release* r = (Release*)user;
    Asset a;

    memset(&a, 0, sizeof(a));
    if (json_peek(j) != '{') {
        return json_skip(j);
    }
    if (!json_object(j, asset_member, &a)) {
        return 0;
    }
    if (strcmp(a.name, "SHA256SUMS.txt") == 0) {
        memcpy(r->sums_url, a.url, sizeof(r->sums_url));
    } else if (!r->has_asset && a.url[0] != '\0' && ends_with(a.name, r->suffix)) {
        r->asset = a;
        r->has_asset = 1;
    }
    return 1;
}

static int release_member(Json* j, const char* key, void* user) {
    Release* r = (Release*)user;

    if (strcmp(key, "tag_name") == 0) {
        return json_string_or_null(j, r->tag, sizeof(r->tag)) ? 1 : -1;
    }
    if (strcmp(key, "html_url") == 0) {
        return json_string_or_null(j, r->page_url, sizeof(r->page_url)) ? 1 : -1;
    }
    if (strcmp(key, "draft") == 0) {
        return json_bool(j, &r->draft) ? 1 : -1;
    }
    if (strcmp(key, "assets") == 0) {
        return json_array(j, asset_element, r) ? 1 : -1;
    }
    return 0;
}

static int release_element(Json* j, void* user) {
    Pick* pick = (Pick*)user;
    Release r;

    memset(&r, 0, sizeof(r));
    r.suffix = pick->suffix;
    if (json_peek(j) != '{') {
        return json_skip(j);
    }
    if (!json_object(j, release_member, &r)) {
        return 0;
    }
    if (r.draft || !r.has_asset || pc_update_version_compare(r.tag, pick->current) <= 0) {
        return 1;
    }
    if (pick->found && pc_update_version_compare(r.tag, pick->best->tag) <= 0) {
        return 1;
    }
    memset(pick->best, 0, sizeof(*pick->best));
    memcpy(pick->best->tag, r.tag, sizeof(pick->best->tag));
    memcpy(pick->best->page_url, r.page_url, sizeof(pick->best->page_url));
    memcpy(pick->best->asset_name, r.asset.name, sizeof(pick->best->asset_name));
    memcpy(pick->best->asset_url, r.asset.url, sizeof(pick->best->asset_url));
    pick->best->asset_size = r.asset.size > 0 ? (uint64_t)r.asset.size : 0;
    memcpy(pick->best->sums_url, r.sums_url, sizeof(pick->best->sums_url));
    pick->found = 1;
    return 1;
}

int pc_update_pick_release(const char* json, size_t len, const char* current, const char* asset_suffix,
                           PcUpdateRelease* out) {
    Json j = { json, json + len, 0 };
    Pick pick = { current, asset_suffix, out, 0 };

    if (json == NULL || asset_suffix == NULL || !json_array(&j, release_element, &pick)) {
        return -1;
    }
    return pick.found;
}

/* =========================================================================
 * SHA256SUMS.txt
 * ========================================================================= */

int pc_update_find_sum(const char* sums, size_t len, const char* file_name, char hex[65]) {
    const char* p = sums;
    const char* end = sums + len;
    size_t name_len = strlen(file_name);

    while (p < end) {
        const char* line_end = memchr(p, '\n', (size_t)(end - p));
        const char* q = p;
        const char* name;
        size_t n, i;

        if (line_end == NULL) {
            line_end = end;
        }
        for (i = 0; i < 64 && q < line_end && isxdigit((unsigned char)*q); i++) {
            q++;
        }
        if (i == 64 && q < line_end && (*q == ' ' || *q == '\t')) {
            name = q;
            while (name < line_end && (*name == ' ' || *name == '\t')) {
                name++;
            }
            if (name < line_end && *name == '*') {
                name++; /* binary mode mark */
            }
            n = (size_t)(line_end - name);
            while (n > 0 && (name[n - 1] == '\r' || name[n - 1] == ' ')) {
                n--;
            }
            if (n == name_len && memcmp(name, file_name, n) == 0) {
                for (i = 0; i < 64; i++) {
                    hex[i] = (char)tolower((unsigned char)p[i]);
                }
                hex[64] = '\0';
                return 1;
            }
        }
        p = line_end + 1;
    }
    return 0;
}

/* =========================================================================
 * Zip
 * ========================================================================= */

static uint32_t le16(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int pc_update_zip_foreach(const uint8_t* zip, size_t len, PcUpdateZipFn fn, void* user) {
    size_t eocd, p;
    uint32_t entries, i;
    int files = 0;

    if (zip == NULL || len < 22) {
        return -1;
    }
    eocd = len - 22;
    for (;;) {
        if (le32(zip + eocd) == 0x06054b50) {
            break;
        }
        if (eocd == 0 || len - eocd > 22 + 0xFFFF) {
            return -1;
        }
        eocd--;
    }
    entries = le16(zip + eocd + 10);
    p = le32(zip + eocd + 16);

    for (i = 0; i < entries; i++) {
        uint32_t method, crc, comp_size, size, name_len, extra_len, comment_len, local;
        char name[512];
        size_t data;
        uint8_t* out;
        int ok;

        if (p > len || len - p < 46 || le32(zip + p) != 0x02014b50) {
            return -1;
        }
        method = le16(zip + p + 10);
        crc = le32(zip + p + 16);
        comp_size = le32(zip + p + 20);
        size = le32(zip + p + 24);
        name_len = le16(zip + p + 28);
        extra_len = le16(zip + p + 30);
        comment_len = le16(zip + p + 32);
        local = le32(zip + p + 42);
        if (len - p - 46 < name_len || name_len == 0 || name_len >= sizeof(name)) {
            return -1;
        }
        memcpy(name, zip + p + 46, name_len);
        name[name_len] = '\0';
        p += 46 + name_len + extra_len + comment_len;

        if (name[name_len - 1] == '/') {
            continue; /* a folder */
        }
        if (local > len || len - local < 30 || le32(zip + local) != 0x04034b50) {
            return -1;
        }
        data = local + 30 + le16(zip + local + 26) + le16(zip + local + 28);
        if (data > len || len - data < comp_size || (method != 0 && method != 8)) {
            return -1;
        }
        out = (uint8_t*)malloc(size > 0 ? size : 1);
        if (out == NULL) {
            return -1;
        }
        if (method == 0) {
            ok = comp_size == size;
            if (ok) {
                memcpy(out, zip + data, size);
            }
        } else {
            ok = ko_inflate(zip + data, comp_size, out, size);
        }
        ok = ok && ko_crc32(0, out, size) == crc && fn(user, name, out, size);
        free(out);
        if (!ok) {
            return -1;
        }
        files++;
    }
    return files;
}

int pc_update_entry_path(const char* name, char* out, size_t n) {
    const char* rel = strchr(name, '/');
    const char* part;
    size_t len;

    if (rel == NULL || rel == name || strchr(name, '\\') != NULL || strchr(name, ':') != NULL) {
        return 0; /* not inside the top folder, or a Windows path */
    }
    rel++;
    len = strlen(rel);
    if (len == 0 || len + 1 > n || rel[0] == '/') {
        return 0;
    }
    /* no "." or ".." anywhere, no empty parts */
    for (part = rel; *part != '\0';) {
        const char* slash = strchr(part, '/');
        size_t plen = slash != NULL ? (size_t)(slash - part) : strlen(part);

        if (plen == 0 || (plen == 1 && part[0] == '.') || (plen == 2 && part[0] == '.' && part[1] == '.')) {
            return 0;
        }
        part += plen;
        if (*part == '/') {
            part++;
            if (*part == '\0') {
                return 0;
            }
        }
    }
    memcpy(out, rel, len + 1);
    return 1;
}
