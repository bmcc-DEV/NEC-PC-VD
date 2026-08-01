#include "mediagx/display_ctrl.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

static int g_fail = 0;
static void check(const char* name, uint32_t got, uint32_t want) {
    if (got == want) printf("PASS  %s = 0x%08X\n", name, got);
    else { printf("FAIL  %s = 0x%08X (want 0x%08X)\n", name, got, want); g_fail++; }
}

// Dword register indices (byte offset / 4)
enum {
    OUT_CFG    = 0x0C / 4,
    FB_ST      = 0x10 / 4,
    LINE_DELTA = 0x24 / 4,
    H_TIM1     = 0x30 / 4,
    V_TIM1     = 0x40 / 4,
    PAL_ADDR   = 0x70 / 4,
    PAL_DATA   = 0x74 / 4,
};

int main() {
    DisplayController dc;
    static uint8_t fb[640 * 480];
    memset(fb, 0, sizeof(fb));
    fb[0] = 5;            // pixel 0 uses palette color 5
    fb[1] = 6;            // pixel 1 uses palette color 6
    dc.set_framebuffer(fb);

    dc.write(OUT_CFG, 0x1, 0xFFFFFFFF);      // 8-bit mode
    dc.write(FB_ST, 0, 0xFFFFFFFF);
    dc.write(LINE_DELTA, 640 / 4, 0xFFFFFFFF); // 640 byte lines
    dc.write(H_TIM1, 639, 0xFFFFFFFF);
    dc.write(V_TIM1, 479, 0xFFFFFFFF);

    // Load color 5: R=0x10, G=0x20, B=0x30
    dc.write(PAL_ADDR, 5, 0xFFFFFFFF);
    dc.write(PAL_DATA, 0x10, 0xFFFFFFFF);
    dc.write(PAL_DATA, 0x20, 0xFFFFFFFF);
    dc.write(PAL_DATA, 0x30, 0xFFFFFFFF);
    check("pal addr auto-advances to 6", dc.read(PAL_ADDR) & 0xFF, 6);

    // Color 6 follows automatically: R=0x40, G=0x50, B=0x60
    dc.write(PAL_DATA, 0x40, 0xFFFFFFFF);
    dc.write(PAL_DATA, 0x50, 0xFFFFFFFF);
    dc.write(PAL_DATA, 0x60, 0xFFFFFFFF);
    check("pal addr auto-advances to 7", dc.read(PAL_ADDR) & 0xFF, 7);

    // Render and check pixel 0 (color 5) and pixel 1 (color 6)
    static uint32_t out[640 * 480];
    dc.render(out, 640, 480);

    // color 5: r=0x10<<2, g=0x20<<2, b=0x30<<2
    check("render color 5", out[0], 0xFF4080C0);
    // color 6: r=0x40<<2, g=0x50<<2, b=0x60<<2
    check("render color 6", out[1], 0xFF014180);

    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
