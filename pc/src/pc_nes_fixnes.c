/* fixNES integration for Animal Crossing PC port.
 *
 * Based on fixNES by FIX94 (https://github.com/FIX94/fixNES)
 * MIT License. Stripped of GLUT/OpenAL, adapted as a library with
 * init/frame/render/cleanup API for the game's famicom.cpp integration.
 *
 * Produces a 256x240 RGB565 framebuffer uploaded as a GL texture.
 * Its sound replaces the game's mix callback while it runs (fixnes_sound_start). */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Include fixNES headers BEFORE any game headers to avoid BUTTON_A etc conflicts */
#include "mapper.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "mem.h"
#include "input.h"

/* Undef fixNES button defines before game headers clobber them */
#undef BUTTON_A
#undef BUTTON_B
#undef BUTTON_SELECT
#undef BUTTON_START
#undef BUTTON_UP
#undef BUTTON_DOWN
#undef BUTTON_LEFT
#undef BUTTON_RIGHT

/* Now include GL via SDL2 (avoid pc_platform.h which pulls in game types.h) */
#include <SDL.h>
#include "pc_gfx.h"
#include "fm2play.h"
#include "audio.h"

/* From pc_gx.c — restore game's GL state after NES emulation */
extern void pc_gx_restore_after_nes(void);

/* Externed directly (not via headers) to avoid fixNES symbol clashes. */
extern int g_pc_window_w;
extern int g_pc_window_h;
extern int pc_settings_get_nes_aspect(void);

/* ======================================================================
 * Global variables required by fixNES modules (normally in main.c)
 * ====================================================================== */

/* Framebuffer — fixNES PPU writes pixels here */
uint16_t textureImage[VISIBLE_DOTS * VISIBLE_LINES];

/* Global state flags */
bool nesPAL = false;
bool nesPause = false;
bool ppuDebugPauseFrame = false;
bool doOverscan = false;
bool nesEmuNSFPlayback = false;
uint8_t emuInitialNT = NT_UNKNOWN;
bool emuSaveEnabled = false;
bool emuFdsHasSideB = false;
bool emuSkipFrame = false;

/* Cycle timers */
uint32_t cpuCycleTimer;
uint32_t vrc7CycleTimer;

/* ROM data pointers (fixNES modules reference these) */
uint8_t *emuNesROM = NULL;
uint32_t emuNesROMsize = 0;
uint8_t *emuPrgRAM = NULL;
uint32_t emuPrgRAMsize = 0;

/* Mapper special-case flags (defined in mapper .c files, referenced here) */
extern bool m30_flashable;
extern bool m30_singlescreen;
extern bool m32_singlescreen;
extern bool m78_m78a;
extern bool ppuMapper5;

/* Input state — written by set_input, read by fixNES input.c */
extern uint8_t inValReads[8];


/* ======================================================================
 * Drawing: the frame as a texture, through pc_gfx's UI drawing
 * ====================================================================== */
static PCTex fixnes_texture = 0;
static int fixnes_initialized = 0;

/* ======================================================================
 * Sound: like the original's EmuSound (jaudio_NES emusound_hvc), the NES
 * replaces the game's mix callback (the N64-style music) while it runs. The
 * emulator makes a frame of APU output per game frame on the game thread; the
 * sound thread takes it at the output rate through a ring.
 * ====================================================================== */
#define FIXNES_OUTPUT_RATE 32000 /* pc_audio.c */
#define FIXNES_RING 8192 /* stereo frames, a power of two */
#define FIXNES_RING_PRIME 1120 /* frames ahead before playing (2 sound frames of 560) */
#define FIXNES_RING_KEEP 2240 /* most frames kept ahead */
#define FIXNES_MIX_MAX 2048

typedef int16_t* (*FixnesMixCallback)(int32_t n_samples);
extern FixnesMixCallback Jac_GetMixcallback(uint8_t* mixmode);
extern void Jac_RegisterMixcallback(FixnesMixCallback mixcallback, uint8_t mixmode);
extern int OSDisableInterrupts(void);
extern int OSRestoreInterrupts(int level);
#define FIXNES_MIXMODE_INTERLEAVE 3 /* MixMode_Interleave */

static int16_t fixnes_ring[FIXNES_RING * 2];
static SDL_atomic_t fixnes_ring_write; /* game thread */
static SDL_atomic_t fixnes_ring_read;  /* sound thread */
static uint32_t fixnes_resample_pos; /* 16.16 position in the APU's samples */
static int16_t fixnes_last[2]; /* the APU sample before this frame's */
static int fixnes_primed = 0; /* sound thread */
static int16_t fixnes_held[2]; /* the last sample played */
static int fixnes_mix_registered = 0;
static FixnesMixCallback fixnes_old_mixcall = NULL;
static uint8_t fixnes_old_mixmode = 0;

