#pragma once
#include <cstdint>
#include "voodoo_defs.h"

namespace voodoo {

class MemoryFIFO {
public:
    MemoryFIFO();
    void configure(uint32_t* base, uint32_t size);
    void reset();
    void add(uint32_t data);
    uint32_t remove();
    bool empty() const { return m_in == m_out; }
    bool configured() const { return m_base != nullptr; }
    int space() const;
    int items() const;

private:
protected:
    uint32_t* m_base = nullptr;
    int m_size = 0;
    int m_in = 0;
    int m_out = 0;
};

class PCIFIFO : public MemoryFIFO {
public:
    PCIFIFO();
    void configure(uint32_t* mem, int size);
    void push(uint32_t offset, uint32_t data);
    uint32_t peek_offset();
    uint32_t peek_data();

private:
    uint32_t* m_mem = nullptr;
};

} // namespace voodoo
