/* pc_rel_overlay.c - load REL data objects from the disc at boot
 *
 * The executable carries the REL data that the build extracted. At boot this
 * replaces it with the data of the disc being played (see
 * pc/tools/gen_rel_overlay.py for the table format). Pointer fields, found
 * through the REL's relocations, keep their native values. */
#include "pc_platform.h"
#include "pc_disc.h"
#include "pc_ko.h"
#include "pc_bswap.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#define RELMAP_PATH "DobutsuNoMoriEPlus.relmap"

enum { KIND_BYTES, KIND_U16, KIND_U32, KIND_VTX };

/* Address reference for the table (its link-time address is in the header) */
u32 pc_rel_overlay_anchor = 0x52454C4F;

typedef struct {
    const char* file;
    int overlay_all; /* data modules; foresta only gets byte data in Korean mode */
} RelModule;

static const RelModule rel_modules[] = {
    { NULL, 0 },
    { "foresta.rel.szs", 0 },
    { "forestd.rel.szs", 1 },
    { "foresti.rel.szs", 1 },
    { "foresto.rel.szs", 1 },
};
#define REL_MODULE_COUNT ((int)(sizeof(rel_modules) / sizeof(rel_modules[0])))
#define REL_MAX_SECTIONS 32

typedef struct {
    u8* rel;
    u32 size;
    u32 num_sections;
    u32 sec_offset[REL_MAX_SECTIONS];
    u32 sec_size[REL_MAX_SECTIONS];
    u8* relocated[REL_MAX_SECTIONS]; /* one flag byte per section byte */
} LoadedRel;

static u32 rd32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static u16 rd16(const u8* p) {
    return (u16)((p[0] << 8) | p[1]);
}

static void mark(LoadedRel* r, u32 sec, u32 off, u32 n) {
    if (sec < r->num_sections && sec < REL_MAX_SECTIONS && r->relocated[sec] != NULL && off + n <= r->sec_size[sec]) {
        memset(r->relocated[sec] + off, 1, n);
    }
}

/* Records which bytes of each section are patched by relocations. */
static void scan_relocations(LoadedRel* r) {
    u32 imp_offset = rd32(r->rel + 0x28);
    u32 imp_size = rd32(r->rel + 0x2C);
    u32 i;

    for (i = 0; i + 8 <= imp_size; i += 8) {
        u32 p = rd32(r->rel + imp_offset + i + 4);
        u32 sec = 0, off = 0;

        while (p + 8 <= r->size) {
            u16 delta = rd16(r->rel + p);
            u8 type = r->rel[p + 2];
            u8 section = r->rel[p + 3];

            p += 8;
            if (type == 203) { /* R_DOLPHIN_END */
                break;
            }
            if (type == 202) { /* R_DOLPHIN_SECTION */
                sec = section;
                off = 0;
                continue;
            }
            off += delta;
            switch (type) {
                case 201: /* R_DOLPHIN_NOP */
                    break;
                case 1:   /* R_PPC_ADDR32 */
                case 2:   /* R_PPC_ADDR24 */
                case 10:  /* R_PPC_REL24 */
                    mark(r, sec, off, 4);
                    break;
                default:  /* 16-bit address halves */
                    mark(r, sec, off, 2);
                    break;
            }
        }
    }
}

static int load_rel(LoadedRel* r, const char* file) {
    u32 sec_table, i;

    memset(r, 0, sizeof(*r));
    r->rel = pc_disc_load_file(file, &r->size);
    if (r->rel == NULL || r->size < 0x40) {
        return 0;
    }
    r->num_sections = rd32(r->rel + 0x0C);
    sec_table = rd32(r->rel + 0x10);
    if (r->num_sections > REL_MAX_SECTIONS || sec_table + r->num_sections * 8 > r->size) {
        return 0;
    }
    for (i = 0; i < r->num_sections; i++) {
        r->sec_offset[i] = rd32(r->rel + sec_table + i * 8) & ~1u;
        r->sec_size[i] = rd32(r->rel + sec_table + i * 8 + 4);
        if (r->sec_offset[i] != 0 && r->sec_offset[i] + r->sec_size[i] <= r->size) {
            r->relocated[i] = (u8*)calloc(r->sec_size[i] ? r->sec_size[i] : 1, 1);
        }
    }
    scan_relocations(r);
    return 1;
}

static void free_rel(LoadedRel* r) {
    u32 i;

    for (i = 0; i < REL_MAX_SECTIONS; i++) {
        free(r->relocated[i]);
    }
    free(r->rel);
}

