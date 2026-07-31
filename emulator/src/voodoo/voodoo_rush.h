#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <functional>
#include "voodoo_defs.h"
#include "voodoo_fifo.h"

class MemoryBus;

namespace voodoo {

class VoodooRushDevice {
public:
    VoodooRushDevice();
    ~VoodooRushDevice();

    void reset();
    void set_memory(MemoryBus* bus) { m_mem = bus; }

    // MMIO access from CPU
    uint32_t read(uint32_t offset);
    void write(uint32_t offset, uint32_t data, uint32_t mem_mask = 0xFFFFFFFF);

    // Frame update: returns 0 if unchanged, 1 if changed
    int update(uint32_t* rgb_buffer, int width, int height);

    // Statistics
    void set_fbmem(int mb) { m_fbmem_mb = mb; }
    void set_tmumem(int mb0, int mb1) { m_tmumem_mb[0] = mb0; m_tmumem_mb[1] = mb1; }

private:
    MemoryBus* m_mem = nullptr;

    // Memory configuration
    int m_fbmem_mb = 4;      // 4MB framebuffer
    int m_tmumem_mb[2] = {4, 0};  // 4MB TMU0, no TMU1

    // Allocated memory
    std::unique_ptr<uint8_t[]> m_memory;
    uint8_t* m_fbram = nullptr;
    uint32_t m_fbmask = 0;
    uint8_t* m_tmumem[2] = {};

    // Register file
    std::array<uint32_t, 512/4> m_regs;
    std::array<uint32_t, 512/4> m_tmu0_regs;

    // FIFOs
    uint32_t m_pci_fifo_mem[128];  // 64 entries * 2 (offset+data)
    PCIFIFO m_pci_fifo;
    MemoryFIFO m_mem_fifo;

    // Stalling
    enum stall_state { NOT_STALLED, STALLED_UNTIL_FIFO_LWM, STALLED_UNTIL_FIFO_EMPTY };
    stall_state m_stall_state = NOT_STALLED;

    // Video state
    uint32_t m_rgboffs[3] = {};
    uint32_t m_auxoffs = ~0;
    int m_frontbuf = 0, m_backbuf = 1;
    bool m_video_changed = false;
    int m_swaps_pending = 0;
    int m_width = 512, m_height = 384;
    int m_rowpixels = 640;
    int m_xoffs = 0, m_yoffs = 0;

    // VBLANK state
    bool m_vblank = false;
    int m_vblank_count = 0;
    int m_vblank_swap = 0, m_vblank_swap_pending = 0;

    // CLUT
    std::array<uint32_t, 33> m_clut;
    bool m_clut_dirty = true;
    std::array<uint32_t, 65536> m_pen;

    // Triangle state
    uint32_t m_init_enable = 0;

    // Helper methods
    void recompute_video_memory();
    uint32_t read_register(int regnum);
    void write_register(int regnum, uint32_t data);
    uint32_t read_status();

    // LFB access
    uint32_t lfb_read(uint32_t offset);
    void lfb_write(uint32_t offset, uint32_t data, uint32_t mem_mask);
    uint16_t* front_buffer();
    uint16_t* back_buffer();
    uint16_t* aux_buffer();
    uint16_t* draw_buffer(int buf);

    // FIFO operations
    void add_to_fifo(uint32_t offset, uint32_t data);
    void flush_fifos();
    bool operation_pending();

    // Rendering helper
    void draw_framebuffer(uint32_t* out, int w, int h);
    void draw_fastfill();
    void draw_triangle();
    uint16_t pack_rgb565(int r, int g, int b);
    uint16_t pack_rgb555(int r, int g, int b);
};

} // namespace voodoo
