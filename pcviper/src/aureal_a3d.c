/*
 * aureal_a3d.c - NEC Aureal Engine A3D 2.0 audio DSP implementation.
 *
 * 64 channels of 16-bit PCM at 48 kHz with Doppler pitch, simplified
 * binaural HRTF (ITD/ILD), biquad filtering, global Schroeder reverb and
 * LFO chorus. Output is an interleaved s16 stereo mix.
 */
#include "aureal_a3d.h"
#include "bus.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FRAC_BITS 16
#define ITD_MAX   32
#define COMB_COUNT 4
#define CHORUS_LEN 2048
#define CHORUS_DELAY 1200

typedef struct A3DChannel {
    uint32_t pos;           /* 16.16 */
    uint32_t inc;           /* 16.16 */
    int16_t itd_l[ITD_MAX + 8];
    int16_t itd_r[ITD_MAX + 8];
    int itd_ptr;
    float b0, b1, b2, a1, a2;
    int32_t x1, x2, y1, y2;
} A3DChannel;

struct AurealA3D {
    Bus* bus;
    uint32_t regs[AUREAL_REGS];

    A3DChannel ch[AUREAL_CHANNELS];

    /* global reverb (Schroeder comb filters) */
    float comb[COMB_COUNT][4096];
    int comb_pos[COMB_COUNT];

    /* global chorus */
    float chorus[CHORUS_LEN];
    int chorus_pos;
    float lfo_phase;
};

static const int s_comb_delay[COMB_COUNT] = { 1557, 1617, 1491, 1422 };

AurealA3D* aureal_create(void) {
    AurealA3D* a = calloc(1, sizeof(AurealA3D));
    if (!a) return NULL;
    aureal_reset(a);
    return a;
}

void aureal_destroy(AurealA3D* a) {
    free(a);
}

void aureal_set_bus(AurealA3D* a, Bus* bus) {
    a->bus = bus;
}

void aureal_reset(AurealA3D* a) {
    memset(a, 0, sizeof(*a));
    a->regs[A3D_REG_MASTER_VOL] = 0xFFFF0000u | 0xFFFFu;  /* 1.0 L/R (0xFFFF = 1.0) */
    a->regs[A3D_REG_SAMPLE_RATE] = AUREAL_RATE;
    for (int c = 0; c < AUREAL_CHANNELS; c++)
        a->ch[c].b0 = 1.0f;   /* biquad defaults to passthrough */
}

uint32_t aureal_reg_read(AurealA3D* a, int regnum) {
    if (regnum < 0 || regnum >= AUREAL_REGS) return 0;
    return a->regs[regnum];
}

void aureal_reg_write(AurealA3D* a, int regnum, uint32_t data) {
    if (regnum < 0 || regnum >= AUREAL_REGS) return;

    int base = A3D_CHAN_BASE;
    int stride = A3D_CHAN_STRIDE;
    if (regnum >= base && regnum < base + AUREAL_CHANNELS * stride) {
        int ch = (regnum - base) / stride;
        int off = (regnum - base) % stride;
        struct AurealA3D* aa = a;
        switch (off) {
        case A3D_CH_CTRL:
            if ((data & 1) && !(a->regs[regnum] & 1)) {
                aa->ch[ch].pos = 0;
                aa->ch[ch].x1 = aa->ch[ch].x2 = aa->ch[ch].y1 = aa->ch[ch].y2 = 0;
            }
            if (!(data & 1))
                aa->ch[ch].pos = 0;
            break;
        case A3D_CH_PITCH:
            aa->ch[ch].inc = data;
            break;
        case A3D_CH_BIQUAD0:
            aa->ch[ch].b0 = (float)(int8_t)(data >> 24) / 128.0f;
            aa->ch[ch].b1 = (float)(int8_t)(data >> 16) / 128.0f;
            aa->ch[ch].b2 = (float)(int8_t)(data >> 8) / 128.0f;
            aa->ch[ch].a1 = (float)(int8_t)(data) / 128.0f;
            break;
        case A3D_CH_BIQUAD1:
            aa->ch[ch].a2 = (float)(int8_t)(data) / 128.0f;
            break;
        default:
            break;
        }
    }
    a->regs[regnum] = data;
}

uint32_t aureal_read(AurealA3D* a, uint32_t offset) {
    return aureal_reg_read(a, (int)((offset >> 2) & 0xFFF));
}

void aureal_write(AurealA3D* a, uint32_t offset, uint32_t data, uint32_t mask) {
    aureal_reg_write(a, (int)((offset >> 2) & 0xFFF), data);
    (void)mask;
}

/* ---- HRTF: simplified binaural ITD/ILD model ---- */

static void hrtf_gains(float az_deg, float el_deg, float* gl, float* gr,
                       int* itd) {
    float a = az_deg;
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;

    /* Woodworth ITD model: ITD = (r/c) * (sin(theta) + theta) */
    float rad = a * (float)M_PI / 180.0f;
    float itd_f = 12.25f * (sinf(rad) + rad);
    int d = (int)(itd_f < 0.0f ? itd_f - 0.5f : itd_f + 0.5f);
    if (d > ITD_MAX) d = ITD_MAX;
    if (d < -ITD_MAX) d = -ITD_MAX;
    *itd = d;

    /* ILD: equal-power pan across the azimuth */
    float pan = a / 90.0f;
    if (pan < -1.0f) pan = -1.0f;
    if (pan > 1.0f) pan = 1.0f;
    *gl = sqrtf((1.0f - pan) * 0.5f);
    *gr = sqrtf((1.0f + pan) * 0.5f);

    /* elevation off-axis attenuation */
    float e = (el_deg < 0.0f ? -el_deg : el_deg) / 90.0f;
    if (e > 1.0f) e = 1.0f;
    float att = 1.0f - 0.5f * e;
    *gl *= att;
    *gr *= att;
}

