/*
 * voodoo_test.c - Voodoo2 EC unit tests (16 MB unified SGRAM).
 *
 * Covers: register read/write (fbiInit5-7, cmdFifo*, setup, blt), CLUT/
 * LFB display path, fastfill, CMDFIFO packet types 1/3/4, and a
 * perspective-correct multitextured triangle (TMU0 pattern * TMU1 white).
 */
#include "voodoo2_ec.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_fail = 0;

static void check(const char* name, uint32_t got, uint32_t want) {
    if (got == want) {
        printf("PASS  %s = 0x%08X\n", name, got);
    } else {
        printf("FAIL  %s = 0x%08X (want 0x%08X)\n", name, got, want);
        g_fail++;
    }
}

/* register byte address with chip select in bits [13:12] */
static uint32_t R(int regnum, int chipsel) {
    return (uint32_t)((regnum << 2) | (chipsel << 12));
}
/* CMDFIFO write window word address */
static uint32_t F(int word) {
    return 0x200000u + (uint32_t)word * 4;
}
static uint32_t fbits(float v) {
    uint32_t b;
    memcpy(&b, &v, 4);
    return b;
}

#define LFB  (1u << 22)
/* texture port base + target SGRAM offset (the port writes SGRAM at
   (offset & 0x7FFFFF), so texBaseAddr must match the SGRAM offset) */
#define TEX0_BASE 0x300000u
#define TEX1_BASE 0x340000u
#define TEX_PORT  0x800000u
#define TEX0(ofs) (TEX_PORT + TEX0_BASE + (ofs))
#define TEX1(ofs) (TEX_PORT + TEX1_BASE + (ofs))

