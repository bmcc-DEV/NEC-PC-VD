#include "memory_bus.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

MemoryBus::MemoryBus() = default;
MemoryBus::~MemoryBus() {
    for (auto* p : m_owned) delete[] p;
}

void MemoryBus::add_region(uint32_t start, uint32_t end, uint8_t* data, bool readonly) {
    m_regions.push_back({start, end, data, readonly});
    std::sort(m_regions.begin(), m_regions.end(),
        [](auto& a, auto& b) { return a.start < b.start; });
}

void MemoryBus::add_read_handler(uint32_t start, uint32_t end, read_t handler) {
    m_read_handlers.push_back({start, end, std::move(handler)});
}

void MemoryBus::add_write_handler(uint32_t start, uint32_t end, write_t handler) {
    m_write_handlers.push_back({start, end, std::move(handler)});
}

uint8_t* MemoryBus::find_region(uint32_t addr, uint32_t size) {
    for (auto& r : m_regions) {
        // Ensure the full access (addr..addr+size-1) fits inside the region
        if (addr >= r.start && addr <= r.end && addr + size - 1 <= r.end)
            return r.data + (addr - r.start);
    }
    return nullptr;
}

uint32_t MemoryBus::read32(uint32_t addr) {
    for (auto& h : m_read_handlers) {
        if (addr >= h.start && addr <= h.end)
            return h.handler(addr);
    }
    uint8_t* p = find_region(addr, 4);
    if (p) {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
    }
    return 0xFFFFFFFF;
}

uint16_t MemoryBus::read16(uint32_t addr) {
    for (auto& h : m_read_handlers) {
        if (addr >= h.start && addr <= h.end) {
            // MMIO handlers return full dword registers; select the addressed word
            uint32_t v = h.handler(addr);
            return (uint16_t)((v >> ((addr & 2) * 8)) & 0xFFFF);
        }
    }
    uint8_t* p = find_region(addr, 2);
    if (p) return p[0] | (p[1] << 8);
    return 0xFFFF;
}

uint8_t MemoryBus::read8(uint32_t addr) {
    for (auto& h : m_read_handlers) {
        if (addr >= h.start && addr <= h.end) {
            // MMIO handlers return full dword registers; select the addressed byte
            uint32_t v = h.handler(addr);
            return (uint8_t)((v >> ((addr & 3) * 8)) & 0xFF);
        }
    }
    uint8_t* p = find_region(addr, 1);
    if (p) return p[0];
    return 0xFF;
}

void MemoryBus::write32(uint32_t addr, uint32_t data, uint32_t mask) {
    for (auto& h : m_write_handlers) {
        if (addr >= h.start && addr <= h.end) {
            h.handler(addr, data, mask);
            return;
        }
    }
    bool readonly = false;
    for (auto& r : m_regions) {
        if (addr >= r.start && addr <= r.end) {
            readonly = r.readonly;
            break;
        }
    }
    uint8_t* p = find_region(addr, 4);
    if (p && !readonly) {
        if (mask & 0x000000FF) p[0] = (data >> 0) & 0xFF;
        if (mask & 0x0000FF00) p[1] = (data >> 8) & 0xFF;
        if (mask & 0x00FF0000) p[2] = (data >> 16) & 0xFF;
        if (mask & 0xFF000000) p[3] = (data >> 24) & 0xFF;
    }
}

void MemoryBus::write16(uint32_t addr, uint16_t data, uint32_t mask) {
    // Align the dword and shift the mask so the addressed word is updated
    uint32_t shift = (addr & 2) * 8;
    write32(addr & ~3u, (uint32_t)data << shift, (mask & 0xFFFF) << shift);
}

void MemoryBus::write8(uint32_t addr, uint8_t data) {
    // Align the dword and shift the mask so the addressed byte is updated
    uint32_t shift = (addr & 3) * 8;
    write32(addr & ~3u, (uint32_t)data << shift, 0xFF << shift);
}

void MemoryBus::load_rom(const char* path, uint32_t base_addr) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open ROM: %s\n", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    auto* buf = new uint8_t[size];
    fread(buf, 1, size, f);
    fclose(f);
    m_owned.push_back(buf);

    uint32_t end = base_addr + size - 1;
    add_region(base_addr, end, buf, true);
    printf("Loaded ROM: %s (%ld bytes) at 0x%08X-0x%08X\n", path, size, base_addr, end);
}
