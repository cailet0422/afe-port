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

/* ---------------------------------------------------------------------------
 * 64-bit builds: the tables as host structures.
 *
 * Wave_Test and Bank_Test turn the tables' 32-bit offsets into pointers in
 * place, which a 64-bit pointer does not fit. After the swap above, these
 * build a copy of every object in the host layout of jaudio_NES/bx.h with
 * real pointers (several offsets may name one object: one copy each); the
 * tests then find every pointer already above the blob's address and leave
 * it. Fields that are not pointers are copied byte for byte, so the copy
 * reads as the 32-bit build reads the blob in place. Envelope tables (s16
 * steps) and the voices' unknown objects stay in the blob.
 * ------------------------------------------------------------------------ */
#if UINTPTR_MAX > 0xFFFFFFFFu
#include "jaudio_NES/bx.h"

typedef struct {
    u32 at;
    void* host;
} HostCopy;

static HostCopy host_copies[VISITED_MAX];
static int host_copy_count;

static void* host_copied(u32 at) {
    for (int i = 0; i < host_copy_count; i++) {
        if (host_copies[i].at == at) return host_copies[i].host;
    }
    return NULL;
}

/* a new host object for the blob's object at `at` (zeroed, size bytes) */
static void* host_new(u32 at, size_t size) {
    void* p = calloc(1, size);

    if (p != NULL && host_copy_count < VISITED_MAX) {
        host_copies[host_copy_count].at = at;
        host_copies[host_copy_count].host = p;
        host_copy_count++;
    }
    return p;
}

static void copy_bytes(void* dst, u32 at, size_t n) {
    if (at + n <= blob_size) memcpy(dst, blob + at, n);
}

static int valid(u32 at) {
    return at != 0 && at < blob_size;
}

/* a pointer into the blob itself (envelopes, unknown objects) */
static void* in_blob(u32 at) {
    return valid(at) ? blob + at : NULL;
}

/* objects without pointers: a byte copy */
static void* host_plain(u32 at, size_t size) {
    void* p;

    if (!valid(at)) return NULL;
    if ((p = host_copied(at)) != NULL) return p;
    p = host_new(at, size);
    if (p != NULL) copy_bytes(p, at, size);
    return p;
}

static Osc_* host_osc(u32 at) {
    Osc_* o;

    if (!valid(at)) return NULL;
    if ((o = (Osc_*)host_copied(at)) != NULL) return o;
    o = (Osc_*)host_new(at, sizeof(Osc_));
    if (o == NULL) return NULL;
    copy_bytes(&o->mode, at + 0x00, 1);
    copy_bytes(&o->rate, at + 0x04, 4);
    o->attackVecOffset = (s16*)in_blob(rd32(at + 0x08));
    o->releaseVecOffset = (s16*)in_blob(rd32(at + 0x0C));
    copy_bytes(&o->width, at + 0x10, 4);
    copy_bytes(&o->vertex, at + 0x14, 4);
    return o;
}

static InstKeymap_* host_inst_keymap(u32 at) {
    InstKeymap_* k;
    u32 n;

    if (!valid(at)) return NULL;
    if ((k = (InstKeymap_*)host_copied(at)) != NULL) return k;
    n = rd32(at + 4);
    if (n > 0x80) n = 0x80;
    k = (InstKeymap_*)host_new(at, sizeof(InstKeymap_) + (n > 2 ? n - 2 : 0) * sizeof(Vmap_*));
    if (k == NULL) return NULL;
    copy_bytes(&k->mBaseKey, at, 1);
    k->mVelocityCount = n;
    for (u32 i = 0; i < n; i++) {
        k->mVelocities[i] = (Vmap_*)host_plain(rd32(at + 8 + i * 4), sizeof(Vmap_));
    }
    return k;
}

static Inst_* host_inst(u32 at) {
    Inst_* in;
    u32 n;

    if (!valid(at)) return NULL;
    if ((in = (Inst_*)host_copied(at)) != NULL) return in;
    n = rd32(at + 0x28);
    if (n > 0x80) n = 0x80;
    in = (Inst_*)host_new(at, sizeof(Inst_) + (n > 5 ? n - 5 : 0) * sizeof(InstKeymap_*));
    if (in == NULL) return NULL;
    copy_bytes(in, at, 0x10); /* magic, flag, frequency and gain */
    for (int j = 0; j < 2; j++) {
        in->mOscillators[j] = host_osc(rd32(at + 0x10 + j * 4));
        in->mEffects[j] = (Rand_*)host_plain(rd32(at + 0x18 + j * 4), sizeof(Rand_));
        in->mSensors[j] = (Sense_*)host_plain(rd32(at + 0x20 + j * 4), sizeof(Sense_));
    }
    in->mKeyRegionCount = (int)n;
    for (u32 j = 0; j < n; j++) {
        in->mKeyRegions[j] = host_inst_keymap(rd32(at + 0x2C + j * 4));
    }
    return in;
}

