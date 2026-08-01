/*
 * bus.h - NEC PC-Viper memory bus (MIPS KSEG0/KSEG1 physical map).
 *
 * The VR5432 accesses physical memory through virtual segments:
 *   KUSEG  0x00000000-0x7FFFFFFF  -> physical 0x00000000-0x7FFFFFFF
 *   KSEG0  0x80000000-0x9FFFFFFF  -> physical 0x00000000-0x1FFFFFFF (cached)
 *   KSEG1  0xA0000000-0xBFFFFFFF  -> physical 0x00000000-0x1FFFFFFF (uncached)
 *
 * Physical map (64-bit addresses, little-endian):
 *   0x00000000-0x03FFFFFF   64 MB SDRAM PC133 (128-bit bus)
 *   0x10000000-0x10FFFFFF   16 MB Voodoo2 EC (registers + CMDFIFO + SGRAM)
 *   0x14000000-0x14000FFF    4 KB Aureal A3D audio DSP
 *   0x1E000000-0x1E000FFF    4 KB Viper peripherals (DVD, Ethernet, MemCards)
 *   0x1F000000-0x1F3FFFFF    4 MB Flash
 *   0x1FC00000-0x1FC3FFFF  256 KB boot ROM (reset vector at 0xBFC00000)
 */
#ifndef VIPER_BUS_H
#define VIPER_BUS_H

#include <stdint.h>
#include <stdbool.h>

#define VIPER_RAM_BASE     0x00000000ull
#define VIPER_RAM_SIZE     0x04000000ull    /* 64 MB */
#define VIPER_VOODOO_BASE  0x10000000ull
#define VIPER_VOODOO_SIZE  0x01000000ull    /* 16 MB */
#define VIPER_AUREAL_BASE  0x14000000ull
#define VIPER_AUREAL_SIZE  0x00001000ull
#define VIPER_PERIPH_BASE  0x1E000000ull
#define VIPER_PERIPH_SIZE  0x00001000ull
#define VIPER_FLASH_BASE   0x1F000000ull
#define VIPER_FLASH_SIZE   0x00400000ull    /* 4 MB */
#define VIPER_ROM_BASE     0x1FC00000ull
#define VIPER_ROM_SIZE     0x00040000ull    /* 256 KB */

typedef struct Bus {
    uint8_t* ram;       /* 64 MB SDRAM */
    uint8_t* voodoo;    /* 16 MB SGRAM unificada (stub na fase 1) */
    uint8_t* aureal;    /* 4 KB A3D DSP (stub) */
    uint8_t* periph;    /* 4 KB periféricos (stub) */
    uint8_t* flash;     /* 4 MB flash */
    uint8_t* rom;       /* 256 KB boot ROM */
    bool little_endian;
} Bus;

Bus* bus_create(void);
void bus_destroy(Bus* bus);

/* Translate a virtual address to a physical one. Returns false if unmapped. */
bool bus_translate(uint64_t vaddr, uint64_t* phys);

/* Load a file into a memory region (returns 0 on success). */
int bus_load_file(Bus* bus, const char* path, uint64_t base, uint64_t max_size);

uint8_t  bus_read8 (Bus* bus, uint64_t vaddr);
uint16_t bus_read16(Bus* bus, uint64_t vaddr);
uint32_t bus_read32(Bus* bus, uint64_t vaddr);
uint64_t bus_read64(Bus* bus, uint64_t vaddr);

void bus_write8 (Bus* bus, uint64_t vaddr, uint8_t  data);
void bus_write16(Bus* bus, uint64_t vaddr, uint16_t data);
void bus_write32(Bus* bus, uint64_t vaddr, uint32_t data);
void bus_write64(Bus* bus, uint64_t vaddr, uint64_t data);

#endif /* VIPER_BUS_H */