/* Const data lives in read-only pages */
static void make_writable(u8* dst, u32 size) {
#ifdef _WIN32
    DWORD old;
    VirtualProtect(dst, size, PAGE_READWRITE, &old);
#else
    uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t start = (uintptr_t)dst & ~(page - 1);
    uintptr_t end = ((uintptr_t)dst + size + page - 1) & ~(page - 1);
    mprotect((void*)start, end - start, PROT_READ | PROT_WRITE);
#endif
}

/* Converts one big-endian element to native order. */
static void convert(u8 kind, const u8* src, u8* out, u32 elem) {
    u32 j;

    switch (kind) {
        case KIND_U16: {
            u16 v = rd16(src);
            memcpy(out, &v, 2);
            break;
        }
        case KIND_U32: {
            u32 v = rd32(src);
            memcpy(out, &v, 4);
            break;
        }
        case KIND_VTX:
            /* s16 ob[3], u16 flag, s16 tc[2], u8 cn[4] */
            for (j = 0; j < 12; j += 2) {
                u16 v = rd16(src + j);
                memcpy(out + j, &v, 2);
            }
            memcpy(out + 12, src + 12, 4);
            break;
        default:
            memcpy(out, src, elem);
            break;
    }
}

/* Copies one object element by element, skipping elements that contain a
 * relocated pointer, and only writes elements that differ. */
static void apply(const LoadedRel* r, u8 sec, u8 kind, u32 off, u32 size, u8* dst) {
    const u8* src = r->rel + r->sec_offset[sec] + off;
    const u8* reloc = r->relocated[sec] + off;
    u32 elem = kind == KIND_U16 ? 2 : (kind == KIND_U32 ? 4 : (kind == KIND_VTX ? 16 : 1));
    int writable = 0;
    u32 i, j;

    for (i = 0; i + elem <= size; i += elem) {
        u8 native[16];
        int skip = 0;

        for (j = 0; j < elem; j++) {
            skip |= reloc[i + j];
        }
        if (skip) {
            continue;
        }
        convert(kind, src + i, native, elem);
        if (memcmp(dst + i, native, elem) == 0) {
            continue;
        }
        if (!writable) {
            if (getenv("PC_RELOVL_DEBUG") != NULL) {
                printf("[RELOVL] sec %u off %#x size %#x kind %u dst %p differs at +%#x\n", sec, off, size, kind,
                       (void*)dst, i);
            }
            make_writable(dst, size);
            writable = 1;
        }
        memcpy(dst + i, native, elem);
    }
}

void pc_rel_overlay_apply(void) {
    FILE* f = fopen(RELMAP_PATH, "rb");
    u8 header[16];
    u32 count, anchor_vma, n, applied = 0;
    uintptr_t slide;
    int module;
    u8* table;

    if (f == NULL) {
        printf("[PC] %s not found; using the data built into the executable\n", RELMAP_PATH);
        return;
    }
    if (fread(header, 1, sizeof(header), f) != sizeof(header) || memcmp(header, "RELO", 4) != 0) {
        fclose(f);
        return;
    }
    memcpy(&count, header + 8, 4);
    memcpy(&anchor_vma, header + 12, 4);
    table = (u8*)malloc((size_t)count * 16);
    if (table == NULL || fread(table, 16, count, f) != count) {
        free(table);
        fclose(f);
        return;
    }
    fclose(f);
    slide = (uintptr_t)&pc_rel_overlay_anchor - anchor_vma;

    for (module = 1; module < REL_MODULE_COUNT; module++) {
        LoadedRel rel;

        if (!rel_modules[module].overlay_all && !g_pc_ko_mode) {
            continue;
        }
        if (!load_rel(&rel, rel_modules[module].file)) {
            free_rel(&rel);
            continue;
        }
        for (n = 0; n < count; n++) {
            const u8* e = table + n * 16;
            u32 off, size, vma;

            if (e[0] != module) {
                continue;
            }
            if (!rel_modules[module].overlay_all && e[2] != KIND_BYTES && e[2] != KIND_U16) {
                continue;
            }
            memcpy(&off, e + 4, 4);
            memcpy(&size, e + 8, 4);
            memcpy(&vma, e + 12, 4);
            if (e[1] >= rel.num_sections || rel.relocated[e[1]] == NULL || off + size > rel.sec_size[e[1]]) {
                continue;
            }
            apply(&rel, e[1], e[2], off, size, (u8*)(uintptr_t)(vma + slide));
            applied++;
        }
        free_rel(&rel);
    }
    free(table);
    printf("[PC] loaded %u data objects from the disc\n", applied);
}
