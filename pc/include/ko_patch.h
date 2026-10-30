/* ko_patch.h - pieces for applying the community Korean patch in the game
 *
 * The patch ships as a zip holding an xdelta3 file (VCDIFF, RFC 3284, no
 * secondary compression, Adler-32 per window) and a manifest.json with the
 * expected hashes. The settings menu's patcher (pc_ko_patch.c) reads the zip
 * or the .xdelta, decodes it against the player's own disc image and checks
 * the result with these. Nothing here touches files or SDL. */
#ifndef KO_PATCH_H
#define KO_PATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- checksums ---- */

typedef struct {
    uint32_t h[8];
    uint64_t length;
    uint8_t block[64];
    uint32_t used;
} KoSha256;

void ko_sha256_init(KoSha256* s);
void ko_sha256_update(KoSha256* s, const void* data, size_t len);
void ko_sha256_final(KoSha256* s, uint8_t digest[32]);
/* Lowercase hex, as manifest.json has it */
void ko_sha256_hex(const uint8_t digest[32], char out[65]);

/* Start with 1 (Adler-32) and 0 (CRC-32) */
uint32_t ko_adler32(uint32_t adler, const uint8_t* data, size_t len);
uint32_t ko_crc32(uint32_t crc, const uint8_t* data, size_t len);

/* ---- zip ---- */

/* Raw deflate data into exactly dst_len bytes; 1 on success */
int ko_inflate(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len);

/* Extracts the first entry whose name ends with suffix (ignoring case) into
 * a malloc'd buffer, checking its CRC-32. name (may be NULL) gets the entry's
 * full name. Returns NULL if there is none or it is damaged. */
uint8_t* ko_zip_extract(const uint8_t* zip, size_t zip_len, const char* suffix, size_t* out_len, char* name,
                        size_t name_size);

/* ---- manifest.json (flat object of strings and numbers) ---- */

/* 1 if key holds a string (copied to out, escapes other than \" \\ kept as is) */
int ko_json_string(const char* json, const char* key, char* out, size_t n);
/* 1 if key holds a non-negative integer */
int ko_json_u64(const char* json, const char* key, uint64_t* out);

/* ---- VCDIFF ---- */

enum {
    KO_VCDIFF_OK = 0,
    KO_VCDIFF_BAD_PATCH,       /* not VCDIFF, or damaged */
    KO_VCDIFF_UNSUPPORTED,     /* secondary compression or a custom code table */
    KO_VCDIFF_SOURCE_MISMATCH, /* a window's Adler-32 differs: wrong source disc */
    KO_VCDIFF_READ_ERROR,
    KO_VCDIFF_WRITE_ERROR,
    KO_VCDIFF_NO_MEMORY,
    KO_VCDIFF_CANCELLED,
};

/* Reads len bytes at offset; 1 on success */
typedef int (*KoReadFn)(void* user, uint64_t offset, uint8_t* dst, size_t len);
/* Appends len bytes to the target; 1 on success */
typedef int (*KoWriteFn)(void* user, const uint8_t* src, size_t len);

typedef struct {
    KoReadFn read_source; /* the original file */
    void* source_user;
    KoWriteFn write_target;
    KoReadFn read_target; /* what was written so far, for windows that copy from it; may be NULL */
    void* target_user;
    void (*progress)(void* user, uint64_t done, uint64_t total); /* after each window; may be NULL */
    void* progress_user;
    volatile int* cancel; /* checked between windows; may be NULL */
} KoVcdiffIo;

/* Size of the file the patch produces; KO_VCDIFF_OK or an error */
int ko_vcdiff_target_size(const uint8_t* patch, size_t len, uint64_t* size);

/* Decodes the whole patch, window by window */
int ko_vcdiff_apply(const uint8_t* patch, size_t len, const KoVcdiffIo* io);

#ifdef __cplusplus
}
#endif

#endif
