/* pc_jaudio_bswap.c - JAudio's wave and instrument tables in the host's byte order.
 *
 * The boot jingle's wave system (WSYS, NIN_WAVE) and instrument bank (IBNK,
 * NIN_BANK) are big-endian blobs in the game code that Wave_Test and Bank_Test
 * read in place as structures (jaudio_NES/bx.h) after turning their offsets
 * into pointers. This walks them the same way, offsets still relative to the
 * blob's start, and swaps every field wider than a byte once; envelope tables
 * (three s16 per step) end at a jump, hold or stop step. */
#include "pc_platform.h"

#define BANK_SLOTS 0xF0  /* BANK_INST_COUNT */
#define BANK_INSTS 0x80  /* BANK_TEST_INST_COUNT, then voices */
#define BANK_PERCS 0xE4  /* BANK_TEST_PERC_OFFSET, 12 of them */
#define VISITED_MAX 512

static u8* blob;
static u32 blob_size;
static u32 visited[VISITED_MAX];
static int visited_count;

static void sw32(u32 at) {
    if (at + 4 <= blob_size) {
        u8* p = blob + at;
        u32 v = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];

        memcpy(p, &v, 4);
    }
}

static void sw16(u32 at) {
    if (at + 2 <= blob_size) {
        u8* p = blob + at;
        u16 v = (u16)((p[0] << 8) | p[1]);

        memcpy(p, &v, 2);
    }
}

static void sw32_run(u32 at, int n) {
    for (int i = 0; i < n; i++) {
        sw32(at + i * 4);
    }
}

/* a field already swapped */
static u32 rd32(u32 at) {
    u32 v = 0;

    if (at + 4 <= blob_size) memcpy(&v, blob + at, 4);
    return v;
}

/* the first visit to an object (several may point at one) */
static int first_visit(u32 at) {
    if (at == 0 || at >= blob_size) return 0;
    for (int i = 0; i < visited_count; i++) {
        if (visited[i] == at) return 0;
    }
    if (visited_count < VISITED_MAX) visited[visited_count++] = at;
    return 1;
}

static void begin(void* data, u32 size) {
    blob = (u8*)data;
    blob_size = size;
    visited_count = 0;
}

static void swap_ctrl(u32 c) { /* C-DF, C-EX, C-ST: wave ids */
    if (!first_visit(c)) return;
    sw32_run(c, 2);
    for (u32 j = 0, n = rd32(c + 4); j < n && j < 0x1000; j++) {
        u32 id;

        sw32(c + 8 + j * 4);
        id = rd32(c + 8 + j * 4);
        if (first_visit(id)) {
            sw32(id + 0x00); /* id */
            sw32(id + 0x30); /* loadStatus */
            sw32(id + 0x34); /* data */
        }
    }
}

void pc_jaudio_bswap_wsys(void* data, u32 size) {
    u32 winf, wbct;

    begin(data, size);
    sw32_run(0, 6); /* magic, size, global id, _0C, WINF, WBCT */
    winf = rd32(0x10);
    wbct = rd32(0x14);
    if (first_visit(winf)) {
        sw32_run(winf, 2);
        for (u32 i = 0, n = rd32(winf + 4); i < n && i < 0x100; i++) {
            u32 arc;

            sw32(winf + 8 + i * 4);
            arc = rd32(winf + 8 + i * 4);
            if (!first_visit(arc)) continue;
            sw32_run(arc + 0x6C, 2); /* load status, wave count (the heap is the game's) */
            for (u32 j = 0, m = rd32(arc + 0x70); j < m && j < 0x1000; j++) {
                u32 wave;

                sw32(arc + 0x74 + j * 4);
                wave = rd32(arc + 0x74 + j * 4);
                if (first_visit(wave)) {
                    sw32_run(wave + 0x04, 7); /* rate, offset, length, loop flag, start, end, count */
                    sw16(wave + 0x20);       /* loop history */
                    sw16(wave + 0x22);
                    sw32(wave + 0x24);
                }
            }
        }
    }
    if (first_visit(wbct)) {
        sw32_run(wbct, 3);
        for (u32 i = 0, n = rd32(wbct + 8); i < n && i < 0x100; i++) {
            u32 scne;

            sw32(wbct + 0xC + i * 4);
            scne = rd32(wbct + 0xC + i * 4);
            if (!first_visit(scne)) continue;
            sw32_run(scne, 6); /* magic, _04, _08, C-DF, C-EX, C-ST */
            swap_ctrl(rd32(scne + 0x0C));
            swap_ctrl(rd32(scne + 0x10));
            swap_ctrl(rd32(scne + 0x14));
        }
    }
}

