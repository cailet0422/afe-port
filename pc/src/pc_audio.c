/* pc_audio.c - SDL2 audio backend with dedicated producer thread.
 *
 * Architecture (matches GC):
 *   Game thread:  Na_GameFrame() queues commands via message queues
 *   Audio thread: pc_audio_process_frame() produces samples into ring buffer
 *   SDL callback: reads ring buffer → speakers
 *
 * The audio thread decouples sample production from the game frame,
 * so OS preemption of the game thread doesn't cause audio dropouts.
 */
#include "pc_platform.h"
#include "pc_settings.h"
#include "jaudio_NES/audiothread.h"

#define PC_AUDIO_SAMPLE_RATE 32000

/* lock-free SPSC ring buffer (producer=audio thread, consumer=SDL callback) */
#define RING_BUF_SAMPLES (32768) /* ~512ms at 32kHz stereo */
#define RING_BUF_MASK    (RING_BUF_SAMPLES - 1)

/* Produce more samples when buffer drops below this level.
 * ~4 audio frames ahead = ~70ms of buffer at 32kHz stereo. */
#define AUDIO_PRODUCE_THRESHOLD 4480

/* --audio-dump: everything the sound system outputs, as a WAV file (testing) */
static FILE* audio_dump_file = NULL;
static u32 audio_dump_bytes = 0;
static u32 audio_dump_header_at = 0;

