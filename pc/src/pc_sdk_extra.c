/* pc_sdk_extra.c - Dolphin SDK functions used by e+ that the ACGC port layer
 * does not provide. Most hardware here does not exist on PC (SD card EXI
 * device, GBA link, stream audio), so these report "not present". */
#include "pc_platform.h"
#include "pc_diag.h"

#include "dolphin/os/OSContext.h"
#include "dolphin/os/OSSemaphore.h"
#include "dolphin/card.h"

/* --- OS ------------------------------------------------------------------ */

/* The game runs single-threaded on PC, so a semaphore is just a counter.
 * Waiting on an empty one would deadlock; report it and carry on. */
void OSInitSemaphore(OSSemaphore* sem, s32 count) {
    memset(sem, 0, sizeof(*sem));
    sem->count = count;
}

s32 OSWaitSemaphore(OSSemaphore* sem) {
    s32 count = sem->count;

    if (count > 0) {
        sem->count--;
    } else {
        PC_DIAG(1, "OSWaitSemaphore: would block (count=%d)\n", (int)count);
    }
    return count;
}

s32 OSTryWaitSemaphore(OSSemaphore* sem) {
    s32 count = sem->count;

    if (count > 0) {
        sem->count--;
    }
    return count;
}

s32 OSSignalSemaphore(OSSemaphore* sem) {
    return sem->count++;
}

s32 OSGetSemaphoreCount(OSSemaphore* sem) {
    return sem->count;
}

static OSContext pc_current_context;

OSContext* OSGetCurrentContext(void) {
    return &pc_current_context;
}

u32 OSGetConsoleSimulatedMemSize(void) {
    return 24 * 1024 * 1024;
}

u8 OSGetLanguage(void) {
    return 0; /* Japanese/English: unused by the JP game */
}

u32 OSGetEuRgb60Mode(void) {
    return 0;
}

OSTime __OSStartTime = 0;
u32 __OSFpscrEnableBits = 0;

/* MSL's FLT_MAX bit pattern */
unsigned long __float_max[] = { 0x7F7FFFFF };

/* --- VI / GX ------------------------------------------------------------- */

static u8 pc_dummy_xfb[640 * 480 * 2];

void* VIGetCurrentFrameBuffer(void) {
    return pc_dummy_xfb;
}

void* VIGetNextFrameBuffer(void) {
    return pc_dummy_xfb;
}

void GXPeekARGB(u16 x, u16 y, u32* color) {
    (void)x;
    (void)y;
    *color = 0;
}

/* --- SI / AI ------------------------------------------------------------- */

#define SI_GC_CONTROLLER 0x09000000
#define SI_ERROR_NO_RESPONSE 0x0008

u32 SIProbe(s32 chan) {
    return chan == 0 ? SI_GC_CONTROLLER : SI_ERROR_NO_RESPONSE;
}

void AIReset(void) {
}

BOOL AICheckInit(void) {
    return TRUE;
}

/* --- CARD ---------------------------------------------------------------- */

s32 __CARDGetStatusEx(s32 chan, s32 fileNo, CARDDir* dirent) {
    (void)chan;
    (void)fileNo;
    memset(dirent, 0, sizeof(*dirent));
    return CARD_RESULT_NOFILE;
}

s32 __CARDSetStatusEx(s32 chan, s32 fileNo, CARDDir* dirent) {
    (void)chan;
    (void)fileNo;
    (void)dirent;
    return CARD_RESULT_READY;
}

/* --- EXI SD card / GBA (not present) ------------------------------------- */

BOOL EXISelectSD(long chan, unsigned long dev, unsigned long freq) {
    (void)chan;
    (void)dev;
    (void)freq;
    return FALSE;
}

BOOL EXIImmEx(s32 channel, void* buffer, s32 length, u32 type) {
    (void)channel;
    (void)buffer;
    (void)length;
    (void)type;
    return FALSE;
}

#define GBA_NOT_READY 1

s32 GBAJoyBoot(s32 chan, s32 palette_color, s32 palette_speed, u8* programp, s32 length, u8* status) {
    (void)chan;
    (void)palette_color;
    (void)palette_speed;
    (void)programp;
    (void)length;
    (void)status;
    return GBA_NOT_READY;
}

/* --- Libultra ------------------------------------------------------------ */

BOOL osIsTerminatedThread(OSThread* t) {
    (void)t;
    return FALSE;
}

/* --- ACGC model viewer (not ported) -------------------------------------- */

int g_pc_model_viewer = 0;
int g_pc_model_viewer_start = 0;
int g_pc_model_viewer_no_cull = 0;
