/*
 * voodoo_adv_test.c - Voodoo2 EC advanced-shading unit tests.
 *
 * Covers the per-pixel pipeline added to voodoo2ec_rasterize():
 *   - Gouraud shading (per-vertex color interpolation)
 *   - distance fog via the 256-entry fogTable (+ linear fog)
 *   - alpha blending (alphaMode: src-over accum)
 *   - single-pass multitexture (TMU0 diffuse * TMU1 lightmap)
 *   - mipmapping with trilinear LOD blending
 */
#include "voodoo2_ec.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_fail = 0;

static void check_cond(const char* name, int cond) {
    if (cond) {
        printf("PASS  %s\n", name);
    } else {
        printf("FAIL  %s\n", name);
        g_fail++;
    }
}

static uint32_t fbits(float v) {
    uint32_t b;
    memcpy(&b, &v, 4);
    return b;
}

/* setup-engine triangle (firmware-style register path). UVs of TMU1 are
 * mirrored from TMU0 (they share the same coordinate range). */
static void tri(Voodoo2EC* v, uint32_t smode,
                float x0, float y0, float w0, uint32_t argb0, float s0, float t0,
                float x1, float y1, float w1, uint32_t argb1, float s1, float t1,
                float x2, float y2, float w2, uint32_t argb2, float s2, float t2) {
    voodoo2ec_reg_write(v, V2_REG_SSETUPMODE, smode);
    struct { float x, y, w; uint32_t argb; float s, t; } vt[3] = {
        { x0, y0, w0, argb0, s0, t0 },
        { x1, y1, w1, argb1, s1, t1 },
        { x2, y2, w2, argb2, s2, t2 },
    };
    voodoo2ec_reg_write(v, V2_REG_SVX, fbits(vt[0].x));
    voodoo2ec_reg_write(v, V2_REG_SVY, fbits(vt[0].y));
    voodoo2ec_reg_write(v, V2_REG_SARGB, vt[0].argb);
    voodoo2ec_reg_write(v, V2_REG_SWB, fbits(1.0f));
    voodoo2ec_reg_write(v, V2_REG_SWTMU0, fbits(vt[0].w));
    voodoo2ec_reg_write(v, V2_REG_SS_W0, fbits(vt[0].s));
    voodoo2ec_reg_write(v, V2_REG_ST_W0, fbits(vt[0].t));
    voodoo2ec_reg_write(v, V2_REG_SWTMU1, fbits(vt[0].w));
    voodoo2ec_reg_write(v, V2_REG_SS_W1, fbits(vt[0].s));
    voodoo2ec_reg_write(v, V2_REG_ST_W1, fbits(vt[0].t));
    voodoo2ec_reg_write(v, V2_REG_SBEGINTRI, 1);
    for (int i = 1; i < 3; i++) {
        voodoo2ec_reg_write(v, V2_REG_SVX, fbits(vt[i].x));
        voodoo2ec_reg_write(v, V2_REG_SVY, fbits(vt[i].y));
        voodoo2ec_reg_write(v, V2_REG_SARGB, vt[i].argb);
        voodoo2ec_reg_write(v, V2_REG_SWB, fbits(1.0f));
        voodoo2ec_reg_write(v, V2_REG_SWTMU0, fbits(vt[i].w));
        voodoo2ec_reg_write(v, V2_REG_SS_W0, fbits(vt[i].s));
        voodoo2ec_reg_write(v, V2_REG_ST_W0, fbits(vt[i].t));
        voodoo2ec_reg_write(v, V2_REG_SWTMU1, fbits(vt[i].w));
        voodoo2ec_reg_write(v, V2_REG_SS_W1, fbits(vt[i].s));
        voodoo2ec_reg_write(v, V2_REG_ST_W1, fbits(vt[i].t));
        voodoo2ec_reg_write(v, V2_REG_SDRAWTRI, 1);
    }
}

