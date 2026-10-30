/* Unit tests for the memory card layer (pc/src/pc_card.c).
 *
 * Walks through the calls m_card.c makes for the first save (a temporary
 * file is created, its permission set, written, given its status and
 * renamed into place) and for loading, in a scratch folder. */
#include "pc_types.h"
#include "dolphin/os/OSContext.h"
#include "dolphin/os/OSSemaphore.h"
#include "dolphin/card.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* dolphin/private/card.h can't be included next to dolphin/card.h */
s32 __CARDGetStatusEx(s32 chan, s32 fileNo, CARDDir* dirent);
s32 __CARDSetStatusEx(s32 chan, s32 fileNo, CARDDir* dirent);

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                \
        }                                                              \
    } while (0)

#define LAND "DobutsunomoriE_MURA"
#define LAND_DUMMY "DobutsunomoriE_MURA_d"
#define LAND_SIZE (57 * 8192)

static u8 data[LAND_SIZE];
static u8 back[LAND_SIZE];

/* fileNo of the file with this name, found the way the game looks for its
 * passports: by going through every status */
static int scan_for(s32 chan, const char* name) {
    CARDStat stat;
    int i, found = -1;

    for (i = 0; i < CARD_MAX_FILE; i++) {
        if (CARDGetStatus(chan, i, &stat) == CARD_RESULT_READY && strcmp(stat.fileName, name) == 0) {
            CHECK(stat.company[0] == '0' && stat.company[1] == '1');
            CHECK(memcmp(stat.gameName, "GAEJ", 4) == 0);
            found = i;
        }
    }
    return found;
}

static int count_files(s32 chan) {
    CARDFileInfo fi;
    int i, n = 0;

    for (i = 0; i < CARD_MAX_FILE; i++) {
        if (CARDFastOpen(chan, i, &fi) == CARD_RESULT_READY) {
            n++;
            CARDClose(&fi);
        }
    }
    return n;
}

static void first_save(void) {
    CARDFileInfo fi;
    CARDDir dir;
    CARDStat stat;
    s32 free_bytes, free_files, free_bytes2, free_files2, mem, sector;
    s32 no;
    int i;

    CHECK(CARDProbeEx(0, &mem, &sector) == CARD_RESULT_READY && sector == 8192);
    CHECK(CARDMount(0, NULL, NULL) == CARD_RESULT_READY);
    CHECK(CARDCheck(0) == CARD_RESULT_READY);
    CHECK(CARDOpen(0, LAND, &fi) == CARD_RESULT_NOFILE); /* no town yet */
    CHECK(CARDDelete(0, LAND_DUMMY) == CARD_RESULT_NOFILE);
    CHECK(CARDFreeBlocks(0, &free_bytes, &free_files) == CARD_RESULT_READY);

    /* create the temporary file; mark it not to be copied */
    CHECK(CARDCreate(0, LAND_DUMMY, LAND_SIZE, &fi) == CARD_RESULT_READY);
    no = fi.fileNo;
    CHECK(no >= 0 && no < CARD_MAX_FILE);
    CHECK(fi.length == LAND_SIZE);
    CHECK(__CARDGetStatusEx(0, no, &dir) == CARD_RESULT_READY);
    CHECK(dir.length == LAND_SIZE / 8192);
    dir.permission |= CARD_ATTR_NO_MOVE | CARD_ATTR_NO_COPY;
    CHECK(__CARDSetStatusEx(0, no, &dir) == CARD_RESULT_READY);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);
    CHECK(CARDCreate(0, LAND_DUMMY, LAND_SIZE, &fi) == CARD_RESULT_EXIST);

    /* reopen by name: same number, permission kept */
    CHECK(CARDOpen(0, LAND_DUMMY, &fi) == CARD_RESULT_READY);
    CHECK(fi.fileNo == no);
    CHECK(__CARDGetStatusEx(0, no, &dir) == CARD_RESULT_READY);
    CHECK((dir.permission & (CARD_ATTR_NO_MOVE | CARD_ATTR_NO_COPY)) == (CARD_ATTR_NO_MOVE | CARD_ATTR_NO_COPY));

    /* write it a sector at a time */
    for (i = 0; i < LAND_SIZE; i++) {
        data[i] = (u8)(i * 7 + (i >> 13));
    }
    for (i = 0; i < LAND_SIZE; i += 8192) {
        CHECK(CARDWrite(&fi, data + i, 8192, i) == CARD_RESULT_READY);
    }
    CHECK(CARDWrite(&fi, data, 8192, LAND_SIZE) == CARD_RESULT_LIMIT); /* past the end */
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);

    /* icon and comment */
    CHECK(CARDOpen(0, LAND_DUMMY, &fi) == CARD_RESULT_READY);
    CHECK(CARDGetStatus(0, fi.fileNo, &stat) == CARD_RESULT_READY);
    CHECK(stat.length == LAND_SIZE);
    stat.commentAddr = 0;
    stat.iconAddr = CARD_COMMENT_SIZE;
    CARDSetIconFormat(&stat, 0, CARD_STAT_ICON_C8);
    CARDSetBannerFormat(&stat, CARD_STAT_BANNER_C8);
    CHECK(CARDSetStatus(0, fi.fileNo, &stat) == CARD_RESULT_READY);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);

    /* into place */
    CHECK(CARDRename(0, LAND_DUMMY, LAND) == CARD_RESULT_READY);
    CHECK(CARDOpen(0, LAND_DUMMY, &fi) == CARD_RESULT_NOFILE);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);

    CHECK(CARDFreeBlocks(0, &free_bytes2, &free_files2) == CARD_RESULT_READY);
    CHECK(free_bytes2 == free_bytes - LAND_SIZE);
    CHECK(free_files2 == free_files - 1);

    /* the status moved with the file */
    CHECK(scan_for(0, LAND) == no);
    CHECK(CARDGetStatus(0, no, &stat) == CARD_RESULT_READY);
    CHECK(stat.iconAddr == CARD_COMMENT_SIZE && CARDGetIconFormat(&stat, 0) == CARD_STAT_ICON_C8);
}

