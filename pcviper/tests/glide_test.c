/*
 * glide_test.c - Glide 2.x API wrapper unit tests.
 *
 * Verifies that textured triangles drawn through the Glide API appear in
 * the Voodoo2 EC framebuffer output (front buffer after a swap + splash).
 */
#include "glide.h"
#include "voodoo2_ec.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_fail = 0;

static void check(const char* name, uint32_t got, uint32_t want) {
    if (got == want) printf("PASS  %s = 0x%08X\n", name, got);
    else { printf("FAIL  %s = 0x%08X (want 0x%08X)\n", name, got, want); g_fail++; }
}

int main(void) {
    GrContext ctx;
    ctx.voodoo = voodoo2ec_create();
    uint32_t rgb[640 * 480];

    grGlideInit();
    grSstSelect(0);
    if (!grSstWinOpen(&ctx, 0, 640, 480, 2, 0)) {
        printf("FAIL  grSstWinOpen\n");
        return 1;
    }

    /* 16x16 RGB565 texture: columns 0-7 red, 8-15 blue */
    uint8_t tex[16 * 16 * 2];
    for (int t = 0; t < 16; t++) {
        for (int s = 0; s < 16; s++) {
            uint16_t c = (s < 8) ? 0xF800u : 0x001Fu;
            tex[(t * 16 + s) * 2 + 0] = (uint8_t)(c & 0xFF);
            tex[(t * 16 + s) * 2 + 1] = (uint8_t)(c >> 8);
        }
    }
    GrTexInfo ti;
    ti.smallLod = 4;
    ti.largeLod = 4;
    ti.aspectRatio = 1.0f;
    ti.format = GR_TEXFMT_RGB_565;
    ti.data = tex;
    grTexUpload(&ctx, GR_TMU0, &ti);
    grTexBind(&ctx, GR_TMU0, &ti);
    grColorCombine(&ctx, 1, 0);    /* use texture */

    grBufferClear(&ctx, 0x00000000u, GR_BUFFER_BACKBUFFER);

    /* textured triangle (screen space, texcoords 0..16) */
    grBeginTriangles(&ctx);
    GrVertex v;
    memset(&v, 0, sizeof(v));
    v.x = 100; v.y = 50; v.w = 1.0f; v.r = 255; v.g = 255; v.b = 255;
    v.s0 = 0; v.t0 = 0;
    grVertex(&ctx, &v);
    v.x = 500; v.y = 50; v.w = 1.0f; v.s0 = 16; v.t0 = 0;
    grVertex(&ctx, &v);
    v.x = 300; v.y = 400; v.w = 1.0f; v.s0 = 8; v.t0 = 16;
    grVertex(&ctx, &v);
    grEndTriangles(&ctx);

    grBufferSwap(&ctx, GR_BUFFER_BACKBUFFER);
    memset(rgb, 0, sizeof(rgb));
    grSplash(&ctx, 0, 0, 640, 480, 0, rgb);

    check("glide triangle left red", rgb[200 + 150 * 640], 0xFFFF0000u);
    check("glide triangle right blue", rgb[400 + 150 * 640], 0xFF0000FFu);

    grSstWinClose(&ctx);
    grGlideShutdown();
    voodoo2ec_destroy(ctx.voodoo);

    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
