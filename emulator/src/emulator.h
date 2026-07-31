#pragma once
#include <cstdint>
#include <memory>
#include "cpu/i386.h"
#include "mediagx/mediagx.h"
#include "bus/memory_bus.h"
#include "bus/pci_bus.h"
#include "frontend/sdl_window.h"
#include "voodoo/voodoo_rush.h"

class Emulator {
public:
    Emulator();
    ~Emulator();

    bool init(const char* rom_path);
    void run();
    void shutdown();

private:
    MemoryBus m_mem;
    PciBus m_pci;
    I386Core m_cpu;
    MediagxSoC m_mediagx;
    voodoo::VoodooRushDevice m_voodoo;
    SdlWindow m_window;

    uint8_t* m_ram = nullptr;
    uint32_t m_ram_size = 32 * 1024 * 1024;

    uint32_t m_framebuffer[640 * 480]{};

    void setup_memory_map();
    void setup_io_handlers();
    void step_frame();

    uint8_t io_read_port(uint16_t port);
    void io_write_port(uint16_t port, uint8_t data);
};
