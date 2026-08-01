/*
 * main.c - NEC PC-Viper emulator (fase 2: VR5432 + Voodoo2 EC).
 *
 * Boota o núcleo MIPS, acessa o Voodoo2 EC (100 MHz, 16 MB SGRAM unificada)
 * via MMIO do barramento e renderiza um triângulo multitexturizado num
 * arquivo PPM de saída.
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

/* Programa de boot embutido (hand-assembled MIPS IV, little-endian):
 *   lui $8, 0xB000; daddiu $8, $8, 0x148; lui $9, 0x00FF
 *   ori $9, $9, 0; sw $9, 0($8); beq $0,$0,-1; nop
 * Escreve color1 = 0x00FF0000 (vermelho) no registrador do Voodoo2 EC
 * através do barramento MIPS (prova o caminho CPU -> MMIO). */
static const uint32_t s_builtin_boot[] = {
    0x3C08B000u,  /* lui  $8, 0xB000 */
    0x65080148u,  /* daddiu $8, $8, 0x148 */
    0x3C0900FFu,  /* lui  $9, 0x00FF */
    0x35290000u,  /* ori  $9, $9, 0 */
    0xAD090000u,  /* sw   $9, 0($8) */
    0x1000FFFFu,  /* beq  $0, $0, -1 (loop) */
    0x00000000u,  /* nop (delay slot) */
};

static uint32_t fbits(float v) {
    uint32_t b;
    memcpy(&b, &v, 4);
    return b;
}

static void write_ppm(const char* path, const uint32_t* rgb, int w, int h);

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

#ifdef HAVE_SDL2
    if (SDL_Init(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want;
        memset(&want, 0, sizeof(want));
        want.freq = AUREAL_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 1024;
        SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
        if (dev) {
            SDL_QueueAudio(dev, mix, (uint32_t)frames * 4);
            SDL_PauseAudioDevice(dev, 0);
            SDL_Delay((uint32_t)seconds * 1000);
            SDL_CloseAudioDevice(dev);
            SDL_Quit();
            printf("pcviper: played %d s of A3D audio via SDL2\n", seconds);
        } else {
            SDL_Quit();
        }
    }
#endif
    free(mix);
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

    if (argc > 1) {
        if (bus_load_file(bus, argv[1], VIPER_ROM_BASE, VIPER_ROM_SIZE) != 0)
            fprintf(stderr, "pcviper: using built-in boot program\n");
    }
    if (!(bus->rom[0] | bus->rom[1] | bus->rom[2] | bus->rom[3])) {
        memcpy(bus->rom, s_builtin_boot, sizeof(s_builtin_boot));
        printf("pcviper: loaded built-in boot program at 0xBFC00000\n");
    }

    Vr5432 cpu;
    vr5432_reset(&cpu, bus);
    printf("pcviper: running VR5432 (reset vector 0x%08llX)\n",
           (unsigned long long)cpu.pc);

    /* execute the boot: the CPU writes color1 through the MIPS bus */
    uint64_t run_cycles = (argc > 2) ? strtoull(argv[2], NULL, 0) : 300;
    vr5432_run(&cpu, run_cycles);

    uint32_t color1 = voodoo2ec_reg_read(voodoo, V2_REG_COLOR1);
    printf("pcviper: voodoo2ec color1 via CPU MMIO = 0x%08X\n", color1);
    printf("pcviper: %s\n",
           (color1 == 0x00FF0000u) ? "CPU -> MMIO OK (0xB0000148)"
                                   : "CPU -> MMIO: unexpected value");

    voodoo_demo(voodoo);
    glide_demo(voodoo);
    soc_demo(soc, bus);
    a3d_demo(audio, bus);

    bus_destroy(bus);
    voodoo2ec_destroy(voodoo);
    aureal_destroy(audio);
    viper_soc_destroy(soc);
    return 0;
}