static void quad(Voodoo2EC* v, uint32_t smode,
                 float x0, float y0, float w0, uint32_t argb0, float s0, float t0,
                 float x1, float y1, float w1, uint32_t argb1, float s1, float t1,
                 float x2, float y2, float w2, uint32_t argb2, float s2, float t2,
                 float x3, float y3, float w3, uint32_t argb3, float s3, float t3) {
    tri(v, smode, x0, y0, w0, argb0, s0, t0, x1, y1, w1, argb1, s1, t1,
        x2, y2, w2, argb2, s2, t2);
    tri(v, smode, x0, y0, w0, argb0, s0, t0, x2, y2, w2, argb2, s2, t2,
        x3, y3, w3, argb3, s3, t3);
}

#define TEX_PORT 0x800000u
#define LFB      (1u << 22)

static void upload_rgb565(Voodoo2EC* v, uint32_t sgram_off,
                          uint32_t size, uint16_t color) {
    for (uint32_t i = 0; i < size * size; i += 2) {
        uint32_t dword = color | ((uint32_t)color << 16);
        voodoo2ec_write(v, TEX_PORT + sgram_off + i * 2, dword, ~0u);
    }
}

int main(void) {
    Voodoo2EC* v = voodoo2ec_create();
    uint32_t rgb[640 * 480];

    /* 640x480 RGB565, unblanked */
    voodoo2ec_reg_write(v, V2_REG_FBIINIT1, 0x0128u);
    const uint32_t SM_TEX0 = 0x31u, SM_TEX01 = 0xB1u, SM_PLAIN = 0x11u;

    /* ================= Gouraud ================= */
    voodoo2ec_reg_write(v, V2_REG_FBZCOLORPATH, 0u);      /* gouraud */
    voodoo2ec_reg_write(v, V2_REG_COLOR1, 0x00000000u);   /* clear */
    voodoo2ec_reg_write(v, V2_REG_FASTFILLCMD, 0);
    tri(v, SM_PLAIN,
        100, 300, 1.0f, 0xFFFF0000u, 0, 0,
        500, 300, 1.0f, 0xFF0000FFu, 0, 0,
        300, 80, 1.0f, 0xFF00FF00u, 0, 0);
    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
    voodoo2ec_update(v, rgb, 640, 480);
    {
        uint32_t left = rgb[120 + 290 * 640], right = rgb[480 + 290 * 640];
        int lr = (left >> 16) & 0xFF, lb = left & 0xFF;
        int rr = (right >> 16) & 0xFF, rb = right & 0xFF;
        check_cond("gouraud: left vertex red", lr > 200 && lb < 80);
        check_cond("gouraud: right vertex blue", rb > 200 && rr < 80);
    }

    /* ================= Fog (fogTable) ================= */
    voodoo2ec_reg_write(v, V2_REG_FBZCOLORPATH, 2u);      /* constant color1 */
    voodoo2ec_reg_write(v, V2_REG_COLOR1, 0x00FF0000u);   /* red object */
    voodoo2ec_reg_write(v, V2_REG_FOGCOLOR, 0x00FFFFFFu); /* white fog */
    voodoo2ec_reg_write(v, V2_REG_FOGMODE, 0x41u);        /* enable + table */
    for (int i = 0; i < 64; i++) {                        /* linear ramp */
        uint32_t d = (uint32_t)(i * 4) | ((uint32_t)(i * 4 + 1) << 8)
                   | ((uint32_t)(i * 4 + 2) << 16) | ((uint32_t)(i * 4 + 3) << 24);
        voodoo2ec_reg_write(v, V2_REG_FOGTABLE, d);       /* 4 entries each */
    }
    /* quad: w=1.0 near (no fog) -> w=0.02 far (fogged) */
    quad(v, SM_PLAIN,
         80, 60, 1.0f, 0xFFFFFFFFu, 0, 0,
         560, 60, 1.0f, 0xFFFFFFFFu, 0, 0,
         560, 420, 0.02f, 0xFFFFFFFFu, 0, 0,
         80, 420, 0.02f, 0xFFFFFFFFu, 0, 0);
    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
    voodoo2ec_update(v, rgb, 640, 480);
    {
        uint32_t near_px = rgb[320 + 80 * 640], far_px = rgb[320 + 400 * 640];
        int nr = (near_px >> 16) & 0xFF, ng = (near_px >> 8) & 0xFF;
        int fr = (far_px >> 16) & 0xFF, fg = (far_px >> 8) & 0xFF, fb = far_px & 0xFF;
        check_cond("fog: near edge keeps object color", nr > 200 && ng < 60);
        check_cond("fog: far edge reaches fog color", fr > 200 && fg > 200 && fb > 200);
    }

    /* ================= Alpha blend ================= */
    voodoo2ec_reg_write(v, V2_REG_FBZCOLORPATH, 0u);      /* vertex colors */
    voodoo2ec_reg_write(v, V2_REG_FOGMODE, 0u);           /* fog off */
    voodoo2ec_reg_write(v, V2_REG_COLOR1, 0x00000000u);
    voodoo2ec_reg_write(v, V2_REG_FASTFILLCMD, 0);
    quad(v, SM_PLAIN,
         100, 100, 1.0f, 0xFFFF0000u, 0, 0,
         540, 100, 1.0f, 0xFFFF0000u, 0, 0,
         540, 380, 1.0f, 0xFFFF0000u, 0, 0,
         100, 380, 1.0f, 0xFFFF0000u, 0, 0);
    /* blue quad at vertex alpha 128 over the red quad */
    voodoo2ec_reg_write(v, V2_REG_ALPHAMODE,
                        (2u << 0) | (1u << 3) | (0u << 6) | (0u << 10));
    quad(v, SM_PLAIN,
         100, 100, 1.0f, 0x800000FFu, 0, 0,
         540, 100, 1.0f, 0x800000FFu, 0, 0,
         540, 380, 1.0f, 0x800000FFu, 0, 0,
         100, 380, 1.0f, 0x800000FFu, 0, 0);
    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
    voodoo2ec_update(v, rgb, 640, 480);
    {
        uint32_t px = rgb[320 + 240 * 640];
        int r = (px >> 16) & 0xFF, b = px & 0xFF;
        check_cond("alpha: 50% blue over red is purple", r > 90 && r < 170 && b > 90 && b < 170);
    }

    /* ================= Multitexture: diffuse * lightmap ================= */
    voodoo2ec_reg_write(v, V2_REG_FBZCOLORPATH, 0u);
    voodoo2ec_reg_write(v, V2_REG_ALPHAMODE, 0u);
    /* TMU0: 16x16 checkerboard (2-texel cells): white/black */
    for (int t = 0; t < 16; t++)
        for (int s = 0; s < 16; s += 2) {
            uint16_t c0 = (((s >> 1) + (t >> 1)) & 1) ? 0xFFFFu : 0x0000u;
            uint16_t c1 = ((((s + 1) >> 1) + (t >> 1)) & 1) ? 0xFFFFu : 0x0000u;
            voodoo2ec_write(v, TEX_PORT + 0x300000u + (uint32_t)(t * 16 + s) * 2,
                            c0 | ((uint32_t)c1 << 16), ~0u);
        }
    /* TMU1: red lightmap */
    upload_rgb565(v, 0x340000u, 16, 0xF800u);

    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXTUREMODE << 2) | (1u << 12)), 0x10, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TLOD << 2) | (1u << 12)), 4 << 3, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXBASEADDR << 2) | (1u << 12)), 0x300000u, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXTUREMODE << 2) | (2u << 12)), 0x10, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TLOD << 2) | (2u << 12)), 4 << 3, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXBASEADDR << 2) | (2u << 12)), 0x340000u, ~0u);

    voodoo2ec_reg_write(v, V2_REG_COLOR1, 0x00000000u);
    voodoo2ec_reg_write(v, V2_REG_FASTFILLCMD, 0);
    /* quad: s 0..16 texels over 256 px -> cell s=0..1 white, s=2..3 black */
    quad(v, SM_TEX01,
         100, 100, 1.0f, 0xFFFFFFFFu, 0, 0,
         356, 100, 1.0f, 0xFFFFFFFFu, 16, 0,
         356, 300, 1.0f, 0xFFFFFFFFu, 16, 16,
         100, 300, 1.0f, 0xFFFFFFFFu, 0, 16);
    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
    voodoo2ec_update(v, rgb, 640, 480);
    {
        /* cell parity: (s>>1 + t>>1) odd = white texel. At y=150, t~4:
         * s in [0,2) -> black texel, s in [2,4) -> white texel.
         * x=108 -> s=0.5 (black), x=132 -> s=2.03 (white). */
        uint32_t black_cell = rgb[108 + 150 * 640];
        uint32_t white_cell = rgb[132 + 150 * 640];
        int wr = (white_cell >> 16) & 0xFF;
        int br = (black_cell >> 16) & 0xFF, bg = (black_cell >> 8) & 0xFF, bb = black_cell & 0xFF;
        check_cond("multitex: white diffuse * red lightmap = red", wr > 200);
        check_cond("multitex: black diffuse * red lightmap = black",
                   br < 30 && bg < 30 && bb < 30);
    }

    /* ================= Mipmap + trilinear ================= */
    /* chain at SGRAM 0x200000: lod6 (64x64) red, lod5 (32x32) green.
     * s spans 0..256 texels over 400 px -> LOD ~5.36 (between 5 and 6). */
    upload_rgb565(v, 0x200000u, 64, 0xF800u);                       /* red   */
    upload_rgb565(v, 0x200000u + 64u * 64u * 2u, 32, 0x07E0u);      /* green */
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXTUREMODE << 2) | (1u << 12)), 0x10, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TLOD << 2) | (1u << 12)), (6 << 3) | (5 << 8), ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TEXBASEADDR << 2) | (1u << 12)), 0x200000u, ~0u);
    voodoo2ec_write(v, (uint32_t)((V2_REG_TREXINIT0 << 2) | (1u << 12)), 0x03u, ~0u);  /* mipmap + trilinear */

    voodoo2ec_reg_write(v, V2_REG_COLOR1, 0x00000000u);
    voodoo2ec_reg_write(v, V2_REG_FASTFILLCMD, 0);
    quad(v, SM_TEX0,
         100, 60, 1.0f, 0xFFFFFFFFu, 0, 0,
         500, 60, 1.0f, 0xFFFFFFFFu, 256, 0,
         500, 420, 1.0f, 0xFFFFFFFFu, 256, 64,
         100, 420, 1.0f, 0xFFFFFFFFu, 0, 64);
    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
    voodoo2ec_update(v, rgb, 640, 480);
    {
        uint32_t px = rgb[300 + 240 * 640];
        int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
        check_cond("trilinear: blends lod5 green + lod6 red", r > 50 && r < 150 && g > 120 && b < 60);
    }

    /* same geometry without trilinear -> pure lod5 green */
    voodoo2ec_write(v, (uint32_t)((V2_REG_TREXINIT0 << 2) | (1u << 12)), 0x01u, ~0u);
    voodoo2ec_reg_write(v, V2_REG_COLOR1, 0x00000000u);
    voodoo2ec_reg_write(v, V2_REG_FASTFILLCMD, 0);
    quad(v, SM_TEX0,
         100, 60, 1.0f, 0xFFFFFFFFu, 0, 0,
         500, 60, 1.0f, 0xFFFFFFFFu, 256, 0,
         500, 420, 1.0f, 0xFFFFFFFFu, 256, 64,
         100, 420, 1.0f, 0xFFFFFFFFu, 0, 64);
    voodoo2ec_reg_write(v, V2_REG_SWAPBUFFERCMD, 0);
    voodoo2ec_update(v, rgb, 640, 480);
    {
        uint32_t px = rgb[300 + 240 * 640];
        int r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF;
        check_cond("mipmap: nearest lod5 is pure green", r < 30 && g > 200);
    }

    voodoo2ec_destroy(v);
    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
