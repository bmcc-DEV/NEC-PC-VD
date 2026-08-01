/*
 * viper_test.c - Viper System SoC unit tests.
 *
 * Covers: DVD -> RAM DMA transfer, memory card write/read, flash
 * read/write through the SoC registers, and the Ethernet MAC.
 */
#include "viper_system.h"
#include "bus.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_fail = 0;

static void check(const char* name, uint64_t got, uint64_t want) {
    if (got == want) printf("PASS  %s = 0x%08llX\n", name, (unsigned long long)got);
    else { printf("FAIL  %s = 0x%08llX (want 0x%08llX)\n", name,
                  (unsigned long long)got, (unsigned long long)want); g_fail++; }
}

int main(void) {
    Bus* bus = bus_create();
    ViperSoC* soc = viper_soc_create();
    viper_soc_set_bus(soc, bus);

    check("DVD present", viper_soc_dvd_sectors(soc), 256);
    check("synthetic disk status", viper_soc_reg_read(soc, VIPER_DVD_STATUS) & 2, 2);

    /* ---- DMA: DVD LBA 5 -> RAM 0x1000, 4096 bytes (2 sectors) ---- */
    viper_soc_reg_write(soc, VIPER_DMA_SRC, 5);
    viper_soc_reg_write(soc, VIPER_DMA_DST, 0x1000);
    viper_soc_reg_write(soc, VIPER_DMA_SIZE, 4096);
    viper_soc_reg_write(soc, VIPER_DMA_CTRL, 1);   /* start */
    check("DMA done bit", viper_soc_reg_read(soc, VIPER_DMA_STATUS) & 2, 2);

    int ok = 1;
    for (int i = 0; i < 4096; i++) {
        uint32_t d = 5 * 2048 + i;              /* absolute byte in the DVD */
        uint8_t want = (uint8_t)((d / 2048) + (d % 2048));  /* (sec + i) */
        if (bus_read8(bus, 0x1000 + i) != want) { ok = 0; break; }
    }
    check("DMA transfer content", ok ? 1 : 0, 1);

    /* ---- Memory card slot 0: write then read back ---- */
    viper_soc_reg_write(soc, VIPER_MCD0_ADDR, 0x1000);
    viper_soc_reg_write(soc, VIPER_MCD0_DATA, 0xDEADBEEFu);
    viper_soc_reg_write(soc, VIPER_MCD0_CTRL, 1);   /* write dword */
    const uint8_t* card = viper_soc_memcard(soc, 0);
    check("memcard write bytes", (uint32_t)card[0x1000] |
          ((uint32_t)card[0x1001] << 8) | ((uint32_t)card[0x1002] << 16) |
          ((uint32_t)card[0x1003] << 24), 0xDEADBEEFu);
    viper_soc_reg_write(soc, VIPER_MCD0_CTRL, 2);   /* read dword */
    check("memcard read back", viper_soc_reg_read(soc, VIPER_MCD0_DATA), 0xDEADBEEFu);

    /* ---- Memory card slot 1 independent ---- */
    viper_soc_reg_write(soc, VIPER_MCD1_ADDR, 0);
    viper_soc_reg_write(soc, VIPER_MCD1_DATA, 0xCAFEBABEu);
    viper_soc_reg_write(soc, VIPER_MCD1_CTRL, 1);
    viper_soc_reg_write(soc, VIPER_MCD1_CTRL, 2);
    check("memcard slot1 read back", viper_soc_reg_read(soc, VIPER_MCD1_DATA), 0xCAFEBABEu);

    /* ---- Flash: write + read via SoC registers (bus at 0x1F000000) ---- */
    viper_soc_reg_write(soc, VIPER_FLASH_ADDR, 0x1000);
    viper_soc_reg_write(soc, VIPER_FLASH_DATA, 0x12345678u);
    viper_soc_reg_write(soc, VIPER_FLASH_CTRL, 2);   /* write */
    check("flash bus content", bus_read32(bus, 0x1F001000ull), 0x12345678u);
    viper_soc_reg_write(soc, VIPER_FLASH_CTRL, 1);   /* read */
    check("flash read back", viper_soc_reg_read(soc, VIPER_FLASH_DATA), 0x12345678u);

    /* ---- Ethernet MAC ---- */
    check("eth MAC hi", viper_soc_reg_read(soc, VIPER_ETH_MAC0), 0x00E00000u);
    check("eth MAC lo", viper_soc_reg_read(soc, VIPER_ETH_MAC1), 0x100000u);

    viper_soc_destroy(soc);
    bus_destroy(bus);
    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
