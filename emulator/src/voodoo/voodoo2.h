#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include "voodoo_defs.h"
#include "voodoo_fifo.h"
#include "tmu.h"
#include "bus/pci_bus.h"

class MemoryBus;

namespace voodoo {

// 3dfx Voodoo 2 (SST-2) graphics accelerator.
//
// Memory layout of the 4 MB MMIO aperture (byte addresses):
//   0x000000-0x1FFFFF  Register window (register index = (addr>>2)&0xFF,
//                       chip select = (addr>>12)&3)
//   0x200000-0x3FFFFF  CMDFIFO write window (when fbiInit7.cmdfifo_enable)
//   0x400000-0x7FFFFF  Linear framebuffer
//   0x800000-0xBFFFFF  Texture memory port
class Voodoo2Device : public PciDevice {
public:
    Voodoo2Device();
    ~Voodoo2Device() override;

    void reset();
    void set_memory(MemoryBus* bus) { m_mem = bus; }

    // MMIO access (byte addresses within the 4 MB aperture)
    uint32_t read(uint32_t offset);
    void write(uint32_t offset, uint32_t data, uint32_t mem_mask = 0xFFFFFFFF);

    // Frame update: returns 0 if unchanged, 1 if changed
    int update(uint32_t* rgb_buffer, int width, int height);

    // Statistics / configuration
    void set_fbmem(int mb) { m_fbmem_mb = mb; }
    void set_tmumem(int mb0, int mb1) { m_tmumem_mb[0] = mb0; m_tmumem_mb[1] = mb1; }

    // ---- CMDFIFO callbacks ----
    uint32_t cmdfifo_register_w(uint32_t regnum, uint32_t data);
    uint32_t cmdfifo_2d_w(uint32_t index, uint32_t data);
    void cmdfifo_lfb_w(uint32_t offset, uint32_t data);
    void cmdfifo_texture_w(uint32_t offset, uint32_t data);
    void cmdfifo_set_setup_mode(uint32_t value) { m_regs[reg_sSetupMode] = value; }
    void cmdfifo_triangle_vertex(const SetupVertex& v, uint32_t command, uint32_t index);

    // ---- PCI interface ----
    uint32_t config_read(int reg, uint32_t mask) override;
    void config_write(int reg, uint32_t data, uint32_t mask) override;
    uint32_t mmio_read(uint32_t offset) override { return read(offset); }
    void mmio_write(uint32_t offset, uint32_t data, uint32_t mask) override { write(offset, data, mask); }
    const char* name() override { return "3dfx Voodoo 2"; }

private:
    MemoryBus* m_mem = nullptr;

    // Memory configuration
    int m_fbmem_mb = 4;
    int m_tmumem_mb[2] = {4, 4};

    // Allocated memory: framebuffer + 2 TMUs
    std::unique_ptr<uint8_t[]> m_memory;
    uint8_t* m_fbram = nullptr;
    uint32_t m_fbmask = 0;
    uint8_t* m_tmumem[2] = {};

    // Register files
    std::array<uint32_t, 256> m_regs;
    std::array<uint32_t, 64> m_tmu_regs[2];
    TMU m_tmu[2];

    // Command FIFO
    Cmdfifo m_cmdfifo;

    // Triangle setup engine state
    uint32_t m_sverts = 0;
    SetupVertex m_svert[3];

    // Video state
    uint32_t m_rgboffs[3] = {};
    uint32_t m_auxoffs = ~0;
    int m_frontbuf = 0, m_backbuf = 1;
    bool m_video_changed = false;
    int m_swaps_pending = 0;
    int m_rowpixels = 640;
    int m_height = 480;
    int m_xoffs = 0, m_yoffs = 0;

    bool m_vblank = false;
    uint32_t m_init_enable = 0;

    // CLUT
    std::array<uint32_t, 33> m_clut;
    bool m_clut_dirty = true;
    std::array<uint32_t, 65536> m_pen;

    // Helpers
    int chipmask_from_offset(uint32_t offset);
    void write_register(int regnum, int chipmask, uint32_t data, uint32_t mask);
    uint32_t read_register(int regnum, int chipmask);
    void recompute_video_memory();
    void setup_display_registers();

    uint16_t* front_buffer();
    uint16_t* back_buffer();
    uint16_t* draw_buffer(int buf);

    void lfb_write(uint32_t offset, uint32_t data, uint32_t mask);
    uint32_t lfb_read(uint32_t offset);
    void texture_write(uint32_t offset, uint32_t data);

    void draw_fastfill();
    void begin_triangle();
    void draw_triangle_setup();
    void setup_and_draw_triangle();
    void draw_triangle_legacy();

    void draw_framebuffer(uint32_t* out, int w, int h);
    uint16_t pack_rgb565(int r, int g, int b);
    uint16_t pack_rgb555(int r, int g, int b);

    void do_blit();

    // Rasterizer
    void rasterize(const SetupVertex& v0, const SetupVertex& v1, const SetupVertex& v2,
                   bool use_tex0, bool use_tex1);
    uint32_t sample_texel(int tmu, uint32_t fmt, int lod, uint32_t s, uint32_t t, uint32_t texbase);
};

} // namespace voodoo