static void swap_envelope(u32 at) {
    if (!first_visit(at)) return;
    for (int step = 0; step < 256 && at + 6 <= blob_size; step++, at += 6) {
        u16 mode;

        sw16(at);
        sw16(at + 2);
        sw16(at + 4);
        memcpy(&mode, blob + at, 2);
        if (mode == 0xD || mode == 0xE || mode == 0xF) break; /* jump, hold, stop */
    }
}

static void swap_vmap(u32 v) { /* velocity, wave id, volume, pitch */
    if (first_visit(v)) sw32_run(v + 4, 3);
}

static void swap_pair(u32 at) { /* an effect or a sensor: two floats after the bytes */
    if (first_visit(at)) sw32_run(at + 4, 2);
}

void pc_jaudio_bswap_ibnk(void* data, u32 size) {
    begin(data, size);
    sw32_run(0, 5); /* magic, _04, bank id, _0C, wave archive bank */
    sw32(0x20);     /* 'BANK' */
    for (u32 i = 0; i < BANK_SLOTS; i++) {
        u32 obj;

        sw32(0x24 + i * 4);
        obj = rd32(0x24 + i * 4);
        if (!first_visit(obj)) continue;
        if (i < BANK_INSTS) {
            sw32_run(obj, 11); /* magic ... key region count */
            for (u32 j = 0; j < 2; j++) {
                u32 osc = rd32(obj + 0x10 + j * 4);

                if (first_visit(osc)) {
                    sw32_run(osc + 4, 5); /* rate, attack, release, width, vertex */
                    swap_envelope(rd32(osc + 8));
                    swap_envelope(rd32(osc + 0xC));
                }
                swap_pair(rd32(obj + 0x18 + j * 4));
                swap_pair(rd32(obj + 0x20 + j * 4));
            }
            for (u32 j = 0, n = rd32(obj + 0x28); j < n && j < 0x80; j++) {
                u32 key;

                sw32(obj + 0x2C + j * 4);
                key = rd32(obj + 0x2C + j * 4);
                if (!first_visit(key)) continue;
                sw32(key + 4);
                for (u32 k = 0, m = rd32(key + 4); k < m && k < 0x80; k++) {
                    sw32(key + 8 + k * 4);
                    swap_vmap(rd32(key + 8 + k * 4));
                }
            }
        } else if (i < BANK_PERCS) {
            sw32(obj + 8); /* a voice: a count of pointers */
            for (u32 j = 0, n = rd32(obj + 8); j < n && j < 0x80; j++) {
                sw32(obj + 0xC + j * 4);
            }
        } else {
            sw32(obj); /* 'PER2' */
            for (u32 j = 0; j < 128; j++) {
                u32 key;

                sw32(obj + 0x88 + j * 4);
                sw16(obj + 0x308 + j * 2);
                key = rd32(obj + 0x88 + j * 4);
                if (!first_visit(key)) continue;
                sw32_run(key, 5); /* pitch, volume, two effects, velocity count */
                swap_pair(rd32(key + 8));
                swap_pair(rd32(key + 0xC));
                for (u32 k = 0, m = rd32(key + 0x10); k < m && k < 0x80; k++) {
                    sw32(key + 0x14 + k * 4);
                    swap_vmap(rd32(key + 0x14 + k * 4));
                }
            }
        }
    }
}
