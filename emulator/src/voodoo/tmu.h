#pragma once
#include <cstdint>
#include <array>
#include "voodoo_defs.h"

namespace voodoo {

struct TexelFormat {
    uint32_t rgb332[256];
    uint32_t alpha8[256];
    uint32_t int8[256];
    uint32_t ai44[256];
    uint32_t rgb565[65536];
    uint32_t argb1555[65536];
    uint32_t argb4444[65536];

    TexelFormat();
    uint32_t lookup8(int fmt, uint8_t val) const;
    uint32_t lookup16(int fmt, uint16_t val) const;
};

class TMU {
public:
    TMU();
    void init(int index, uint8_t* ram, uint32_t size);
    void reset();

    void write_reg(int regnum, uint32_t data);
    uint32_t read_reg(int regnum) const;
    uint32_t read_palette(int index) const;
    void write_palette(int index, uint32_t data);
    void write_texture(uint32_t offset, uint32_t data);

    void mark_dirty() { m_dirty = true; }
    bool is_dirty() const { return m_dirty; }
    void clear_dirty() { m_dirty = false; }

    // Texture configuration
    uint32_t texel_lookup(uint32_t s, uint32_t t, int lod) const;

private:
    int m_index = 0;
    uint8_t* m_ram = nullptr;
    uint32_t m_mask = 0;
    bool m_dirty = true;

    std::array<uint32_t, 512/4> m_regs;
    std::array<uint32_t, 512> m_palette;



    // LOD settings
    uint32_t lod_base(int lod) const;
    uint32_t lod_size(int lod) const;
};

} // namespace voodoo
