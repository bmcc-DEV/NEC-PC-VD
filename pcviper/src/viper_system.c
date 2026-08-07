/*
 * viper_system.c - Viper System SoC implementation.
 *
 * The model exposes the register-level contract used by the VR5432 through
 * the 0x1E000000 MMIO window: DVD/DMA, two memory cards, flash and Ethernet.
 * Operations are deterministic and complete synchronously, which keeps the
 * emulator useful both for firmware validation and for headless tests.
 */
#include "viper_system.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define VIPER_DVD_DEFAULT_SECTORS 256
#define VIPER_MEMCARD_MASK (VIPER_MEMCARD_SIZE - 1)
#define VIPER_FLASH_BUS 0x1F000000ull
#define VIPER_DMA_START 0x1u
#define VIPER_DMA_FROM_DVD 0x0u
#define VIPER_DMA_TO_DVD 0x2u

struct ViperSoC {
    Bus* bus;
    uint32_t regs[256];
    uint8_t* dvd;
    uint64_t dvd_sectors;
    bool dvd_present;
    uint8_t* memcard[2];
};

static uint32_t merge_mask(uint32_t old, uint32_t data, uint32_t mask) {
    return (old & ~mask) | (data & mask);
}

ViperSoC* viper_soc_create(void) {
    ViperSoC* s = calloc(1, sizeof(ViperSoC));
    if (!s) return NULL;

    for (int i = 0; i < 2; i++) {
        s->memcard[i] = calloc(1, VIPER_MEMCARD_SIZE);
        if (!s->memcard[i]) {
            viper_soc_destroy(s);
            return NULL;
        }
    }

    s->dvd_sectors = VIPER_DVD_DEFAULT_SECTORS;
    s->dvd = calloc(1, VIPER_DVD_DEFAULT_SECTORS * VIPER_DVD_SECTOR);
    if (!s->dvd) {
        viper_soc_destroy(s);
        return NULL;
    }

    for (uint64_t sec = 0; sec < s->dvd_sectors; sec++) {
        for (uint32_t i = 0; i < VIPER_DVD_SECTOR; i++)
            s->dvd[sec * VIPER_DVD_SECTOR + i] = (uint8_t)(sec + i);
    }
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
    if (s) s->bus = bus;
}

void viper_soc_reset(ViperSoC* s) {
    if (!s) return;
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[VIPER_DVD_STATUS] = s->dvd_present ? 0x3u : 0u;
    s->regs[VIPER_ETH_MAC0] = 0x00E00000u;
    s->regs[VIPER_ETH_MAC1] = 0x00100000u;
}

int viper_soc_load_dvd(ViperSoC* s, const char* iso_path) {
    if (!s || !iso_path) return -1;
    FILE* f = fopen(iso_path, "rb");
    if (!f) {
        fprintf(stderr, "viper: cannot open DVD image %s\n", iso_path);
        return -1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }

    uint64_t sectors = (uint64_t)size / VIPER_DVD_SECTOR;
    if (sectors == 0 || sectors > SIZE_MAX / VIPER_DVD_SECTOR) {
        fclose(f);
        return -1;
    }

    uint8_t* image = malloc((size_t)sectors * VIPER_DVD_SECTOR);
    if (!image) {
        fclose(f);
        return -1;
    }
    size_t bytes = (size_t)sectors * VIPER_DVD_SECTOR;
    if (fread(image, 1, bytes, f) != bytes) {
        free(image);
        fclose(f);
        return -1;
    }
    fclose(f);

    free(s->dvd);
    s->dvd = image;
    s->dvd_sectors = sectors;
    s->dvd_present = true;
    s->regs[VIPER_DVD_STATUS] = 0x3u;

    printf("viper: loaded DVD image %s (%llu sectors)\n", iso_path,
           (unsigned long long)sectors);
    return 0;
}

uint64_t viper_soc_dvd_sectors(const ViperSoC* s) {
    return s ? s->dvd_sectors : 0;
}

uint32_t viper_soc_reg_read(ViperSoC* s, int regnum) {
    if (!s || regnum < 0 || regnum >= 256) return 0;
    return s->regs[regnum];
}

