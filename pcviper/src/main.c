/*
 * main.c - NEC PC-Viper emulator (fase 2: VR5432 + Voodoo2 EC).
 *
 * Boota o núcleo MIPS, acessa o Voodoo2 EC (100 MHz, 16 MB SGRAM unificada)
 * via MMIO do barramento e renderiza um triângulo multitexturizado num
 * arquivo PPM de saída. Com SDL2 e a flag --sdl, roda o firmware demo3d em
 * tempo real numa janela, com rotação/movimento/zoom via teclado ou gamepad.
 */
#include "bus.h"
#include "vr5432.h"
#include "voodoo2_ec.h"
#include "voodoo_fifo.h"
#include "aureal_a3d.h"
#include "viper_system.h"
#include "glide.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VIPER_VOODOO_MMIO 0x10000000ull
#define VIPER_VOODOO_KSEG1 0xB0000000ull   /* KSEG1 -> physical 0x10000000 */
#define VIPER_AUREAL_MMIO 0x14000000ull
#define VIPER_PERIPH_MMIO 0x1E000000ull

/* ---- tiny MIPS IV assembler for the boot program ---- */
typedef struct {
    uint32_t w[512];
    int n;
} Prog;

static void p_emit(Prog* p, uint32_t w) { p->w[p->n++] = w; }
static void p_lui(Prog* p, int rt, uint16_t imm) {
    p_emit(p, (0x0Fu << 26) | ((uint32_t)rt << 16) | imm);
}
static void p_ori(Prog* p, int rt, int rs, uint16_t imm) {
    p_emit(p, (0x0Du << 26) | ((uint32_t)rs << 21) | ((uint32_t)rt << 16) | imm);
}
static void p_sw(Prog* p, int rt, int rs, int16_t off) {
    p_emit(p, (0x2Bu << 26) | ((uint32_t)rs << 21) | ((uint32_t)rt << 16) |
              ((uint32_t)off & 0xFFFF));
}
static void p_beq(Prog* p, int rs, int rt, int16_t off) {
    p_emit(p, (0x04u << 26) | ((uint32_t)rs << 21) | ((uint32_t)rt << 16) |
              ((uint32_t)off & 0xFFFF));
}
static void p_nop(Prog* p) { p_emit(p, 0); }
static void p_li(Prog* p, int rt, uint32_t v) {
    p_lui(p, rt, (uint16_t)(v >> 16));
    if (v & 0xFFFF) p_ori(p, rt, rt, (uint16_t)v);
}

/* Boot program: programs the Voodoo2 EC color1, the Viper SoC DMA
 * (DVD -> RAM) and the Aureal A3D channel 0 entirely through CPU MMIO. */
static void build_boot(Prog* p) {
    /* Voodoo color1 = red via KSEG1 0xB0000148 */
    p_li(p, 8, 0xB0000000u + 0x148);
    p_li(p, 9, 0x00FF0000u);
    p_sw(p, 9, 8, 0);

    /* SoC DMA: DVD LBA 0 -> RAM 0x100000, 4096 bytes (KSEG1 0xBE000000) */
    p_li(p, 8, 0xBE000000u);
    p_li(p, 9, 0);
    p_sw(p, 9, 8, 0x04);      /* DMA_SRC */
    p_li(p, 9, 0x100000);
    p_sw(p, 9, 8, 0x08);      /* DMA_DST */
    p_li(p, 9, 4096);
    p_sw(p, 9, 8, 0x0C);      /* DMA_SIZE */
    p_li(p, 9, 1);
    p_sw(p, 9, 8, 0x00);      /* DMA_CTRL start */

    /* A3D channel 0 at KSEG1 0xB4000000 + 0x40 */
    p_li(p, 8, 0xB4000000u + 0x40);
    p_li(p, 9, 0x100000);     /* SRC = RAM (DVD data) */
    p_sw(p, 9, 8, 0x08);      /* A3D_CH_SRC */
    p_li(p, 9, 4096);         /* LEN = 4096 bytes (2048 samples) */
    p_sw(p, 9, 8, 0x0C);      /* A3D_CH_LEN */
    p_li(p, 9, 0x10000);      /* PITCH 1.0 */
    p_sw(p, 9, 8, 0x04);      /* A3D_CH_PITCH */
    p_li(p, 9, 0xFFFF);
    p_sw(p, 9, 8, 0x14);      /* A3D_CH_VOL_L */
    p_sw(p, 9, 8, 0x18);      /* A3D_CH_VOL_R */
    p_li(p, 9, 0xC2340000u);  /* AZIMUTH = -45.0f */
    p_sw(p, 9, 8, 0x28);      /* A3D_CH_AZIMUTH */
    p_li(p, 9, 3);            /* enable + loop */
    p_sw(p, 9, 8, 0x00);      /* A3D_CH_CTRL */

    p_beq(p, 0, 0, -1);
    p_nop(p);
}

/* Validate what the CPU programmed through the MMIO path. */
static void cpu_driven_validate(Bus* bus, AurealA3D* audio, ViperSoC* soc) {
    int ok = 1;

    /* SoC DMA done bit */
    ok = (viper_soc_reg_read(soc, VIPER_DMA_STATUS) & 2) != 0;
    printf("pcviper: CPU->SoC DMA status: %s\n", ok ? "OK (done)" : "FAIL");

    /* SoC DMA: RAM 0x100000 must contain the DVD pattern */
    ok = 1;
    for (int i = 0; i < 4096; i++) {
        uint32_t d = i;
        if (bus_read8(bus, 0x100000 + i) != (uint8_t)((d / 2048) + (d % 2048))) {
            ok = 0;
            break;
        }
    }
    printf("pcviper: CPU->SoC DMA: %s\n", ok ? "OK (DVD pattern in RAM)" : "FAIL");

    /* A3D channel 0 registers programmed by the CPU */
    int base = A3D_CHAN_BASE;
    ok = 1;
    if (aureal_reg_read(audio, base + A3D_CH_SRC) != 0x100000) ok = 0;
    if (aureal_reg_read(audio, base + A3D_CH_LEN) != 4096) ok = 0;
    if (aureal_reg_read(audio, base + A3D_CH_PITCH) != 0x10000) ok = 0;
    if (aureal_reg_read(audio, base + A3D_CH_CTRL) != 3) ok = 0;
    printf("pcviper: CPU->A3D channel regs: %s\n", ok ? "OK" : "FAIL");

    /* render audio driven by the CPU-programmed channel */
    int16_t mix[4800 * 2];
    aureal_render(audio, mix, 4800);
    double rms_l = 0, rms_r = 0;
    for (int i = 0; i < 4800; i++) {
        rms_l += (double)mix[2 * i] * mix[2 * i];
        rms_r += (double)mix[2 * i + 1] * mix[2 * i + 1];
    }
    rms_l = sqrt(rms_l / 4800);
    rms_r = sqrt(rms_r / 4800);
    printf("pcviper: CPU->A3D audio L RMS=%.0f R RMS=%.0f %s\n",
           rms_l, rms_r,
           (rms_l > 1000.0 && rms_l > rms_r * 1.4) ? "OK (left pan, -45 deg)"
                                                   : "unexpected");
}