static Voice_* host_voice(u32 at) {
    Voice_* v;
    u32 n;

    if (!valid(at)) return NULL;
    if ((v = (Voice_*)host_copied(at)) != NULL) return v;
    n = rd32(at + 8);
    if (n > 0x80) n = 0x80;
    v = (Voice_*)host_new(at, sizeof(Voice_) + (n > 1 ? n - 1 : 0) * sizeof(void*));
    if (v == NULL) return NULL;
    copy_bytes(v->_00, at, 8);
    v->size = (int)n;
    for (u32 j = 0; j < n; j++) {
        v->_0C[j] = in_blob(rd32(at + 0xC + j * 4));
    }
    return v;
}

static PercKeymap_* host_perc_keymap(u32 at) {
    PercKeymap_* k;
    u32 n;

    if (!valid(at)) return NULL;
    if ((k = (PercKeymap_*)host_copied(at)) != NULL) return k;
    n = rd32(at + 0x10);
    if (n > 0x80) n = 0x80;
    k = (PercKeymap_*)host_new(at, sizeof(PercKeymap_) + (n > 2 ? n - 2 : 0) * sizeof(Vmap_*));
    if (k == NULL) return NULL;
    copy_bytes(k, at, 8); /* pitch, volume */
    k->_08 = host_plain(rd32(at + 0x08), sizeof(Rand_));
    k->_0C = host_plain(rd32(at + 0x0C), sizeof(Rand_));
    k->mVelocityCount = (int)n;
    for (u32 i = 0; i < n; i++) {
        k->mVelocities[i] = (Vmap_*)host_plain(rd32(at + 0x14 + i * 4), sizeof(Vmap_));
    }
    return k;
}

static Perc_* host_perc(u32 at) {
    Perc_* p;

    if (!valid(at)) return NULL;
    if ((p = (Perc_*)host_copied(at)) != NULL) return p;
    p = (Perc_*)host_new(at, sizeof(Perc_));
    if (p == NULL) return NULL;
    copy_bytes(p, at, 0x88); /* magic and the unknown bytes */
    for (int j = 0; j < 128; j++) {
        p->mKeyRegions[j] = host_perc_keymap(rd32(at + 0x88 + j * 4));
    }
    copy_bytes(p->_288, at + 0x288, sizeof(p->_288));
    copy_bytes(p->_308, at + 0x308, sizeof(p->_308));
    return p;
}

void* pc_jaudio_host_ibnk(void* data, u32 size) {
    /* Bank_Test finds the bank 0x20 bytes in and Bank_Regist reads the id at
     * 8: the header's 0x20 bytes as they are, the host Bank_ after them */
    u8* h;
    Bank_* bank;

    begin(data, size);
    host_copy_count = 0;
    h = (u8*)calloc(1, 0x20 + sizeof(Bank_));
    if (h == NULL) return data;
    copy_bytes(h, 0, 0x20);
    bank = (Bank_*)(h + 0x20);
    copy_bytes(&bank->mMagic, 0x20, 4);
    for (u32 i = 0; i < BANK_INST_COUNT; i++) {
        u32 obj = rd32(0x24 + i * 4);

        if (i < BANK_TEST_INST_COUNT) {
            bank->mInstruments[i] = host_inst(obj);
        } else if (i < BANK_TEST_PERC_OFFSET) {
            bank->mVoices[i] = host_voice(obj);
        } else {
            bank->mPercs[i] = host_perc(obj);
        }
    }
    return h;
}

static Wave_* host_wave(u32 at) {
    Wave_* w;

    if (!valid(at)) return NULL;
    if ((w = (Wave_*)host_copied(at)) != NULL) return w;
    w = (Wave_*)host_new(at, sizeof(Wave_));
    if (w == NULL) return NULL;
    copy_bytes(w, at, 0x24); /* everything before the load status pointer */
    w->fileLoadStatus = (u32*)(uintptr_t)rd32(at + 0x24); /* as in the blob: never followed */
    return w;
}

