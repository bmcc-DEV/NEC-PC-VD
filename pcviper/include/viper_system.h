/*
 * viper_system.h - Viper System SoC (DMA, DVD-ROM 6x, Flash, Memory Cards,
 * Ethernet).
 *
 * Register space: 4 KB at physical 0x1E000000 (dword offsets):
 *   DMA:   0x00 CTRL (bit0 start, bit1 direction), 0x04 SRC (DVD LBA),
 *          0x08 DST (RAM address), 0x0C SIZE (bytes), 0x10 STATUS
 *   DVD:   0x20 LBA, 0x24 COUNT, 0x28 STATUS, 0x2C IRQ
 *   MCD0:  0x40 CTRL, 0x44 ADDR, 0x48 DATA, 0x4C STATUS
 *   MCD1:  0x50 CTRL, 0x54 ADDR, 0x58 DATA, 0x5C STATUS
 *   FLASH: 0x80 CTRL, 0x84 ADDR, 0x88 DATA, 0x8C STATUS
 *   ETH:   0x90 CTRL, 0x94 STATUS, 0x98 MAC0, 0x9C MAC1
 */
#ifndef VIPER_SYSTEM_H
#define VIPER_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include "bus.h"

#define VIPER_DVD_SECTOR     2048
#define VIPER_MEMCARD_SIZE   (8 * 1024 * 1024)

enum {
    VIPER_DMA_CTRL   = 0x000 / 4,
    VIPER_DMA_SRC    = 0x004 / 4,
    VIPER_DMA_DST    = 0x008 / 4,
    VIPER_DMA_SIZE   = 0x00C / 4,
    VIPER_DMA_STATUS = 0x010 / 4,

    VIPER_DVD_LBA    = 0x020 / 4,
    VIPER_DVD_COUNT  = 0x024 / 4,
    VIPER_DVD_STATUS = 0x028 / 4,
    VIPER_DVD_IRQ    = 0x02C / 4,

    VIPER_MCD0_CTRL   = 0x040 / 4,
    VIPER_MCD0_ADDR   = 0x044 / 4,
    VIPER_MCD0_DATA   = 0x048 / 4,
    VIPER_MCD0_STATUS = 0x04C / 4,
    VIPER_MCD1_CTRL   = 0x050 / 4,
    VIPER_MCD1_ADDR   = 0x054 / 4,
    VIPER_MCD1_DATA   = 0x058 / 4,
    VIPER_MCD1_STATUS = 0x05C / 4,

    VIPER_FLASH_CTRL   = 0x080 / 4,
    VIPER_FLASH_ADDR   = 0x084 / 4,
    VIPER_FLASH_DATA   = 0x088 / 4,
    VIPER_FLASH_STATUS = 0x08C / 4,

    VIPER_ETH_CTRL   = 0x090 / 4,
    VIPER_ETH_STATUS = 0x094 / 4,
    VIPER_ETH_MAC0   = 0x098 / 4,
    VIPER_ETH_MAC1   = 0x09C / 4,
};

typedef struct ViperSoC ViperSoC;

ViperSoC* viper_soc_create(void);
void viper_soc_destroy(ViperSoC* s);
void viper_soc_set_bus(ViperSoC* s, Bus* bus);
void viper_soc_reset(ViperSoC* s);

int viper_soc_load_dvd(ViperSoC* s, const char* iso_path);
uint64_t viper_soc_dvd_sectors(const ViperSoC* s);

uint32_t viper_soc_read(ViperSoC* s, uint32_t offset);
void viper_soc_write(ViperSoC* s, uint32_t offset, uint32_t data, uint32_t mask);

uint32_t viper_soc_reg_read(ViperSoC* s, int regnum);
void viper_soc_reg_write(ViperSoC* s, int regnum, uint32_t data);
void viper_soc_reg_write_masked(ViperSoC* s, int regnum, uint32_t data, uint32_t mask);
const uint8_t* viper_soc_memcard(ViperSoC* s, int slot);

#endif /* VIPER_SYSTEM_H */