static uint32_t fbits(float v) {
    uint32_t b;
    memcpy(&b, &v, 4);
    return b;
}

static void write_ppm(const char* path, const uint32_t* rgb, int w, int h);

/* Capture the Voodoo2 EC front buffer to a PPM (used to show what CPU
 * firmware/demos rendered). */
static void voodoo_capture(Voodoo2EC* v, const char* path) {
    uint32_t rgb[640 * 480];
    memset(rgb, 0, sizeof(rgb));
    voodoo2ec_update(v, rgb, 640, 480);
    write_ppm(path, rgb, 640, 480);
}

/* MMIO bridge: adapt the device API to the bus handler signatures */
static uint32_t voodoo_mmio_read(void* ctx, uint64_t offset) {
    return voodoo2ec_read((Voodoo2EC*)ctx, (uint32_t)offset);
}

static void voodoo_mmio_write(void* ctx, uint64_t offset, uint32_t data, uint32_t mask) {
    voodoo2ec_write((Voodoo2EC*)ctx, (uint32_t)offset, data, mask);
}

static uint32_t aureal_mmio_read(void* ctx, uint64_t offset) {
    return aureal_read((AurealA3D*)ctx, (uint32_t)offset);
}

static void aureal_mmio_write(void* ctx, uint64_t offset, uint32_t data, uint32_t mask) {
    aureal_write((AurealA3D*)ctx, (uint32_t)offset, data, mask);
}

static uint32_t viper_mmio_read(void* ctx, uint64_t offset) {
    return viper_soc_read((ViperSoC*)ctx, (uint32_t)offset);
}

static void viper_mmio_write(void* ctx, uint64_t offset, uint32_t data, uint32_t mask) {
    viper_soc_write((ViperSoC*)ctx, (uint32_t)offset, data, mask);
}

static void write_wav(const char* path, const int16_t* buf, int frames) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "main: cannot write %s\n", path); return; }
    uint32_t data_len = (uint32_t)frames * 4;
    fwrite("RIFF", 1, 4, f);
    uint32_t v32 = 36 + data_len;
    fwrite(&v32, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    v32 = 16; fwrite(&v32, 4, 1, f);
    uint16_t v16 = 1; fwrite(&v16, 2, 1, f);   /* PCM */
    v16 = 2; fwrite(&v16, 2, 1, f);            /* stereo */
    v32 = AUREAL_RATE; fwrite(&v32, 4, 1, f);
    v32 = AUREAL_RATE * 4; fwrite(&v32, 4, 1, f);  /* byte rate */
    v16 = 4; fwrite(&v16, 2, 1, f);            /* block align */
    v16 = 16; fwrite(&v16, 2, 1, f);           /* bits per sample */
    fwrite("data", 1, 4, f);
    fwrite(&data_len, 4, 1, f);
    fwrite(buf, 1, data_len, f);
    fclose(f);
    printf("pcviper: wrote %s (%d frames)\n", path, frames);
}

static void a3d_demo(AurealA3D* a, Bus* bus) {
    /* A-major chord: A4, C#5, E5 (16-bit PCM loops in RAM) */
    const float notes[3] = { 440.0f, 554.37f, 659.25f };
    const uint32_t srcs[3] = { 0x000000u, 0x018000u, 0x030000u };
    for (int n = 0; n < 3; n++) {
        for (int i = 0; i < 48000; i++) {
            int16_t s = (int16_t)(sinf(2.0f * (float)M_PI * notes[n] * i / 48000.0f)
                                  * 12000.0f);
            bus_write16(bus, (uint64_t)srcs[n] + (uint64_t)i * 2, (uint16_t)s);
        }
    }
    const float az[3] = { -45.0f, 0.0f, 45.0f };
    for (int c = 0; c < 3; c++) {
        int base = A3D_CHAN_BASE + c * A3D_CHAN_STRIDE;
        uint32_t azb;
        memcpy(&azb, &az[c], 4);
        aureal_reg_write(a, base + A3D_CH_SRC, srcs[c]);
        aureal_reg_write(a, base + A3D_CH_LEN, 48000u);
        aureal_reg_write(a, base + A3D_CH_PITCH, 0x10000u);
        aureal_reg_write(a, base + A3D_CH_VOL_L, 0xFFFFu);
        aureal_reg_write(a, base + A3D_CH_VOL_R, 0xFFFFu);
        aureal_reg_write(a, base + A3D_CH_AZIMUTH, azb);
        aureal_reg_write(a, base + A3D_CH_REVERB, (c == 1) ? 0x6000u : 0x2000u);
        aureal_reg_write(a, base + A3D_CH_CTRL, 3u);   /* enable + loop */
    }

    int seconds = 3;
    int frames = AUREAL_RATE * seconds;
    int16_t* mix = malloc((size_t)frames * 4);
    if (!mix) return;
    aureal_render(a, mix, frames);

    printf("pcviper: aureal a3d demo (%d channels @ %d Hz)\n",
           AUREAL_CHANNELS, AUREAL_RATE);

    write_wav("aureal.wav", mix, frames);
    free(mix);
}

/* Loads the audio buffer that firmware/demo3d.c wrote at the end of
 * voodoo_setup() and verifies A3D 3D-positioning: at -45 deg azimuth the
 * left channel should dominate, at +45 deg the right. */
