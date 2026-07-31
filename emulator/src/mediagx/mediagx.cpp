#include "mediagx.h"
#include <cstdio>
#include <cstring>

MediagxSoC::MediagxSoC(MemoryBus& mem, PciBus& pci)
    : m_mem(mem), m_pci(pci) {
    reset();
}

void MediagxSoC::reset() {
    m_biu_regs.fill(0);
    m_config_reg_sel = 0;
    memset(m_config_regs, 0, sizeof(m_config_regs));
    m_display.reset();
    m_memory.reset();
}

void MediagxSoC::set_ram(uint8_t* ram_base, uint32_t size) {
    m_ram = ram_base;
    m_ram_size = size;
    // Framebuffer UMA está em 0x40800000 = RAM offset 0x800000
    m_display.set_framebuffer(ram_base + 0x800000);
}

uint32_t MediagxSoC::biu_read(int offset) {
    return m_biu_regs[offset];
}

void MediagxSoC::biu_write(int offset, uint32_t data, uint32_t mask) {
    if (mask != 0xFFFFFFFF) {
        uint32_t old = m_biu_regs[offset];
        m_biu_regs[offset] = (old & ~mask) | (data & mask);
    } else {
        m_biu_regs[offset] = data;
    }
}

uint8_t MediagxSoC::io_read(int port) {
    switch (port) {
        case 0x22: return m_config_reg_sel;
        case 0x23: return m_config_regs[m_config_reg_sel];
        default: return 0xFF;
    }
}

void MediagxSoC::io_write(int port, uint8_t data) {
    switch (port) {
        case 0x22: m_config_reg_sel = data; break;
        case 0x23: m_config_regs[m_config_reg_sel] = data; break;
    }
}

void MediagxSoC::update_display(uint32_t* fb, int w, int h) {
    m_display.render(fb, w, h);
}
