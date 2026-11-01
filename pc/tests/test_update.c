/* Unit tests for the updater's network-free parts (pc/src/pc_update_core.c):
 * versions, GitHub's release list, SHA256SUMS.txt and the release zip. */
#include "pc_update_core.h"
#include "ko_patch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void test_versions(void) {
    CHECK(pc_update_version_compare("v0.1.7", "0.1.7") == 0);
    CHECK(pc_update_version_compare("v0.1.8", "v0.1.7") > 0);
    CHECK(pc_update_version_compare("v0.1.7", "v0.1.10") < 0);
    CHECK(pc_update_version_compare("v0.2.0", "v0.1.99") > 0);
    CHECK(pc_update_version_compare("v1.0", "v0.9.9") > 0);
    CHECK(pc_update_version_compare("v1.0", "v1.0.0") == 0);
    CHECK(pc_update_version_compare("v0.1.8-rc1", "v0.1.8") < 0);
    CHECK(pc_update_version_compare("v0.1.8-rc1", "v0.1.7") > 0);
    CHECK(pc_update_version_compare("nightly", "v0.1.7") < 0);
    CHECK(pc_update_version_compare("v0.1.7", "") > 0);
    CHECK(pc_update_version_compare(NULL, NULL) == 0);
}

/* Shaped like GitHub's answer: newest first, strings with escapes, nested
 * objects (author, uploader), a draft, a release without this platform's file */
static const char* s_releases =
    "[\n"
    "  {\"url\":\"https://api.github.com/repos/o/r/releases/9\",\"tag_name\":\"v0.2.0\",\"draft\":true,"
    "   \"prerelease\":true,\"html_url\":\"https://github.com/o/r/releases/tag/v0.2.0\",\"assets\":["
    "     {\"name\":\"DobutsuNoMoriEPlus-v0.2.0-Windows-x64.zip\",\"size\":1,"
    "      \"browser_download_url\":\"https://example/draft.zip\"}]},\n"
    "  {\"tag_name\":\"v0.1.9\",\"draft\":false,\"prerelease\":true,\"name\":\"v0.1.9 \\\"preview\\\"\","
    "   \"body\":\"Fixes [x] {not json} \\\\ \\u00e9 \\ud83d\\ude00\\n\",\"author\":{\"login\":\"a\",\"id\":1},"
    "   \"html_url\":\"https://github.com/o/r/releases/tag/v0.1.9\",\"assets\":["
    "     {\"name\":\"DobutsuNoMoriEPlus-v0.1.9-Android.apk\",\"size\":30000000,"
    "      \"uploader\":{\"login\":\"a\"},\"browser_download_url\":\"https://example/v0.1.9.apk\"},"
    "     {\"name\":\"SHA256SUMS.txt\",\"size\":400,\"browser_download_url\":\"https://example/v0.1.9/SHA256SUMS.txt\"}]},\n"
    "  {\"tag_name\":\"v0.1.8\",\"draft\":false,\"prerelease\":true,\"name\":null,"
    "   \"html_url\":\"https://github.com/o/r/releases/tag/v0.1.8\",\"assets\":["
    "     {\"name\":\"SHA256SUMS.txt\",\"size\":400,\"browser_download_url\":\"https://example/v0.1.8/SHA256SUMS.txt\"},"
    "     {\"name\":\"DobutsuNoMoriEPlus-v0.1.8-Windows-x64.zip\",\"size\":6123456,"
    "      \"browser_download_url\":\"https://example/v0.1.8-x64.zip\"},"
    "     {\"name\":\"DobutsuNoMoriEPlus-v0.1.8-Android.apk\",\"size\":29000000,"
    "      \"browser_download_url\":\"https://example/v0.1.8.apk\"}],\"reactions\":{\"+1\":2}},\n"
    "  {\"tag_name\":\"v0.1.7\",\"draft\":false,\"assets\":[{\"name\":\"DobutsuNoMoriEPlus-v0.1.7-Windows-x64.zip\","
    "    \"size\":6000000,\"browser_download_url\":\"https://example/v0.1.7-x64.zip\"}]}\n"
    "]\n";

