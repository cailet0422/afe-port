/* pc_card.c - memory card API → local file I/O
 * Channel 0 (Slot A) → save/card_a/
 * Channel 1 (Slot B) → save/card_b/
 *
 * Each card file is a plain file holding the data only. The card directory
 * (file numbers, status, permissions) is kept in memory: file numbers are
 * given out when a file is first seen and stay put while the game runs, as
 * the game looks files up by number between calls. */
#include "pc_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dolphin/os/OSContext.h"
#include "dolphin/os/OSSemaphore.h"
#include "dolphin/card.h"
#include <sys/stat.h>   /* mkdir (Linux), stat */
#ifdef _WIN32
#include <direct.h>  /* _mkdir */
#include <windows.h>
#else
#include <dirent.h>
#endif

#define CARD_SECTOR_SIZE 8192
#define CARD_USER_BLOCKS 1019 /* a "Memory Card 1019" (64 Mbit) */
#define CARD_MEM_SIZE_MBIT 64

/* Seconds from 1970-01-01 to 2000-01-01, the card's epoch */
#define CARD_EPOCH_OFFSET 946684800u

/* Files on the cards are this game's own */
static const u8 card_game_name[4] = { 'G', 'A', 'E', 'J' };
static const u8 card_company[2] = { '0', '1' };

typedef struct {
    int     used;
    CARDDir dir; /* fileName, permission, banner/icon/comment fields */
} CARDEntry;

static CARDEntry card_entries[2][CARD_MAX_FILE];

typedef struct {
    CARDFileInfo* owner; /* NULL = slot free */
    FILE*         fp;
} CARDOpenSlot;

#define CARD_MAX_OPEN 8
static CARDOpenSlot card_slots[CARD_MAX_OPEN];

static CARDOpenSlot* card_slot_find(CARDFileInfo* fi) {
    int i;
    for (i = 0; i < CARD_MAX_OPEN; i++) {
        if (card_slots[i].owner == fi) return &card_slots[i];
    }
    return NULL;
}

/* A file info being reopened without a close drops its old handle */
static CARDOpenSlot* card_slot_alloc(CARDFileInfo* fi) {
    CARDOpenSlot* slot = card_slot_find(fi);
    int i;

    if (slot != NULL) {
        if (slot->fp) fclose(slot->fp);
        slot->fp = NULL;
        return slot;
    }
    for (i = 0; i < CARD_MAX_OPEN; i++) {
        if (card_slots[i].owner == NULL) {
            card_slots[i].owner = fi;
            card_slots[i].fp = NULL;
            return &card_slots[i];
        }
    }
    return NULL;
}

static void card_slot_free(CARDFileInfo* fi) {
    CARDOpenSlot* slot = card_slot_find(fi);
    if (slot) {
        if (slot->fp) fclose(slot->fp);
        slot->owner = NULL;
        slot->fp = NULL;
    }
}

/* Per-channel directory: chan 0 = card_a, chan 1 = card_b */
static const char* card_dir[2] = { "save/card_a", "save/card_b" };

static int card_chan_valid(s32 chan) {
    return chan >= 0 && chan <= 1;
}

/* Card file names are at most 32 bytes; reject path traversal */
static int card_filename_safe(const char* name) {
    if (!name || !name[0]) return 0;
    if (strlen(name) > CARD_FILENAME_MAX) return 0;
    if (strstr(name, "..")) return 0;
    if (strchr(name, '/') || strchr(name, '\\') || strchr(name, ':')) return 0;
    return 1;
}

static void card_path(char* out, size_t size, s32 chan, const char* name) {
    snprintf(out, size, "%s/%s", card_dir[chan], name);
}

static void card_entry_name(const CARDEntry* e, char* out) {
    memcpy(out, e->dir.fileName, CARD_FILENAME_MAX);
    out[CARD_FILENAME_MAX] = '\0';
}

