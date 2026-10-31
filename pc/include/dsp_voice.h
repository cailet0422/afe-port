/* dsp_voice.h - one voice of the GameCube's JAudio DSP program, in software.
 *
 * The boot jingle plays through JAudio, whose sounds the GameCube's DSP mixes:
 * each voice reads a wave from ARAM (AFC ADPCM in 9- or 5-byte blocks of 16
 * samples, or 8/16-bit PCM), loops it or ends, and resamples it with a 4-tap
 * filter to the output rate. The game keeps the voices' parameters in
 * DSPchannel_ (jaudio_NES/dspinterface.h); pc_dsp.c reads them and uses this to
 * make the samples. No game data or headers here (pc/tests/test_dsp_voice.c). */
#ifndef DSP_VOICE_H
#define DSP_VOICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DSP_VOICE_AFC9,  /* 16 samples in 9 bytes: header, then 4-bit codes */
    DSP_VOICE_AFC5,  /* 16 samples in 5 bytes: header, then 2-bit codes */
    DSP_VOICE_PCM8,
    DSP_VOICE_PCM16, /* big-endian */
};

#define DSP_VOICE_NO_BLOCK 0xFFFFFFFFu

typedef struct DspWave {
    const uint8_t* data; /* the wave's first byte */
    uint32_t data_size;  /* bytes that may be read from data */
    int format;          /* DSP_VOICE_* */
    uint32_t end;        /* samples: the loop's end, or the whole length */
    int looping;
    uint32_t loop_start; /* sample */
    int16_t loop_yn1;    /* AFC history to decode the loop's block with */
    int16_t loop_yn2;
} DspWave;

typedef struct DspVoice {
    uint32_t pos;      /* next sample of the wave to read */
    int16_t yn1, yn2;  /* AFC history */
    int16_t block[16]; /* the AFC block holding pos */
    uint32_t block_no; /* which one, or DSP_VOICE_NO_BLOCK */
    int16_t hist[4];   /* the resampler's input window */
    uint32_t frac;     /* position between hist[1] and hist[2], 12 bits */
    int done;          /* the wave ended (it does not loop) */
} DspVoice;

void dsp_voice_reset(DspVoice* v);

/* Decodes one AFC block (9 or 5 bytes) with the 16 coefficient pairs of the
 * DSP's table, carrying the history in yn1 and yn2. */
void dsp_afc_decode_block(const uint8_t* src, int bytes, const int16_t afc_coefs[32], int16_t* yn1, int16_t* yn2,
                          int16_t out[16]);

/* Makes n samples at the resampling ratio (20.12, 0x1000 = the wave's own
 * rate) with the 64-phase 4-tap filter (res_coefs[64 * 4], 1.15). Returns
 * nonzero once the wave has ended; the rest is silence. */
int dsp_voice_render(DspVoice* v, const DspWave* w, uint32_t ratio, const int16_t res_coefs[256],
                     const int16_t afc_coefs[32], int16_t* out, int n);

#ifdef __cplusplus
}
#endif

#endif