int main(void) {
    Voodoo2EC* v = voodoo2ec_create();
    uint32_t rgb[640 * 480];

    check("sgram size 16 MB", voodoo2ec_sgram_size(v), 0x01000000u);

    /* ---- cleared output must be transparent (blanked) ---- */
    memset(rgb, 0xA5, sizeof(rgb));
    voodoo2ec_update(v, rgb, 640, 480);
    check("cleared pixel transparent", rgb[0], 0x00000000u);

    /* ---- register read/write: fbiInit5..7 and FIFO/setup/blt regs ---- */
    voodoo2ec_reg_write(v, V2_REG_FBIINIT5, 0x00000200u);
    check("fbiInit5 write/read", voodoo2ec_reg_read(v, V2_REG_FBIINIT5), 0x00000200u);
    voodoo2ec_reg_write(v, V2_REG_FBIINIT6, 0x00000001u);
    check("fbiInit6 write/read", voodoo2ec_reg_read(v, V2_REG_FBIINIT6), 0x00000001u);
    voodoo2ec_reg_write(v, V2_REG_FBIINIT7, 0x00000000u);
    check("fbiInit7 write/read", voodoo2ec_reg_read(v, V2_REG_FBIINIT7), 0x00000000u);
    voodoo2ec_reg_write(v, V2_REG_CMDFIFOBASEADDR, 0x00000000u);
    check("cmdFifoBaseAddr write/read", voodoo2ec_reg_read(v, V2_REG_CMDFIFOBASEADDR), 0);
    voodoo2ec_reg_write(v, V2_REG_SSETUPMODE, 0x00000120u);
    check("sSetupMode write/read", voodoo2ec_reg_read(v, V2_REG_SSETUPMODE), 0x00000120u);
    voodoo2ec_reg_write(v, V2_REG_BLTROP, 0x0000CCCCu);
    check("bltRop write/read", voodoo2ec_reg_read(v, V2_REG_BLTROP), 0x0000CCCCu);

    /* ---- CLUT expansion: LFB write + swap + unblank ---- */
    voodoo2ec_write(v, LFB | 0, 0xFFFF, 0xFFFFFFFF);        /* white -> back */
    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);        /* swap */
    voodoo2ec_reg_write(v, V2_REG_FBIINIT1, 0x0128u);       /* unblank */
    memset(rgb, 0xA5, sizeof(rgb));
    voodoo2ec_update(v, rgb, 640, 480);
    check("white LFB pixel via expanded CLUT", rgb[0], 0xFFFFFFFFu);

    /* ---- fastfill ---- */
    voodoo2ec_reg_write(v, V2_REG_COLOR1, 0x00FF0000u);     /* red */
    voodoo2ec_reg_write(v, V2_REG_FASTFILLCMD, 0);
    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
    memset(rgb, 0xA5, sizeof(rgb));
    voodoo2ec_update(v, rgb, 640, 480);
    check("fastfill red pixel", rgb[0], 0xFFFF0000u);

    /* ---- CMDFIFO setup ---- */
    voodoo2ec_reg_write(v, V2_REG_CMDFIFOBASEADDR, 0x00000000u);  /* base 0, end 4096 */
    voodoo2ec_reg_write(v, V2_REG_FBIINIT7, 0x00000001u);         /* enable */

    /* ---- Packet type 1: sequential register write (color1 = green) ---- */
    {
        uint32_t target = V2_REG_COLOR1;
        voodoo2ec_write(v, F(0), (1u << 16) | (1u << 15) | (target << 3) | 1u, ~0u);
        voodoo2ec_write(v, F(1), 0x0000FF00u, ~0u);
        check("packet1 color1 written", voodoo2ec_reg_read(v, V2_REG_COLOR1), 0x0000FF00u);
        check("fifo depth drained", voodoo2ec_fifo_depth(v), 0);
    }

    /* ---- Packet type 4: masked register writes (color0 + color1) ---- */
    {
        uint32_t target = V2_REG_COLOR0;
        voodoo2ec_write(v, F(2), (0b11u << 15) | (target << 3) | 4u, ~0u);
        voodoo2ec_write(v, F(3), 0x00112233u, ~0u);
        voodoo2ec_write(v, F(4), 0x44556677u, ~0u);
        check("packet4 color0", voodoo2ec_reg_read(v, V2_REG_COLOR0), 0x00112233u);
        check("packet4 color1", voodoo2ec_reg_read(v, V2_REG_COLOR1), 0x44556677u);
        check("fifo depth drained (p4)", voodoo2ec_fifo_depth(v), 0);
    }

    /* ---- Upload 16x16 textures into unified SGRAM ----
     * TMU0 at SGRAM 0x100000: columns 0-7 red, 8-15 blue (RGB565)
     * TMU1 at SGRAM 0x140000: white (RGB565)                       */
    for (int t = 0; t < 16; t++) {
        for (int s = 0; s < 16; s += 2) {
            uint16_t c0 = (s < 8) ? 0xF800u : 0x001Fu;
            uint16_t c1 = ((s + 1) < 8) ? 0xF800u : 0x001Fu;
            uint32_t dword = c0 | ((uint32_t)c1 << 16);
            voodoo2ec_write(v, TEX0((uint32_t)(t * 16 + s) * 2), dword, ~0u);
        }
    }
    for (int k = 0; k < 128; k++)
        voodoo2ec_write(v, TEX1((uint32_t)k * 4), 0xFFFFFFFFu, ~0u);

    /* ---- Configure both TMUs (chipsel 1 = TMU0, 2 = TMU1) ---- */
    voodoo2ec_write(v, R(V2_REG_TEXTUREMODE, 1), 0x10, ~0u);   /* RGB565 */
    voodoo2ec_write(v, R(V2_REG_TLOD, 1), 4 << 3, ~0u);        /* lod 4 (16x16) */
    voodoo2ec_write(v, R(V2_REG_TEXBASEADDR, 1), TEX0_BASE, ~0u);
    voodoo2ec_write(v, R(V2_REG_TEXTUREMODE, 2), 0x10, ~0u);
    voodoo2ec_write(v, R(V2_REG_TLOD, 2), 4 << 3, ~0u);
    voodoo2ec_write(v, R(V2_REG_TEXBASEADDR, 2), TEX1_BASE, ~0u);

    /* ---- Packet type 3: multitextured triangle ---- */
    {
        voodoo2ec_reg_write(v, V2_REG_COLOR1, 0x00000000u);   /* clear back */
        voodoo2ec_reg_write(v, V2_REG_FASTFILLCMD, 0);

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
            voodoo2ec_write(v, F(5 + i), words[i], ~0u);

        voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
        memset(rgb, 0xA5, sizeof(rgb));
        voodoo2ec_update(v, rgb, 640, 480);
        check("multitex triangle left red", rgb[200 + 150 * 640], 0xFFFF0000u);
        check("multitex triangle right blue", rgb[400 + 150 * 640], 0xFF0000FFu);
        check("fifo depth drained (p3)", voodoo2ec_fifo_depth(v), 0);
    }

    voodoo2ec_destroy(v);
    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