static void dma_execute(ViperSoC* s) {
    uint32_t ctrl = s->regs[VIPER_DMA_CTRL];
    if (!(ctrl & VIPER_DMA_START)) return;

    uint32_t src = s->regs[VIPER_DMA_SRC];
    uint32_t dst = s->regs[VIPER_DMA_DST];
    uint32_t size = s->regs[VIPER_DMA_SIZE];
    s->regs[VIPER_DMA_STATUS] = 1; /* busy */

    bool valid = s->bus && s->dvd_present && s->dvd;
    if (ctrl & VIPER_DMA_TO_DVD) {
        /* The current hardware model has a read-only optical disc. */
        valid = false;
    } else if (valid) {
        uint64_t dvd_off = (uint64_t)src * VIPER_DVD_SECTOR;
        uint64_t dvd_bytes = s->dvd_sectors * VIPER_DVD_SECTOR;
        if (dvd_off > dvd_bytes || size > dvd_bytes - dvd_off) valid = false;
        if ((uint64_t)dst >= VIPER_RAM_SIZE ||
            (uint64_t)size > VIPER_RAM_SIZE - (uint64_t)dst) valid = false;

        if (valid) {
            for (uint32_t i = 0; i < size; i++)
                bus_write8(s->bus, (uint64_t)dst + i, s->dvd[dvd_off + i]);
        }
    }

    s->regs[VIPER_DMA_STATUS] = valid ? 2u : 4u; /* done / error */
    s->regs[VIPER_DMA_CTRL] &= ~VIPER_DMA_START;
}

static void memcard_execute(ViperSoC* s, int slot) {
    if (slot < 0 || slot > 1) return;
    int base = (slot == 0) ? VIPER_MCD0_CTRL : VIPER_MCD1_CTRL;
    uint32_t ctrl = s->regs[base];
    uint32_t addr = s->regs[base + 1] & VIPER_MEMCARD_MASK;
    uint8_t* card = s->memcard[slot];

    if (ctrl & 4u) { /* erase 64 KiB block */
        uint32_t block = addr & ~0xFFFFu;
        memset(card + block, 0xFF, 0x10000);
    }
    if (ctrl & 1u) { /* write dword */
        uint32_t data = s->regs[base + 2];
        for (int i = 0; i < 4; i++)
            card[(addr + (uint32_t)i) & VIPER_MEMCARD_MASK] =
                (uint8_t)(data >> (8 * i));
    }
    if (ctrl & 2u) { /* read dword */
        uint32_t data = 0;
        for (int i = 0; i < 4; i++)
            data |= (uint32_t)card[(addr + (uint32_t)i) & VIPER_MEMCARD_MASK] << (8 * i);
        s->regs[base + 2] = data;
    }
    s->regs[base + 3] = 1; /* ready */
}

static void flash_execute(ViperSoC* s) {
    uint32_t ctrl = s->regs[VIPER_FLASH_CTRL];
    uint32_t addr = s->regs[VIPER_FLASH_ADDR] & (uint32_t)(VIPER_FLASH_SIZE - 1);
    uint64_t bus_addr = VIPER_FLASH_BUS + addr;

    if (!s->bus) return;
    if (ctrl & 1u)
        s->regs[VIPER_FLASH_DATA] = bus_read32(s->bus, bus_addr);
    if (ctrl & 2u)
        bus_write32(s->bus, bus_addr, s->regs[VIPER_FLASH_DATA]);
    s->regs[VIPER_FLASH_STATUS] = 1;
}

void viper_soc_reg_write(ViperSoC* s, int regnum, uint32_t data) {
    viper_soc_reg_write_masked(s, regnum, data, 0xFFFFFFFFu);
}

void viper_soc_reg_write_masked(ViperSoC* s, int regnum, uint32_t data, uint32_t mask) {
    if (!s || regnum < 0 || regnum >= 256) return;
    s->regs[regnum] = merge_mask(s->regs[regnum], data, mask);

    switch (regnum) {
    case VIPER_DMA_CTRL:
        if (s->regs[regnum] & VIPER_DMA_START) dma_execute(s);
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
    viper_soc_reg_write_masked(s, (int)((offset >> 2) & 0xFF), data, mask);
}

const uint8_t* viper_soc_memcard(ViperSoC* s, int slot) {
    return (s && (slot == 0 || slot == 1)) ? s->memcard[slot] : NULL;
}