static WaveArchive_* host_wave_archive(u32 at) {
    WaveArchive_* a;
    u32 n;

    if (!valid(at)) return NULL;
    if ((a = (WaveArchive_*)host_copied(at)) != NULL) return a;
    n = rd32(at + 0x70);
    if (n > 0x1000) n = 0x1000;
    a = (WaveArchive_*)host_new(at, sizeof(WaveArchive_) + (n > 1 ? n - 1 : 0) * sizeof(Wave_*));
    if (a == NULL) return NULL;
    copy_bytes(a->filePath, at, sizeof(a->filePath));
    /* the heap is the game's (Jac_InitHeap) */
    a->fileLoadStatus = rd32(at + 0x6C);
    a->waveCount = (int)n;
    for (u32 j = 0; j < n; j++) {
        a->waves[j] = host_wave(rd32(at + 0x74 + j * 4));
    }
    return a;
}

static WaveID_* host_wave_id(u32 at) {
    WaveID_* w;

    if (!valid(at)) return NULL;
    if ((w = (WaveID_*)host_copied(at)) != NULL) return w;
    w = (WaveID_*)host_new(at, sizeof(WaveID_));
    if (w == NULL) return NULL;
    w->id = rd32(at + 0x00);
    w->loadStatus = rd32(at + 0x30);
    w->data = (Wave_*)(uintptr_t)rd32(at + 0x34); /* as in the blob: set when loaded */
    return w;
}

static Ctrl_* host_ctrl(u32 at) {
    Ctrl_* c;
    u32 n;

    if (!valid(at)) return NULL;
    if ((c = (Ctrl_*)host_copied(at)) != NULL) return c;
    n = rd32(at + 4);
    if (n > 0x1000) n = 0x1000;
    c = (Ctrl_*)host_new(at, sizeof(Ctrl_) + (n > 1 ? n - 1 : 0) * sizeof(WaveID_*));
    if (c == NULL) return NULL;
    c->magic = (int)rd32(at);
    c->count = (int)n;
    for (u32 j = 0; j < n; j++) {
        c->waveIDs[j] = host_wave_id(rd32(at + 8 + j * 4));
    }
    return c;
}

static SCNE_* host_scene(u32 at) {
    SCNE_* s;

    if (!valid(at)) return NULL;
    if ((s = (SCNE_*)host_copied(at)) != NULL) return s;
    s = (SCNE_*)host_new(at, sizeof(SCNE_));
    if (s == NULL) return NULL;
    copy_bytes(s, at, 0x0C); /* magic, _04, _08 */
    s->cdf = host_ctrl(rd32(at + 0x0C));
    s->cex = host_ctrl(rd32(at + 0x10));
    s->cst = host_ctrl(rd32(at + 0x14));
    copy_bytes(s->_18, at + 0x18, sizeof(s->_18));
    return s;
}

void* pc_jaudio_host_wsys(void* data, u32 size) {
    Wsys_* h;
    u32 winf, wbct;

    begin(data, size);
    host_copy_count = 0;
    h = (Wsys_*)calloc(1, sizeof(Wsys_));
    if (h == NULL) return data;
    copy_bytes(h, 0, 0x10); /* magic, size, global id, _0C */
    winf = rd32(0x10);
    wbct = rd32(0x14);
    if (valid(winf)) {
        u32 n = rd32(winf + 4);
        WaveArchiveBank_* b;

        if (n > 0x100) n = 0x100;
        b = (WaveArchiveBank_*)calloc(1, sizeof(WaveArchiveBank_) + (n > 1 ? n - 1 : 0) * sizeof(WaveArchive_*));
        if (b != NULL) {
            b->magic = (int)rd32(winf);
            b->count = (int)n;
            for (u32 i = 0; i < n; i++) {
                b->waveGroups[i] = host_wave_archive(rd32(winf + 8 + i * 4));
            }
        }
        h->waveArcBank = b;
    }
    if (valid(wbct)) {
        u32 n = rd32(wbct + 8);
        CtrlGroup_* g;

        if (n > 0x100) n = 0x100;
        g = (CtrlGroup_*)calloc(1, sizeof(CtrlGroup_) + (n > 1 ? n - 1 : 0) * sizeof(SCNE_*));
        if (g != NULL) {
            g->magic = (int)rd32(wbct);
            g->_04 = rd32(wbct + 4);
            g->count = (int)n;
            for (u32 i = 0; i < n; i++) {
                g->scenes[i] = host_scene(rd32(wbct + 0xC + i * 4));
            }
        }
        h->ctrlGroup = g;
    }
    return h;
}
#else
void* pc_jaudio_host_ibnk(void* data, u32 size) {
    (void)size;
    return data;
}

void* pc_jaudio_host_wsys(void* data, u32 size) {
    (void)size;
    return data;
}
#endif
