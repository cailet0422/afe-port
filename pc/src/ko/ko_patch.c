/* ko_patch.c - see ko_patch.h */
#include "ko_patch.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * SHA-256 (FIPS 180-4)
 * ========================================================================= */

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(KoSha256* s, const uint8_t* p) {
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) | ((uint32_t)p[i * 4 + 2] << 8) |
               p[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROR32(w[i - 15], 7) ^ ROR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR32(w[i - 2], 17) ^ ROR32(w[i - 2], 19) ^ (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->h[0];
    b = s->h[1];
    c = s->h[2];
    d = s->h[3];
    e = s->h[4];
    f = s->h[5];
    g = s->h[6];
    h = s->h[7];
    for (i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25)) + ((e & f) ^ (~e & g)) + sha256_k[i] + w[i];
        uint32_t t2 = (ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));

        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    s->h[0] += a;
    s->h[1] += b;
    s->h[2] += c;
    s->h[3] += d;
    s->h[4] += e;
    s->h[5] += f;
    s->h[6] += g;
    s->h[7] += h;
}

void ko_sha256_init(KoSha256* s) {
    static const uint32_t init[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };

    memcpy(s->h, init, sizeof(init));
    s->length = 0;
    s->used = 0;
}

void ko_sha256_update(KoSha256* s, const void* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;

    s->length += len;
    if (s->used > 0) {
        size_t take = 64 - s->used;

        if (take > len) {
            take = len;
        }
        memcpy(s->block + s->used, p, take);
        s->used += (uint32_t)take;
        p += take;
        len -= take;
        if (s->used < 64) {
            return;
        }
        sha256_block(s, s->block);
        s->used = 0;
    }
    while (len >= 64) {
        sha256_block(s, p);
        p += 64;
        len -= 64;
    }
    memcpy(s->block, p, len);
    s->used = (uint32_t)len;
}