static void load(void) {
    CARDFileInfo fi;

    CHECK(CARDMount(0, NULL, NULL) == CARD_RESULT_READY);
    CHECK(CARDOpen(0, LAND, &fi) == CARD_RESULT_READY);
    CHECK(fi.length == LAND_SIZE);
    memset(back, 0, sizeof(back));
    CHECK(CARDRead(&fi, back, LAND_SIZE, 0) == CARD_RESULT_READY);
    CHECK(memcmp(back, data, LAND_SIZE) == 0);
    CHECK(CARDRead(&fi, back, 512, LAND_SIZE) == CARD_RESULT_LIMIT);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);
    CHECK(CARDUnmount(0) == CARD_RESULT_READY);
}

static void names(void) {
    CARDFileInfo fi;

    CHECK(CARDCreate(0, "a_name_that_is_longer_than_32_bytes", 8192, &fi) == CARD_RESULT_NAMETOOLONG);
    CHECK(CARDCreate(0, "../escape", 8192, &fi) == CARD_RESULT_NAMETOOLONG);
    CHECK(CARDCreate(0, "odd_size", 100, &fi) == CARD_RESULT_FATAL_ERROR);

    /* renaming onto an existing file is refused, as on the console */
    CHECK(CARDCreate(0, "other", 8192, &fi) == CARD_RESULT_READY);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);
    CHECK(CARDRename(0, "other", LAND) == CARD_RESULT_EXIST);
    CHECK(CARDRename(0, "missing", "x") == CARD_RESULT_NOFILE);
    CHECK(count_files(0) == 2);
    CHECK(CARDDelete(0, "other") == CARD_RESULT_READY);
    CHECK(CARDDelete(0, "other") == CARD_RESULT_NOFILE);
    CHECK(count_files(0) == 1);
    CHECK(count_files(1) == 0); /* slot B is a separate card */
    CHECK(CARDMount(2, NULL, NULL) == CARD_RESULT_NOCARD);
}

/* Files copied into the folder (another save, a backup) show up at the next
 * mount, and ones removed from it go away, as when the game starts again */
static void outside_changes(void) {
    FILE* f;
    CARDFileInfo fi;
    s32 no;

    f = fopen("save/card_b/" LAND, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fwrite(data, 1, LAND_SIZE, f);
        fclose(f);
    }
    CHECK(CARDMount(1, NULL, NULL) == CARD_RESULT_READY);
    CHECK(CARDOpen(1, LAND, &fi) == CARD_RESULT_READY);
    no = fi.fileNo;
    CHECK(CARDRead(&fi, back, 8192, 8192) == CARD_RESULT_READY);
    CHECK(memcmp(back, data + 8192, 8192) == 0);
    CHECK(CARDClose(&fi) == CARD_RESULT_READY);

    remove("save/card_b/" LAND);
    CHECK(CARDMount(1, NULL, NULL) == CARD_RESULT_READY);
    CHECK(CARDFastOpen(1, no, &fi) == CARD_RESULT_NOFILE);
    CHECK(count_files(1) == 0);
}

int main(void) {
    char dir[] = "/tmp/test_pc_card.XXXXXX";

    if (mkdtemp(dir) == NULL || chdir(dir) != 0) {
        printf("FAIL: no scratch folder\n");
        return 1;
    }
    CARDInit();
    first_save();
    load();
    names();
    outside_changes();

    remove("save/card_a/" LAND);
    rmdir("save/card_a");
    rmdir("save/card_b");
    rmdir("save");
    if (chdir("/") != 0 || rmdir(dir) != 0) {
        printf("could not remove %s\n", dir);
    }
    if (failures == 0) {
        printf("all pc_card tests passed\n");
    }
    return failures != 0;
}
