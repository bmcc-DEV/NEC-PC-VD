#pragma once
#include <cstdint>
#include <array>

class MemoryController {
public:
    MemoryController();

    void reset();
    uint32_t read(int offset);
    void write(int offset, uint32_t data, uint32_t mask = 0xFFFFFFFF);

    uint32_t get_pal_index() const { return m_pal_index; }
    void set_pal_index(uint32_t i) { m_pal_index = i; }

private:
    std::array<uint32_t, 64> m_regs;
    uint32_t m_pal_index = 0;
};
