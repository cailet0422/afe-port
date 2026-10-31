/* pc_dsp.c - the GameCube DSP's JAudio mixing, in software.
 *
 * JAudio (the boot jingle) asks the DSP for a frame of JAC_SUBFRAMES
 * subframes of 80 samples (DsyncFrame) and, after each one, updates its
 * voices (UpdateDSP on the DSP interrupt). Here the sound thread renders a
 * subframe before each of those updates (pc_audio_process_frame): every voice
 * of the channel table (DSPchannel_, DSP_SetupBuffer) is decoded from ARAM
 * (audio/dsp_voice.c), its volume ramped to the target over the subframe and
 * added to the buses it is connected to. The front buses are the output, at
 * the frame's mixer level; the effect sends are left out.
 * Unlike the GameCube's split halves the frame is written interleaved, as
 * Jac_VframeWork reads it on PC. */
#include "pc_platform.h"
#include "dsp_voice.h"
#include "jaudio_NES/dspinterface.h"

#define DSP_SUBFRAME 80
#define DSP_CHANNELS 64 /* CH_BUF_LENGTH */

/* bus ids (DSP_SetBusConnect): the front and back speakers */
#define DSP_BUS_FRONT_LEFT  0x0D00
#define DSP_BUS_FRONT_RIGHT 0x0D60
#define DSP_BUS_BACK_LEFT   0x0F40
#define DSP_BUS_BACK_RIGHT  0x0CA0

extern u8* pc_aram_get_base(void);

static DSPchannel_* dsp_channels = NULL;
static int16_t dsp_res_coefs[256];
static int16_t dsp_afc_coefs[32];
static DspVoice dsp_voices[DSP_CHANNELS];

static s16* dsp_out = NULL; /* the frame being made, interleaved */
static u32 dsp_subframes = 0;
static u32 dsp_subframe = 0;
static u16 dsp_mixer_level = 0x4000; /* 4.12 */

/* the tables are u32s holding two s16s each, the first in the high half as
 * the big-endian DSP reads them */
static void dsp_read_table(const u32* src, int16_t* dst, int n) {
    int i;

    for (i = 0; i < n; i++) {
        u32 w = src[i / 2];

        dst[i] = (int16_t)((i & 1) ? (w & 0xFFFF) : (w >> 16));
    }
}

void pc_dsp_setup_table(uintptr_t channels, uintptr_t res_filter, uintptr_t afc_filter) {
    int i;

    dsp_channels = (DSPchannel_*)channels;
    dsp_read_table((const u32*)res_filter, dsp_res_coefs, 256);
    dsp_read_table((const u32*)afc_filter, dsp_afc_coefs, 32);
    for (i = 0; i < DSP_CHANNELS; i++) {
        dsp_voice_reset(&dsp_voices[i]);
    }
}

void pc_dsp_sync_frame(u32 subframes, uintptr_t out, u16 mixer_level) {
    dsp_out = (s16*)out;
    dsp_subframes = subframes;
    dsp_subframe = 0;
    dsp_mixer_level = mixer_level;
    if (dsp_out != NULL) {
        memset(dsp_out, 0, subframes * DSP_SUBFRAME * 2 * sizeof(s16));
    }
}

/* what the channel says about its wave; 0 for the oscillators, not made here */
static int dsp_wave_of(const DSPchannel_* ch, DspWave* w) {
    u8* aram = pc_aram_get_base();

    switch (ch->samplesSourceType) {
        case 9: w->format = DSP_VOICE_AFC9; break;
        case 5: w->format = DSP_VOICE_AFC5; break;
        case 8: w->format = DSP_VOICE_PCM8; break;
        case 16: w->format = DSP_VOICE_PCM16; break;
        default: return 0;
    }
    if (aram == NULL || ch->baseAddress >= PC_ARAM_SIZE) return 0;
    w->data = aram + ch->baseAddress;
    w->data_size = PC_ARAM_SIZE - ch->baseAddress;
    w->looping = ch->isLooping;
    /* JAudio's wave info: the loop's start and end, or the length */
    w->end = ch->loopStartPosition;
    w->loop_start = ch->loopAddress;
    w->loop_yn1 = ch->loopYN1;
    w->loop_yn2 = ch->loopYN2;
    return 1;
}

void pc_dsp_render_subframe(void) {
    s32 bus[2][DSP_SUBFRAME];
    int16_t samples[DSP_SUBFRAME];
    int i, j, k;

    if (dsp_out == NULL || dsp_channels == NULL || dsp_subframe >= dsp_subframes) return;
    memset(bus, 0, sizeof(bus));
    for (i = 0; i < DSP_CHANNELS; i++) {
        DSPchannel_* ch = &dsp_channels[i];
        DspVoice* v = &dsp_voices[i];
        DspWave w;

        if (!ch->enabled || ch->done) continue;
        if (ch->resetVpb) {
            dsp_voice_reset(v);
            ch->resetVpb = DSP_FALSE;
        }
        if (!dsp_wave_of(ch, &w)) continue;
        if (dsp_voice_render(v, &w, ch->resamplingRatio, dsp_res_coefs, dsp_afc_coefs, samples, DSP_SUBFRAME)) {
            ch->done = DSP_TRUE; /* UpdateDSPchannelAll lets the channel go */
        }
        if (ch->useConstantSample) continue; /* paused */
        for (j = 0; j < 6; j++) {
            DSPMixerChannel* mix = &ch->mixChannels[j];
            int side;
            s32 cur = (s16)mix->currentVolume;
            s32 step = ((s32)(s16)mix->targetVolume - cur) * 65536 / DSP_SUBFRAME;
            s32 vol = cur * 65536;

            switch (mix->id) {
                case DSP_BUS_FRONT_LEFT:
                case DSP_BUS_BACK_LEFT: side = 0; break;
                case DSP_BUS_FRONT_RIGHT:
                case DSP_BUS_BACK_RIGHT: side = 1; break;
                default: side = -1; break; /* effect sends */
            }
            if (side >= 0 && (cur != 0 || step != 0)) {
                for (k = 0; k < DSP_SUBFRAME; k++) {
                    bus[side][k] += (samples[k] * (vol >> 16)) >> 16;
                    vol += step;
                }
            }
            mix->currentVolume = mix->targetVolume;
        }
    }
    {
        s16* out = dsp_out + dsp_subframe * DSP_SUBFRAME * 2;

        for (k = 0; k < DSP_SUBFRAME; k++) {
            for (j = 0; j < 2; j++) {
                s32 s = (bus[j][k] * dsp_mixer_level) >> 12;

                out[k * 2 + j] = (s16)(s > 32767 ? 32767 : (s < -32768 ? -32768 : s));
            }
        }
    }
    dsp_subframe++;
}