static void audio_dump_put32(u8* p, u32 v) {
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

/* the RIFF header, with the sizes written so far: the file stays readable if
 * the game never gets to pc_audio_shutdown */
static void audio_dump_header(void) {
    u8 h[44];

    memcpy(h, "RIFF", 4);
    audio_dump_put32(h + 4, 36 + audio_dump_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    audio_dump_put32(h + 16, 16);
    h[20] = 1; h[21] = 0; /* PCM */
    h[22] = 2; h[23] = 0; /* stereo */
    audio_dump_put32(h + 24, PC_AUDIO_SAMPLE_RATE);
    audio_dump_put32(h + 28, PC_AUDIO_SAMPLE_RATE * 4);
    h[32] = 4; h[33] = 0; /* bytes per frame */
    h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    audio_dump_put32(h + 40, audio_dump_bytes);
    fseek(audio_dump_file, 0, SEEK_SET);
    fwrite(h, 1, sizeof(h), audio_dump_file);
    fseek(audio_dump_file, 0, SEEK_END);
    fflush(audio_dump_file);
    audio_dump_header_at = audio_dump_bytes;
}

int pc_audio_dump_open(const char* path) {
    audio_dump_file = fopen(path, "wb");
    if (audio_dump_file == NULL) {
        fprintf(stderr, "[AUDIO] cannot write %s\n", path);
        return 0;
    }
    audio_dump_bytes = 0;
    audio_dump_header();
    return 1;
}

/* samples as the game made them (before the volume setting), little-endian */
static void audio_dump_write(const s16* src, u32 n_samples) {
    u8 buf[2048];
    u32 i = 0;

    while (i < n_samples) {
        u32 n = 0;

        for (; i < n_samples && n < sizeof(buf); i++, n += 2) {
            buf[n] = (u8)src[i];
            buf[n + 1] = (u8)((u16)src[i] >> 8);
        }
        fwrite(buf, 1, n, audio_dump_file);
        audio_dump_bytes += n;
    }
    if (audio_dump_bytes - audio_dump_header_at >= PC_AUDIO_SAMPLE_RATE * 4) {
        audio_dump_header(); /* once a second */
    }
}

static void audio_dump_close(void) {
    if (audio_dump_file == NULL) return;
    audio_dump_header();
    fclose(audio_dump_file);
    audio_dump_file = NULL;
}

static s16 ring_buffer[RING_BUF_SAMPLES];
static SDL_atomic_t ring_write_pos; /* written by audio producer thread */
static SDL_atomic_t ring_read_pos;  /* written by SDL audio callback */
static SDL_AudioDeviceID audio_device = 0;

typedef void (*AIDMACallback)(void);
static AIDMACallback ai_dma_callback = NULL;
static u32 ai_dsp_sample_rate = PC_AUDIO_SAMPLE_RATE;

/* --- Audio producer thread --- */
static SDL_Thread* audio_producer_thread = NULL;
static SDL_atomic_t audio_thread_running;

static int pc_audio_producer_func(void* data) {
    (void)data;
    while (SDL_AtomicGet(&audio_thread_running)) {
        int fill = pc_audio_get_buffer_fill();
        if (fill < AUDIO_PRODUCE_THRESHOLD) {
            /* a frame of sound is an interrupt on the GameCube: it never
             * runs while the game has interrupts off (pc_os.c) */
            BOOL level = OSDisableInterrupts();

            pc_audio_process_frame();
            OSRestoreInterrupts(level);
        } else {
            SDL_Delay(1);
        }
    }
    return 0;
}

/* --deterministic: the game makes as much sound per retrace as plays in a
 * 60th of a second, on the game's thread, so nothing that waits on the sound
 * system depends on how fast the frames go by */
static int audio_frame_driven = 0;
static u32 audio_produced = 0; /* samples, both channels */

void pc_audio_frame(void) {
    static u64 due = 0;

    if (!audio_frame_driven) return;
    due += PC_AUDIO_SAMPLE_RATE * 2; /* samples due, times 60 */
    while ((u64)audio_produced * 60 < due) {
        u32 before = audio_produced;

        pc_audio_process_frame();
        if (audio_produced == before) break;
    }
}

/* msleep: with --deterministic, the sound of that much time instead of a wait */
int pc_audio_sleep(u32 ms) {
    u32 frames = ms * 60 / 1000;

    if (!audio_frame_driven) return 0;
    while (frames-- > 0) {
        pc_audio_frame();
    }
    return 1;
}

void pc_audio_start_producer_thread(void) {
    if (g_pc_deterministic) {
        audio_frame_driven = 1;
        return;
    }
    if (audio_producer_thread) return;
    SDL_AtomicSet(&audio_thread_running, 1);
    audio_producer_thread = SDL_CreateThread(pc_audio_producer_func, "AudioProducer", NULL);
    if (audio_producer_thread) {
        printf("[AUDIO] Producer thread started\n");
    } else {
        printf("[AUDIO] Failed to create producer thread: %s\n", SDL_GetError());
    }
}

/* --- SDL audio callback (runs on SDL's audio device thread) --- */
static void pc_audio_callback(void* userdata, Uint8* stream, int len) {
    s16* out = (s16*)stream;
    int total_samples = len / sizeof(s16);
    u32 wp = (u32)SDL_AtomicGet(&ring_write_pos);
    SDL_MemoryBarrierAcquire();
    u32 rp = (u32)SDL_AtomicGet(&ring_read_pos);
    u32 used = wp - rp;

    /* overrun: producer lapped us */
    if (used > RING_BUF_SAMPLES) {
        rp = wp - RING_BUF_SAMPLES;
        rp &= ~1u; /* stereo-align */
        used = wp - rp;
    }

    int avail = (int)used;
    avail &= ~1; /* whole stereo frames only */
    int copy = (avail < total_samples) ? avail : total_samples;
    copy &= ~1;

    for (int i = 0; i < copy; i++) {
        out[i] = ring_buffer[(rp + i) & RING_BUF_MASK];
    }
    if (copy < total_samples) {
        memset(&out[copy], 0, (total_samples - copy) * sizeof(s16));
    }

    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&ring_read_pos, (int)(rp + copy));
}

/* --- AI (Audio Interface) --- */

void AIInit(u8* stack) {
    (void)stack;
    if (audio_device != 0) return;

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = PC_AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = pc_audio_callback;

    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device != 0) {
        printf("[AUDIO] Opened: freq=%d fmt=0x%04X ch=%d samples=%d (requested: freq=%d)\n",
               have.freq, have.format, have.channels, have.samples, want.freq);
    } else {
        printf("[AUDIO] Failed to open: %s\n", SDL_GetError());
        /* The game waits for the sound system to run, so keep consuming
         * samples in real time without output (no device, e.g. no speakers) */
        if (SDL_AudioInit("dummy") == 0) {
            audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        }
        printf("[AUDIO] %s\n", audio_device != 0 ? "Running without sound" : SDL_GetError());
    }
}

