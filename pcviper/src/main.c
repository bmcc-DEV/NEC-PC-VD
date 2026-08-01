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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIPER_VOODOO_MMIO 0x10000000ull
#define VIPER_VOODOO_KSEG1 0xB0000000ull   /* KSEG1 -> physical 0x10000000 */

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

/* MMIO bridge: adapt the device API to the bus handler signatures */
static uint32_t voodoo_mmio_read(void* ctx, uint64_t offset) {
    return voodoo2ec_read((Voodoo2EC*)ctx, (uint32_t)offset);
}

static void voodoo_mmio_write(void* ctx, uint64_t offset, uint32_t data, uint32_t mask) {
    voodoo2ec_write((Voodoo2EC*)ctx, (uint32_t)offset, data, mask);
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
    if (!bus || !voodoo) {
        fprintf(stderr, "pcviper: allocation failed\n");
        return 1;
    }

    /* Voodoo2 EC MMIO at physical 0x10000000 (16 MB aperture) */
    bus_register_mmio(bus, VIPER_VOODOO_MMIO, VOODOO2_EC_SGRAM_SIZE, voodoo,
                      voodoo_mmio_read, voodoo_mmio_write);

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

    bus_destroy(bus);
    voodoo2ec_destroy(voodoo);
    return 0;
}
