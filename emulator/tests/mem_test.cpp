#include "bus/memory_bus.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

static int g_fail = 0;
static void check(const char* name, uint32_t got, uint32_t want) {
    if (got == want) printf("PASS  %s = 0x%08X\n", name, got);
    else { printf("FAIL  %s = 0x%08X (want 0x%08X)\n", name, got, want); g_fail++; }
}

int main() {
    MemoryBus mem;

    // ---- RAM region (0x1000..0x101F) ----
    static uint8_t ram[0x40];
    memset(ram, 0xEE, sizeof(ram));
    mem.add_region(0x00001000, 0x0000101F, ram, false);

    // ---- MMIO handler: 4 fake dword registers at 0x40008000 ----
    static uint32_t regs[4];
    regs[0] = 0x11223344; regs[1] = 0x55667788; regs[2] = 0x99AABBCC; regs[3] = 0xDDEEFF00;
    mem.add_read_handler(0x40008000, 0x4000800F, [](uint32_t a) -> uint32_t {
        return regs[(a - 0x40008000) / 4];
    });
    mem.add_write_handler(0x40008000, 0x4000800F, [](uint32_t a, uint32_t data, uint32_t mask) {
        int idx = (a - 0x40008000) / 4;
        regs[idx] = (regs[idx] & ~mask) | (data & mask);
    });

    // ---- 1. RAM byte write at odd address must land in the right byte ----
    mem.write8(0x00001001, 0xAB);
    check("RAM write8 odd addr [0x1001]", ram[1], 0xAB);
    check("RAM write8 does not clobber [0x1000]", ram[0], 0xEE);
    check("RAM write8 does not clobber [0x1002]", ram[2], 0xEE);

    // ---- 2. RAM word write at odd address ----
    mem.write16(0x00001002, 0xCDEF, 0xFFFF);
    check("RAM write16 odd addr [0x1002]", (ram[2] | (ram[3] << 8)), 0xCDEF);

    // ---- 3. read8/read16 unaligned from MMIO ----
    check("MMIO read8 byte0", mem.read8(0x40008000), 0x44);
    check("MMIO read8 byte1", mem.read8(0x40008001), 0x33);
    check("MMIO read8 byte3", mem.read8(0x40008003), 0x11);
    check("MMIO read16 word0", mem.read16(0x40008000), 0x3344);
    check("MMIO read16 word2 (reg0 high)", mem.read16(0x40008002), 0x1122);
    check("MMIO read32 reg0", mem.read32(0x40008000), 0x11223344);
    check("MMIO read32 reg3", mem.read32(0x4000800C), 0xDDEEFF00);

    // ---- 4. write8 unaligned into MMIO must hit the addressed byte ----
    mem.write8(0x40008001, 0x99);          // byte1 of reg0
    check("MMIO write8 byte1 reg0", regs[0], 0x11229944);
    mem.write8(0x40008002, 0x77);          // byte2 of reg0
    check("MMIO write8 byte2 reg0", regs[0], 0x11779944);
    mem.write8(0x4000800D, 0x33);          // byte1 of reg3
    check("MMIO write8 byte1 reg3", regs[3], 0xDDEE3300);

    // ---- 5. write16 unaligned into MMIO ----
    regs[1] = 0x00000000;
    mem.write16(0x40008004, 0xBEEF, 0xFFFF);  // word0 of reg1
    check("MMIO write16 word0 reg1", regs[1], 0x0000BEEF);
    regs[2] = 0x00000000;
    mem.write16(0x4000800A, 0xCAFE, 0xFFFF);  // word2 (high word) of reg2
    check("MMIO write16 word2 reg2", regs[2], 0xCAFE0000);

    // ---- 6. OOB guard: read32/write32 at region end must not overflow ----
    mem.write32(0x0000101C, 0xDEADBEEF);  // fits: 0x101C+3 = 0x101F = end
    check("write32 at end", mem.read32(0x0000101C), 0xDEADBEEF);
    mem.write32(0x0000101E, 0x11111111);  // 0x101E+3 = 0x1021 > end -> dropped
    check("write32 past end dropped", ram[0x1E], 0xAD);
    check("read32 past end = 0xFFFFFFFF", mem.read32(0x0000101E), 0xFFFFFFFF);
    check("read8 at exact end", mem.read8(0x0000101F), ram[0x1F]);
    check("read32 at exact end = 0xFFFFFFFF", mem.read32(0x0000101F), 0xFFFFFFFF);

    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
