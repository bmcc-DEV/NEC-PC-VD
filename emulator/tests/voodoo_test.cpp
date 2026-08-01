#include "voodoo/voodoo2.h"
#include "bus/memory_bus.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>

using namespace voodoo;

static int g_fail = 0;
static void check(const char* name, uint32_t got, uint32_t want) {
    if (got == want) printf("PASS  %s = 0x%08X\n", name, got);
    else { printf("FAIL  %s = 0x%08X (want 0x%08X)\n", name, got, want); g_fail++; }
}

// Register byte address with optional chip select in bits [13:12]
static uint32_t R(int regnum, int chipsel = 0) {
    return (uint32_t)((regnum << 2) | (chipsel << 12));
}

// CMDFIFO write window word address
static uint32_t F(int word) {
    return 0x200000u + (uint32_t)word * 4;
}

static uint32_t fbits(float v) {
    uint32_t b;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}

int main() {
    MemoryBus mem;
    Voodoo2Device voodoo;
    voodoo.set_memory(&mem);
    // reset() is called by the constructor
    uint32_t rgb[640 * 480];

    // ---- PCI identity ----
    check("PCI vendor id", voodoo.config_read(0x00, ~0) & 0xFFFF, 0x121A);
    check("PCI device id", (voodoo.config_read(0x00, ~0) >> 16) & 0xFFFF, 0x0002);

    // ---- 3.1: cleared output must be transparent ----
    memset(rgb, 0xA5, sizeof(rgb));
    voodoo.update(rgb, 640, 480);
    check("cleared pixel transparent", rgb[0], 0x00000000);

    // ---- register read/write: fbiInit5..7 and command FIFO registers ----
    voodoo.write(R(reg_fbiInit5), 0x00000200, 0xFFFFFFFF);
    check("fbiInit5 write/read", voodoo.read(R(reg_fbiInit5)), 0x00000200);
    voodoo.write(R(reg_fbiInit6), 0x00000001, 0xFFFFFFFF);
    check("fbiInit6 write/read", voodoo.read(R(reg_fbiInit6)), 0x00000001);
    voodoo.write(R(reg_fbiInit7), 0x00000000, 0xFFFFFFFF);
    check("fbiInit7 write/read", voodoo.read(R(reg_fbiInit7)), 0x00000000);

    voodoo.write(R(reg_cmdFifoBaseAddr), 0x00000000, 0xFFFFFFFF);
    check("cmdFifoBaseAddr write/read", voodoo.read(R(reg_cmdFifoBaseAddr)), 0x00000000);
    voodoo.write(R(reg_sSetupMode), 0x00000120, 0xFFFFFFFF);
    check("sSetupMode write/read", voodoo.read(R(reg_sSetupMode)), 0x00000120);
    voodoo.write(R(reg_bltRop), 0x0000CCCC, 0xFFFFFFFF);
    check("bltRop write/read", voodoo.read(R(reg_bltRop)), 0x0000CCCC);

    // ---- CLUT expansion: LFB write + swap + unblank ----
    const uint32_t LFB = 1 << 22;
    voodoo.write(LFB | 0, 0xFFFF, 0xFFFFFFFF);            // white 565 -> back buffer
    voodoo.write(R(reg_swapbufferCMD), 0, 0xFFFFFFFF);     // swap
    voodoo.write(R(reg_fbiInit1), 0x0128, 0xFFFFFFFF);     // unblank (xtiles=20, bit8)
    memset(rgb, 0xA5, sizeof(rgb));
    voodoo.update(rgb, 640, 480);
    check("white LFB pixel via expanded CLUT", rgb[0], 0xFFFFFFFF);

    // ---- fastfill ----
    voodoo.write(R(reg_color1), 0x00FF0000, 0xFFFFFFFF);   // red (ARGB)
    voodoo.write(R(reg_fastfillCMD), 0, 0xFFFFFFFF);       // fill back buffer
    voodoo.write(R(reg_swapbufferCMD), 0, 0xFFFFFFFF);     // swap
    memset(rgb, 0xA5, sizeof(rgb));
    voodoo.update(rgb, 640, 480);
    check("fastfill red pixel", rgb[0], 0xFFFF0000);

    // ---- CMDFIFO setup ----
    voodoo.write(R(reg_cmdFifoBaseAddr), 0x00000000, 0xFFFFFFFF);  // base=0, end=4096
    voodoo.write(R(reg_fbiInit7), 0x00000001, 0xFFFFFFFF);         // enable cmdfifo

    // ---- Packet type 1: sequential register write (color1 = green) ----
    {
        uint32_t target = reg_color1;
        uint32_t cmd = (1u << 16) | (1u << 15) | (target << 3) | 1u;
        voodoo.write(F(0), cmd, 0xFFFFFFFF);
        voodoo.write(F(1), 0x0000FF00, 0xFFFFFFFF);
        check("packet1 color1 written", voodoo.read(R(reg_color1)), 0x0000FF00);
        check("fifo depth drained", voodoo.read(R(reg_cmdFifoDepth)), 0);
    }

    // ---- Packet type 4: masked register writes (color0 + color1) ----
    {
        uint32_t target = reg_color0;
        uint32_t maskfield = 0b11;                    // 2 consecutive registers
        uint32_t cmd = (maskfield << 15) | (target << 3) | 4u;
        voodoo.write(F(2), cmd, 0xFFFFFFFF);
        voodoo.write(F(3), 0x00112233, 0xFFFFFFFF);
        voodoo.write(F(4), 0x44556677, 0xFFFFFFFF);
        check("packet4 color0", voodoo.read(R(reg_color0)), 0x00112233);
        check("packet4 color1", voodoo.read(R(reg_color1)), 0x44556677);
        check("fifo depth drained (p4)", voodoo.read(R(reg_cmdFifoDepth)), 0);
    }

    // ---- Upload textures (16x16) ----
    // TMU0: columns 0-7 red, columns 8-15 blue (RGB565)
    for (int t = 0; t < 16; t++) {
        for (int s = 0; s < 16; s += 2) {
            uint16_t c0 = (s < 8) ? 0xF800 : 0x001F;
            uint16_t c1 = ((s + 1) < 8) ? 0xF800 : 0x001F;
            uint32_t dword = c0 | (c1 << 16);
            voodoo.write(0x800000u + (uint32_t)(t * 16 + s) * 2, dword, 0xFFFFFFFF);
        }
    }
    // TMU1: white (RGB565 0xFFFF)
    for (int k = 0; k < 128; k++)
        voodoo.write(0xA00000u + (uint32_t)k * 4, 0xFFFFFFFF, 0xFFFFFFFF);

    // ---- Configure both TMUs ----
    voodoo.write(R(reg_textureMode, 1), 0x10, 0xFFFFFFFF);  // TMU0: RGB565 (fmt 4)
    voodoo.write(R(reg_tLOD, 1), 4 << 3, 0xFFFFFFFF);       // TMU0: lod 4 (16x16)
    voodoo.write(R(reg_texBaseAddr, 1), 0x00000, 0xFFFFFFFF);
    voodoo.write(R(reg_textureMode, 2), 0x10, 0xFFFFFFFF);  // TMU1
    voodoo.write(R(reg_tLOD, 2), 4 << 3, 0xFFFFFFFF);
    voodoo.write(R(reg_texBaseAddr, 2), 0x00000, 0xFFFFFFFF);

    // ---- Packet type 3: multitextured triangle ----
    {
        // Clear back buffer with black first
        voodoo.write(R(reg_color1), 0x00000000, 0xFFFFFFFF);
        voodoo.write(R(reg_fastfillCMD), 0, 0xFFFFFFFF);

        // Setup flags: RGB(10), W0(14), S0/T0(15), W1(16), S1/T1(17)
        uint32_t cmd = (3u << 6) | (1u << 10) | (1u << 14) | (1u << 15)
                     | (1u << 16) | (1u << 17) | 3u;
        // words per vertex = 2 + 3(RGB) + 1(W0) + 2(S0/T0) + 1(W1) + 2(S1/T1) = 11
        uint32_t words[34];
        words[0] = cmd;
        // v0 = (100,50) white, s0=(0,0) w0=1, s1=(0,0) w1=1
        words[1] = fbits(100.0f); words[2] = fbits(50.0f);
        words[3] = fbits(255.0f); words[4] = fbits(255.0f); words[5] = fbits(255.0f);
        words[6] = fbits(1.0f); words[7] = fbits(0.0f); words[8] = fbits(0.0f);
        words[9] = fbits(1.0f); words[10] = fbits(0.0f); words[11] = fbits(0.0f);
        // v1 = (500,50) white, s0=(16,0) w0=1, s1=(16,0) w1=1
        words[12] = fbits(500.0f); words[13] = fbits(50.0f);
        words[14] = fbits(255.0f); words[15] = fbits(255.0f); words[16] = fbits(255.0f);
        words[17] = fbits(1.0f); words[18] = fbits(16.0f); words[19] = fbits(0.0f);
        words[20] = fbits(1.0f); words[21] = fbits(16.0f); words[22] = fbits(0.0f);
        // v2 = (300,400) white, s0=(8,16) w0=1, s1=(8,16) w1=1
        words[23] = fbits(300.0f); words[24] = fbits(400.0f);
        words[25] = fbits(255.0f); words[26] = fbits(255.0f); words[27] = fbits(255.0f);
        words[28] = fbits(1.0f); words[29] = fbits(8.0f); words[30] = fbits(16.0f);
        words[31] = fbits(1.0f); words[32] = fbits(8.0f); words[33] = fbits(16.0f);

        for (int i = 0; i < 34; i++)
            voodoo.write(F(5 + i), words[i], 0xFFFFFFFF);

        voodoo.write(R(reg_swapbufferCMD), 0, 0xFFFFFFFF);   // swap -> triangle visible
        memset(rgb, 0xA5, sizeof(rgb));
        voodoo.update(rgb, 640, 480);
        // left of center: s ~ 4 -> red; right of center: s ~ 12 -> blue
        check("multitex triangle left red", rgb[200 + 150 * 640], 0xFFFF0000);
        check("multitex triangle right blue", rgb[400 + 150 * 640], 0xFF0000FF);
        check("fifo depth drained (p3)", voodoo.read(R(reg_cmdFifoDepth)), 0);
    }

    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