/* Size in bytes and modification time (card epoch) of a card file */
static int card_file_stat(s32 chan, const char* name, u32* size, u32* time_out) {
    char path[512];
    struct stat st;

    card_path(path, sizeof(path), chan, name);
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    if (size) *size = (u32)st.st_size;
    if (time_out) *time_out = (u32)st.st_mtime > CARD_EPOCH_OFFSET ? (u32)st.st_mtime - CARD_EPOCH_OFFSET : 0;
    return 1;
}

static u32 card_now(void) {
    time_t t = time(NULL);
    return (u32)t > CARD_EPOCH_OFFSET ? (u32)t - CARD_EPOCH_OFFSET : 0;
}

static int card_find(s32 chan, const char* name) {
    char entry_name[CARD_FILENAME_MAX + 1];
    int i;

    for (i = 0; i < CARD_MAX_FILE; i++) {
        if (!card_entries[chan][i].used) continue;
        card_entry_name(&card_entries[chan][i], entry_name);
        if (strcmp(entry_name, name) == 0) return i;
    }
    return -1;
}

static int card_add(s32 chan, const char* name) {
    int i;

    for (i = 0; i < CARD_MAX_FILE; i++) {
        CARDEntry* e = &card_entries[chan][i];

        if (e->used) continue;
        memset(e, 0, sizeof(*e));
        e->used = 1;
        memcpy(e->dir.gameName, card_game_name, sizeof(e->dir.gameName));
        memcpy(e->dir.company, card_company, sizeof(e->dir.company));
        strncpy((char*)e->dir.fileName, name, CARD_FILENAME_MAX);
        e->dir.iconAddr = 0xFFFFFFFF;
        e->dir.commentAddr = 0xFFFFFFFF;
        e->dir.permission = CARD_ATTR_PUBLIC;
        e->dir.time = card_now();
        return i;
    }
    return -1;
}

/* Brings the directory in line with the files on disk (copied in or deleted
 * while the game runs) without renumbering the files it already knows. */
