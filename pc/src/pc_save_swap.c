/* pc_save_swap.c - e+ save data between the PC port's byte order (little-endian)
 * and the GameCube's (big-endian). See pc_save_swap.h.
 *
 * The field list is pc_save_swap_table.inc, generated from the e+ headers by
 * pc/tools/gen_save_swap.py; it carries static checks against those headers.
 * This file walks it, and holds what the generator cannot know: the tagged
 * unions (which member is in use depends on another field) and the
 * checksums. */
#include "m_common_data.h"
#include "m_card.h"
#include "m_quest.h"
#include "m_event.h"
#include "Famicom/famicom.h"

#include "pc_save_swap.h"

#include <stddef.h>
#include <string.h>

/* --- table types (used by the generated file) --- */

typedef struct {
    u8 k; /* first bit, counted from the least significant bit (PC order) */
    u8 w; /* width in bits */
} pc_swap_bitfield_t;

enum { PC_SWK_END, PC_SWK_SCALAR, PC_SWK_STRUCT, PC_SWK_BITS, PC_SWK_CUSTOM };

typedef void (*pc_swap_custom_fn)(u8* p, pc_swap_dir_t dir);

typedef struct pc_swap_entry_s {
    u32 ofs;
    u8 kind;
    u8 width;  /* scalar: bytes per element; bits: bytes of the unit */
    u32 count; /* scalar/struct: elements; bits: fields */
    u32 stride;
    const struct pc_swap_entry_s* table;
    const pc_swap_bitfield_t* fields;
    pc_swap_custom_fn fn;
} pc_swap_entry_t;

#define PC_SWAP_SCALAR(o, w, n) { (o), PC_SWK_SCALAR, (w), (n), (w), NULL, NULL, NULL }
#define PC_SWAP_STRUCT(o, t, n, s) { (o), PC_SWK_STRUCT, 0, (n), (s), (t), NULL, NULL }
#define PC_SWAP_BITS(o, w, n, f) { (o), PC_SWK_BITS, (w), (n), 0, NULL, (f), NULL }
#define PC_SWAP_CUSTOM(o, fn, s) { (o), PC_SWK_CUSTOM, 0, 1, (s), NULL, NULL, (fn) }
#define PC_SWAP_END { 0, PC_SWK_END, 0, 0, 0, NULL, NULL, NULL }
#define PC_SWAP_CHECK(x) _Static_assert(x, #x)

/* the tagged unions, defined below */
static void pc_swap_errand_info(u8* p, pc_swap_dir_t dir);
static void pc_swap_contest_info(u8* p, pc_swap_dir_t dir);
static void pc_swap_special_event(u8* p, pc_swap_dir_t dir);

#include "pc_save_swap_table.inc"

/* --- the file layout in pc_save_swap.h against the headers --- */

PC_SWAP_CHECK(PC_LAND_FILE_SIZE == mCD_LAND_SAVE_SIZE);
PC_SWAP_CHECK(PC_LAND_DIARY_OFS == sizeof(MemcardHeader_c) + 32);
PC_SWAP_CHECK(PC_LAND_DIARY_SIZE == ALIGN_NEXT(sizeof(mCD_keep_diary_c), 32));
PC_SWAP_CHECK(offsetof(mCD_keep_diary_c, checksum) == 0);
PC_SWAP_CHECK(PC_LAND_DIARY_OFS + PC_LAND_DIARY_SIZE <= mCD_MISC_SAVE_SIZE);
PC_SWAP_CHECK(PC_LAND_SAVE_OFS == mCD_MISC_SAVE_SIZE);
PC_SWAP_CHECK(PC_LAND_SAVE_SIZE == sizeof(Save));
PC_SWAP_CHECK(PC_LAND_SAVE_T_SIZE == sizeof(Save_t));
PC_SWAP_CHECK(PC_LAND_SAVE_CHECKSUM == offsetof(Save_t, save_check.checksum));
PC_SWAP_CHECK(PC_LAND_BACKUP_OFS + PC_LAND_SAVE_SIZE <= PC_LAND_FILE_SIZE);
PC_SWAP_CHECK(PC_FAMICOM_HEADER_SIZE == FAMICOM_SAVE_HEADER_SIZE);
PC_SWAP_CHECK(PC_FAMICOM_HEADER_SIZE == sizeof(FamicomSaveDataHeader));
PC_SWAP_CHECK(PC_FAMICOM_PLAYERS == PLAYER_NUM);
PC_SWAP_CHECK(offsetof(FamicomSaveDataHeader, size) == 0x0C);
PC_SWAP_CHECK(offsetof(FamicomSaveDataHeader, headerSize) == 0x0A);