static void a3d_demo3d_audio(AurealA3D* a, Bus* bus) {
    /* sample the sine wave that demo3d.c generated at physical 0x00100000 */
    int n_samples = 0;
    for (int i = 0; i < 2400; i++) {
        int16_t s = (int16_t)bus_read16(bus, 0x00100000u + (uint64_t)i * 2);
        if (s != 0) n_samples++;
    }
    if (n_samples == 0) return;     /* firmware never wrote the audio buffer */

    int base = A3D_CHAN_BASE;
    aureal_reg_write(a, base + A3D_CH_SRC, 0x00100000u);
    aureal_reg_write(a, base + A3D_CH_LEN, 2400u);
    aureal_reg_write(a, base + A3D_CH_PITCH, 0x10000u);
    aureal_reg_write(a, base + A3D_CH_VOL_L, 0xFFFFu);
    aureal_reg_write(a, base + A3D_CH_VOL_R, 0xFFFFu);
    aureal_reg_write(a, base + A3D_CH_ELEVATION, 0);

    /* Render twice with two opposite azimuths, measure stereo balance. */
    int seconds = 1;
    int frames = AUREAL_RATE * seconds;
    float az[2] = { -45.0f, 45.0f };
    const char* labels[2] = { "az=-45", "az=+45" };
    int ok = 1;
    for (int k = 0; k < 2; k++) {
        /* reset state by stopping and re-enabling */
        aureal_reg_write(a, base + A3D_CH_CTRL, 0);
        uint32_t azb;
        memcpy(&azb, &az[k], 4);
        aureal_reg_write(a, base + A3D_CH_AZIMUTH, azb);
        aureal_reg_write(a, base + A3D_CH_CTRL, 3u);

        int16_t* mix = malloc((size_t)frames * 4);
        if (!mix) return;
        aureal_render(a, mix, frames);

        double rms_l = 0.0, rms_r = 0.0;
        for (int i = 0; i < frames; i++) {
            rms_l += (double)mix[2 * i] * mix[2 * i];
            rms_r += (double)mix[2 * i + 1] * mix[2 * i + 1];
        }
        rms_l = sqrt(rms_l / frames);
        rms_r = sqrt(rms_r / frames);
        const char* which = (k == 0 && rms_l > rms_r * 1.1f) ? "left" :
                            (k == 1 && rms_r > rms_l * 1.1f) ? "right" : "bal";
        printf("pcviper: demo3d a3d %s L RMS=%.0f R RMS=%.0f -> %s\n",
               labels[k], rms_l, rms_r, which);
        if (!strcmp("bal", which)) ok = 0;
        free(mix);
    }
    printf("pcviper: %s\n", ok ? "demo3d 3D positional audio OK"
                               : "demo3d 3D positional audio: unexpected balance");
}

static uint32_t v2_fbits(float v) {
    uint32_t b;
    memcpy(&b, &v, 4);
    return b;
}

/* setup-engine triangle (firmware-style register path) */
static void v2_tri(Voodoo2EC* v, uint32_t smode,
                   float x0, float y0, float w0, uint32_t argb0, float s0, float t0,
                   float x1, float y1, float w1, uint32_t argb1, float s1, float t1,
                   float x2, float y2, float w2, uint32_t argb2, float s2, float t2) {
    struct { float x, y, w; uint32_t argb; float s, t; } vt[3] = {
        { x0, y0, w0, argb0, s0, t0 }, { x1, y1, w1, argb1, s1, t1 },
        { x2, y2, w2, argb2, s2, t2 },
    };
    voodoo2ec_reg_write(v, V2_REG_SSETUPMODE, smode);
    for (int i = 0; i < 3; i++) {
        voodoo2ec_reg_write(v, V2_REG_SVX, v2_fbits(vt[i].x));
        voodoo2ec_reg_write(v, V2_REG_SVY, v2_fbits(vt[i].y));
        voodoo2ec_reg_write(v, V2_REG_SARGB, vt[i].argb);
        voodoo2ec_reg_write(v, V2_REG_SWB, v2_fbits(1.0f));
        voodoo2ec_reg_write(v, V2_REG_SWTMU0, v2_fbits(vt[i].w));
        voodoo2ec_reg_write(v, V2_REG_SS_W0, v2_fbits(vt[i].s));
        voodoo2ec_reg_write(v, V2_REG_ST_W0, v2_fbits(vt[i].t));
        voodoo2ec_reg_write(v, V2_REG_SWTMU1, v2_fbits(vt[i].w));
        voodoo2ec_reg_write(v, V2_REG_SS_W1, v2_fbits(vt[i].s));
        voodoo2ec_reg_write(v, V2_REG_ST_W1, v2_fbits(vt[i].t));
        voodoo2ec_reg_write(v, i ? V2_REG_SDRAWTRI : V2_REG_SBEGINTRI, 1);
    }
}

static void v2_quad(Voodoo2EC* v, uint32_t smode,
                    float x0, float y0, float w0, uint32_t argb0, float s0, float t0,
                    float x1, float y1, float w1, uint32_t argb1, float s1, float t1,
                    float x2, float y2, float w2, uint32_t argb2, float s2, float t2,
                    float x3, float y3, float w3, uint32_t argb3, float s3, float t3) {
    v2_tri(v, smode, x0, y0, w0, argb0, s0, t0, x1, y1, w1, argb1, s1, t1,
           x2, y2, w2, argb2, s2, t2);
    v2_tri(v, smode, x0, y0, w0, argb0, s0, t0, x2, y2, w2, argb2, s2, t2,
           x3, y3, w3, argb3, s3, t3);
}

#define V2_TEX_PORT 0x800000u

static void v2_upload_rgb565(Voodoo2EC* v, uint32_t sgram_off,
                             uint32_t size, uint16_t color) {
    for (uint32_t i = 0; i < size * size; i += 2) {
        uint32_t dword = color | ((uint32_t)color << 16);
        voodoo2ec_write(v, V2_TEX_PORT + sgram_off + i * 2, dword, ~0u);
    }
}

