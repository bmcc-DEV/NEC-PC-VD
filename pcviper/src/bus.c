/*
 * bus.c - NEC PC-Viper memory bus implementation.
 */
#include "bus.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

Bus* bus_create(void) {
    Bus* bus = calloc(1, sizeof(Bus));
    if (!bus) return NULL;
    bus->little_endian = true;

    bus->ram = calloc(1, VIPER_RAM_SIZE);
    bus->voodoo = calloc(1, VIPER_VOODOO_SIZE);
    bus->aureal = calloc(1, VIPER_AUREAL_SIZE);
    bus->periph = calloc(1, VIPER_PERIPH_SIZE);
    bus->flash = calloc(1, VIPER_FLASH_SIZE);
    bus->rom = calloc(1, VIPER_ROM_SIZE);

    if (!bus->ram || !bus->voodoo || !bus->aureal || !bus->periph ||
        !bus->flash || !bus->rom) {
        bus_destroy(bus);
        return NULL;
    }
    return bus;
}

void bus_destroy(Bus* bus) {
    if (!bus) return;
    free(bus->ram);
    free(bus->voodoo);
    free(bus->aureal);
    free(bus->periph);
    free(bus->flash);
    free(bus->rom);
    free(bus);
}

bool bus_translate(uint64_t vaddr, uint64_t* phys) {
    if (vaddr < 0x80000000ull) {
        *phys = vaddr;                 /* KUSEG */
        return true;
    }
    if (vaddr < 0xA0000000ull) {
        *phys = vaddr - 0x80000000ull; /* KSEG0 (cached) */
        return true;
    }
    if (vaddr < 0xC0000000ull) {
        *phys = vaddr - 0xA0000000ull; /* KSEG1 (uncached) */
        return true;
    }
    *phys = 0;
    return false;                      /* KSEG2 / supervisor segments */
}

int bus_load_file(Bus* bus, const char* path, uint64_t base, uint64_t max_size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "bus: failed to open %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return -1; }
    if ((uint64_t)size > max_size) size = (long)max_size;

    uint8_t* dest = NULL;
    if (base < VIPER_RAM_BASE + VIPER_RAM_SIZE)
        dest = bus->ram + base - VIPER_RAM_BASE;
    else if (base < VIPER_ROM_BASE + VIPER_ROM_SIZE && base >= VIPER_ROM_BASE)
        dest = bus->rom + (base - VIPER_ROM_BASE);
    else if (base < VIPER_FLASH_BASE + VIPER_FLASH_SIZE && base >= VIPER_FLASH_BASE)
        dest = bus->flash + (base - VIPER_FLASH_BASE);

    if (!dest) { fclose(f); return -1; }
    if (fread(dest, 1, (size_t)size, f) != (size_t)size) { fclose(f); return -1; }
    fclose(f);
    printf("bus: loaded %s (%ld bytes) at 0x%08llX\n", path, size,
           (unsigned long long)base);
    return 0;
}

/* ------- physical access dispatch ------- */

static const BusMmio* find_mmio(Bus* bus, uint64_t phys, uint64_t size) {
    for (int i = 0; i < bus->mmio_count; i++) {
        const BusMmio* m = &bus->mmio[i];
        if (phys >= m->base && phys + size <= m->base + m->size)
            return m;
    }
    return NULL;
}

int bus_register_mmio(Bus* bus, uint64_t base, uint64_t size, void* ctx,
                      bus_mmio_read_fn read_fn, bus_mmio_write_fn write_fn) {
    if (bus->mmio_count >= VIPER_MAX_MMIO) return -1;
    BusMmio* m = &bus->mmio[bus->mmio_count++];
    m->ctx = ctx;
    m->base = base;
    m->size = size;
    m->read = read_fn;
    m->write = write_fn;
    return 0;
}

static uint8_t* region_ptr(Bus* bus, uint64_t phys, uint64_t size) {
    if (phys + size <= VIPER_RAM_BASE + VIPER_RAM_SIZE)
        return bus->ram + phys - VIPER_RAM_BASE;
    if (phys >= VIPER_VOODOO_BASE && phys + size <= VIPER_VOODOO_BASE + VIPER_VOODOO_SIZE)
        return bus->voodoo + (phys - VIPER_VOODOO_BASE);
    if (phys >= VIPER_AUREAL_BASE && phys + size <= VIPER_AUREAL_BASE + VIPER_AUREAL_SIZE)
        return bus->aureal + (phys - VIPER_AUREAL_BASE);
    if (phys >= VIPER_PERIPH_BASE && phys + size <= VIPER_PERIPH_BASE + VIPER_PERIPH_SIZE)
        return bus->periph + (phys - VIPER_PERIPH_BASE);
    if (phys >= VIPER_FLASH_BASE && phys + size <= VIPER_FLASH_BASE + VIPER_FLASH_SIZE)
        return bus->flash + (phys - VIPER_FLASH_BASE);
    if (phys >= VIPER_ROM_BASE && phys + size <= VIPER_ROM_BASE + VIPER_ROM_SIZE)
        return bus->rom + (phys - VIPER_ROM_BASE);
    return NULL;
}