/* --- the walk --- */

static u64 get_be(const u8* p, int n) {
    u64 v = 0;
    int i;
    for (i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

static u64 get_le(const u8* p, int n) {
    u64 v = 0;
    int i;
    for (i = n - 1; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static void put_be(u8* p, int n, u64 v) {
    int i;
    for (i = n - 1; i >= 0; i--, v >>= 8) p[i] = (u8)v;
}

static void put_le(u8* p, int n, u64 v) {
    int i;
    for (i = 0; i < n; i++, v >>= 8) p[i] = (u8)v;
}

static void swap_bytes(u8* p, int n) {
    int i;
    for (i = 0; i < n / 2; i++) {
        u8 t = p[i];
        p[i] = p[n - 1 - i];
        p[n - 1 - i] = t;
    }
}

/* A bit-field unit: the GameCube fills it from the most significant bit, the
 * PC from the least. Each field keeps its value; bits no field covers are
 * mirrored, so that converting back gives the same bytes. */
static void repack_bits(u8* p, int n, const pc_swap_bitfield_t* f, int nf, pc_swap_dir_t dir) {
    int bits = n * 8;
    u64 src = dir == PC_SWAP_FROM_BE ? get_be(p, n) : get_le(p, n);
    u64 dst = 0, covered = 0;
    int i;

    for (i = 0; i < nf; i++) {
        u64 mask = (f[i].w >= 64 ? ~0ull : ((1ull << f[i].w) - 1));
        int le_pos = f[i].k;
        int be_pos = bits - f[i].k - f[i].w;
        int from = dir == PC_SWAP_FROM_BE ? be_pos : le_pos;
        int to = dir == PC_SWAP_FROM_BE ? le_pos : be_pos;

        dst |= ((src >> from) & mask) << to;
        covered |= mask << le_pos;
    }
    for (i = 0; i < bits; i++) {
        /* a bit outside the fields, at LE position i, is at BE position i too
         * when counted from the other end */
        if (!((covered >> i) & 1)) {
            int from = dir == PC_SWAP_FROM_BE ? bits - 1 - i : i;
            int to = dir == PC_SWAP_FROM_BE ? i : bits - 1 - i;
            dst |= ((src >> from) & 1) << to;
        }
    }
    if (dir == PC_SWAP_FROM_BE) {
        put_le(p, n, dst);
    } else {
        put_be(p, n, dst);
    }
}

static void walk(u8* base, const pc_swap_entry_t* t, pc_swap_dir_t dir) {
    for (; t->kind != PC_SWK_END; t++) {
        u8* p = base + t->ofs;
        u32 i;

        switch (t->kind) {
            case PC_SWK_SCALAR:
                for (i = 0; i < t->count; i++) swap_bytes(p + i * t->width, t->width);
                break;
            case PC_SWK_STRUCT:
                for (i = 0; i < t->count; i++) walk(p + i * t->stride, t->table, dir);
                break;
            case PC_SWK_BITS:
                repack_bits(p, t->width, t->fields, (int)t->count, dir);
                break;
            case PC_SWK_CUSTOM:
                t->fn(p, dir);
                break;
        }
    }
}

/* --- tagged unions ---
 * Entries are walked in offset order, so when a union is reached the field
 * telling its member (earlier in the structure) is already in the target
 * order: little-endian when coming from the GameCube, big-endian going to it. */

/* mQst_errand_c.info: errand_type is a 3-bit field in the byte after item */
#define ERRAND_TYPE_BYTE (offsetof(mQst_errand_c, item) + sizeof(mActor_name_t))
PC_SWAP_CHECK(ERRAND_TYPE_BYTE == 0x36 && offsetof(mQst_errand_c, info) == 0x38);

static void pc_swap_errand_info(u8* p, pc_swap_dir_t dir) {
    u8 b = p[ERRAND_TYPE_BYTE - offsetof(mQst_errand_c, info)];
    /* pockets_idx : 5, errand_type : 3 (signed) */
    int bits = dir == PC_SWAP_FROM_BE ? (b >> 5) & 7 : b & 7;
    int type = bits >= 4 ? bits - 8 : bits;

    if (type == mQst_ERRAND_TYPE_FIRST_JOB) {
        walk(p, pc_swt_mQst_errand_info_u_first_job, dir);
    } else {
        walk(p, pc_swt_mQst_errand_info_u_chain, dir);
    }
}

/* mQst_contest_c.info: by mQst_contest_c.type (a byte) */
static void pc_swap_contest_info(u8* p, pc_swap_dir_t dir) {
    s8 type = (s8)p[(ptrdiff_t)offsetof(mQst_contest_c, type) - (ptrdiff_t)offsetof(mQst_contest_c, info)];

    switch (type) {
        case mQst_CONTEST_DATA_LETTER:
            walk(p, pc_swt_mQst_contest_info_u_letter_data, dir);
            break;
        case mQst_CONTEST_DATA_FLOWER:
            walk(p, pc_swt_mQst_contest_info_u_flower_data, dir);
            break;
        case mQst_CONTEST_DATA_GRASS:
            walk(p, pc_swt_mQst_contest_info_u_grass_data, dir);
            break;
        default: /* no data: the bytes stay */
            break;
    }
}

/* mEv_special_c.event: by mEv_special_c.kind, an mEv_SPNPC_* value */
PC_SWAP_CHECK(sizeof(((mEv_special_c*)0)->kind) == 4);

static void pc_swap_special_event(u8* p, pc_swap_dir_t dir) {
    static const pc_swap_entry_t* const tables[] = {
        pc_swt_mEv_special_u_bargin, pc_swt_mEv_special_u_designer, pc_swt_mEv_special_u_broker,
        pc_swt_mEv_special_u_artist, pc_swt_mEv_special_u_arabian,  pc_swt_mEv_special_u_gypsy,
    };
    const u8* kp = p - offsetof(mEv_special_c, event) + offsetof(mEv_special_c, kind);
    u32 kind = (u32)(dir == PC_SWAP_FROM_BE ? get_le(kp, 4) : get_be(kp, 4));

    if (kind < (u32)mEv_SPNPC_END) {
        walk(p, tables[kind], dir);
    }
    /* no special event (-1): the bytes stay */
}
PC_SWAP_CHECK(mEv_SPNPC_SHOP == 0 && mEv_SPNPC_END == 6); /* m_event.c indexes special_events[] with it */

/* --- structures --- */

void pc_swap_save_t(uint8_t* save, pc_swap_dir_t dir) {
    walk(save, PC_SWAP_TABLE_Save_t, dir);
}

void pc_swap_keep_diary(uint8_t* diary, pc_swap_dir_t dir) {
    walk(diary, PC_SWAP_TABLE_mCD_keep_diary_c, dir);
}

void pc_swap_famicom_header(uint8_t* header, pc_swap_dir_t dir) {
    walk(header, PC_SWAP_TABLE_FamicomSaveDataHeader, dir);
}

/* --- checksums --- */

uint16_t pc_save_sum16(const uint8_t* data, size_t size, int big_endian) {
    u16 sum = 0;
    size_t i;

    for (i = 0; i + 1 < size; i += 2) {
        sum += big_endian ? (u16)((data[i] << 8) | data[i + 1]) : (u16)(data[i] | (data[i + 1] << 8));
    }
    return sum;
}

void pc_save_set_checksum(uint8_t* data, size_t size, size_t checksum_ofs, int big_endian) {
    u16 sum;

    data[checksum_ofs] = data[checksum_ofs + 1] = 0;
    sum = (u16)(0x10000 - pc_save_sum16(data, size, big_endian));
    if (big_endian) {
        put_be(data + checksum_ofs, 2, sum);
    } else {
        put_le(data + checksum_ofs, 2, sum);
    }
}

/* Convert a block with a checksum: right before, right after; wrong before,
 * wrong after */
static int convert_checked(u8* block, size_t size, size_t checksum_ofs, void (*swap)(u8*, pc_swap_dir_t),
                           pc_swap_dir_t dir) {
    int src_be = dir == PC_SWAP_FROM_BE;
    int ok = pc_save_sum16(block, size, src_be) == 0;

    swap(block, dir);
    if (ok) {
        pc_save_set_checksum(block, size, checksum_ofs, !src_be);
    } else if (pc_save_sum16(block, size, !src_be) == 0) {
        block[checksum_ofs] ^= 1; /* became right by chance: keep it broken */
    }
    return ok;
}

int pc_save_land_convert(uint8_t* data, size_t size, pc_swap_dir_t dir, pc_save_report_t* report) {
    pc_save_report_t r;

    if (size != PC_LAND_FILE_SIZE) return -1;
    memset(&r, 0, sizeof(r));
    r.diary_ok = convert_checked(data + PC_LAND_DIARY_OFS, PC_LAND_DIARY_SIZE, 0, pc_swap_keep_diary, dir);
    r.save_ok = convert_checked(data + PC_LAND_SAVE_OFS, PC_LAND_SAVE_SIZE, PC_LAND_SAVE_CHECKSUM, pc_swap_save_t, dir);
    r.backup_ok =
        convert_checked(data + PC_LAND_BACKUP_OFS, PC_LAND_SAVE_SIZE, PC_LAND_SAVE_CHECKSUM, pc_swap_save_t, dir);
    if (report) *report = r;
    return 0;
}

static u8 byte_sum(const u8* p, size_t n) {
    u8 s = 0;
    while (n--) s += *p++;
    return s;
}

int pc_save_famicom_valid(const uint8_t* data, size_t size, int big_endian) {
    const u8* h = data;
    u32 player_size;
    u32 i;

    if (size < PC_FAMICOM_HEADER_SIZE) return 0;
    if (memcmp(h, PC_FAMICOM_ID, 8) != 0 || byte_sum(h, PC_FAMICOM_HEADER_SIZE) != 0) return 0;
    if (!((h[8] == 1 && h[9] == 0) || (h[8] == 0 && h[9] == 1)) || h[0x0A] != PC_FAMICOM_HEADER_SIZE) return 0;
    player_size = (u32)(big_endian ? get_be(h + 0x0C, 2) : get_le(h + 0x0C, 2));
    if (PC_FAMICOM_HEADER_SIZE + player_size * PC_FAMICOM_PLAYERS > size) return 0;
    for (i = 0; i < PC_FAMICOM_PLAYERS; i++) {
        const u8* d = data + PC_FAMICOM_HEADER_SIZE + i * player_size;
        if (d[0] != 'D' || byte_sum(d, player_size) != 0) return 0;
    }
    return 1;
}

int pc_save_famicom_convert(uint8_t* data, size_t size, pc_swap_dir_t dir, pc_save_report_t* report) {
    if (size != PC_FAMICOM_SAVE_SIZE || memcmp(data, PC_FAMICOM_ID, 8) != 0) return -1;
    if (report) {
        memset(report, 0, sizeof(*report));
        report->famicom_ok = pc_save_famicom_valid(data, size, dir == PC_SWAP_FROM_BE);
    }
    /* the player data is bytes for the NES emulator; the header's byte sum
     * does not change with the order of its bytes */
    pc_swap_famicom_header(data, dir);
    return 0;
}