static void test_releases(void) {
    PcUpdateRelease r;
    size_t len = strlen(s_releases);

    /* the draft is left out; v0.1.9 has no Windows file */
    CHECK(pc_update_pick_release(s_releases, len, "0.1.7", "-Windows-x64.zip", &r) == 1);
    CHECK(strcmp(r.tag, "v0.1.8") == 0);
    CHECK(strcmp(r.asset_url, "https://example/v0.1.8-x64.zip") == 0);
    CHECK(strcmp(r.asset_name, "DobutsuNoMoriEPlus-v0.1.8-Windows-x64.zip") == 0);
    CHECK(r.asset_size == 6123456);
    CHECK(strcmp(r.sums_url, "https://example/v0.1.8/SHA256SUMS.txt") == 0);
    CHECK(strcmp(r.page_url, "https://github.com/o/r/releases/tag/v0.1.8") == 0);

    CHECK(pc_update_pick_release(s_releases, len, "0.1.7", "-Android.apk", &r) == 1);
    CHECK(strcmp(r.tag, "v0.1.9") == 0 && strcmp(r.asset_url, "https://example/v0.1.9.apk") == 0);

    /* up to date, or newer than everything */
    CHECK(pc_update_pick_release(s_releases, len, "0.1.8", "-Windows-x64.zip", &r) == 0);
    CHECK(pc_update_pick_release(s_releases, len, "0.3.0", "-Android.apk", &r) == 0);
    /* no file for this platform at all */
    CHECK(pc_update_pick_release(s_releases, len, "0.1.0", "-Linux-x86_64.zip", &r) == 0);

    /* not a list, cut off, nested too deep */
    CHECK(pc_update_pick_release("{\"message\":\"API rate limit exceeded\"}", 38, "0.1.7", ".zip", &r) == -1);
    CHECK(pc_update_pick_release(s_releases, len / 2, "0.1.7", "-Windows-x64.zip", &r) == -1);
    {
        char deep[300];

        memset(deep, '[', sizeof(deep) - 1);
        deep[sizeof(deep) - 1] = '\0';
        CHECK(pc_update_pick_release(deep, strlen(deep), "0.1.7", ".zip", &r) == -1);
    }
    CHECK(pc_update_pick_release("[]", 2, "0.1.7", ".zip", &r) == 0);
}

static void test_sums(void) {
    static const char sums[] =
        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef  DobutsuNoMoriEPlus-v0.1.8-Android.apk\n"
        "ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789 *DobutsuNoMoriEPlus-v0.1.8-Windows-x64.zip\r\n"
        "bad line\n"
        "1111111111111111111111111111111111111111111111111111111111111111  DobutsuNoMoriEPlus-v0.1.8-Windows-x86.zip";
    char hex[65];

    CHECK(pc_update_find_sum(sums, sizeof(sums) - 1, "DobutsuNoMoriEPlus-v0.1.8-Windows-x64.zip", hex));
    CHECK(strcmp(hex, "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789") == 0);
    CHECK(pc_update_find_sum(sums, sizeof(sums) - 1, "DobutsuNoMoriEPlus-v0.1.8-Android.apk", hex));
    CHECK(hex[0] == '0' && hex[63] == 'f');
    CHECK(pc_update_find_sum(sums, sizeof(sums) - 1, "DobutsuNoMoriEPlus-v0.1.8-Windows-x86.zip", hex));
    CHECK(hex[0] == '1');
    CHECK(!pc_update_find_sum(sums, sizeof(sums) - 1, "DobutsuNoMoriEPlus-v0.1.8-Windows.zip", hex));
    CHECK(!pc_update_find_sum(sums, sizeof(sums) - 1, "Android.apk", hex));
}

/* --- a stored (uncompressed) zip ------------------------------------- */

typedef struct {
    uint8_t data[4096];
    size_t len;
    uint8_t central[2048];
    size_t central_len;
    int entries;
} Zip;

static void put16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t* p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}

static void zip_add(Zip* z, const char* name, const char* text) {
    size_t name_len = strlen(name), size = strlen(text);
    uint32_t crc = ko_crc32(0, (const uint8_t*)text, size);
    uint8_t* l = z->data + z->len;
    uint8_t* c = z->central + z->central_len;

    memset(l, 0, 30);
    put32(l, 0x04034b50);
    put32(l + 14, crc);
    put32(l + 18, (uint32_t)size);
    put32(l + 22, (uint32_t)size);
    put16(l + 26, (uint32_t)name_len);
    memcpy(l + 30, name, name_len);
    memcpy(l + 30 + name_len, text, size);

    memset(c, 0, 46);
    put32(c, 0x02014b50);
    put32(c + 16, crc);
    put32(c + 20, (uint32_t)size);
    put32(c + 24, (uint32_t)size);
    put16(c + 28, (uint32_t)name_len);
    put32(c + 42, (uint32_t)z->len);
    memcpy(c + 46, name, name_len);

    z->len += 30 + name_len + size;
    z->central_len += 46 + name_len;
    z->entries++;
}

static void zip_finish(Zip* z) {
    uint8_t* e;

    memcpy(z->data + z->len, z->central, z->central_len);
    e = z->data + z->len + z->central_len;
    memset(e, 0, 22);
    put32(e, 0x06054b50);
    put16(e + 8, (uint32_t)z->entries);
    put16(e + 10, (uint32_t)z->entries);
    put32(e + 12, (uint32_t)z->central_len);
    put32(e + 16, (uint32_t)z->len);
    z->len += z->central_len + 22;
}