static void voodoo_advanced_demo(Voodoo2EC* v) {
    voodoo2ec_reset(v);
    voodoo2ec_reg_write(v, V2_REG_FBIINIT1, 0x0128u);
    uint32_t rgb[640 * 480];
    const uint32_t SM_TEX0 = 0x31u, SM_TEX01 = 0xB1u, SM_PLAIN = 0x11u;

    /* ---- fog table: linear ramp 0..255 ---- */
    for (int i = 0; i < 64; i++) {
        uint32_t d = (uint32_t)(i * 4) | ((uint32_t)(i * 4 + 1) << 8)
                   | ((uint32_t)(i * 4 + 2) << 16) | ((uint32_t)(i * 4 + 3) << 24);
        voodoo2ec_reg_write(v, V2_REG_FOGTABLE, d);
    }

    /* 1. Gouraud + distance fog (top-left): red near -> blue far, fogged
     *    to white at the far edge via the fogTable. */
    voodoo2ec_reg_write(v, V2_REG_FOGMODE, 0x41u);      /* enable + table */
    voodoo2ec_reg_write(v, V2_REG_FOGCOLOR, 0x00FFFFFFu);
    v2_quad(v, SM_PLAIN,
            20, 20, 1.0f, 0xFFFF0000u, 0, 0,
            300, 20, 1.0f, 0xFFFF0000u, 0, 0,
            300, 220, 0.02f, 0xFF0000FFu, 0, 0,
            20, 220, 0.02f, 0xFF0000FFu, 0, 0);

    /* 2. Alpha blend (top-right): opaque red + 50% blue on top. */
    voodoo2ec_reg_write(v, V2_REG_FOGMODE, 0u);
    voodoo2ec_reg_write(v, V2_REG_FBZCOLORPATH, 0u);
    v2_quad(v, SM_PLAIN,
            340, 20, 1.0f, 0xFFFF0000u, 0, 0,
            620, 20, 1.0f, 0xFFFF0000u, 0, 0,
            620, 220, 1.0f, 0xFFFF0000u, 0, 0,
            340, 220, 1.0f, 0xFFFF0000u, 0, 0);
    voodoo2ec_reg_write(v, V2_REG_ALPHAMODE,
                        (2u << 0) | (1u << 3) | (0u << 6) | (0u << 10));
    v2_quad(v, SM_PLAIN,
            340, 20, 1.0f, 0x800000FFu, 0, 0,
            620, 20, 1.0f, 0x800000FFu, 0, 0,
            620, 220, 1.0f, 0x800000FFu, 0, 0,
            340, 220, 1.0f, 0x800000FFu, 0, 0);
    voodoo2ec_reg_write(v, V2_REG_ALPHAMODE, 0u);

    /* 3. Multitexture (bottom-left): TMU0 16x16 checkerboard diffuse *
     *    TMU1 red lightmap (single pass, TMU0+TMU1). */
    for (int t = 0; t < 16; t++)
        for (int s = 0; s < 16; s += 2) {
            uint16_t c0 = (((s >> 1) + (t >> 1)) & 1) ? 0xFFFFu : 0x0000u;
            uint16_t c1 = ((((s + 1) >> 1) + (t >> 1)) & 1) ? 0xFFFFu : 0x0000u;
            voodoo2ec_write(v, V2_TEX_PORT + 0x300000u + (uint32_t)(t * 16 + s) * 2,
                            c0 | ((uint32_t)c1 << 16), ~0u);
        }
    v2_upload_rgb565(v, 0x340000u, 16, 0xF800u);        /* red lightmap */
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXTUREMODE << 2) | (1u << 12)), 0x10, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TLOD << 2) | (1u << 12)), 4 << 3, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXBASEADDR << 2) | (1u << 12)), 0x300000u, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXTUREMODE << 2) | (2u << 12)), 0x10, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TLOD << 2) | (2u << 12)), 4 << 3, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXBASEADDR << 2) | (2u << 12)), 0x340000u, ~0u);
    v2_quad(v, SM_TEX01,
            20, 260, 1.0f, 0xFFFFFFFFu, 0, 0,
            300, 260, 1.0f, 0xFFFFFFFFu, 16, 0,
            300, 460, 1.0f, 0xFFFFFFFFu, 16, 16,
            20, 460, 1.0f, 0xFFFFFFFFu, 0, 16);

    /* 4. Mipmap + trilinear (bottom-right): 64x64 red + 32x32 green,
     *    LOD ~5.4 across the quad blends the two levels. */
    v2_upload_rgb565(v, 0x200000u, 64, 0xF800u);
    v2_upload_rgb565(v, 0x200000u + 64u * 64u * 2u, 32, 0x07E0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXTUREMODE << 2) | (1u << 12)), 0x10, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TLOD << 2) | (1u << 12)), (6 << 3) | (5 << 8), ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXBASEADDR << 2) | (1u << 12)), 0x200000u, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TREXINIT0 << 2) | (1u << 12)), 0x03u, ~0u);
    v2_quad(v, SM_TEX0,
            340, 260, 1.0f, 0xFFFFFFFFu, 0, 0,
            620, 260, 1.0f, 0xFFFFFFFFu, 200, 0,
            620, 460, 1.0f, 0xFFFFFFFFu, 200, 64,
            340, 460, 1.0f, 0xFFFFFFFFu, 0, 64);

    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
    voodoo2ec_update(v, rgb, 640, 480);

    uint32_t fog_near = rgb[40 + 40 * 640], fog_far = rgb[40 + 200 * 640];
    uint32_t blend = rgb[480 + 120 * 640];
    uint32_t mtex_w = rgb[64 + 310 * 640], mtex_b = rgb[29 + 310 * 640];
    uint32_t mip = rgb[480 + 360 * 640];

    int ok = 1;
    ok = ok && ((fog_near >> 16) & 0xFF) > 200;                  /* red near */
    ok = ok && (fog_far & 0xFF) > 200 && ((fog_far >> 8) & 0xFF) > 200;
    ok = ok && (((blend >> 16) & 0xFF) > 90 && ((blend >> 16) & 0xFF) < 170);
    ok = ok && ((mtex_w >> 16) & 0xFF) > 200 && (mtex_b & 0xFF) < 40;
    ok = ok && (((mip >> 8) & 0xFF) > 60 && ((mip >> 16) & 0xFF) > 60);
    printf("pcviper: advanced demo fog=%08X/%08X blend=%08X mtex=%08X/%08X mip=%08X\n",
           fog_near, fog_far, blend, mtex_w, mtex_b, mip);
    printf("pcviper: %s\n", ok ? "gouraud+fog / alpha / multitex / mipmap OK"
                               : "advanced demo: unexpected output");
    write_ppm("advanced.ppm", rgb, 640, 480);
}

