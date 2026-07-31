#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <array>

class MemoryBus {
public:
    using read_t = std::function<uint32_t(uint32_t addr)>;
    using write_t = std::function<void(uint32_t addr, uint32_t data, uint32_t mask)>;

    struct Region {
        uint32_t start;
        uint32_t end;
        uint8_t* data;
        bool readonly;
    };

    struct ReadHandler {
        uint32_t start;
        uint32_t end;
        read_t handler;
    };

    struct WriteHandler {
        uint32_t start;
        uint32_t end;
        write_t handler;
    };

    MemoryBus();
    ~MemoryBus();

    void add_region(uint32_t start, uint32_t end, uint8_t* data, bool readonly = false);
    void add_read_handler(uint32_t start, uint32_t end, read_t handler);
    void add_write_handler(uint32_t start, uint32_t end, write_t handler);

    uint32_t read32(uint32_t addr);
    uint16_t read16(uint32_t addr);
    uint8_t  read8(uint32_t addr);
    void write32(uint32_t addr, uint32_t data, uint32_t mask = 0xFFFFFFFF);
    void write16(uint32_t addr, uint16_t data, uint32_t mask = 0x0000FFFF);
    void write8(uint32_t addr, uint8_t data);

    void load_rom(const char* path, uint32_t base_addr);

private:
    std::vector<Region> m_regions;
    std::vector<ReadHandler> m_read_handlers;
    std::vector<WriteHandler> m_write_handlers;
    std::vector<uint8_t*> m_owned;

    uint8_t* find_region(uint32_t addr, uint32_t size);
};