typedef struct {
    char names[8][64];
    char texts[8][64];
    int count;
    int stop_at;
} Seen;

static int seen_fn(void* user, const char* name, const uint8_t* data, size_t size) {
    Seen* s = (Seen*)user;

    if (s->count == s->stop_at) {
        return 0;
    }
    snprintf(s->names[s->count], sizeof(s->names[0]), "%s", name);
    snprintf(s->texts[s->count], sizeof(s->texts[0]), "%.*s", (int)size, (const char*)data);
    s->count++;
    return 1;
}

static void test_zip(void) {
    Zip z;
    Seen seen;

    memset(&z, 0, sizeof(z));
    zip_add(&z, "DobutsuNoMoriEPlus/", "");
    zip_add(&z, "DobutsuNoMoriEPlus/DobutsuNoMoriEPlus.exe", "MZ new game");
    zip_add(&z, "DobutsuNoMoriEPlus/shaders/", "");
    zip_add(&z, "DobutsuNoMoriEPlus/shaders/default.vert", "void main() {}");
    zip_finish(&z);

    memset(&seen, 0, sizeof(seen));
    seen.stop_at = -1;
    CHECK(pc_update_zip_foreach(z.data, z.len, seen_fn, &seen) == 2);
    CHECK(seen.count == 2);
    CHECK(strcmp(seen.names[0], "DobutsuNoMoriEPlus/DobutsuNoMoriEPlus.exe") == 0);
    CHECK(strcmp(seen.texts[0], "MZ new game") == 0);
    CHECK(strcmp(seen.names[1], "DobutsuNoMoriEPlus/shaders/default.vert") == 0);

    /* the callback stops it */
    memset(&seen, 0, sizeof(seen));
    seen.stop_at = 1;
    CHECK(pc_update_zip_foreach(z.data, z.len, seen_fn, &seen) == -1);

    /* a damaged file (CRC) and a cut-off zip */
    memset(&seen, 0, sizeof(seen));
    seen.stop_at = -1;
    z.data[30 + strlen("DobutsuNoMoriEPlus/") + 30 + strlen("DobutsuNoMoriEPlus/DobutsuNoMoriEPlus.exe")] ^= 1;
    CHECK(pc_update_zip_foreach(z.data, z.len, seen_fn, &seen) == -1);
    CHECK(pc_update_zip_foreach(z.data, z.len / 2, seen_fn, &seen) == -1);
    CHECK(pc_update_zip_foreach(NULL, 0, seen_fn, &seen) == -1);
}

static void test_paths(void) {
    char out[128];

    CHECK(pc_update_entry_path("DobutsuNoMoriEPlus/DobutsuNoMoriEPlus.exe", out, sizeof(out)));
    CHECK(strcmp(out, "DobutsuNoMoriEPlus.exe") == 0);
    CHECK(pc_update_entry_path("DobutsuNoMoriEPlus/shaders/default.frag", out, sizeof(out)));
    CHECK(strcmp(out, "shaders/default.frag") == 0);
    CHECK(pc_update_entry_path("DobutsuNoMoriEPlus/..hidden", out, sizeof(out)));

    CHECK(!pc_update_entry_path("README.txt", out, sizeof(out)));                  /* no top folder */
    CHECK(!pc_update_entry_path("/etc/passwd", out, sizeof(out)));
    CHECK(!pc_update_entry_path("DobutsuNoMoriEPlus//x", out, sizeof(out)));
    CHECK(!pc_update_entry_path("DobutsuNoMoriEPlus/../../evil", out, sizeof(out)));
    CHECK(!pc_update_entry_path("DobutsuNoMoriEPlus/save/../../evil", out, sizeof(out)));
    CHECK(!pc_update_entry_path("DobutsuNoMoriEPlus/./x", out, sizeof(out)));
    CHECK(!pc_update_entry_path("DobutsuNoMoriEPlus/a\\..\\b", out, sizeof(out)));
    CHECK(!pc_update_entry_path("DobutsuNoMoriEPlus/C:/x", out, sizeof(out)));
    CHECK(!pc_update_entry_path("DobutsuNoMoriEPlus/", out, sizeof(out)));
    CHECK(!pc_update_entry_path("DobutsuNoMoriEPlus/dir/", out, sizeof(out)));
    CHECK(!pc_update_entry_path("DobutsuNoMoriEPlus/a/very/long/name", out, 8));
}

int main(void) {
    test_versions();
    test_releases();
    test_sums();
    test_zip();
    test_paths();
    if (failures == 0) {
        printf("update: all tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