/* ---- global reverb (Schroeder) ---- */

static float reverb_process(AurealA3D* a, float input) {
    float out = 0.0f;
    for (int i = 0; i < COMB_COUNT; i++) {
        int size = s_comb_delay[i];
        int p = a->comb_pos[i];
        float v = a->comb[i][p];
        a->comb[i][p] = input + v * 0.77f;
        a->comb_pos[i] = (p + 1) % size;
        out += v;
    }
    return out * 0.25f;
}

/* ---- global chorus ---- */

static float chorus_process(AurealA3D* a, float input) {
    a->chorus[a->chorus_pos] = input;
    /* LFO ~0.4 Hz modulating the read point by +/-100 samples */
    a->lfo_phase += 0.4f * 2.0f * (float)M_PI / AUREAL_RATE;
    if (a->lfo_phase > 2.0f * (float)M_PI) a->lfo_phase -= 2.0f * (float)M_PI;
    int mod = (int)(100.0f * sinf(a->lfo_phase));
    int read = a->chorus_pos - CHORUS_DELAY + mod;
    while (read < 0) read += CHORUS_LEN;
    a->chorus_pos = (a->chorus_pos + 1) % CHORUS_LEN;
    return a->chorus[read % CHORUS_LEN];
}

/* ---- mix ---- */

static inline float clamp_s16(float v) {
    if (v > 32767.0f) return 32767.0f;
    if (v < -32768.0f) return -32768.0f;
    return v;
}

void aureal_render(AurealA3D* a, int16_t* out, int frames) {
    if (!a->bus) {
        memset(out, 0, (size_t)frames * 2 * 2);
        return;
    }

    uint32_t master = a->regs[A3D_REG_MASTER_VOL];
    float mvol_l = (float)(master >> 16) / 65535.0f;
    float mvol_r = (float)(master & 0xFFFF) / 65535.0f;

    for (int i = 0; i < frames; i++) {
        float acc_l = 0.0f, acc_r = 0.0f;
        float reverb_in = 0.0f, chorus_in = 0.0f;

        for (int c = 0; c < AUREAL_CHANNELS; c++) {
            int base = A3D_CHAN_BASE + c * A3D_CHAN_STRIDE;
            uint32_t ctrl = a->regs[base + A3D_CH_CTRL];
            if (!(ctrl & 1)) continue;

            A3DChannel* ch = &a->ch[c];
            uint32_t src = a->regs[base + A3D_CH_SRC];
            uint32_t len = a->regs[base + A3D_CH_LEN];

            uint32_t idx = ch->pos >> FRAC_BITS;
            if (idx >= len) {
                if (ctrl & 2) {   /* loop */
                    ch->pos -= len << FRAC_BITS;
                    idx = ch->pos >> FRAC_BITS;
                } else {
                    a->regs[base + A3D_CH_CTRL] = ctrl & ~1u;  /* stop */
                    continue;
                }
            }
            int16_t s = (int16_t)bus_read16(a->bus, src + (uint32_t)idx * 2);
            if (ctrl & 4) s = (int16_t)((uint16_t)s ^ 0x8000u);   /* unsigned */

            /* biquad filter */
            float y = ch->b0 * (float)s + ch->b1 * (float)ch->x1 +
                      ch->b2 * (float)ch->x2 - ch->a1 * (float)ch->y1 -
                      ch->a2 * (float)ch->y2;
            ch->x2 = ch->x1; ch->x1 = s;
            ch->y2 = ch->y1; ch->y1 = (int32_t)y;

            /* advance playback */
            ch->pos += ch->inc;

            /* HRTF gains */
            float az = 0.0f, el = 0.0f;
            memcpy(&az, &a->regs[base + A3D_CH_AZIMUTH], 4);
            memcpy(&el, &a->regs[base + A3D_CH_ELEVATION], 4);
            float gl, gr;
            int itd;
            hrtf_gains(az, el, &gl, &gr, &itd);

            /* ITD: delay the ear that hears the sound last */
            ch->itd_l[ch->itd_ptr] = (int16_t)y;
            ch->itd_r[ch->itd_ptr] = (int16_t)y;
            int dl = (itd < 0) ? -itd : 0;
            int dr = (itd > 0) ? itd : 0;
            int lp = (ch->itd_ptr + ITD_MAX + 8 - dl) % (ITD_MAX + 8);
            int rp = (ch->itd_ptr + ITD_MAX + 8 - dr) % (ITD_MAX + 8);
            float yl = (float)ch->itd_l[lp];
            float yr = (float)ch->itd_r[rp];
            ch->itd_ptr = (ch->itd_ptr + 1) % (ITD_MAX + 8);

            float vl = (float)(a->regs[base + A3D_CH_VOL_L] & 0xFFFF) / 65535.0f;
            float vr = (float)(a->regs[base + A3D_CH_VOL_R] & 0xFFFF) / 65535.0f;
            acc_l += yl * vl * gl;
            acc_r += yr * vr * gr;

            reverb_in += y * (float)(a->regs[base + A3D_CH_REVERB] & 0xFFFF) / 65535.0f;
            chorus_in += y * (float)(a->regs[base + A3D_CH_CHORUS] & 0xFFFF) / 65535.0f;
        }

        float rv = reverb_process(a, reverb_in);
        float cs = chorus_process(a, chorus_in);
        acc_l += rv * 0.4f + cs * 0.3f;
        acc_r += rv * 0.4f + cs * 0.3f;

        out[2 * i] = (int16_t)clamp_s16(acc_l * mvol_l);
        out[2 * i + 1] = (int16_t)clamp_s16(acc_r * mvol_r);
    }
}
