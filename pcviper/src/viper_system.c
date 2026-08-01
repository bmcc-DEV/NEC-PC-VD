/*
 * viper_system.c - Viper System SoC implementation.
 *
 * DMA transfers DVD sectors into the 64 MB SDRAM; memory cards (2x 8 MB)
 * and the 4 MB flash are accessed through command registers. Ethernet is
 * a register stub (MAC + status).
 */
#include "viper_system.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define VIPER_DVD_DEFAULT_SECTORS 256    /* synthetic disk: 512 KB */
#define VIPER_MEMCARD_MASK (VIPER_MEMCARD_SIZE - 1)
#define VIPER_FLASH_BUS 0x1F000000ull

struct ViperSoC {
    Bus* bus;
    uint32_t regs[256];
    uint8_t* dvd;
    uint64_t dvd_sectors;
    bool dvd_present;
    uint8_t* memcard[2];
};

ViperSoC* viper_soc_create(void) {
    ViperSoC* s = calloc(1, sizeof(ViperSoC));
    if (!s) return NULL;
    for (int i = 0; i < 2; i++) {
        s->memcard[i] = calloc(1, VIPER_MEMCARD_SIZE);
        if (!s->memcard[i]) { viper_soc_destroy(s); return NULL; }
    }
    /* synthetic DVD: 256 sectors filled with a recognizable pattern */
    s->dvd_sectors = VIPER_DVD_DEFAULT_SECTORS;
    s->dvd = calloc(1, VIPER_DVD_DEFAULT_SECTORS * VIPER_DVD_SECTOR);
    if (!s->dvd) { viper_soc_destroy(s); return NULL; }
    for (uint64_t sec = 0; sec < s->dvd_sectors; sec++)
        for (int i = 0; i < VIPER_DVD_SECTOR; i++)
            s->dvd[sec * VIPER_DVD_SECTOR + i] = (uint8_t)(sec + i);
    s->dvd_present = true;

    viper_soc_reset(s);
    return s;
}

void viper_soc_destroy(ViperSoC* s) {
    if (!s) return;
    free(s->dvd);
    free(s->memcard[0]);
    free(s->memcard[1]);
    free(s);
}

void viper_soc_set_bus(ViperSoC* s, Bus* bus) {
    s->bus = bus;
}

void viper_soc_reset(ViperSoC* s) {
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[VIPER_DVD_STATUS] = 0x3;                    /* ready + present */
    s->regs[VIPER_ETH_MAC0] = 0x00E00000u;             /* vendor OUI */
    s->regs[VIPER_ETH_MAC1] = 0x100000u;               /* NIC id */
}

int viper_soc_load_dvd(ViperSoC* s, const char* iso_path) {
    FILE* f = fopen(iso_path, "rb");
    if (!f) { fprintf(stderr, "viper: cannot open DVD image %s\n", iso_path); return -1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return -1; }
    uint64_t sectors = (uint64_t)size / VIPER_DVD_SECTOR;
    if (sectors == 0) { fclose(f); return -1; }
    free(s->dvd);
    s->dvd = malloc((size_t)sectors * VIPER_DVD_SECTOR);
    if (!s->dvd) { fclose(f); return -1; }
    if (fread(s->dvd, VIPER_DVD_SECTOR, (size_t)sectors, f) != sectors) {
        fclose(f);
        return -1;
    }
    fclose(f);
    s->dvd_sectors = sectors;
    s->dvd_present = true;
    printf("viper: loaded DVD image %s (%llu sectors)\n", iso_path,
           (unsigned long long)sectors);
    return 0;
}

uint64_t viper_soc_dvd_sectors(const ViperSoC* s) { return s->dvd_sectors; }

uint32_t viper_soc_reg_read(ViperSoC* s, int regnum) {
    if (regnum < 0 || regnum >= 256) return 0;
    return s->regs[regnum];
}

