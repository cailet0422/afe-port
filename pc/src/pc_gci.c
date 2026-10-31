/* pc_gci.c - GameCube memory card files (.gci) for the e+ saves; see pc_gci.h */
#include "pc_gci.h"
#include "pc_save_swap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* card directory entry (dolphin/card.h CARDDir), big-endian */
#define DE_GAME         0x00
#define DE_COMPANY      0x04
#define DE_PAD0         0x06
#define DE_BANNER_FMT   0x07
#define DE_FILENAME     0x08
#define DE_TIME         0x28
#define DE_ICON_ADDR    0x2C
#define DE_ICON_FMT     0x30
#define DE_ICON_SPEED   0x32
#define DE_PERMISSION   0x34
#define DE_COPY_TIMES   0x35
#define DE_START_BLOCK  0x36
#define DE_LENGTH       0x38
#define DE_PAD1         0x3A
#define DE_COMMENT_ADDR 0x3C

/* CARDStat formats (dolphin/card.h) */
#define BANNER_C8     1
#define BANNER_RGB5A3 2
#define ICON_C8       1
#define ICON_RGB5A3   2
#define ICON_C8_OWN   3 /* C8 with its own palette (later SDKs) */
#define SPEED_SLOW    3
#define ATTR_PUBLIC   0x04
#define ATTR_NO_COPY  0x08
#define ATTR_NO_MOVE  0x10

#define COMMENT_SIZE 64
#define FIRST_USER_BLOCK 5

/* The town file's entry as the game sets it: m_card.c
 * mCD_SaveHome_bg_get_status_2 (comment at 0, icon after it, one C8 frame at
 * slow speed, C8 banner) and the file's creation (no copy, no move) */
#define LAND_ICON_ADDR COMMENT_SIZE

/* "どうぶつの森ｅ＋" in Shift-JIS: the first comment line of the game's files
 * (m_card.c l_comment_0_str) */
static const char comment_title_sjis[] = "\x82\xC7\x82\xA4\x82\xD4\x82\xC2\x82\xCC\x90\x58\x82\x85\x81\x7B";
static const char comment_famicom[] = "Famicom (PC port export)";

static uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void wr32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void wr16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void pc_gci_parse_dentry(const uint8_t* h, pc_gci_dentry_t* d) {
    memset(d, 0, sizeof(*d));
    memcpy(d->game, h + DE_GAME, 4);
    memcpy(d->company, h + DE_COMPANY, 2);
    d->banner_format = h[DE_BANNER_FMT];
    memcpy(d->filename, h + DE_FILENAME, 32);
    d->time = rd32(h + DE_TIME);
    d->icon_addr = rd32(h + DE_ICON_ADDR);
    d->icon_format = rd16(h + DE_ICON_FMT);
    d->icon_speed = rd16(h + DE_ICON_SPEED);
    d->permission = h[DE_PERMISSION];
    d->copy_times = h[DE_COPY_TIMES];
    d->start_block = rd16(h + DE_START_BLOCK);
    d->length = rd16(h + DE_LENGTH);
    d->comment_addr = rd32(h + DE_COMMENT_ADDR);
}

void pc_gci_write_dentry(uint8_t* h, const pc_gci_dentry_t* d) {
    memset(h, 0, PC_GCI_HEADER_SIZE);
    memcpy(h + DE_GAME, d->game, 4);
    memcpy(h + DE_COMPANY, d->company, 2);
    h[DE_PAD0] = 0xFF;
    h[DE_BANNER_FMT] = d->banner_format;
    strncpy((char*)h + DE_FILENAME, d->filename, 32);
    wr32(h + DE_TIME, d->time);
    wr32(h + DE_ICON_ADDR, d->icon_addr);
    wr16(h + DE_ICON_FMT, d->icon_format);
    wr16(h + DE_ICON_SPEED, d->icon_speed);
    h[DE_PERMISSION] = d->permission;
    h[DE_COPY_TIMES] = d->copy_times;
    wr16(h + DE_START_BLOCK, d->start_block);
    wr16(h + DE_LENGTH, d->length);
    h[DE_PAD1] = h[DE_PAD1 + 1] = 0xFF;
    wr32(h + DE_COMMENT_ADDR, d->comment_addr);
}