static void fixnes_capture_audio(void) {
    uint32_t buf_size = apuGetBufSize();
    int16_t* src = (int16_t*)apuGetBuf();
    uint32_t n = buf_size / (2 * sizeof(int16_t)); /* stereo frames */
    /* a NES frame (60.1 Hz) becomes a 60th of a second, as the game paces it */
    uint32_t step = (uint32_t)((((uint64_t)n << 16) * 60) / FIXNES_OUTPUT_RATE);
    uint32_t wp, rp;

    if (src == NULL || n == 0) return;
    wp = (uint32_t)SDL_AtomicGet(&fixnes_ring_write);
    rp = (uint32_t)SDL_AtomicGet(&fixnes_ring_read);
    /* linear interpolation from the APU rate (about 224 kHz) to the output
     * rate; position 0 is the last sample of the previous frame */
    while ((fixnes_resample_pos >> 16) < n) {
        uint32_t i = fixnes_resample_pos >> 16;
        int32_t frac = (int32_t)(fixnes_resample_pos & 0xFFFF);
        const int16_t* a = i == 0 ? fixnes_last : &src[(i - 1) * 2];
        const int16_t* b = &src[i * 2];

        if (wp - rp < FIXNES_RING) {
            for (int c = 0; c < 2; c++) {
                fixnes_ring[(wp & (FIXNES_RING - 1)) * 2 + c] =
                    (int16_t)(a[c] + (((b[c] - a[c]) * (frac >> 1)) >> 15));
            }
            wp++;
        }
        fixnes_resample_pos += step;
    }
    fixnes_resample_pos -= n << 16;
    fixnes_last[0] = src[(n - 1) * 2];
    fixnes_last[1] = src[(n - 1) * 2 + 1];
    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&fixnes_ring_write, (int)wp);
}

/* the sound thread's mix callback (Jac_VframeWork): n stereo frames */
static int16_t* fixnes_mix_callback(int32_t n) {
    static int16_t out[FIXNES_MIX_MAX * 2];
    uint32_t wp = (uint32_t)SDL_AtomicGet(&fixnes_ring_write);
    uint32_t rp, avail;
    int32_t i;

    SDL_MemoryBarrierAcquire();
    rp = (uint32_t)SDL_AtomicGet(&fixnes_ring_read);
    if (n > FIXNES_MIX_MAX) n = FIXNES_MIX_MAX;
    avail = wp - rp;
    if (avail > FIXNES_RING_KEEP + (uint32_t)n) { /* fell behind: drop the oldest */
        rp = wp - FIXNES_RING_KEEP - (uint32_t)n;
        avail = FIXNES_RING_KEEP + (uint32_t)n;
    }
    /* The game makes a 60th of a second per frame and the sound thread takes
     * 560 frames at a time: keep some ahead so the two never meet */
    if (!fixnes_primed && avail < FIXNES_RING_PRIME) {
        avail = 0;
    } else {
        fixnes_primed = 1;
    }
    for (i = 0; i < n && (uint32_t)i < avail; i++, rp++) {
        fixnes_held[0] = fixnes_ring[(rp & (FIXNES_RING - 1)) * 2 + 0];
        fixnes_held[1] = fixnes_ring[(rp & (FIXNES_RING - 1)) * 2 + 1];
        out[i * 2 + 0] = fixnes_held[0];
        out[i * 2 + 1] = fixnes_held[1];
    }
    if (i < n) {
        fixnes_primed = 0; /* ran dry: wait for some again */
    }
    for (; i < n; i++) {
        out[i * 2 + 0] = fixnes_held[0];
        out[i * 2 + 1] = fixnes_held[1];
    }
    SDL_AtomicSet(&fixnes_ring_read, (int)rp);
    return out;
}

static void fixnes_sound_start(void) {
    int level;

    SDL_AtomicSet(&fixnes_ring_write, 0);
    SDL_AtomicSet(&fixnes_ring_read, 0);
    fixnes_resample_pos = 0;
    fixnes_last[0] = fixnes_last[1] = 0;
    fixnes_primed = 0;
    fixnes_held[0] = fixnes_held[1] = 0;
    level = OSDisableInterrupts(); /* the sound thread reads the pair per frame */
    if (!fixnes_mix_registered) {
        fixnes_old_mixcall = Jac_GetMixcallback(&fixnes_old_mixmode);
        fixnes_mix_registered = 1;
    }
    Jac_RegisterMixcallback(fixnes_mix_callback, FIXNES_MIXMODE_INTERLEAVE);
    OSRestoreInterrupts(level);
}