static inline uint64_t rd_le(const uint8_t* p, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

static inline void wr_le(uint8_t* p, int n, uint64_t v) {
    for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (i * 8));
}

uint8_t bus_read8(Bus* bus, uint64_t vaddr) {
    uint64_t phys;
    if (!bus_translate(vaddr, &phys)) return 0xFF;
    const BusMmio* m = find_mmio(bus, phys, 1);
    if (m) {
        uint32_t v = m->read(m->ctx, phys - m->base);
        return (uint8_t)((v >> ((phys & 3) * 8)) & 0xFF);
    }
    uint8_t* p = region_ptr(bus, phys, 1);
    return p ? p[0] : 0xFF;
}

uint16_t bus_read16(Bus* bus, uint64_t vaddr) {
    uint64_t phys;
    if (!bus_translate(vaddr, &phys)) return 0xFFFF;
    const BusMmio* m = find_mmio(bus, phys, 2);
    if (m) {
        uint32_t v = m->read(m->ctx, phys - m->base);
        return (uint16_t)((v >> ((phys & 2) * 8)) & 0xFFFF);
    }
    uint8_t* p = region_ptr(bus, phys, 2);
    return p ? (uint16_t)rd_le(p, 2) : 0xFFFF;
}

uint32_t bus_read32(Bus* bus, uint64_t vaddr) {
    uint64_t phys;
    if (!bus_translate(vaddr, &phys)) return 0xFFFFFFFF;
    const BusMmio* m = find_mmio(bus, phys, 4);
    if (m) return m->read(m->ctx, phys - m->base);
    uint8_t* p = region_ptr(bus, phys, 4);
    return p ? (uint32_t)rd_le(p, 4) : 0xFFFFFFFF;
}

uint64_t bus_read64(Bus* bus, uint64_t vaddr) {
    uint64_t phys;
    if (!bus_translate(vaddr, &phys)) return 0xFFFFFFFFFFFFFFFFull;
    uint8_t* p = region_ptr(bus, phys, 8);
    return p ? rd_le(p, 8) : 0xFFFFFFFFFFFFFFFFull;
}

void bus_write8(Bus* bus, uint64_t vaddr, uint8_t data) {
    uint64_t phys;
    if (!bus_translate(vaddr, &phys)) return;
    const BusMmio* m = find_mmio(bus, phys, 1);
    if (m) {
        uint32_t shift = (phys & 3) * 8;
        m->write(m->ctx, (phys & ~3ull) - m->base,
                 (uint32_t)data << shift, 0xFFu << shift);
        return;
    }
    uint8_t* p = region_ptr(bus, phys, 1);
    if (p) p[0] = data;
}

void bus_write16(Bus* bus, uint64_t vaddr, uint16_t data) {
    uint64_t phys;
    if (!bus_translate(vaddr, &phys)) return;
    const BusMmio* m = find_mmio(bus, phys, 2);
    if (m) {
        uint32_t shift = (phys & 2) * 8;
        m->write(m->ctx, (phys & ~3ull) - m->base,
                 (uint32_t)data << shift, 0xFFFFu << shift);
        return;
    }
    uint8_t* p = region_ptr(bus, phys, 2);
    if (p) wr_le(p, 2, data);
}

void bus_write32(Bus* bus, uint64_t vaddr, uint32_t data) {
    uint64_t phys;
    if (!bus_translate(vaddr, &phys)) return;
    const BusMmio* m = find_mmio(bus, phys, 4);
    if (m) {
        m->write(m->ctx, phys - m->base, data, 0xFFFFFFFF);
        return;
    }
    uint8_t* p = region_ptr(bus, phys, 4);
    if (p) wr_le(p, 4, data);
}

void bus_write64(Bus* bus, uint64_t vaddr, uint64_t data) {
    uint64_t phys;
    if (!bus_translate(vaddr, &phys)) return;
    uint8_t* p = region_ptr(bus, phys, 8);
    if (p) wr_le(p, 8, data);
}
