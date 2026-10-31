/* Unit tests for the software JAudio DSP voice (pc/src/audio/dsp_voice.c). */
#include "dsp_voice.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            failures++;                                                   \
        }                                                                 \
    } while (0)

/* the DSP's AFC table (DSPADPCM_FILTER in dspinterface.c) */
static const int16_t afc_coefs[32] = {
    0x0000, 0x0000, 0x0800, 0x0000, 0x0000, 0x0800, 0x0400, 0x0400,
    0x1000, -0x0800, 0x0E00, -0x0600, 0x0C00, -0x0400, 0x1200, -0x0A00,
    0x1068, -0x08C8, 0x12C0, -0x08FC, 0x1400, -0x0C00, 0x0800, -0x0800,
    0x0400, -0x0400, -0x0400, 0x0400, -0x0400, 0x0000, -0x0800, 0x0000,
};

/* a resampling table that takes half of hist[1] at every phase (1.0 does
 * not fit in 1.15), so the waves below hold twice the values to see */
static int16_t pick_coefs[256];

static void test_afc_hq(void) {
    /* table entry 0 (no prediction), scale 1: the codes are the samples */
    const uint8_t block[9] = { 0x00, 0x12, 0x34, 0x56, 0x7F, 0xED, 0xCB, 0xA9, 0x80 };
    const int16_t want[16] = { 1, 2, 3, 4, 5, 6, 7, -1, -2, -3, -4, -5, -6, -7, -8, 0 };
    int16_t out[16], yn1 = 0, yn2 = 0;

    dsp_afc_decode_block(block, 9, afc_coefs, &yn1, &yn2, out);
    CHECK(memcmp(out, want, sizeof(want)) == 0);
    CHECK(yn1 == 0 && yn2 == -8);
}

static void test_afc_prediction(void) {
    /* entry 1 keeps the last sample (0x800 is 1.0 in 4.11), scale 16 */
    const uint8_t block[9] = { 0x41, 0x10, 0, 0, 0, 0, 0, 0, 0 };
    int16_t out[16], yn1 = 100, yn2 = 0;

    dsp_afc_decode_block(block, 9, afc_coefs, &yn1, &yn2, out);
    CHECK(out[0] == 116); /* 100 + 16 * 1 */
    CHECK(out[1] == 116 && out[15] == 116);
}

static void test_afc_lq(void) {
    /* 2-bit codes 1, -2, -1, 0 at scale 1: 4 per step (code << 13 >> 11) */
    const uint8_t block[5] = { 0x00, 0x6C, 0x00, 0x00, 0x00 };
    int16_t out[16], yn1 = 0, yn2 = 0;

    dsp_afc_decode_block(block, 5, afc_coefs, &yn1, &yn2, out);
    CHECK(out[0] == 4 && out[1] == -8 && out[2] == -4 && out[3] == 0);
}

static void test_pcm16_loop(void) {
    /* samples 0..9, looping 4..9: after the first ten, 4 5 6 7 8 9 4 ... */
    uint8_t data[20];
    DspWave w = { data, sizeof(data), DSP_VOICE_PCM16, 10, 1, 4, 0, 0 };
    DspVoice v;
    int16_t out[30];
    int i;

    for (i = 0; i < 10; i++) {
        data[i * 2] = 0;
        data[i * 2 + 1] = (uint8_t)(i * 2);
    }
    dsp_voice_reset(&v);
    CHECK(dsp_voice_render(&v, &w, 0x1000, pick_coefs, afc_coefs, out, 30) == 0);
    /* the window fills first: sample k comes out at k + 3 */
    CHECK(out[3] == 0 && out[4] == 1 && out[12] == 9);
    CHECK(out[13] == 4 && out[18] == 9 && out[19] == 4);
}

static void test_pcm8_end(void) {
    uint8_t data[4] = { 0x10, 0x20, 0xF0, 0x7F };
    DspWave w = { data, sizeof(data), DSP_VOICE_PCM8, 4, 0, 0, 0, 0 };
    DspVoice v;
    int16_t out[10];

    dsp_voice_reset(&v);
    CHECK(dsp_voice_render(&v, &w, 0x1000, pick_coefs, afc_coefs, out, 10) != 0);
    CHECK(out[3] == 0x800 && out[4] == 0x1000 && out[5] == -0x800 && out[6] == 0x3F80);
    CHECK(out[7] == 0 && out[9] == 0);
}

static void test_ratio(void) {
    /* twice the rate: every other sample */
    uint8_t data[64];
    DspWave w = { data, sizeof(data), DSP_VOICE_PCM16, 32, 0, 0, 0, 0 };
    DspVoice v;
    int16_t out[8];
    int i;

    for (i = 0; i < 32; i++) {
        data[i * 2] = 0;
        data[i * 2 + 1] = (uint8_t)(i * 2);
    }
    dsp_voice_reset(&v);
    dsp_voice_render(&v, &w, 0x2000, pick_coefs, afc_coefs, out, 8);
    CHECK(out[2] == 1 && out[3] == 3 && out[7] == 11);
}

static void test_afc_loop_block(void) {
    /* two blocks at scale 2: the first without prediction (all 2), the second
     * adding 2 each sample; the loop starts in the second block, whose
     * decoding begins at its start with the loop's history */
    uint8_t data[18] = { 0x10, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
                         0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 };
    DspWave w = { data, sizeof(data), DSP_VOICE_AFC9, 32, 1, 20, 50, 0 };
    DspVoice v;
    int16_t out[60];

    dsp_voice_reset(&v);
    dsp_voice_render(&v, &w, 0x1000, pick_coefs, afc_coefs, out, 60);
    CHECK(out[3] == 1 && out[18] == 1);
    CHECK(out[19] == 2 && out[34] == 17);
    /* the loop: block 1 decoded again from 50, sample 20 is its fifth: 60 */
    CHECK(out[35] == 30 && out[46] == 41 && out[47] == 30);
}

int main(void) {
    int i;

    for (i = 0; i < 64; i++) {
        pick_coefs[i * 4 + 1] = 0x4000;
    }
    test_afc_hq();
    test_afc_prediction();
    test_afc_lq();
    test_pcm16_loop();
    test_pcm8_end();
    test_ratio();
    test_afc_loop_block();
    if (failures == 0) {
        printf("dsp_voice: all tests passed\n");
    }
    return failures != 0;
}
