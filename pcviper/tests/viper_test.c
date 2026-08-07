/*
 * viper_test.c - Viper System SoC unit tests.
 *
 * Covers DVD -> RAM DMA transfer, DMA bounds/error handling, memory card
 * write/read/erase, flash access, masked MMIO writes and Ethernet identity.
 */
#include "viper_system.h"
#include "bus.h"
#include <stdio.h>
#include <stdint.h>

static int g_fail = 0;

static void check(const char* name, uint64_t got, uint64_t want) {
    if (got == want) printf("PASS  %s = 0x%08llX\n", name, (unsigned long long)got);
    else {
        printf("FAIL  %s = 0x%08llX (want 0x%08llX)\n", name,
               (unsigned long long)got, (unsigned long long)want);
        g_fail++;
    }
}

int main(void) {
    Bus* bus = bus_create();
    ViperSoC* soc = viper_soc_create();
    if (!bus || !soc) return 2;
    viper_soc_set_bus(soc, bus);

    check("DVD present", viper_soc_dvd_sectors(soc), 256);
    check("synthetic disk status", viper_soc_reg_read(soc, VIPER_DVD_STATUS) & 2, 2);

    /* ---- DMA: DVD LBA 5 -> RAM 0x1000, 4096 bytes (2 sectors) ---- */
    viper_soc_reg_write(soc, VIPER_DMA_SRC, 5);
    viper_soc_reg_write(soc, VIPER_DMA_DST, 0x1000);
    viper_soc_reg_write(soc, VIPER_DMA_SIZE, 4096);
    viper_soc_reg_write(soc, VIPER_DMA_CTRL, 1);
    check("DMA done bit", viper_soc_reg_read(soc, VIPER_DMA_STATUS) & 2, 2);
    check("DMA start self-cleared", viper_soc_reg_read(soc, VIPER_DMA_CTRL) & 1, 0);

    int ok = 1;
    for (int i = 0; i < 4096; i++) {
        uint32_t d = 5 * 2048u + (uint32_t)i;
        uint8_t want = (uint8_t)((d / 2048u) + (d % 2048u));
        if (bus_read8(bus, 0x1000 + i) != want) { ok = 0; break; }
    }
    check("DMA transfer content", ok, 1);

    /* Out-of-range destination must fail without touching RAM. */
    viper_soc_reg_write(soc, VIPER_DMA_SRC, 0);
    viper_soc_reg_write(soc, VIPER_DMA_DST, 0x03FFFF00u);
    viper_soc_reg_write(soc, VIPER_DMA_SIZE, 0x1000u);
    viper_soc_reg_write(soc, VIPER_DMA_CTRL, 1);
    check("DMA bounds error", viper_soc_reg_read(soc, VIPER_DMA_STATUS), 4);

    /* Reverse direction is not implemented by the optical drive model. */
    viper_soc_reg_write(soc, VIPER_DMA_SRC, 0);
    viper_soc_reg_write(soc, VIPER_DMA_DST, 0x2000);
    viper_soc_reg_write(soc, VIPER_DMA_SIZE, 2048);
    viper_soc_reg_write(soc, VIPER_DMA_CTRL, 3);
    check("DMA reverse-direction error", viper_soc_reg_read(soc, VIPER_DMA_STATUS), 4);

    /* ---- Memory card slot 0: write, read and erase ---- */
    viper_soc_reg_write(soc, VIPER_MCD0_ADDR, 0x1000);
    viper_soc_reg_write(soc, VIPER_MCD0_DATA, 0xDEADBEEFu);
    viper_soc_reg_write(soc, VIPER_MCD0_CTRL, 1);
    const uint8_t* card = viper_soc_memcard(soc, 0);
    check("memcard write bytes", (uint32_t)card[0x1000] |
          ((uint32_t)card[0x1001] << 8) | ((uint32_t)card[0x1002] << 16) |
          ((uint32_t)card[0x1003] << 24), 0xDEADBEEFu);
    viper_soc_reg_write(soc, VIPER_MCD0_CTRL, 2);
    check("memcard read back", viper_soc_reg_read(soc, VIPER_MCD0_DATA), 0xDEADBEEFu);
    viper_soc_reg_write(soc, VIPER_MCD0_CTRL, 4);
    check("memcard erase", card[0x1000], 0xFF);

    /* ---- Memory card slot 1 is independent ---- */
    viper_soc_reg_write(soc, VIPER_MCD1_ADDR, 0);
    viper_soc_reg_write(soc, VIPER_MCD1_DATA, 0xCAFEBABEu);
    viper_soc_reg_write(soc, VIPER_MCD1_CTRL, 1);
    viper_soc_reg_write(soc, VIPER_MCD1_CTRL, 2);
    check("memcard slot1 read back", viper_soc_reg_read(soc, VIPER_MCD1_DATA), 0xCAFEBABEu);

    /* ---- Masked register write, matching bus byte/halfword MMIO semantics ---- */
    viper_soc_reg_write(soc, VIPER_ETH_CTRL, 0xAABBCCDDu);
    viper_soc_reg_write_masked(soc, VIPER_ETH_CTRL, 0x00000011u, 0x000000FFu);
    check("masked MMIO write", viper_soc_reg_read(soc, VIPER_ETH_CTRL), 0xAABBCC11u);

    /* ---- Flash: write + read via SoC registers ---- */
    viper_soc_reg_write(soc, VIPER_FLASH_ADDR, 0x1000);
    viper_soc_reg_write(soc, VIPER_FLASH_DATA, 0x12345678u);
    viper_soc_reg_write(soc, VIPER_FLASH_CTRL, 2);
    check("flash bus content", bus_read32(bus, 0x1F001000ull), 0x12345678u);
    viper_soc_reg_write(soc, VIPER_FLASH_CTRL, 1);
    check("flash read back", viper_soc_reg_read(soc, VIPER_FLASH_DATA), 0x12345678u);

    /* ---- Ethernet identity ---- */
    check("eth MAC hi", viper_soc_reg_read(soc, VIPER_ETH_MAC0), 0x00E00000u);
    check("eth MAC lo", viper_soc_reg_read(soc, VIPER_ETH_MAC1), 0x00100000u);

    viper_soc_destroy(soc);
    bus_destroy(bus);
    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