/* As the card library's UpdateIconOffsets: banner (96x32), then the icon
 * frames (32x32) up to the first without a format, then the shared palette */
uint32_t pc_gci_data_offset(const pc_gci_dentry_t* d) {
    uint32_t ofs = d->icon_addr;
    int banner = d->banner_format & 3;
    int shared_tlut = 0;
    int i;

    if (ofs == 0xFFFFFFFFu) return 0;
    if (banner == BANNER_C8) {
        ofs += 96 * 32 + 2 * 256;
    } else if (banner == BANNER_RGB5A3) {
        ofs += 2 * 96 * 32;
    }
    for (i = 0; i < 8; i++) {
        int fmt = (d->icon_format >> (2 * i)) & 3;

        if (fmt == ICON_C8) {
            ofs += 32 * 32;
            shared_tlut = 1;
        } else if (fmt == ICON_RGB5A3) {
            ofs += 2 * 32 * 32;
        } else if (fmt == ICON_C8_OWN) {
            ofs += 32 * 32 + 2 * 256;
        }
    }
    if (shared_tlut) ofs += 2 * 256;
    return ofs;
}

static pc_gci_kind_t kind_of(const char* filename) {
    if (strcmp(filename, PC_GCI_LAND_NAME) == 0) return PC_GCI_KIND_LAND;
    if (strcmp(filename, PC_GCI_FAMICOM_NAME) == 0) return PC_GCI_KIND_FAMICOM;
    return PC_GCI_KIND_UNKNOWN;
}

#define FAIL(code, ...)                               \
    do {                                              \
        if (err) snprintf(err, err_size, __VA_ARGS__); \
        return (code);                                \
    } while (0)

