#include "mem_ctrl.h"
#include <cstdio>

MemoryController::MemoryController() {
    reset();
}

void MemoryController::reset() {
    m_regs.fill(0);
    m_pal_index = 0;
}

uint32_t MemoryController::read(int offset) {
    return m_regs[offset];
}

void MemoryController::write(int offset, uint32_t data, uint32_t mask) {
    if (offset == 0x20/4) {
        // Palette/memory config access
        m_pal_index = data;
        m_regs[offset] = data;
        return;
    }

    if (mask != 0xFFFFFFFF) {
        uint32_t old = m_regs[offset];
        m_regs[offset] = (old & ~mask) | (data & mask);
    } else {
        m_regs[offset] = data;
    }
}
