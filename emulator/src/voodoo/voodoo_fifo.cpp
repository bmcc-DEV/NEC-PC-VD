#include "voodoo_fifo.h"

namespace voodoo {

MemoryFIFO::MemoryFIFO() = default;

void MemoryFIFO::configure(uint32_t* base, uint32_t size) {
    m_base = base;
    m_size = size;
    reset();
}

void MemoryFIFO::reset() {
    m_in = 0;
    m_out = 0;
}

void MemoryFIFO::add(uint32_t data) {
    if (!m_base || m_size == 0) return;
    int next = m_in + 1;
    if (next >= m_size) next = 0;
    if (next != m_out) {
        m_base[m_in] = data;
        m_in = next;
    }
}

uint32_t MemoryFIFO::remove() {
    if (m_out == m_in) return 0xFFFFFFFF;
    int next = m_out + 1;
    if (next >= m_size) next = 0;
    uint32_t data = m_base[m_out];
    m_out = next;
    return data;
}

int MemoryFIFO::space() const {
    int s = m_in - m_out;
    if (s < 0) s += m_size;
    return (m_size - 1) - s;
}

int MemoryFIFO::items() const {
    int s = m_in - m_out;
    if (s < 0) s += m_size;
    return s;
}

PCIFIFO::PCIFIFO() = default;

void PCIFIFO::configure(uint32_t* mem, int size) {
    m_mem = mem;
    MemoryFIFO::configure(mem, size);
}

void PCIFIFO::push(uint32_t offset, uint32_t data) {
    add(offset);
    add(data);
}

uint32_t PCIFIFO::peek_offset() {
    if (empty()) return 0;
    return m_base[m_out];
}

uint32_t PCIFIFO::peek_data() {
    int next = m_out + 1;
    if (next >= m_size) next = 0;
    if (next == m_in) return 0xFFFFFFFF;
    return m_base[next];
}

} // namespace voodoo
