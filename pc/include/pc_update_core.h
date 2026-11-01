/* pc_update_core.h - the parts of the updater (pc_update.c) that need neither
 * the network nor files: comparing versions, reading GitHub's release list,
 * SHA256SUMS.txt and the release zip. pc/tests/test_update.c tests them. */
#ifndef PC_UPDATE_CORE_H
#define PC_UPDATE_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "v0.1.7", "0.1.7" or "0.1.7-rc1" (the part after '-' is a test build,
 * older than the version without one): <0, 0, >0 like strcmp. A string that
 * is no version is older than any version. */
int pc_update_version_compare(const char* a, const char* b);

typedef struct {
    char tag[32];        /* "v0.1.8" */
    char page_url[256];  /* the release's page */
    char asset_name[128];
    char asset_url[512];
    uint64_t asset_size;
    char sums_url[512];  /* SHA256SUMS.txt, "" if the release has none */
} PcUpdateRelease;

/* From the JSON of GET /repos/<owner>/<repo>/releases: the newest release
 * (not a draft) newer than current that has an asset whose name ends with
 * asset_suffix ("-Windows-x64.zip"). Returns 1 if there is one, 0 if not,
 * -1 if the JSON cannot be read. */
int pc_update_pick_release(const char* json, size_t len, const char* current, const char* asset_suffix,
                           PcUpdateRelease* out);

/* The SHA-256 (lowercase hex) SHA256SUMS.txt gives for file_name
 * ("<hex>  <name>" or "<hex> *<name>" lines). Returns 1 if found. */
int pc_update_find_sum(const char* sums, size_t len, const char* file_name, char hex[65]);

/* Calls fn for each file in a zip (folders left out) with its data, checked
 * against the zip's CRC-32. Stops when fn returns 0. Returns the number of
 * files, or -1 if the zip is damaged, uses a method other than stored or
 * deflate, or fn stopped. */
typedef int (*PcUpdateZipFn)(void* user, const char* name, const uint8_t* data, size_t size);
int pc_update_zip_foreach(const uint8_t* zip, size_t len, PcUpdateZipFn fn, void* user);

/* The path a release zip's entry goes to inside the game folder: without the
 * zip's top folder ("DobutsuNoMoriEPlus/shaders/default.vert" ->
 * "shaders/default.vert"). 0 if it may not be written there (outside the
 * folder, an absolute path, a drive letter, a backslash, empty). */
int pc_update_entry_path(const char* name, char* out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PC_UPDATE_CORE_H */