void AIInitDMA(uintptr_t addr, u32 size) {
    s16* src = (s16*)addr;
    u32 n_samples = size / sizeof(s16);
    n_samples &= ~1u; /* whole stereo frames */
    audio_produced += n_samples;
    if (audio_dump_file != NULL) {
        audio_dump_write(src, n_samples);
    }

    u32 wp = (u32)SDL_AtomicGet(&ring_write_pos);
    u32 rp = (u32)SDL_AtomicGet(&ring_read_pos);
    SDL_MemoryBarrierAcquire();
    u32 used = wp - rp;
    u32 free = RING_BUF_SAMPLES - used;

    if (n_samples > free) {
        n_samples = free & ~1u;
    }

    int vol = g_pc_settings.master_volume;
    if (vol < 0)   vol = 0;
    if (vol > 100) vol = 100;

    for (u32 i = 0; i < n_samples; i++) {
        int s = ((int)src[i] * vol) / 100;
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        ring_buffer[(wp + i) & RING_BUF_MASK] = (s16)s;
    }

    SDL_MemoryBarrierRelease();
    SDL_AtomicSet(&ring_write_pos, (int)(wp + n_samples));
}

void AIStartDMA(void) {
    if (audio_device != 0) SDL_PauseAudioDevice(audio_device, 0);
}

void AIStopDMA(void) {
    if (audio_device != 0) SDL_PauseAudioDevice(audio_device, 1);
}

void pc_audio_set_paused(int paused) {
    /* Currently unused */
    if (audio_device != 0) SDL_PauseAudioDevice(audio_device, paused ? 1 : 0);
}

u32  AIGetDMAStartAddr(void) { return 0; }
u16  AIGetDMALength(void) { return 0; }
u32  AIGetStreamTrigger(void) { return 0; }
u32  AIGetStreamSampleCount(void) { return 0; }
void AISetStreamPlayState(u32 state) { (void)state; }
u32  AIGetStreamPlayState(void) { return 0; }
void AISetStreamSampleRate(u32 rate) { (void)rate; }
u32  AIGetStreamSampleRate(void) { return PC_AUDIO_SAMPLE_RATE; }
void AISetStreamVolLeft(u8 vol) { (void)vol; }
void AISetStreamVolRight(u8 vol) { (void)vol; }
u8   AIGetStreamVolLeft(void) { return 0; }
u8   AIGetStreamVolRight(void) { return 0; }
void AIResetStreamSampleCount(void) {}
void AISetDSPSampleRate(u32 rate) { ai_dsp_sample_rate = rate; }
u32  AIGetDSPSampleRate(void) { return ai_dsp_sample_rate; }

void* AIRegisterDMACallback(void* callback) {
    void* old = (void*)ai_dma_callback;
    ai_dma_callback = (AIDMACallback)callback;
    return old;
}

/* --- DSP stubs (rspsim does everything in software) --- */

void DSPInit(void) {}
BOOL DSPCheckMailToDSP(void) { return FALSE; }
BOOL DSPCheckMailFromDSP(void) { return FALSE; }
u32  DSPReadMailFromDSP(void) { return 0; }
void DSPSendMailToDSP(u32 mail) { (void)mail; }
void DSPAssertInt(void) {}
void* DSPAddTask(void* task) { return task; }

/* --- ring buffer queries for frame pacing (pc_vi.c) --- */

int pc_audio_get_buffer_fill(void) {
    return SDL_AtomicGet(&ring_write_pos) - SDL_AtomicGet(&ring_read_pos);
}

int pc_audio_is_active(void) {
    return audio_device != 0;
}

void pc_audio_shutdown(void) {
    /* Stop producer thread first */
    SDL_AtomicSet(&audio_thread_running, 0);
    pc_os_interrupts_release();
    if (audio_producer_thread) {
        SDL_WaitThread(audio_producer_thread, NULL);
        audio_producer_thread = NULL;
    }
    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
    audio_dump_close();
}
