/*
 * aureal_test.c - Aureal A3D 2.0 unit tests.
 *
 * Verifies: sample rate, 16-bit PCM synthesis, stereo HRTF (left/right
 * separation and inter-aural time delay), Doppler pitch (frequency shift)
 * and reverb tail.
 */
#include "aureal_a3d.h"
#include "bus.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_fail = 0;

static void check(const char* name, double got, double want, double tol) {
    if (fabs(got - want) <= tol) {
        printf("PASS  %s = %.3f (want %.3f +/-%.3f)\n", name, got, want, tol);
    } else {
        printf("FAIL  %s = %.3f (want %.3f +/-%.3f)\n", name, got, want, tol);
        g_fail++;
    }
}

static double rms_ch(const int16_t* buf, int frames, int ch) {
    double s = 0;
    for (int i = 0; i < frames; i++) s += (double)buf[2 * i + ch] * buf[2 * i + ch];
    return sqrt(s / frames);
}

static double freq_hz(const int16_t* buf, int frames, int ch) {
    int crossings = 0, last = 0;
    for (int i = 0; i < frames; i++) {
        int16_t s = buf[2 * i + ch];
        if (i > 0 && ((last >= 0 && s < 0) || (last < 0 && s >= 0))) crossings++;
        last = s;
    }
    return (double)crossings * AUREAL_RATE / (2.0 * frames);
}

static int itd_estimate(const int16_t* buf, int frames) {
    double best = -1e30;
    int bestk = 0;
    for (int k = -40; k <= 40; k++) {
        double c = 0;
        for (int i = 200; i < 4200; i++) {
            int j = i + k;
            if (j >= 0 && j < frames)
                c += (double)buf[2 * i] * buf[2 * j + 1];
        }
        if (c > best) { best = c; bestk = k; }
    }
    return bestk;
}

#define CH_BASE(ch) (A3D_CHAN_BASE + (ch) * A3D_CHAN_STRIDE)

int main(void) {
    Bus* bus = bus_create();
    AurealA3D* a = aureal_create();
    aureal_set_bus(a, bus);

    check("sample rate 48000", aureal_reg_read(a, A3D_REG_SAMPLE_RATE), 48000, 0);
    check("64 channels register space", AUREAL_CHANNELS, 64, 0);

    /* 1 second of 440 Hz sine (16-bit PCM) at physical RAM 0 */
    for (int i = 0; i < 48000; i++) {
        int16_t s = (int16_t)(sinf(2.0f * (float)M_PI * 440.0f * i / 48000.0f) * 20000.0f);
        bus_write16(bus, (uint64_t)i * 2, (uint16_t)s);
    }

    int base = CH_BASE(0);
    aureal_reg_write(a, base + A3D_CH_SRC, 0x00000000u);
    aureal_reg_write(a, base + A3D_CH_LEN, 48000u);
    aureal_reg_write(a, base + A3D_CH_PITCH, 0x10000u);   /* 1.0 */
    aureal_reg_write(a, base + A3D_CH_VOL_L, 0xFFFFu);
    aureal_reg_write(a, base + A3D_CH_VOL_R, 0xFFFFu);

    int16_t buf[48000 * 2];

    /* ---- centered tone: both ears similar ---- */
    aureal_reg_write(a, base + A3D_CH_AZIMUTH, 0);
    aureal_reg_write(a, base + A3D_CH_CTRL, 3u);         /* enable + loop */
    aureal_render(a, buf, 48000);
    check("center: RMS ~10k", rms_ch(buf, 48000, 0), 10000.0, 5000.0);
    check("center L/R ratio", rms_ch(buf, 48000, 0) / rms_ch(buf, 48000, 1), 1.0, 0.05);
    check("center frequency 440 Hz", freq_hz(buf, 48000, 0), 440.0, 3.0);

    /* ---- pitch 2.0: Doppler doubles the frequency ---- */
    aureal_reg_write(a, base + A3D_CH_PITCH, 0x20000u);
    aureal_render(a, buf, 48000);
    check("pitch 2.0 -> 880 Hz", freq_hz(buf, 48000, 0), 880.0, 6.0);
    aureal_reg_write(a, base + A3D_CH_PITCH, 0x10000u);

    /* ---- HRTF right (+45 deg): right louder, ITD ~18 samples ---- */
    uint32_t az45;
    float f45 = 45.0f;
    memcpy(&az45, &f45, 4);
    aureal_reg_write(a, base + A3D_CH_AZIMUTH, az45);
    aureal_render(a, buf, 48000);
    check("right: R/L ratio > 1", rms_ch(buf, 48000, 1) / rms_ch(buf, 48000, 0), 1.73, 0.35);
    int itd = itd_estimate(buf, 48000);
    printf("INFO  ITD estimate at +45 deg = %d samples\n", itd);
    check("right: ITD ~ +18", itd, 18.0, 8.0);

    /* ---- HRTF left (-45 deg): left louder, ITD ~ -18 ---- */
    float fneg = -45.0f;
    memcpy(&az45, &fneg, 4);
    aureal_reg_write(a, base + A3D_CH_AZIMUTH, az45);
    aureal_render(a, buf, 48000);
    check("left: L/R ratio > 1", rms_ch(buf, 48000, 0) / rms_ch(buf, 48000, 1), 1.73, 0.35);
    itd = itd_estimate(buf, 48000);
    printf("INFO  ITD estimate at -45 deg = %d samples\n", itd);
    check("left: ITD ~ -18", itd, -18.0, 8.0);

    /* ---- reverb tail: energy continues after the channel stops ---- */
    float f0 = 0.0f;
    memcpy(&az45, &f0, 4);
    aureal_reg_write(a, base + A3D_CH_AZIMUTH, az45);
    aureal_reg_write(a, base + A3D_CH_REVERB, 0x8000u);   /* 0.5 send */
    aureal_render(a, buf, 48000);                          /* feed reverb */
    aureal_reg_write(a, base + A3D_CH_CTRL, 0u);           /* stop channel */
    aureal_render(a, buf, 48000);                          /* tail only */
    check("reverb tail present", rms_ch(buf, 48000, 0), 2000.0, 1500.0);

    aureal_destroy(a);
    bus_destroy(bus);
    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