static void fixnes_sound_exit(void) {
    int level;

    if (!fixnes_mix_registered) return;
    level = OSDisableInterrupts();
    Jac_RegisterMixcallback(fixnes_old_mixcall, fixnes_old_mixmode);
    fixnes_mix_registered = 0;
    OSRestoreInterrupts(level);
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void pc_fixnes_init(uint8_t *ines_data, int ines_size) {
    /* Reset all state */
    nesPAL = false;
    nesPause = false;
    nesEmuNSFPlayback = false;
    emuSaveEnabled = false;
    emuFdsHasSideB = false;
    emuSkipFrame = false;
    m30_flashable = false;
    m30_singlescreen = false;
    m32_singlescreen = false;
    m78_m78a = false;
    ppuMapper5 = false;
    emuInitialNT = NT_UNKNOWN;
    fixnes_initialized = 0;

    /* Free any previous allocation */
    if (emuPrgRAM) {
        free(emuPrgRAM);
        emuPrgRAM = NULL;
    }

    /* Store ROM pointer */
    emuNesROM = ines_data;
    emuNesROMsize = (uint32_t)ines_size;

    /* Parse iNES header */
    if (ines_size < 16)
        return;

    uint8_t mapper = ((emuNesROM[6] & 0xF0) >> 4) | ((emuNesROM[7] & 0xF0));
    emuSaveEnabled = (emuNesROM[6] & (1<<1)) != 0;
    bool trainer = (emuNesROM[6] & (1<<2)) != 0;
    uint32_t ROMsize = emuNesROMsize - 16;
    uint32_t prgROMsize = emuNesROM[4] * 0x4000;
    if (prgROMsize > ROMsize)
        prgROMsize = ROMsize;
    ROMsize -= prgROMsize;
    uint32_t chrROMsize = emuNesROM[5] * 0x2000;
    if (prgROMsize == 0) {
        prgROMsize = ROMsize;
        if (chrROMsize)
            chrROMsize = 0;
    } else if (chrROMsize > ROMsize) {
        chrROMsize = ROMsize;
    }

    /* Allocate PRG RAM */
    if (mapper == 5)
        emuPrgRAMsize = 0x10000;
    else {
        emuPrgRAMsize = emuNesROM[8] * 0x2000;
        if (emuPrgRAMsize == 0) emuPrgRAMsize = 0x2000;
    }
    emuPrgRAM = malloc(emuPrgRAMsize);
    memset(emuPrgRAM, 0, emuPrgRAMsize);

    uint8_t *prgROM = emuNesROM + 16;
    if (trainer) prgROM += 512;

    uint8_t *chrROM = NULL;
    if (chrROMsize) {
        chrROM = emuNesROM + 16 + prgROMsize;
        if (trainer) chrROM += 512;
    }

    /* Initialize fixNES subsystems */
    apuInitBufs();
    cpuInit();
    ppuInit();
    memInit();
    apuInit();
    inputInit();

    /* Set nametable mirroring */
    if (emuNesROM[6] & 8) {
        emuInitialNT = NT_4SCREEN;
        ppuSetNameTbl4Screen();
    } else if (emuNesROM[6] & 1) {
        emuInitialNT = NT_VERTICAL;
        ppuSetNameTblVertical();
    } else {
        emuInitialNT = NT_HORIZONTAL;
        ppuSetNameTblHorizontal();
    }

    if (mapper == 5)
        ppuMapper5 = true;

    /* Initialize mapper */
    if (!mapperInit(mapper, prgROM, prgROMsize, emuPrgRAM, emuPrgRAMsize, chrROM, chrROMsize))
        return;

    cpuCycleTimer = nesPAL ? 16 : 12;
    vrc7CycleTimer = 432 / cpuCycleTimer;
    fixnes_initialized = 1;

    memset(textureImage, 0, sizeof(textureImage));
    fixnes_sound_start();
}

void pc_fixnes_write_wram(unsigned int ofs, const uint8_t *data, unsigned int size) {
    /* Targeted write to fixNES internal RAM — used by update_highscore_raw
     * to inject saved high scores into the running NES game. */
    uint8_t *mem = memGetMainMem();
    if (mem && data && ofs + size <= 0x800)
        memcpy(mem + ofs, data, size);
}

void pc_fixnes_sync_wram(uint8_t *dst_wram) {
    /* Copy fixNES internal 2KB RAM → ksNes sp->wram so
     * nesinfo_update_highscore can detect high-score changes. */
    uint8_t *src = memGetMainMem();
    if (src && dst_wram)
        memcpy(dst_wram, src, 0x800);
}

void pc_fixnes_set_input(uint8_t buttons) {
    /* buttons: bit0=A, bit1=B, bit2=Select, bit3=Start,
     *          bit4=Up, bit5=Down, bit6=Left, bit7=Right
     * fixNES inValReads[]: index 0=A, 1=B, 2=Select, ... 7=Right */
    for (int i = 0; i < 8; i++)
        inValReads[i] = (buttons >> i) & 1;
}

uint16_t *pc_fixnes_frame(void) {
    if (!fixnes_initialized) return textureImage;

    /* Run one full NES frame: cycle CPU/PPU/APU until ppuDrawDone() */
    while (1) {
        if (!cpuCycle())
            break; /* CPU halted */
        ppuCycle();
        apuCycle();
        mapperCycle();
        if (ppuDrawDone()) {
            /* Frame complete — capture audio BEFORE apuUpdate resets curBufPos */
            fixnes_capture_audio();
            apuUpdate();
            break;
        }
    }

    return textureImage;
}

void pc_fixnes_cleanup(void) {
    /* The game's music again */
    fixnes_sound_exit();

    /* Free APU output buffers */
    apuDeinitBufs();

    /* Free PRG RAM */
    if (emuPrgRAM) {
        free(emuPrgRAM);
        emuPrgRAM = NULL;
    }

    /* Don't free emuNesROM — it's owned by the famicom system, not us. famicom_cleanup frees this, this is NOT a memory leak. */
    emuNesROM = NULL;
    emuNesROMsize = 0;

    fixnes_initialized = 0;

    /* Restore game's GL state — NES emulator used its own shader/VAO */
    pc_gx_restore_after_nes();
}

void pc_fixnes_render_frame(uint16_t *fb) {
    /* fixNES outputs RGB565 with COL_TEX_BSWAP (red in the low bits).
     * Skip top 8 rows (often garbage), show 224 lines. */
    if (!fixnes_texture) fixnes_texture = pc_gfx_tex_new();
    pc_gfx_tex_upload(fixnes_texture, 256, 224, PC_TEXF_RGB565, fb + 256 * 8, 256 * 224 * 2, PC_FILTER_NEAREST);

    /* 0 = stretch to window, 1 = centered 4:3 with pillar/letterbox. */
    int win_w = g_pc_window_w;
    int win_h = g_pc_window_h;
    int vp_w, vp_h, vp_x, vp_y;
    if (pc_settings_get_nes_aspect() == 0) {
        vp_w = win_w; vp_h = win_h; vp_x = 0; vp_y = 0;
    } else if (win_w * 3 > win_h * 4) {
        vp_h = win_h;
        vp_w = (win_h * 4) / 3;
        vp_x = (win_w - vp_w) / 2;
        vp_y = 0;
    } else {
        vp_w = win_w;
        vp_h = (win_w * 3) / 4;
        vp_x = 0;
        vp_y = (win_h - vp_h) / 2;
    }

    /* Clear full window first so the bars are black, not stale pixels. */
    {
        static const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        PCGfxUIVertex q[6];
        const float x[6] = { 0, 1, 1, 0, 1, 0 }, y[6] = { 1, 1, 0, 1, 0, 0 };

        pc_gfx_scissor_test(0);
        pc_gfx_viewport(0, 0, win_w, win_h);
        pc_gfx_clear(1, 0, black, 1.0f);

        pc_gfx_viewport(vp_x, vp_y, vp_w, vp_h);
        pc_gfx_depth_test(0);
        pc_gfx_blend(0);
        memset(q, 0, sizeof(q));
        for (int i = 0; i < 6; i++) { /* the viewport's corners, the texture upright */
            q[i].x = x[i] * vp_w;
            q[i].y = y[i] * vp_h;
            q[i].u = x[i];
            q[i].v = y[i];
            memset(q[i].color, 255, 4);
        }
        pc_gfx_draw_ui(fixnes_texture, q, 6, NULL, 0, (float)vp_w, (float)vp_h);
    }

    pc_gfx_viewport(0, 0, win_w, win_h);
}