static void dma_execute(ViperSoC* s) {
    uint32_t ctrl = s->regs[VIPER_DMA_CTRL];
    if (!(ctrl & 1)) return;
    uint32_t src = s->regs[VIPER_DMA_SRC];      /* DVD LBA */
    uint32_t dst = s->regs[VIPER_DMA_DST];      /* RAM address */
    uint32_t size = s->regs[VIPER_DMA_SIZE];    /* bytes */
    s->regs[VIPER_DMA_STATUS] |= 1;             /* busy */

    if ((ctrl & 2) == 0) {                       /* DVD -> RAM */
        uint64_t dvd_off = (uint64_t)src * VIPER_DVD_SECTOR;
        if (dvd_off + size <= s->dvd_sectors * VIPER_DVD_SECTOR && s->bus) {
            for (uint32_t i = 0; i < size; i++)
                bus_write8(s->bus, (uint64_t)dst + i, s->dvd[dvd_off + i]);
        }
    }
    s->regs[VIPER_DMA_STATUS] &= ~1u;           /* done */
    s->regs[VIPER_DMA_STATUS] |= 2;
}

static void memcard_execute(ViperSoC* s, int slot) {
    int base = (slot == 0) ? VIPER_MCD0_CTRL : VIPER_MCD1_CTRL;
    uint32_t ctrl = s->regs[base];
    uint32_t addr = s->regs[base + 1] & VIPER_MEMCARD_MASK;
    uint8_t* card = s->memcard[slot];

    if (ctrl & 1) {                             /* write dword */
        uint32_t data = s->regs[base + 2];
        for (int i = 0; i < 4; i++)
            card[addr + i] = (uint8_t)(data >> (8 * i));
    }
    if (ctrl & 2) {                             /* read dword */
        uint32_t data = 0;
        for (int i = 0; i < 4; i++)
            data |= (uint32_t)card[addr + i] << (8 * i);
        s->regs[base + 2] = data;
    }
    if (ctrl & 4) {                             /* erase block (64 KB) */
        uint32_t block = addr & ~0xFFFFu;
        memset(card + block, 0xFF, 0x10000);
    }
    s->regs[base + 3] = 1;                      /* ready */
}

static void flash_execute(ViperSoC* s) {
    uint32_t ctrl = s->regs[VIPER_FLASH_CTRL];
    uint32_t addr = s->regs[VIPER_FLASH_ADDR] & 0x3FFFFF;
    uint64_t bus_addr = VIPER_FLASH_BUS + addr;
    if (!s->bus) return;
    if (ctrl & 1) {                             /* read dword */
        s->regs[VIPER_FLASH_DATA] = bus_read32(s->bus, bus_addr);
    }
    if (ctrl & 2) {                             /* write dword */
        bus_write32(s->bus, bus_addr, s->regs[VIPER_FLASH_DATA]);
    }
    s->regs[VIPER_FLASH_STATUS] = 1;
}

void viper_soc_reg_write(ViperSoC* s, int regnum, uint32_t data) {
    if (regnum < 0 || regnum >= 256) return;
    s->regs[regnum] = data;

    switch (regnum) {
    case VIPER_DMA_CTRL:
        if (data & 1) dma_execute(s);
        break;
    case VIPER_MCD0_CTRL:
        memcard_execute(s, 0);
        break;
    case VIPER_MCD1_CTRL:
        memcard_execute(s, 1);
        break;
    case VIPER_FLASH_CTRL:
        flash_execute(s);
        break;
    default:
        break;
    }
}

uint32_t viper_soc_read(ViperSoC* s, uint32_t offset) {
    return viper_soc_reg_read(s, (int)((offset >> 2) & 0xFF));
}

void viper_soc_write(ViperSoC* s, uint32_t offset, uint32_t data, uint32_t mask) {
    viper_soc_reg_write(s, (int)((offset >> 2) & 0xFF), data);
    (void)mask;
}

const uint8_t* viper_soc_memcard(ViperSoC* s, int slot) {
    return (slot == 0 || slot == 1) ? s->memcard[slot] : NULL;
}