int pc_gci_import(const uint8_t* gci, size_t gci_size, uint8_t** out, size_t* out_size, pc_gci_info_t* info,
                  char* err, size_t err_size) {
    pc_gci_dentry_t d;
    pc_gci_info_t inf;
    size_t data_size;
    const uint8_t* data;
    uint8_t* buf;

    memset(&inf, 0, sizeof(inf));
    *out = NULL;
    *out_size = 0;
    if (gci_size < PC_GCI_HEADER_SIZE) FAIL(PC_GCI_ERR_FORMAT, "too small for a GCI");
    pc_gci_parse_dentry(gci, &d);
    data = gci + PC_GCI_HEADER_SIZE;
    data_size = gci_size - PC_GCI_HEADER_SIZE;
    if (memcmp(d.game, "GAEJ", 4) != 0 || memcmp(d.company, "01", 2) != 0) {
        FAIL(PC_GCI_ERR_FORMAT, "not a file of Doubutsu no Mori e+ (game %.4s, maker %.2s; this port reads GAEJ01)",
             d.game, d.company);
    }
    if ((size_t)d.length * PC_GCI_BLOCK_SIZE != data_size) {
        FAIL(PC_GCI_ERR_FORMAT, "the entry says %u blocks, the file has 0x%zX bytes of data", d.length, data_size);
    }
    inf.kind = kind_of(d.filename);

    if (inf.kind == PC_GCI_KIND_LAND) {
        pc_save_report_t r;

        if (data_size != PC_LAND_FILE_SIZE) FAIL(PC_GCI_ERR_FORMAT, "town file of 0x%zX bytes, expected 0x%X", data_size, PC_LAND_FILE_SIZE);
        buf = (uint8_t*)malloc(data_size);
        if (!buf) FAIL(PC_GCI_ERR_MEMORY, "out of memory");
        memcpy(buf, data, data_size);
        pc_save_land_convert(buf, data_size, PC_SWAP_FROM_BE, &r);
        inf.save_ok = r.save_ok;
        inf.backup_ok = r.backup_ok;
        inf.diary_ok = r.diary_ok;
        if (!r.save_ok && !r.backup_ok) {
            free(buf);
            FAIL(PC_GCI_ERR_DATA, "neither copy of the town data has a valid checksum");
        }
        *out = buf;
        *out_size = data_size;
    } else if (inf.kind == PC_GCI_KIND_FAMICOM) {
        pc_save_report_t r;
        uint32_t ofs = pc_gci_data_offset(&d);

        if (ofs + PC_FAMICOM_SAVE_SIZE > data_size) FAIL(PC_GCI_ERR_FORMAT, "Famicom save data past the end of the file");
        if (memcmp(data + ofs, PC_FAMICOM_ID, 8) != 0) {
            FAIL(PC_GCI_ERR_DATA, "no Famicom save header at 0x%X (after comment, banner and icon)", ofs);
        }
        buf = (uint8_t*)malloc(PC_FAMICOM_SAVE_SIZE);
        if (!buf) FAIL(PC_GCI_ERR_MEMORY, "out of memory");
        memcpy(buf, data + ofs, PC_FAMICOM_SAVE_SIZE);
        pc_save_famicom_convert(buf, PC_FAMICOM_SAVE_SIZE, PC_SWAP_FROM_BE, &r);
        inf.famicom_ok = r.famicom_ok;
        if (!r.famicom_ok) {
            free(buf);
            FAIL(PC_GCI_ERR_DATA, "the Famicom save's checksums are not valid");
        }
        *out = buf;
        *out_size = PC_FAMICOM_SAVE_SIZE;
    } else {
        FAIL(PC_GCI_ERR_FORMAT, "'%s' is not a file this tool converts (%s, %s)", d.filename, PC_GCI_LAND_NAME,
             PC_GCI_FAMICOM_NAME);
    }
    if (info) *info = inf;
    return PC_GCI_OK;
}

