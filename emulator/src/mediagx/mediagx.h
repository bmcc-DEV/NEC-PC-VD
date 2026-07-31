#pragma once
#include <cstdint>
#include <array>
#include "display_ctrl.h"
#include "mem_ctrl.h"
#include "bus/memory_bus.h"
#include "bus/pci_bus.h"

class MediagxSoC {
public:
    MediagxSoC(MemoryBus& mem, PciBus& pci);

    void reset();
    void set_ram(uint8_t* ram_base, uint32_t size);

    uint32_t biu_read(int offset);
    void biu_write(int offset, uint32_t data, uint32_t mask = 0xFFFFFFFF);

    DisplayController& display() { return m_display; }
    MemoryController& memory() { return m_memory; }

    uint8_t io_read(int port);
    void io_write(int port, uint8_t data);

    void update_display(uint32_t* fb, int w, int h);

private:
    MemoryBus& m_mem;
    PciBus& m_pci;
    DisplayController m_display;
    MemoryController m_memory;

    std::array<uint32_t, 64> m_biu_regs;
    uint8_t m_config_regs[256]{};
    uint8_t m_config_reg_sel = 0;

    uint8_t* m_ram = nullptr;
    uint32_t m_ram_size = 0;
};
