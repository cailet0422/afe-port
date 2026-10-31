/* dsp_voice.c - one voice of the GameCube's JAudio DSP program (dsp_voice.h) */
#include "dsp_voice.h"

#include <string.h>

static int16_t clamp16(int32_t s) {
    if (s > 32767) return 32767;
    if (s < -32768) return -32768;
    return (int16_t)s;
}

void dsp_voice_reset(DspVoice* v) {
    memset(v, 0, sizeof(*v));
    v->block_no = DSP_VOICE_NO_BLOCK;
}

void dsp_afc_decode_block(const uint8_t* src, int bytes, const int16_t afc_coefs[32], int16_t* yn1, int16_t* yn2,
                          int16_t out[16]) {
    int32_t delta = 1 << ((src[0] >> 4) & 0xF);
    int idx = src[0] & 0xF;
    int32_t codes[16];
    int i;

    if (bytes == 9) {
        for (i = 0; i < 16; i++) {
            int32_t c = (src[1 + i / 2] >> ((i & 1) ? 0 : 4)) & 0xF;

            codes[i] = (c >= 8 ? c - 16 : c) << 11;
        }
    } else {
        for (i = 0; i < 16; i++) {
            int32_t c = (src[1 + i / 4] >> (6 - 2 * (i & 3))) & 3;

            codes[i] = (c >= 2 ? c - 4 : c) << 13;
        }
    }
    for (i = 0; i < 16; i++) {
        int32_t s = delta * codes[i] + *yn1 * afc_coefs[idx * 2] + *yn2 * afc_coefs[idx * 2 + 1];

        out[i] = clamp16(s >> 11);
        *yn2 = *yn1;
        *yn1 = out[i];
    }
}

static int block_bytes(int format) {
    return format == DSP_VOICE_AFC9 ? 9 : 5;
}

/* the wave's next sample, looping or ending at w->end */
static int16_t next_sample(DspVoice* v, const DspWave* w, const int16_t afc_coefs[32]) {
    uint32_t at;

    if (v->done) return 0;
    if (v->pos >= w->end) {
        if (!w->looping || w->loop_start >= w->end) {
            v->done = 1;
            return 0;
        }
        /* the loop's block decodes from its start with the loop's history,
         * and plays from the loop's sample in it */
        v->pos = w->loop_start;
        v->yn1 = w->loop_yn1;
        v->yn2 = w->loop_yn2;
        v->block_no = DSP_VOICE_NO_BLOCK;
        if (w->format == DSP_VOICE_AFC9 || w->format == DSP_VOICE_AFC5) {
            uint32_t no = v->pos / 16;

            at = no * (uint32_t)block_bytes(w->format);
            if (at + (uint32_t)block_bytes(w->format) > w->data_size) {
                v->done = 1;
                return 0;
            }
            dsp_afc_decode_block(w->data + at, block_bytes(w->format), afc_coefs, &v->yn1, &v->yn2, v->block);
            v->block_no = no;
        }
    }
    switch (w->format) {
        case DSP_VOICE_AFC9:
        case DSP_VOICE_AFC5: {
            uint32_t no = v->pos / 16;

            if (no != v->block_no) {
                at = no * (uint32_t)block_bytes(w->format);
                if (at + (uint32_t)block_bytes(w->format) > w->data_size) {
                    v->done = 1;
                    return 0;
                }
                dsp_afc_decode_block(w->data + at, block_bytes(w->format), afc_coefs, &v->yn1, &v->yn2, v->block);
                v->block_no = no;
            }
            return v->block[v->pos++ % 16];
        }
        case DSP_VOICE_PCM8:
            if (v->pos >= w->data_size) break;
            return (int16_t)((int8_t)w->data[v->pos++] * 256);
        case DSP_VOICE_PCM16:
            if (v->pos * 2 + 1 >= w->data_size) break;
            at = v->pos++ * 2;
            return (int16_t)((w->data[at] << 8) | w->data[at + 1]);
    }
    v->done = 1;
    return 0;
}

int dsp_voice_render(DspVoice* v, const DspWave* w, uint32_t ratio, const int16_t res_coefs[256],
                     const int16_t afc_coefs[32], int16_t* out, int n) {
    int i;

    for (i = 0; i < n; i++) {
        const int16_t* c = &res_coefs[((v->frac >> 6) & 0x3F) * 4];
        int32_t s = c[0] * v->hist[0] + c[1] * v->hist[1] + c[2] * v->hist[2] + c[3] * v->hist[3];

        out[i] = clamp16(s >> 15);
        v->frac += ratio;
        while (v->frac >= 0x1000) {
            v->frac -= 0x1000;
            v->hist[0] = v->hist[1];
            v->hist[1] = v->hist[2];
            v->hist[2] = v->hist[3];
            v->hist[3] = next_sample(v, w, afc_coefs);
        }
    }
    return v->done;
}