static void card_sync(s32 chan) {
    int i;

    for (i = 0; i < CARD_MAX_FILE; i++) {
        char name[CARD_FILENAME_MAX + 1];

        if (!card_entries[chan][i].used) continue;
        card_entry_name(&card_entries[chan][i], name);
        if (!card_file_stat(chan, name, NULL, NULL)) card_entries[chan][i].used = 0;
    }

#ifdef _WIN32
    {
        WIN32_FIND_DATAA fd;
        HANDLE h;
        char search[300];

        snprintf(search, sizeof(search), "%s\\*", card_dir[chan]);
        h = FindFirstFileA(search, &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (!card_filename_safe(fd.cFileName) || card_find(chan, fd.cFileName) >= 0) continue;
            card_add(chan, fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    {
        DIR* d = opendir(card_dir[chan]);
        struct dirent* ent;

        if (!d) return;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.' || !card_filename_safe(ent->d_name)) continue;
            if (card_find(chan, ent->d_name) >= 0 || !card_file_stat(chan, ent->d_name, NULL, NULL)) continue;
            card_add(chan, ent->d_name);
        }
        closedir(d);
    }
#endif
}

static void ensure_dirs(void) {
#ifdef _WIN32
    _mkdir("save");
    _mkdir("save/card_a");
    _mkdir("save/card_b");
#else
    mkdir("save", 0755);
    mkdir("save/card_a", 0755);
    mkdir("save/card_b", 0755);
#endif
}

void CARDInit(void) {
    ensure_dirs();
}

s32 CARDMount(s32 chan, void* workArea, CARDCallback detachCallback) {
    (void)workArea; (void)detachCallback;
    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    ensure_dirs();
    card_sync(chan);
    return CARD_RESULT_READY;
}

s32 CARDMountAsync(s32 chan, void* workArea, CARDCallback detachCallback, CARDCallback attachCallback) {
    s32 result = CARDMount(chan, workArea, detachCallback);
    if (attachCallback) attachCallback(chan, result);
    return result;
}

s32 CARDUnmount(s32 chan) {
    return card_chan_valid(chan) ? CARD_RESULT_READY : CARD_RESULT_NOCARD;
}

static s32 card_open_no(s32 chan, s32 fileNo, CARDFileInfo* fileInfo) {
    char name[CARD_FILENAME_MAX + 1];
    char path[512];
    CARDOpenSlot* slot;
    u32 size;

    card_entry_name(&card_entries[chan][fileNo], name);
    if (!card_file_stat(chan, name, &size, NULL)) return CARD_RESULT_NOFILE;
    slot = card_slot_alloc(fileInfo);
    if (!slot) return CARD_RESULT_LIMIT;

    card_path(path, sizeof(path), chan, name);
    slot->fp = fopen(path, "r+b");
    if (!slot->fp) {
        card_slot_free(fileInfo);
        return CARD_RESULT_IOERROR;
    }
    fileInfo->chan = chan;
    fileInfo->fileNo = fileNo;
    fileInfo->offset = 0;
    fileInfo->length = (s32)size;
    fileInfo->iBlock = 0;
    return CARD_RESULT_READY;
}

s32 CARDOpen(s32 chan, const char* fileName, CARDFileInfo* fileInfo) {
    int no;

    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    if (!card_filename_safe(fileName)) return CARD_RESULT_NOFILE;
    card_sync(chan);
    no = card_find(chan, fileName);
    if (no < 0) return CARD_RESULT_NOFILE;
    return card_open_no(chan, no, fileInfo);
}

s32 CARDFastOpen(s32 chan, s32 fileNo, CARDFileInfo* fileInfo) {
    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    if (fileNo < 0 || fileNo >= CARD_MAX_FILE) return CARD_RESULT_FATAL_ERROR;
    if (!card_entries[chan][fileNo].used) return CARD_RESULT_NOFILE;
    return card_open_no(chan, fileNo, fileInfo);
}

s32 CARDClose(CARDFileInfo* fileInfo) {
    card_slot_free(fileInfo);
    return CARD_RESULT_READY;
}

static u32 card_used_blocks(s32 chan, int* files) {
    u32 blocks = 0;
    int i;

    *files = 0;
    for (i = 0; i < CARD_MAX_FILE; i++) {
        char name[CARD_FILENAME_MAX + 1];
        u32 size;

        if (!card_entries[chan][i].used) continue;
        card_entry_name(&card_entries[chan][i], name);
        if (card_file_stat(chan, name, &size, NULL)) blocks += (size + CARD_SECTOR_SIZE - 1) / CARD_SECTOR_SIZE;
        (*files)++;
    }
    return blocks;
}

s32 CARDCreate(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo) {
    char path[512];
    CARDOpenSlot* slot;
    int files, no;
    u8* zeros;

    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    if (!card_filename_safe(fileName)) return CARD_RESULT_NAMETOOLONG;
    if (size == 0 || size % CARD_SECTOR_SIZE != 0) return CARD_RESULT_FATAL_ERROR;
    card_sync(chan);
    if (card_find(chan, fileName) >= 0) return CARD_RESULT_EXIST;
    if (card_used_blocks(chan, &files) + size / CARD_SECTOR_SIZE > CARD_USER_BLOCKS) return CARD_RESULT_INSSPACE;
    no = card_add(chan, fileName);
    if (no < 0) return CARD_RESULT_NOENT;

    slot = card_slot_alloc(fileInfo);
    if (!slot) {
        card_entries[chan][no].used = 0;
        return CARD_RESULT_LIMIT;
    }
    card_path(path, sizeof(path), chan, fileName);
    zeros = (u8*)calloc(1, size);
    slot->fp = zeros ? fopen(path, "w+b") : NULL;
    if (!slot->fp || fwrite(zeros, 1, size, slot->fp) != size || fflush(slot->fp) != 0) {
        free(zeros);
        card_slot_free(fileInfo);
        remove(path);
        card_entries[chan][no].used = 0;
        return CARD_RESULT_IOERROR;
    }
    free(zeros);

    fileInfo->chan = chan;
    fileInfo->fileNo = no;
    fileInfo->offset = 0;
    fileInfo->length = (s32)size;
    fileInfo->iBlock = 0;
    return CARD_RESULT_READY;
}

s32 CARDCreateAsync(s32 chan, const char* fileName, u32 size, CARDFileInfo* fileInfo, CARDCallback callback) {
    s32 result = CARDCreate(chan, fileName, size, fileInfo);
    if (callback) callback(chan, result);
    return result;
}

s32 CARDRead(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset) {
    CARDOpenSlot* slot = card_slot_find(fileInfo);
    if (!slot || !slot->fp) return CARD_RESULT_NOFILE;
    if (offset < 0 || length < 0 || offset + length > fileInfo->length) return CARD_RESULT_LIMIT;
    if (fseek(slot->fp, offset, SEEK_SET) != 0) return CARD_RESULT_IOERROR;
    if ((s32)fread(buf, 1, length, slot->fp) != length) return CARD_RESULT_IOERROR;
    return CARD_RESULT_READY;
}

s32 CARDReadAsync(CARDFileInfo* fileInfo, void* buf, s32 length, s32 offset, CARDCallback callback) {
    s32 result = CARDRead(fileInfo, buf, length, offset);
    if (callback) callback(fileInfo->chan, result);
    return result;
}

s32 CARDWrite(CARDFileInfo* fileInfo, const void* buf, s32 length, s32 offset) {
    CARDOpenSlot* slot = card_slot_find(fileInfo);
    if (!slot || !slot->fp) return CARD_RESULT_NOFILE;
    if (offset < 0 || length < 0 || offset + length > fileInfo->length) return CARD_RESULT_LIMIT;
    if (fseek(slot->fp, offset, SEEK_SET) != 0) return CARD_RESULT_IOERROR;
    if ((s32)fwrite(buf, 1, length, slot->fp) != length) return CARD_RESULT_IOERROR;
    if (fflush(slot->fp) != 0) return CARD_RESULT_IOERROR;
    card_entries[fileInfo->chan][fileInfo->fileNo].dir.time = card_now();
    return CARD_RESULT_READY;
}

s32 CARDWriteAsync(CARDFileInfo* fileInfo, const void* buf, s32 length, s32 offset, CARDCallback callback) {
    s32 result = CARDWrite(fileInfo, buf, length, offset);
    if (callback) callback(fileInfo->chan, result);
    return result;
}

/* Windows cannot delete or rename a file that is still open */
static void card_close_no(s32 chan, int no) {
    int i;
    for (i = 0; i < CARD_MAX_OPEN; i++) {
        CARDFileInfo* fi = card_slots[i].owner;
        if (fi != NULL && fi->chan == chan && fi->fileNo == no) card_slot_free(fi);
    }
}

s32 CARDDelete(s32 chan, const char* fileName) {
    char path[512];
    int no;

    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    if (!card_filename_safe(fileName)) return CARD_RESULT_NOFILE;
    card_sync(chan);
    no = card_find(chan, fileName);
    if (no < 0) return CARD_RESULT_NOFILE;
    card_close_no(chan, no);
    card_path(path, sizeof(path), chan, fileName);
    if (remove(path) != 0) return CARD_RESULT_IOERROR;
    card_entries[chan][no].used = 0;
    return CARD_RESULT_READY;
}

s32 CARDDeleteAsync(s32 chan, const char* fileName, CARDCallback callback) {
    s32 result = CARDDelete(chan, fileName);
    if (callback) callback(chan, result);
    return result;
}

s32 CARDFastDelete(s32 chan, s32 fileNo) {
    char name[CARD_FILENAME_MAX + 1];

    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    if (fileNo < 0 || fileNo >= CARD_MAX_FILE) return CARD_RESULT_FATAL_ERROR;
    if (!card_entries[chan][fileNo].used) return CARD_RESULT_NOFILE;
    card_entry_name(&card_entries[chan][fileNo], name);
    return CARDDelete(chan, name);
}

s32 CARDRename(s32 chan, const char* oldName, const char* newName) {
    char oldPath[512], newPath[512];
    CARDDir* dir;
    int no;

    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    if (!card_filename_safe(oldName)) return CARD_RESULT_NOFILE;
    if (!card_filename_safe(newName)) return CARD_RESULT_NAMETOOLONG;
    card_sync(chan);
    no = card_find(chan, oldName);
    if (no < 0) return CARD_RESULT_NOFILE;
    if (card_find(chan, newName) >= 0) return CARD_RESULT_EXIST;
    card_close_no(chan, no);
    card_path(oldPath, sizeof(oldPath), chan, oldName);
    card_path(newPath, sizeof(newPath), chan, newName);
    if (rename(oldPath, newPath) != 0) return CARD_RESULT_IOERROR;
    dir = &card_entries[chan][no].dir;
    memset(dir->fileName, 0, sizeof(dir->fileName));
    strncpy((char*)dir->fileName, newName, CARD_FILENAME_MAX);
    dir->time = card_now();
    return CARD_RESULT_READY;
}

s32 CARDRenameAsync(s32 chan, const char* oldName, const char* newName, CARDCallback callback) {
    s32 result = CARDRename(chan, oldName, newName);
    if (callback) callback(chan, result);
    return result;
}

s32 CARDGetResultCode(s32 chan) {
    return card_chan_valid(chan) ? CARD_RESULT_READY : CARD_RESULT_NOCARD;
}

s32 CARDFreeBlocks(s32 chan, s32* byteNotUsed, s32* filesNotUsed) {
    int files;
    u32 used;

    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    card_sync(chan);
    used = card_used_blocks(chan, &files);
    if (byteNotUsed) *byteNotUsed = used < CARD_USER_BLOCKS ? (s32)((CARD_USER_BLOCKS - used) * CARD_SECTOR_SIZE) : 0;
    if (filesNotUsed) *filesNotUsed = CARD_MAX_FILE - files;
    return CARD_RESULT_READY;
}

s32 CARDGetSectorSize(s32 chan, u32* size) {
    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    if (size) *size = CARD_SECTOR_SIZE;
    return CARD_RESULT_READY;
}

/* memSize is in megabits, as on the console */
s32 CARDProbeEx(s32 chan, s32* memSize, s32* sectorSize) {
    if (!card_chan_valid(chan)) return CARD_RESULT_NOCARD;
    if (memSize) *memSize = CARD_MEM_SIZE_MBIT;
    if (sectorSize) *sectorSize = CARD_SECTOR_SIZE;
    return CARD_RESULT_READY;
}

BOOL CARDProbe(s32 chan) { return card_chan_valid(chan); }

s32 CARDCheck(s32 chan) { return card_chan_valid(chan) ? CARD_RESULT_READY : CARD_RESULT_NOCARD; }
s32 CARDCheckAsync(s32 chan, CARDCallback callback) {
    s32 result = CARDCheck(chan);
    if (callback) callback(chan, result);
    return result;
}
s32 CARDCheckEx(s32 chan, s32* xferBytes) {
    if (xferBytes) *xferBytes = 0;
    return CARDCheck(chan);
}
s32 CARDCheckExAsync(s32 chan, s32* xferBytes, CARDCallback callback) {
    s32 result = CARDCheckEx(chan, xferBytes);
    if (callback) callback(chan, result);
    return result;
}

static CARDDir* card_dir_entry(s32 chan, s32 fileNo, s32* result) {
    if (!card_chan_valid(chan)) {
        *result = CARD_RESULT_NOCARD;
    } else if (fileNo < 0 || fileNo >= CARD_MAX_FILE) {
        *result = CARD_RESULT_FATAL_ERROR;
    } else if (!card_entries[chan][fileNo].used) {
        *result = CARD_RESULT_NOFILE;
    } else {
        *result = CARD_RESULT_READY;
        return &card_entries[chan][fileNo].dir;
    }
    return NULL;
}

/* Fills in the size fields from the file on disk */
static void card_dir_refresh(s32 chan, CARDDir* dir) {
    char name[CARD_FILENAME_MAX + 1];
    u32 size = 0;

    memcpy(name, dir->fileName, CARD_FILENAME_MAX);
    name[CARD_FILENAME_MAX] = '\0';
    card_file_stat(chan, name, &size, NULL);
    dir->length = (u16)((size + CARD_SECTOR_SIZE - 1) / CARD_SECTOR_SIZE);
}

s32 __CARDGetStatusEx(s32 chan, s32 fileNo, CARDDir* dirent) {
    s32 result;
    CARDDir* dir = card_dir_entry(chan, fileNo, &result);

    if (dir == NULL) return result;
    card_dir_refresh(chan, dir);
    *dirent = *dir;
    return CARD_RESULT_READY;
}

/* The name, size and owner stay; the rest (permission, icon ...) is taken */
s32 __CARDSetStatusEx(s32 chan, s32 fileNo, CARDDir* dirent) {
    s32 result;
    CARDDir* dir = card_dir_entry(chan, fileNo, &result);
    CARDDir keep;

    if (dir == NULL) return result;
    keep = *dir;
    *dir = *dirent;
    memcpy(dir->gameName, keep.gameName, sizeof(dir->gameName));
    memcpy(dir->company, keep.company, sizeof(dir->company));
    memcpy(dir->fileName, keep.fileName, sizeof(dir->fileName));
    dir->length = keep.length;
    dir->startBlock = keep.startBlock;
    return CARD_RESULT_READY;
}

s32 CARDGetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
    s32 result;
    CARDDir* dir = card_dir_entry(chan, fileNo, &result);
    char name[CARD_FILENAME_MAX + 1];
    u32 size = 0;

    if (dir == NULL) return result;
    memcpy(name, dir->fileName, CARD_FILENAME_MAX);
    name[CARD_FILENAME_MAX] = '\0';
    card_file_stat(chan, name, &size, NULL);

    memset(stat, 0, sizeof(*stat));
    memcpy(stat->fileName, dir->fileName, CARD_FILENAME_MAX);
    stat->length = size;
    stat->time = dir->time;
    memcpy(stat->gameName, dir->gameName, sizeof(stat->gameName));
    memcpy(stat->company, dir->company, sizeof(stat->company));
    stat->bannerFormat = dir->bannerFormat;
    stat->iconAddr = dir->iconAddr;
    stat->iconFormat = dir->iconFormat;
    stat->iconSpeed = dir->iconSpeed;
    stat->commentAddr = dir->commentAddr;
    return CARD_RESULT_READY;
}

s32 CARDSetStatus(s32 chan, s32 fileNo, CARDStat* stat) {
    s32 result;
    CARDDir* dir = card_dir_entry(chan, fileNo, &result);

    if (dir == NULL) return result;
    dir->bannerFormat = stat->bannerFormat;
    dir->iconAddr = stat->iconAddr;
    dir->iconFormat = stat->iconFormat;
    dir->iconSpeed = stat->iconSpeed;
    dir->commentAddr = stat->commentAddr;
    dir->time = card_now();
    return CARD_RESULT_READY;
}

s32 CARDSetStatusAsync(s32 chan, s32 fileNo, CARDStat* stat, CARDCallback callback) {
    s32 result = CARDSetStatus(chan, fileNo, stat);
    if (callback) callback(chan, result);
    return result;
}

s32 CARDGetAttributes(s32 chan, s32 fileNo, u8* attr) {
    s32 result;
    CARDDir* dir = card_dir_entry(chan, fileNo, &result);

    if (dir == NULL) return result;
    *attr = dir->permission;
    return CARD_RESULT_READY;
}

s32 CARDSetAttributes(s32 chan, s32 fileNo, u8 attr) {
    s32 result;
    CARDDir* dir = card_dir_entry(chan, fileNo, &result);

    if (dir == NULL) return result;
    dir->permission = attr;
    return CARD_RESULT_READY;
}

s32 CARDSetAttributesAsync(s32 chan, s32 fileNo, u8 attr, CARDCallback callback) {
    s32 result = CARDSetAttributes(chan, fileNo, attr);
    if (callback) callback(chan, result);
    return result;
}

/* The port never reports a damaged or foreign card, so the game has no
 * reason to format one; keep the files */
s32 CARDFormat(s32 chan) {
    return card_chan_valid(chan) ? CARD_RESULT_READY : CARD_RESULT_NOCARD;
}

s32 CARDFormatAsync(s32 chan, CARDCallback callback) {
    s32 result = CARDFormat(chan);
    if (callback) callback(chan, result);
    return result;
}