void ko_sha256_final(KoSha256* s, uint8_t digest[32]) {
    uint64_t bits = s->length * 8;
    int i;

    s->block[s->used++] = 0x80;
    if (s->used > 56) {
        memset(s->block + s->used, 0, 64 - s->used);
        sha256_block(s, s->block);
        s->used = 0;
    }
    memset(s->block + s->used, 0, 56 - s->used);
    for (i = 0; i < 8; i++) {
        s->block[56 + i] = (uint8_t)(bits >> (56 - i * 8));
    }
    sha256_block(s, s->block);
    for (i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(s->h[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(s->h[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(s->h[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)s->h[i];
    }
}

void ko_sha256_hex(const uint8_t digest[32], char out[65]) {
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 0; i < 32; i++) {
        out[i * 2] = hex[digest[i] >> 4];
        out[i * 2 + 1] = hex[digest[i] & 15];
    }
    out[64] = '\0';
}

uint32_t ko_adler32(uint32_t adler, const uint8_t* data, size_t len) {
    uint32_t a = adler & 0xFFFF;
    uint32_t b = adler >> 16;

    while (len > 0) {
        /* 5552 bytes keep b below 2^32 before the modulo (as in zlib) */
        size_t n = len < 5552 ? len : 5552;

        len -= n;
        while (n-- > 0) {
            a += *data++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

uint32_t ko_crc32(uint32_t crc, const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static int ready = 0;
    size_t i;

    if (!ready) {
        uint32_t n;

        for (n = 0; n < 256; n++) {
            uint32_t c = n;
            int k;

            for (k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        ready = 1;
    }
    crc = ~crc;
    for (i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

/* =========================================================================
 * Inflate (RFC 1951)
 * ========================================================================= */

#define INF_MAX_BITS 15

typedef struct {
    const uint8_t* src;
    size_t src_len;
    size_t pos;
    uint32_t bits;
    int nbits;
    uint8_t* dst;
    size_t dst_len;
    size_t out;
} Inflate;

/* Canonical Huffman code: how many codes of each length, and the symbols in
 * code order */
typedef struct {
    uint16_t count[INF_MAX_BITS + 1];
    uint16_t symbol[288];
} Huffman;

static int inf_bits(Inflate* s, int n, uint32_t* v) {
    while (s->nbits < n) {
        if (s->pos >= s->src_len) {
            return 0;
        }
        s->bits |= (uint32_t)s->src[s->pos++] << s->nbits;
        s->nbits += 8;
    }
    *v = s->bits & ((1u << n) - 1);
    s->bits >>= n;
    s->nbits -= n;
    return 1;
}

/* 0 = fine (complete or incomplete), -1 = over-subscribed */
static int huff_build(Huffman* h, const uint8_t* lengths, int n) {
    uint16_t next[INF_MAX_BITS + 1];
    int left = 1;
    int len;
    int i;

    memset(h->count, 0, sizeof(h->count));
    for (i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    for (len = 1; len <= INF_MAX_BITS; len++) {
        left = left * 2 - h->count[len];
        if (left < 0) {
            return -1;
        }
    }
    next[1] = 0;
    for (len = 1; len < INF_MAX_BITS; len++) {
        next[len + 1] = next[len] + h->count[len];
    }
    for (i = 0; i < n; i++) {
        if (lengths[i] != 0) {
            h->symbol[next[lengths[i]]++] = (uint16_t)i;
        }
    }
    return 0;
}

/* Codes are stored first bit first, so the code grows one bit at a time and
 * is compared with the first code of each length */
static int huff_decode(Inflate* s, const Huffman* h) {
    int code = 0;
    int first = 0;
    int index = 0;
    int len;

    for (len = 1; len <= INF_MAX_BITS; len++) {
        uint32_t bit;
        int count = h->count[len];

        if (!inf_bits(s, 1, &bit)) {
            return -1;
        }
        code |= (int)bit;
        if (code - first < count) {
            return h->symbol[index + code - first];
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

static const uint16_t len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static const uint8_t len_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
static const uint16_t dist_base[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
static const uint8_t dist_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

static int inf_codes(Inflate* s, const Huffman* lit, const Huffman* dist) {
    for (;;) {
        int sym = huff_decode(s, lit);

        if (sym < 0) {
            return 0;
        }
        if (sym < 256) {
            if (s->out >= s->dst_len) {
                return 0;
            }
            s->dst[s->out++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 1;
        } else {
            uint32_t extra;
            size_t length;
            size_t distance;

            sym -= 257;
            if (sym >= 29 || !inf_bits(s, len_extra[sym], &extra)) {
                return 0;
            }
            length = len_base[sym] + extra;
            sym = huff_decode(s, dist);
            if (sym < 0 || sym >= 30 || !inf_bits(s, dist_extra[sym], &extra)) {
                return 0;
            }
            distance = dist_base[sym] + extra;
            if (distance > s->out || length > s->dst_len - s->out) {
                return 0;
            }
            /* may overlap itself: copy forwards one byte at a time */
            while (length-- > 0) {
                s->dst[s->out] = s->dst[s->out - distance];
                s->out++;
            }
        }
    }
}

static int inf_stored(Inflate* s) {
    size_t len;

    /* the rest of the current byte is padding */
    s->bits = 0;
    s->nbits = 0;
    if (s->src_len - s->pos < 4) {
        return 0;
    }
    len = s->src[s->pos] | ((size_t)s->src[s->pos + 1] << 8);
    if ((s->src[s->pos + 2] ^ 0xFF) != (len & 0xFF) || (s->src[s->pos + 3] ^ 0xFF) != (len >> 8)) {
        return 0;
    }
    s->pos += 4;
    if (len > s->src_len - s->pos || len > s->dst_len - s->out) {
        return 0;
    }
    memcpy(s->dst + s->out, s->src + s->pos, len);
    s->pos += len;
    s->out += len;
    return 1;
}

static int inf_fixed(Inflate* s) {
    static Huffman lit;
    static Huffman dist;
    static int ready = 0;

    if (!ready) {
        uint8_t lengths[288];
        int i;

        for (i = 0; i < 144; i++) lengths[i] = 8;
        for (; i < 256; i++) lengths[i] = 9;
        for (; i < 280; i++) lengths[i] = 7;
        for (; i < 288; i++) lengths[i] = 8;
        huff_build(&lit, lengths, 288);
        for (i = 0; i < 30; i++) lengths[i] = 5;
        huff_build(&dist, lengths, 30);
        ready = 1;
    }
    return inf_codes(s, &lit, &dist);
}

static int inf_dynamic(Inflate* s) {
    static const uint8_t order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    uint8_t lengths[320];
    Huffman lit;
    Huffman dist;
    uint32_t nlit, ndist, ncode, v;
    uint32_t i = 0;

    if (!inf_bits(s, 5, &nlit) || !inf_bits(s, 5, &ndist) || !inf_bits(s, 4, &ncode)) {
        return 0;
    }
    nlit += 257;
    ndist += 1;
    ncode += 4;
    if (nlit > 286 || ndist > 30) {
        return 0;
    }
    memset(lengths, 0, 19);
    for (i = 0; i < ncode; i++) {
        if (!inf_bits(s, 3, &v)) {
            return 0;
        }
        lengths[order[i]] = (uint8_t)v;
    }
    if (huff_build(&lit, lengths, 19) != 0) {
        return 0;
    }

    /* literal/length and distance code lengths, with repeats */
    i = 0;
    while (i < nlit + ndist) {
        int sym = huff_decode(s, &lit);
        uint8_t fill = 0;
        uint32_t repeat;

        if (sym < 0) {
            return 0;
        }
        if (sym < 16) {
            lengths[i++] = (uint8_t)sym;
            continue;
        }
        if (sym == 16) {
            if (i == 0 || !inf_bits(s, 2, &repeat)) {
                return 0;
            }
            fill = lengths[i - 1];
            repeat += 3;
        } else if (sym == 17) {
            if (!inf_bits(s, 3, &repeat)) {
                return 0;
            }
            repeat += 3;
        } else {
            if (!inf_bits(s, 7, &repeat)) {
                return 0;
            }
            repeat += 11;
        }
        if (i + repeat > nlit + ndist) {
            return 0;
        }
        while (repeat-- > 0) {
            lengths[i++] = fill;
        }
    }
    if (lengths[256] == 0) {
        return 0; /* no end-of-block code */
    }
    if (huff_build(&lit, lengths, (int)nlit) != 0 || huff_build(&dist, lengths + nlit, (int)ndist) != 0) {
        return 0;
    }
    return inf_codes(s, &lit, &dist);
}

int ko_inflate(const uint8_t* src, size_t src_len, uint8_t* dst, size_t dst_len) {
    Inflate s;
    uint32_t last;
    uint32_t type;

    memset(&s, 0, sizeof(s));
    s.src = src;
    s.src_len = src_len;
    s.dst = dst;
    s.dst_len = dst_len;
    do {
        int ok;

        if (!inf_bits(&s, 1, &last) || !inf_bits(&s, 2, &type)) {
            return 0;
        }
        switch (type) {
            case 0: ok = inf_stored(&s); break;
            case 1: ok = inf_fixed(&s); break;
            case 2: ok = inf_dynamic(&s); break;
            default: ok = 0; break;
        }
        if (!ok) {
            return 0;
        }
    } while (!last);
    return s.out == dst_len;
}

/* =========================================================================
 * Zip
 * ========================================================================= */

static uint32_t le16(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t le32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int ends_with_ci(const char* s, size_t len, const char* suffix) {
    size_t n = strlen(suffix);
    size_t i;

    if (len < n) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (tolower((unsigned char)s[len - n + i]) != tolower((unsigned char)suffix[i])) {
            return 0;
        }
    }
    return 1;
}

uint8_t* ko_zip_extract(const uint8_t* zip, size_t zip_len, const char* suffix, size_t* out_len, char* name,
                        size_t name_size) {
    size_t eocd;
    size_t p;
    uint32_t entries;
    uint32_t i;

    if (zip_len < 22) {
        return NULL;
    }
    /* End of central directory, behind at most a 64 KB comment */
    eocd = zip_len - 22;
    for (;;) {
        if (le32(zip + eocd) == 0x06054b50) {
            break;
        }
        if (eocd == 0 || zip_len - eocd > 22 + 0xFFFF) {
            return NULL;
        }
        eocd--;
    }
    entries = le16(zip + eocd + 10);
    p = le32(zip + eocd + 16);

    for (i = 0; i < entries; i++) {
        uint32_t method, crc, comp_size, size, name_len, extra_len, comment_len, local;
        const char* entry_name;

        if (p > zip_len || zip_len - p < 46 || le32(zip + p) != 0x02014b50) {
            return NULL;
        }
        method = le16(zip + p + 10);
        crc = le32(zip + p + 16);
        comp_size = le32(zip + p + 20);
        size = le32(zip + p + 24);
        name_len = le16(zip + p + 28);
        extra_len = le16(zip + p + 30);
        comment_len = le16(zip + p + 32);
        local = le32(zip + p + 42);
        entry_name = (const char*)zip + p + 46;
        if (zip_len - p - 46 < name_len) {
            return NULL;
        }

        if (ends_with_ci(entry_name, name_len, suffix)) {
            size_t data;
            uint8_t* out;
            int ok;

            if (local > zip_len || zip_len - local < 30 || le32(zip + local) != 0x04034b50) {
                return NULL;
            }
            data = local + 30 + le16(zip + local + 26) + le16(zip + local + 28);
            if (data > zip_len || zip_len - data < comp_size || (method != 0 && method != 8)) {
                return NULL;
            }
            out = (uint8_t*)malloc(size > 0 ? size : 1);
            if (out == NULL) {
                return NULL;
            }
            if (method == 0) {
                ok = comp_size == size;
                if (ok) {
                    memcpy(out, zip + data, size);
                }
            } else {
                ok = ko_inflate(zip + data, comp_size, out, size);
            }
            if (!ok || ko_crc32(0, out, size) != crc) {
                free(out);
                return NULL;
            }
            if (name != NULL && name_size > 0) {
                size_t n = name_len < name_size - 1 ? name_len : name_size - 1;

                memcpy(name, entry_name, n);
                name[n] = '\0';
            }
            *out_len = size;
            return out;
        }
        p += 46 + name_len + extra_len + comment_len;
    }
    return NULL;
}

/* =========================================================================
 * manifest.json
 * ========================================================================= */

/* Points after the ':' that follows "key", or NULL */
static const char* json_value(const char* json, const char* key) {
    size_t klen = strlen(key);
    const char* p = json;

    while ((p = strchr(p, '"')) != NULL) {
        if (strncmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
            const char* q = p + 2 + klen;

            while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
            if (*q == ':') {
                q++;
                while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
                return q;
            }
        }
        /* skip this string */
        p++;
        while (*p != '\0' && *p != '"') {
            if (*p == '\\' && p[1] != '\0') p++;
            p++;
        }
        if (*p == '\0') {
            return NULL;
        }
        p++;
    }
    return NULL;
}

int ko_json_string(const char* json, const char* key, char* out, size_t n) {
    const char* p = json_value(json, key);
    size_t len = 0;

    if (p == NULL || *p != '"' || n == 0) {
        return 0;
    }
    p++;
    while (*p != '\0' && *p != '"') {
        if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) {
            p++;
        }
        if (len + 1 < n) {
            out[len++] = *p;
        }
        p++;
    }
    out[len] = '\0';
    return *p == '"';
}

int ko_json_u64(const char* json, const char* key, uint64_t* out) {
    const char* p = json_value(json, key);
    uint64_t v = 0;

    if (p == NULL || *p < '0' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint64_t)(*p - '0');
        p++;
    }
    *out = v;
    return 1;
}

/* =========================================================================
 * VCDIFF (RFC 3284)
 * ========================================================================= */

#define VCD_DECOMPRESS 0x01
#define VCD_CODETABLE 0x02
#define VCD_APPHEADER 0x04 /* xdelta3 */
#define VCD_SOURCE 0x01
#define VCD_TARGET 0x02
#define VCD_ADLER32 0x04 /* xdelta3 */

/* Larger windows would not come from xdelta3; refuse them before allocating */
#define VCD_MAX_WINDOW (256u << 20)

enum { INST_NOOP, INST_ADD, INST_RUN, INST_COPY };

typedef struct {
    uint8_t type[2];
    uint8_t size[2];
    uint8_t mode[2];
} CodeEntry;

static CodeEntry code_table[256];

/* The default code table, RFC 3284 section 5.6 */
static void build_code_table(void) {
    static int ready = 0;
    int i = 0;
    int mode, size, add, copy;

    if (ready) {
        return;
    }
    memset(code_table, 0, sizeof(code_table));
    code_table[i].type[0] = INST_RUN;
    i++;
    for (size = 0; size <= 17; size++, i++) {
        code_table[i].type[0] = INST_ADD;
        code_table[i].size[0] = (uint8_t)size;
    }
    for (mode = 0; mode < 9; mode++) {
        code_table[i].type[0] = INST_COPY;
        code_table[i].mode[0] = (uint8_t)mode;
        i++;
        for (size = 4; size <= 18; size++, i++) {
            code_table[i].type[0] = INST_COPY;
            code_table[i].size[0] = (uint8_t)size;
            code_table[i].mode[0] = (uint8_t)mode;
        }
    }
    for (mode = 0; mode < 9; mode++) {
        for (add = 1; add <= 4; add++) {
            for (copy = 4; copy <= (mode < 6 ? 6 : 4); copy++, i++) {
                code_table[i].type[0] = INST_ADD;
                code_table[i].size[0] = (uint8_t)add;
                code_table[i].type[1] = INST_COPY;
                code_table[i].size[1] = (uint8_t)copy;
                code_table[i].mode[1] = (uint8_t)mode;
            }
        }
    }
    for (mode = 0; mode < 9; mode++, i++) {
        code_table[i].type[0] = INST_COPY;
        code_table[i].size[0] = 4;
        code_table[i].mode[0] = (uint8_t)mode;
        code_table[i].type[1] = INST_ADD;
        code_table[i].size[1] = 1;
    }
    ready = 1;
}

typedef struct {
    const uint8_t* p;
    const uint8_t* end;
} Cursor;

static int cur_byte(Cursor* c, uint32_t* v) {
    if (c->p >= c->end) {
        return 0;
    }
    *v = *c->p++;
    return 1;
}

static int cur_varint(Cursor* c, uint64_t* v) {
    uint64_t r = 0;
    int i;

    for (i = 0; i < 10; i++) {
        uint32_t b;

        if (!cur_byte(c, &b) || (r >> 57) != 0) {
            return 0;
        }
        r = (r << 7) | (b & 0x7F);
        if (!(b & 0x80)) {
            *v = r;
            return 1;
        }
    }
    return 0;
}

typedef struct {
    uint32_t indicator;
    uint64_t seg_len;
    uint64_t seg_pos;
    uint64_t target_len;
    uint32_t checksum;
    Cursor data;
    Cursor inst;
    Cursor addr;
} Window;

/* Reads the next window header; the sections stay in the patch */
static int read_window(Cursor* c, Window* w) {
    uint64_t delta_len, data_len, inst_len, addr_len;
    uint32_t delta_indicator;
    const uint8_t* body;

    memset(w, 0, sizeof(*w));
    if (!cur_byte(c, &w->indicator)) {
        return KO_VCDIFF_BAD_PATCH;
    }
    if ((w->indicator & ~7u) != 0 || (w->indicator & 3) == 3) {
        return KO_VCDIFF_BAD_PATCH;
    }
    if (w->indicator & 3) {
        if (!cur_varint(c, &w->seg_len) || !cur_varint(c, &w->seg_pos)) {
            return KO_VCDIFF_BAD_PATCH;
        }
    }
    if (!cur_varint(c, &delta_len) || delta_len > (uint64_t)(c->end - c->p)) {
        return KO_VCDIFF_BAD_PATCH;
    }
    body = c->p + delta_len;
    if (!cur_varint(c, &w->target_len) || !cur_byte(c, &delta_indicator)) {
        return KO_VCDIFF_BAD_PATCH;
    }
    if (delta_indicator != 0) {
        return KO_VCDIFF_UNSUPPORTED; /* secondary compression */
    }
    if (!cur_varint(c, &data_len) || !cur_varint(c, &inst_len) || !cur_varint(c, &addr_len)) {
        return KO_VCDIFF_BAD_PATCH;
    }
    if (w->indicator & VCD_ADLER32) {
        uint32_t b[4];

        if (!cur_byte(c, &b[0]) || !cur_byte(c, &b[1]) || !cur_byte(c, &b[2]) || !cur_byte(c, &b[3])) {
            return KO_VCDIFF_BAD_PATCH;
        }
        w->checksum = (b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    }
    if (c->p > body || data_len > (uint64_t)(body - c->p) || inst_len > (uint64_t)(body - c->p) - data_len ||
        addr_len != (uint64_t)(body - c->p) - data_len - inst_len) {
        return KO_VCDIFF_BAD_PATCH;
    }
    w->data.p = c->p;
    w->data.end = w->inst.p = c->p + data_len;
    w->inst.end = w->addr.p = w->inst.p + inst_len;
    w->addr.end = body;
    c->p = body;
    return KO_VCDIFF_OK;
}

static int read_header(Cursor* c) {
    uint32_t b[5];
    int i;

    for (i = 0; i < 5; i++) {
        if (!cur_byte(c, &b[i])) {
            return KO_VCDIFF_BAD_PATCH;
        }
    }
    if (b[0] != 0xD6 || b[1] != 0xC3 || b[2] != 0xC4 || b[3] != 0) {
        return KO_VCDIFF_BAD_PATCH;
    }
    if (b[4] & (VCD_DECOMPRESS | VCD_CODETABLE)) {
        return KO_VCDIFF_UNSUPPORTED;
    }
    if (b[4] & ~7u) {
        return KO_VCDIFF_BAD_PATCH;
    }
    if (b[4] & VCD_APPHEADER) {
        uint64_t len;

        if (!cur_varint(c, &len) || len > (uint64_t)(c->end - c->p)) {
            return KO_VCDIFF_BAD_PATCH;
        }
        c->p += len;
    }
    return KO_VCDIFF_OK;
}

int ko_vcdiff_target_size(const uint8_t* patch, size_t len, uint64_t* size) {
    Cursor c = { patch, patch + len };
    uint64_t total = 0;
    int err = read_header(&c);

    while (err == KO_VCDIFF_OK && c.p < c.end) {
        Window w;

        err = read_window(&c, &w);
        total += w.target_len;
    }
    *size = total;
    return err;
}

/* Runs one window's instructions into out (target_len bytes); src holds the
 * window's source segment */
static int decode_window(Window* w, const uint8_t* src, uint8_t* out) {
    uint64_t near_cache[4] = { 0, 0, 0, 0 };
    uint64_t same_cache[3 * 256];
    int next_near = 0;
    uint64_t pos = 0;

    memset(same_cache, 0, sizeof(same_cache));
    while (w->inst.p < w->inst.end) {
        uint32_t index;
        int half;

        cur_byte(&w->inst, &index);
        for (half = 0; half < 2; half++) {
            const CodeEntry* e = &code_table[index];
            uint64_t size = e->size[half];
            uint32_t mode = e->mode[half];

            if (e->type[half] == INST_NOOP) {
                continue;
            }
            if (size == 0 && !cur_varint(&w->inst, &size)) {
                return KO_VCDIFF_BAD_PATCH;
            }
            if (size > w->target_len - pos) {
                return KO_VCDIFF_BAD_PATCH;
            }

            if (e->type[half] == INST_ADD) {
                if (size > (uint64_t)(w->data.end - w->data.p)) {
                    return KO_VCDIFF_BAD_PATCH;
                }
                memcpy(out + pos, w->data.p, (size_t)size);
                w->data.p += size;
                pos += size;
            } else if (e->type[half] == INST_RUN) {
                uint32_t b;

                if (!cur_byte(&w->data, &b)) {
                    return KO_VCDIFF_BAD_PATCH;
                }
                memset(out + pos, (int)b, (size_t)size);
                pos += size;
            } else {
                uint64_t here = w->seg_len + pos;
                uint64_t addr;
                uint64_t v;

                if (mode == 0) {
                    if (!cur_varint(&w->addr, &addr)) return KO_VCDIFF_BAD_PATCH;
                } else if (mode == 1) {
                    if (!cur_varint(&w->addr, &v) || v > here) return KO_VCDIFF_BAD_PATCH;
                    addr = here - v;
                } else if (mode < 6) {
                    if (!cur_varint(&w->addr, &v)) return KO_VCDIFF_BAD_PATCH;
                    addr = near_cache[mode - 2] + v;
                } else {
                    uint32_t b;

                    if (!cur_byte(&w->addr, &b)) return KO_VCDIFF_BAD_PATCH;
                    addr = same_cache[(mode - 6) * 256 + b];
                }
                if (addr >= here) {
                    return KO_VCDIFF_BAD_PATCH;
                }
                near_cache[next_near] = addr;
                next_near = (next_near + 1) & 3;
                same_cache[addr % (3 * 256)] = addr;

                /* from the source segment, then on into this window's target,
                 * which may overlap what is being written */
                while (size > 0 && addr < w->seg_len) {
                    uint64_t n = w->seg_len - addr < size ? w->seg_len - addr : size;

                    memcpy(out + pos, src + addr, (size_t)n);
                    pos += n;
                    addr += n;
                    size -= n;
                }
                if (size > 0) {
                    uint64_t from = addr - w->seg_len;

                    if (from + size <= pos) {
                        memcpy(out + pos, out + from, (size_t)size);
                        pos += size;
                    } else {
                        while (size-- > 0) {
                            out[pos++] = out[from++];
                        }
                    }
                }
            }
        }
    }
    if (pos != w->target_len) {
        return KO_VCDIFF_BAD_PATCH;
    }
    return KO_VCDIFF_OK;
}

int ko_vcdiff_apply(const uint8_t* patch, size_t len, const KoVcdiffIo* io) {
    Cursor c = { patch, patch + len };
    uint64_t total = 0;
    uint64_t done = 0;
    uint8_t* src = NULL;
    uint8_t* out = NULL;
    size_t src_cap = 0;
    size_t out_cap = 0;
    int err;

    build_code_table();
    err = ko_vcdiff_target_size(patch, len, &total);
    if (err == KO_VCDIFF_OK) {
        err = read_header(&c);
    }

    while (err == KO_VCDIFF_OK && c.p < c.end) {
        Window w;

        if (io->cancel != NULL && *io->cancel) {
            err = KO_VCDIFF_CANCELLED;
            break;
        }
        err = read_window(&c, &w);
        if (err != KO_VCDIFF_OK) {
            break;
        }
        if (w.seg_len > VCD_MAX_WINDOW || w.target_len > VCD_MAX_WINDOW) {
            err = KO_VCDIFF_UNSUPPORTED;
            break;
        }

        if (w.seg_len > src_cap) {
            uint8_t* p = (uint8_t*)realloc(src, (size_t)w.seg_len);

            if (p == NULL) {
                err = KO_VCDIFF_NO_MEMORY;
                break;
            }
            src = p;
            src_cap = (size_t)w.seg_len;
        }
        if (w.target_len > out_cap || out == NULL) {
            uint8_t* p = (uint8_t*)realloc(out, w.target_len > 0 ? (size_t)w.target_len : 1);

            if (p == NULL) {
                err = KO_VCDIFF_NO_MEMORY;
                break;
            }
            out = p;
            out_cap = (size_t)w.target_len;
        }

        if (w.indicator & VCD_SOURCE) {
            if (!io->read_source(io->source_user, w.seg_pos, src, (size_t)w.seg_len)) {
                err = KO_VCDIFF_READ_ERROR;
                break;
            }
        } else if (w.indicator & VCD_TARGET) {
            if (io->read_target == NULL || w.seg_pos > done || w.seg_len > done - w.seg_pos ||
                !io->read_target(io->target_user, w.seg_pos, src, (size_t)w.seg_len)) {
                err = io->read_target == NULL ? KO_VCDIFF_UNSUPPORTED : KO_VCDIFF_READ_ERROR;
                break;
            }
        }

        err = decode_window(&w, src, out);
        if (err != KO_VCDIFF_OK) {
            break;
        }
        if ((w.indicator & VCD_ADLER32) && ko_adler32(1, out, (size_t)w.target_len) != w.checksum) {
            err = KO_VCDIFF_SOURCE_MISMATCH;
            break;
        }
        if (!io->write_target(io->target_user, out, (size_t)w.target_len)) {
            err = KO_VCDIFF_WRITE_ERROR;
            break;
        }
        done += w.target_len;
        if (io->progress != NULL) {
            io->progress(io->progress_user, done, total);
        }
    }

    free(src);
    free(out);
    return err;
}