static void glide_demo(Voodoo2EC* voodoo) {
    GrContext ctx;
    ctx.voodoo = voodoo;
    grGlideInit();
    grSstSelect(0);
    grSstWinOpen(&ctx, 0, 640, 480, 2, 0);

    /* textured triangle via the Glide API (offset from the CMDFIFO demo) */
    uint8_t tex[16 * 16 * 2];
    for (int t = 0; t < 16; t++)
        for (int s = 0; s < 16; s++) {
            uint16_t c = (s < 8) ? 0xF800u : 0x001Fu;
            tex[(t * 16 + s) * 2 + 0] = (uint8_t)(c & 0xFF);
            tex[(t * 16 + s) * 2 + 1] = (uint8_t)(c >> 8);
        }
    GrTexInfo ti;
    ti.smallLod = 4;
    ti.largeLod = 4;
    ti.aspectRatio = 1.0f;
    ti.format = GR_TEXFMT_RGB_565;
    ti.data = tex;
    grTexUpload(&ctx, GR_TMU0, &ti);
    grTexBind(&ctx, GR_TMU0, &ti);
    grColorCombine(&ctx, 1, 0);
    grBufferClear(&ctx, 0x00000000u, GR_BUFFER_BACKBUFFER);

    grBeginTriangles(&ctx);
    GrVertex v;
    memset(&v, 0, sizeof(v));
    v.x = 150; v.y = 100; v.w = 1.0f; v.r = 255; v.g = 255; v.b = 255;
    v.s0 = 0; v.t0 = 0;
    grVertex(&ctx, &v);
    v.x = 550; v.y = 100; v.w = 1.0f; v.s0 = 16; v.t0 = 0;
    grVertex(&ctx, &v);
    v.x = 350; v.y = 450; v.w = 1.0f; v.s0 = 8; v.t0 = 16;
    grVertex(&ctx, &v);
    grEndTriangles(&ctx);

    grBufferSwap(&ctx, GR_BUFFER_BACKBUFFER);
    static uint32_t rgb[640 * 480];
    memset(rgb, 0, sizeof(rgb));
    grSplash(&ctx, 0, 0, 640, 480, 0, rgb);

    uint32_t l = rgb[250 + 200 * 640];
    uint32_t r = rgb[450 + 200 * 640];
    printf("pcviper: glide demo left=0x%08X right=0x%08X\n", l, r);
    printf("pcviper: %s\n",
           (l == 0xFFFF0000u && r == 0xFF0000FFu) ? "glide textured triangle OK"
                                                  : "glide: unexpected output");
    write_ppm("glide.ppm", rgb, 640, 480);
    grSstWinClose(&ctx);
    grGlideShutdown();
}

static void soc_demo(ViperSoC* soc, Bus* bus) {
    /* DMA 4 sectors from DVD LBA 2 into RAM 0x4000 */
    viper_soc_reg_write(soc, VIPER_DMA_SRC, 2);
    viper_soc_reg_write(soc, VIPER_DMA_DST, 0x4000);
    viper_soc_reg_write(soc, VIPER_DMA_SIZE, 4 * VIPER_DVD_SECTOR);
    viper_soc_reg_write(soc, VIPER_DMA_CTRL, 1);
    int ok = 1;
    for (int i = 0; i < 4 * VIPER_DVD_SECTOR; i++) {
        uint32_t d = 2 * 2048 + i;
        if (bus_read8(bus, 0x4000 + i) != (uint8_t)((d / 2048) + (d % 2048))) {
            ok = 0;
            break;
        }
    }
    printf("pcviper: soc DMA DVD->RAM: %s (%llu sectors, status=0x%02X)\n",
           ok ? "OK" : "FAIL",
           (unsigned long long)viper_soc_dvd_sectors(soc),
           viper_soc_reg_read(soc, VIPER_DMA_STATUS));

    /* memory card slot 0 write/read */
    viper_soc_reg_write(soc, VIPER_MCD0_ADDR, 0x40);
    viper_soc_reg_write(soc, VIPER_MCD0_DATA, 0xA3D2EC01u);
    viper_soc_reg_write(soc, VIPER_MCD0_CTRL, 1);
    viper_soc_reg_write(soc, VIPER_MCD0_CTRL, 2);
    uint32_t mcd = viper_soc_reg_read(soc, VIPER_MCD0_DATA);
    printf("pcviper: soc memcard slot0: 0x%08X %s\n", mcd,
           (mcd == 0xA3D2EC01u) ? "OK" : "FAIL");
}

static void write_ppm(const char* path, const uint32_t* rgb, int w, int h) {
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "main: cannot write %s\n", path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t px[3] = { (uint8_t)(rgb[i] >> 16), (uint8_t)(rgb[i] >> 8),
                          (uint8_t)rgb[i] };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    printf("pcviper: wrote %s (%dx%d)\n", path, w, h);
}