int pc_gci_export(pc_gci_kind_t kind, const uint8_t* pc, size_t pc_size, const uint8_t* template_gci,
                  size_t template_size, uint32_t time, uint8_t** out, size_t* out_size, pc_gci_info_t* info,
                  char* err, size_t err_size) {
    pc_gci_dentry_t d;
    pc_gci_info_t inf;
    uint8_t* gci;
    size_t data_size;

    memset(&inf, 0, sizeof(inf));
    memset(&d, 0, sizeof(d));
    *out = NULL;
    *out_size = 0;
    inf.kind = kind;
    memcpy(d.game, "GAEJ", 4);
    memcpy(d.company, "01", 2);
    d.time = time;
    d.start_block = FIRST_USER_BLOCK; /* the card gives it a place on import */

    if (kind == PC_GCI_KIND_LAND) {
        pc_save_report_t r;

        if (pc_size != PC_LAND_FILE_SIZE) FAIL(PC_GCI_ERR_FORMAT, "town file of 0x%zX bytes, expected 0x%X", pc_size, PC_LAND_FILE_SIZE);
        data_size = pc_size;
        gci = (uint8_t*)malloc(PC_GCI_HEADER_SIZE + data_size);
        if (!gci) FAIL(PC_GCI_ERR_MEMORY, "out of memory");
        memcpy(gci + PC_GCI_HEADER_SIZE, pc, data_size);
        pc_save_land_convert(gci + PC_GCI_HEADER_SIZE, data_size, PC_SWAP_TO_BE, &r);
        inf.save_ok = r.save_ok;
        inf.backup_ok = r.backup_ok;
        inf.diary_ok = r.diary_ok;
        if (!r.save_ok && !r.backup_ok) {
            free(gci);
            FAIL(PC_GCI_ERR_DATA, "neither copy of the town data has a valid checksum");
        }
        strcpy(d.filename, PC_GCI_LAND_NAME);
        d.banner_format = BANNER_C8;
        d.icon_addr = LAND_ICON_ADDR;
        d.icon_format = ICON_C8;
        d.icon_speed = SPEED_SLOW;
        d.permission = ATTR_PUBLIC | ATTR_NO_COPY | ATTR_NO_MOVE;
        d.comment_addr = 0;
    } else if (kind == PC_GCI_KIND_FAMICOM) {
        pc_save_report_t r;
        uint8_t save[PC_FAMICOM_SAVE_SIZE];
        uint32_t ofs;

        if (pc_size != PC_FAMICOM_SAVE_SIZE) FAIL(PC_GCI_ERR_FORMAT, "Famicom save of 0x%zX bytes, expected 0x%X", pc_size, PC_FAMICOM_SAVE_SIZE);
        memcpy(save, pc, sizeof(save));
        if (pc_save_famicom_convert(save, sizeof(save), PC_SWAP_TO_BE, &r) != 0) {
            FAIL(PC_GCI_ERR_DATA, "not a Famicom common save (no %s header)", "GAEJFSC");
        }
        inf.famicom_ok = r.famicom_ok;
        if (!r.famicom_ok) FAIL(PC_GCI_ERR_DATA, "the Famicom save's checksums are not valid");

        if (template_gci) {
            pc_gci_dentry_t t;

            if (template_size < PC_GCI_HEADER_SIZE) FAIL(PC_GCI_ERR_FORMAT, "template too small for a GCI");
            pc_gci_parse_dentry(template_gci, &t);
            if (strcmp(t.filename, PC_GCI_FAMICOM_NAME) != 0) FAIL(PC_GCI_ERR_FORMAT, "the template is not %s", PC_GCI_FAMICOM_NAME);
            ofs = pc_gci_data_offset(&t);
            if (PC_GCI_HEADER_SIZE + ofs > template_size) FAIL(PC_GCI_ERR_FORMAT, "template shorter than its own picture");
            d = t;
            d.time = time;
        } else {
            /* comment only: the data follows it (iconAddr past the comment,
             * no banner or icon: offsetData = 0x40) */
            strcpy(d.filename, PC_GCI_FAMICOM_NAME);
            d.banner_format = 0;
            d.icon_addr = COMMENT_SIZE;
            d.icon_format = 0;
            d.icon_speed = 0;
            d.permission = ATTR_PUBLIC;
            d.comment_addr = 0;
            ofs = COMMENT_SIZE;
        }
        data_size = (ofs + PC_FAMICOM_SAVE_SIZE + PC_GCI_BLOCK_SIZE - 1) / PC_GCI_BLOCK_SIZE * PC_GCI_BLOCK_SIZE;
        gci = (uint8_t*)calloc(1, PC_GCI_HEADER_SIZE + data_size);
        if (!gci) FAIL(PC_GCI_ERR_MEMORY, "out of memory");
        if (template_gci) {
            memcpy(gci + PC_GCI_HEADER_SIZE, template_gci + PC_GCI_HEADER_SIZE, ofs);
        } else {
            memcpy(gci + PC_GCI_HEADER_SIZE, comment_title_sjis, sizeof(comment_title_sjis) - 1);
            memcpy(gci + PC_GCI_HEADER_SIZE + 32, comment_famicom, sizeof(comment_famicom) - 1);
        }
        memcpy(gci + PC_GCI_HEADER_SIZE + ofs, save, sizeof(save));
    } else {
        FAIL(PC_GCI_ERR_FORMAT, "unknown kind of file");
    }
    d.length = (uint16_t)(data_size / PC_GCI_BLOCK_SIZE);
    pc_gci_write_dentry(gci, &d);
    *out = gci;
    *out_size = PC_GCI_HEADER_SIZE + data_size;
    if (info) *info = inf;
    return PC_GCI_OK;
}