static void voodoo_demo(Voodoo2EC* v) {
    /* upload 16x16 textures into unified SGRAM (away from the framebuffer
     * buffers at 0 / 0x100000 / 0x200000) */
    for (int t = 0; t < 16; t++)
        for (int s = 0; s < 16; s += 2) {
            uint16_t c0 = (s < 8) ? 0xF800u : 0x001Fu;
            uint16_t c1 = ((s + 1) < 8) ? 0xF800u : 0x001Fu;
            voodoo2ec_write(v, 0x800000u + 0x300000u + (uint32_t)(t * 16 + s) * 2,
                            c0 | ((uint32_t)c1 << 16), ~0u);
        }
    for (int k = 0; k < 128; k++)
        voodoo2ec_write(v, 0x800000u + 0x340000u + (uint32_t)k * 4, 0xFFFFFFFFu, ~0u);

    /* configure both TMUs */
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXTUREMODE << 2) | (1u << 12)), 0x10, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TLOD << 2) | (1u << 12)), 4 << 3, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXBASEADDR << 2) | (1u << 12)), 0x300000u, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXTUREMODE << 2) | (2u << 12)), 0x10, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TLOD << 2) | (2u << 12)), 4 << 3, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXBASEADDR << 2) | (2u << 12)), 0x340000u, ~0u);

    /* enable the command FIFO */
    voodoo2ec_reg_write(v, V2_REG_CMDFIFOBASEADDR, 0x00000000u);
    voodoo2ec_reg_write(v, V2_REG_FBIINIT7, 0x00000001u);

    /* clear the back buffer first */
    voodoo2ec_reg_write(v, V2_REG_COLOR1, 0x00000000u);
    voodoo2ec_reg_write(v, V2_REG_FASTFILLCMD, 0);

    /* CMDFIFO packet type 3: multitextured triangle */
    uint32_t cmd = (3u << 6) | (1u << 10) | (1u << 14) | (1u << 15)
                 | (1u << 16) | (1u << 17) | 3u;
    uint32_t words[34];
    words[0] = cmd;
    words[1] = fbits(100.0f); words[2] = fbits(50.0f);
    words[3] = fbits(255.0f); words[4] = fbits(255.0f); words[5] = fbits(255.0f);
    words[6] = fbits(1.0f); words[7] = fbits(0.0f); words[8] = fbits(0.0f);
    words[9] = fbits(1.0f); words[10] = fbits(0.0f); words[11] = fbits(0.0f);
    words[12] = fbits(500.0f); words[13] = fbits(50.0f);
    words[14] = fbits(255.0f); words[15] = fbits(255.0f); words[16] = fbits(255.0f);
    words[17] = fbits(1.0f); words[18] = fbits(16.0f); words[19] = fbits(0.0f);
    words[20] = fbits(1.0f); words[21] = fbits(16.0f); words[22] = fbits(0.0f);
    words[23] = fbits(300.0f); words[24] = fbits(400.0f);
    words[25] = fbits(255.0f); words[26] = fbits(255.0f); words[27] = fbits(255.0f);
    words[28] = fbits(1.0f); words[29] = fbits(8.0f); words[30] = fbits(16.0f);
    words[31] = fbits(1.0f); words[32] = fbits(8.0f); words[33] = fbits(16.0f);

    for (int i = 0; i < 34; i++)
        voodoo2ec_write(v, 0x200000u + (uint32_t)i * 4, words[i], ~0u);

    /* swap, unblank and render */
    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
    voodoo2ec_reg_write(v, V2_REG_FBIINIT1, 0x0128u);

    uint32_t rgb[640 * 480];
    memset(rgb, 0, sizeof(rgb));
    voodoo2ec_update(v, rgb, 640, 480);

    uint32_t left = rgb[200 + 150 * 640];
    uint32_t right = rgb[400 + 150 * 640];
    printf("pcviper: voodoo2ec demo left=0x%08X right=0x%08X\n", left, right);
    printf("pcviper: %s\n",
           (left == 0xFFFF0000u && right == 0xFF0000FFu)
               ? "voodoo2ec multitexture OK"
               : "voodoo2ec multitexture: unexpected");
    write_ppm("voodoo.ppm", rgb, 640, 480);
}

/* ---- host/firmware input block ----
 * Shared with firmware/demo3d.c at physical 0x00000800. The host streams
 * cos/sin of the rotation angles, camera distance and translation; the
 * firmware still does the full FPU geometry. */
#define VIPER_INPUT_ADDR      0x00000800ull
#define VIPER_INPUT_MAGIC     0xA3D2EC01u
#define VIPER_SDL_FB_W        640
#define VIPER_SDL_FB_H        480
#define VIPER_SDL_CYCLES      50000   /* cycles per firmware frame (~43k) */

typedef struct {
    float cy, sy;      /* cos/sin of yaw */
    float cx, sx;      /* cos/sin of pitch */
    float camz;        /* camera distance (zoom) */
    float tx, ty;      /* screen-space translation (pixels) */
    float azimuth;     /* sound source azimuth in degrees */
    uint32_t magic;
} ViperInput;

static void viper_input_write(Bus* bus, const ViperInput* in) {
    const uint32_t* w = (const uint32_t*)in;
    for (int i = 0; i < 9; i++)   /* 8 floats + 1 uint32 = 9 dwords */
        bus_write32(bus, VIPER_INPUT_ADDR + (uint64_t)i * 4, w[i]);
}

/* Headless override: PCVIPER_INPUT_YAW/PITCH/CAMZ/TX/TY/AZIMUTH (env) writes the
 * input block so non-interactive runs (and validate.sh) can exercise the
 * same host->firmware path used by the SDL2 interactive demo. */
static int viper_input_from_env(Bus* bus) {
    const char* yaw = getenv("PCVIPER_INPUT_YAW");
    const char* pitch = getenv("PCVIPER_INPUT_PITCH");
    const char* camz = getenv("PCVIPER_INPUT_CAMZ");
    const char* tx = getenv("PCVIPER_INPUT_TX");
    const char* ty = getenv("PCVIPER_INPUT_TY");
    const char* azimuth = getenv("PCVIPER_INPUT_AZIMUTH");
    if (!yaw && !pitch && !camz && !tx && !ty && !azimuth) return 0;
    ViperInput in;
    in.magic = VIPER_INPUT_MAGIC;
    in.cy = 0.9396926f; in.sy = 0.3420201f;   /* default yaw 20 deg */
    in.cx = 1.0f; in.sx = 0.0f;
    in.camz = 3.0f; in.tx = 0.0f; in.ty = 0.0f;
    in.azimuth = 0.0f;
    if (yaw) {
        in.cy = cosf(strtof(yaw, NULL) * M_PI / 180.0f);
        in.sy = sinf(strtof(yaw, NULL) * M_PI / 180.0f);
    }
    if (pitch) {
        in.cx = cosf(strtof(pitch, NULL) * M_PI / 180.0f);
        in.sx = sinf(strtof(pitch, NULL) * M_PI / 180.0f);
    }
    if (camz) in.camz = strtof(camz, NULL);
    if (tx) in.tx = strtof(tx, NULL);
    if (ty) in.ty = strtof(ty, NULL);
    if (azimuth) in.azimuth = strtof(azimuth, NULL);
    viper_input_write(bus, &in);
    return 1;
}

#ifdef HAVE_SDL2
static int interactive_demo(Bus* bus, Vr5432* cpu, Voodoo2EC* voodoo) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "pcviper: SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win = SDL_CreateWindow(
        "PC-Viper 3D (VR5432 + Voodoo2 EC)", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, VIPER_SDL_FB_W, VIPER_SDL_FB_H,
        SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "pcviper: SDL window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) {
        fprintf(stderr, "pcviper: SDL renderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         VIPER_SDL_FB_W, VIPER_SDL_FB_H);
    if (!tex) {
        fprintf(stderr, "pcviper: SDL texture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    /* initial input: yaw 20 deg (matches the validated static frame) */
    ViperInput in;
    in.cy = 0.9396926f; in.sy = 0.3420201f;
    in.cx = 1.0f; in.sx = 0.0f;
    in.camz = 3.0f; in.tx = 0.0f; in.ty = 0.0f;
    in.azimuth = 0.0f;
    in.magic = VIPER_INPUT_MAGIC;
    viper_input_write(bus, &in);

    static uint32_t rgb[VIPER_SDL_FB_W * VIPER_SDL_FB_H];
    voodoo2ec_update(voodoo, rgb, VIPER_SDL_FB_W, VIPER_SDL_FB_H);

    float yaw_deg = 20.0f, pitch_deg = 0.0f;
    float camz = 3.0f, tx = 0.0f, ty = 0.0f;
    float azimuth = 0.0f;  /* sound source orbits around camera */

    /* optional auto-quit for headless/CI smoke tests */
    long auto_frames = 0;
    const char* af = getenv("PCVIPER_SDL_FRAMES");
    if (af) auto_frames = strtol(af, NULL, 10);

    SDL_GameController* pad = NULL;
    int quit = 0;
    long shown = 0;
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit = 1;
            else if (ev.type == SDL_KEYDOWN &&
                     ev.key.keysym.sym == SDLK_ESCAPE) quit = 1;
            else if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad)
                pad = SDL_GameControllerOpen(ev.cdevice.which);
            else if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad) {
                SDL_JoystickID jid = SDL_JoystickInstanceID(
                    SDL_GameControllerGetJoystick(pad));
                if (ev.cdevice.which == jid) {
                    SDL_GameControllerClose(pad);
                    pad = NULL;
                }
            }
        }

        const Uint8* k = SDL_GetKeyboardState(NULL);
        float dyaw = 0.0f, dpitch = 0.0f;
        float dtx = 0.0f, dty = 0.0f, dcam = 0.0f;
        float daz = 0.0f;
        if (k[SDL_SCANCODE_LEFT])  dyaw   += 1.5f;
        if (k[SDL_SCANCODE_RIGHT]) dyaw   -= 1.5f;
        if (k[SDL_SCANCODE_UP])    dpitch += 1.5f;
        if (k[SDL_SCANCODE_DOWN])  dpitch -= 1.5f;
        if (k[SDL_SCANCODE_A])     dtx    -= 3.0f;
        if (k[SDL_SCANCODE_D])     dtx    += 3.0f;
        if (k[SDL_SCANCODE_W])     dty    -= 3.0f;
        if (k[SDL_SCANCODE_S])     dty    += 3.0f;
        if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_EQUALS]) dcam += 0.05f;
        if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_MINUS])  dcam -= 0.05f;
        if (k[SDL_SCANCODE_Q])     daz    -= 2.0f;  /* azimuth CCW */
        if (k[SDL_SCANCODE_E])     daz    += 2.0f;  /* azimuth CW */

        if (pad) {
            Sint16 lx = SDL_GameControllerGetAxis(
                pad, SDL_CONTROLLER_AXIS_LEFTX);
            Sint16 ly = SDL_GameControllerGetAxis(
                pad, SDL_CONTROLLER_AXIS_LEFTY);
            if (abs(lx) > 8000) dyaw   += (float)lx / 32767.0f * 2.0f;
            if (abs(ly) > 8000) dpitch += (float)ly / 32767.0f * 2.0f;
            Sint16 rt = SDL_GameControllerGetAxis(
                pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            Sint16 lt = SDL_GameControllerGetAxis(
                pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            if (rt > 8000) dcam += (float)rt / 32767.0f * 0.06f;
            if (lt > 8000) dcam -= (float)lt / 32767.0f * 0.06f;
            if (SDL_GameControllerGetButton(
                    pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) dtx -= 3.0f;
            if (SDL_GameControllerGetButton(
                    pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) dtx += 3.0f;
            if (SDL_GameControllerGetButton(
                    pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) dty -= 3.0f;
            if (SDL_GameControllerGetButton(
                    pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) dty += 3.0f;
            if (SDL_GameControllerGetButton(
                    pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) daz -= 2.0f;
            if (SDL_GameControllerGetButton(
                    pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) daz += 2.0f;
        }

        yaw_deg += dyaw;
        pitch_deg += dpitch;
        if (pitch_deg > 89.0f) pitch_deg = 89.0f;
        if (pitch_deg < -89.0f) pitch_deg = -89.0f;
        camz += dcam;
        if (camz < 1.2f) camz = 1.2f;
        if (camz > 30.0f) camz = 30.0f;
        tx += dtx; ty += dty;
        if (tx > 640.0f) tx = 640.0f;
        if (tx < -640.0f) tx = -640.0f;
        if (ty > 480.0f) ty = 480.0f;
        if (ty < -480.0f) ty = -480.0f;
        azimuth += daz;
        if (azimuth > 180.0f) azimuth -= 360.0f;
        if (azimuth < -180.0f) azimuth += 360.0f;

        in.cy = cosf(yaw_deg * M_PI / 180.0f);
        in.sy = sinf(yaw_deg * M_PI / 180.0f);
        in.cx = cosf(pitch_deg * M_PI / 180.0f);
        in.sx = sinf(pitch_deg * M_PI / 180.0f);
        in.camz = camz; in.tx = tx; in.ty = ty;
        in.azimuth = azimuth;
        viper_input_write(bus, &in);

        /* run one frame's worth of the demo firmware; it re-reads the
         * input at the top of every loop iteration, so the next swap
         * already uses the new transform */
        vr5432_run(cpu, cpu->cycles + VIPER_SDL_CYCLES);
        if (voodoo2ec_update(voodoo, rgb, VIPER_SDL_FB_W, VIPER_SDL_FB_H)) {
            SDL_UpdateTexture(tex, NULL, rgb, VIPER_SDL_FB_W * 4);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, NULL, NULL);
            SDL_RenderPresent(ren);
            shown++;
            if (auto_frames > 0 && shown >= auto_frames) quit = 1;
        } else {
            SDL_Delay(1);
        }
    }

    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("pcviper: interactive demo: %ld frames shown\n", shown);
    return 0;
}
#endif /* HAVE_SDL2 */

int main(int argc, char** argv) {
    Bus* bus = bus_create();
    Voodoo2EC* voodoo = voodoo2ec_create();
    AurealA3D* audio = aureal_create();
    ViperSoC* soc = viper_soc_create();
    if (!bus || !voodoo || !audio || !soc) {
        fprintf(stderr, "pcviper: allocation failed\n");
        return 1;
    }
    aureal_set_bus(audio, bus);
    viper_soc_set_bus(soc, bus);

    /* Voodoo2 EC MMIO at physical 0x10000000 (16 MB aperture) */
    bus_register_mmio(bus, VIPER_VOODOO_MMIO, VOODOO2_EC_SGRAM_SIZE, voodoo,
                      voodoo_mmio_read, voodoo_mmio_write);
    /* Aureal A3D MMIO at physical 0x14000000 (16 KB register window) */
    bus_register_mmio(bus, VIPER_AUREAL_MMIO, 0x4000ull, audio,
                      aureal_mmio_read, aureal_mmio_write);
    /* Viper SoC MMIO at physical 0x1E000000 (4 KB) */
    bus_register_mmio(bus, VIPER_PERIPH_MMIO, 0x1000ull, soc,
                      viper_mmio_read, viper_mmio_write);

    /* args: [rom.bin [cycles]] [--sdl|-i] [--cycles N] */
    const char* rom = NULL;
    uint64_t run_cycles = 600000;   /* covers demo3d boot (~500k + a render frame) */
    int interactive = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sdl") == 0 || strcmp(argv[i], "-i") == 0) {
            interactive = 1;
        } else if (strcmp(argv[i], "--cycles") == 0) {
            if (i + 1 < argc) run_cycles = strtoull(argv[++i], NULL, 0);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "pcviper: unknown option '%s'\n", argv[i]);
            return 1;
        } else if (!rom) {
            rom = argv[i];
        } else {
            run_cycles = strtoull(argv[i], NULL, 0);  /* 2nd positional: cycles */
        }
    }

    if (rom) {
        if (bus_load_file(bus, rom, VIPER_ROM_BASE, VIPER_ROM_SIZE) != 0)
            fprintf(stderr, "pcviper: cannot load %s, using built-in boot program\n",
                    rom);
    }
    if (!(bus->rom[0] | bus->rom[1] | bus->rom[2] | bus->rom[3])) {
        Prog boot;
        boot.n = 0;
        build_boot(&boot);
        memcpy(bus->rom, boot.w, (size_t)boot.n * 4);
        printf("pcviper: loaded built-in boot program at 0xBFC00000 "
               "(%d instrs)\n", boot.n);
    }

    Vr5432 cpu;
    vr5432_reset(&cpu, bus);
    printf("pcviper: running VR5432 (reset vector 0x%08llX)\n",
           (unsigned long long)cpu.pc);

    /* execute the boot: the CPU programs the Voodoo, SoC DMA and the
     * Aureal A3D channel entirely through the MIPS MMIO path */
    if (interactive) {
        int rc;
#ifdef HAVE_SDL2
        if (!rom) {
            fprintf(stderr, "pcviper: interactive mode requires a ROM "
                            "(e.g. firmware/demo3d.bin)\n");
            rc = 1;
        } else {
            printf("pcviper: interactive mode (SDL2). Keys: arrows = yaw/pitch, "
                   "WASD = move, Z/X = zoom, ESC = quit.\n");
            rc = interactive_demo(bus, &cpu, voodoo);
        }
#else
        fprintf(stderr, "pcviper: interactive mode needs SDL2 "
                        "(install libsdl2-dev and rebuild)\n");
        rc = 1;
#endif
        bus_destroy(bus);
        voodoo2ec_destroy(voodoo);
        aureal_destroy(audio);
        viper_soc_destroy(soc);
        return rc;
    }

    /* optional headless input override (host -> firmware path) */
    if (viper_input_from_env(bus))
        printf("pcviper: wrote input block from PCVIPER_INPUT_* env\n");

    vr5432_run(&cpu, run_cycles);

    uint32_t color1 = voodoo2ec_reg_read(voodoo, V2_REG_COLOR1);
    printf("pcviper: voodoo2ec color1 via CPU MMIO = 0x%08X\n", color1);
    printf("pcviper: %s\n",
           (color1 == 0x00FF0000u) ? "CPU -> MMIO OK (0xB0000148)"
                                   : "CPU -> MMIO: unexpected value");

    uint32_t post = bus_read32(bus, 0x0000F000ull);
    printf("pcviper: firmware POST code = 0x%08X %s\n", post,
           (post == 0xA3D201FFu) ? "READY (all CPU/FPU/MMIO self-tests OK)"
                                 : "(incomplete)");

    cpu_driven_validate(bus, audio, soc);

    /* if a custom ROM was loaded, show what the CPU rendered on the Voodoo */
    if (argc > 1)
        voodoo_capture(voodoo, "cpu3d.ppm");

    voodoo_demo(voodoo);
    glide_demo(voodoo);
    voodoo_advanced_demo(voodoo);
    soc_demo(soc, bus);
    a3d_demo(audio, bus);
    if (argc > 1)
        a3d_demo3d_audio(audio, bus);

    bus_destroy(bus);
    voodoo2ec_destroy(voodoo);
    aureal_destroy(audio);
    viper_soc_destroy(soc);
    return 0;
}
