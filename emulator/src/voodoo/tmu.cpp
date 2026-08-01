#include "tmu.h"

namespace voodoo {

// Expand color components from N bits to 8 bits
static inline uint8_t expand_n(uint32_t v, int n) {
    if (n == 8) return v & 0xFF;
    if (n == 6) return (v << 2) | (v >> 4);
    if (n == 5) return (v << 3) | (v >> 2);
    if (n == 4) return (v << 4) | v;
    if (n == 3) return (v << 5) | (v << 2) | (v >> 1);
    return 0;
}

TexelFormat::TexelFormat() {
    // 8-bit RGB 3-3-2
    for (int i = 0; i < 256; i++) {
        rgb332[i] = 0xFF000000
            | (expand_n((i >> 5) & 7, 3) << 16)
            | (expand_n((i >> 2) & 7, 3) << 8)
            | (expand_n(i & 3, 2));
    }

    // 8-bit alpha
    for (int i = 0; i < 256; i++)
        alpha8[i] = (i << 24) | 0xFFFFFF;

    // 8-bit intensity
    for (int i = 0; i < 256; i++)
        int8[i] = 0xFF000000 | (i << 16) | (i << 8) | i;

    // 8-bit alpha-intensity 4-4
    for (int i = 0; i < 256; i++)
        ai44[i] = (expand_n((i >> 4) & 0xF, 4) << 24)
                | (expand_n(i & 0xF, 4) << 16)
                | (expand_n(i & 0xF, 4) << 8)
                | expand_n(i & 0xF, 4);

    // 16-bit RGB 5-6-5
    for (int i = 0; i < 65536; i++) {
        rgb565[i] = 0xFF000000
            | (expand_n((i >> 11) & 0x1F, 5) << 16)
            | (expand_n((i >> 5) & 0x3F, 6) << 8)
            | (expand_n(i & 0x1F, 5));
    }

    // 16-bit ARGB 1-5-5-5
    for (int i = 0; i < 65536; i++) {
        argb1555[i] = (expand_n((i >> 15) & 1, 1) << 24)
            | (expand_n((i >> 10) & 0x1F, 5) << 16)
            | (expand_n((i >> 5) & 0x1F, 5) << 8)
            | (expand_n(i & 0x1F, 5));
    }

    // 16-bit ARGB 4-4-4-4
    for (int i = 0; i < 65536; i++) {
        argb4444[i] = (expand_n((i >> 12) & 0xF, 4) << 24)
            | (expand_n((i >> 8) & 0xF, 4) << 16)
            | (expand_n((i >> 4) & 0xF, 4) << 8)
            | (expand_n(i & 0xF, 4));
    }
}

TMU::TMU() = default;

void TMU::init(uint8_t* ram, uint32_t size) {
    m_ram = ram;
    m_mask = size - 1;
}

void TMU::reset() {
}

const TexelFormat& TMU::tables() {
    static TexelFormat fmt;
    return fmt;
}

uint32_t TMU::texel_lookup(uint32_t fmt, int lod, uint32_t s, uint32_t t, uint32_t texbase) const {
    if (!m_ram) return 0xFF000000;

    // Texture dimensions are 2^lod texels per side; the base holds the byte
    // address of LOD 0. Coordinates wrap within the current LOD.
    uint32_t size = 1u << lod;
    uint32_t sm = s & (size - 1);
    uint32_t tm = t & (size - 1);

    // texbase is a byte offset in TMU RAM; each LOD lives at lod*size*size
    // texels after the base (mipmap chain).
    uint32_t texel = sm + tm * size;
    uint32_t bpp = 1;
    switch (fmt) {
    case 0: case 1: case 2: case 3: bpp = 1; break;              // 8-bit formats
    case 4: case 5: case 7: bpp = 2; break;                      // 16-bit formats
    default: bpp = 4; break;                                     // 24/32-bit formats
    }

    uint32_t addr = (texbase + texel * bpp) & m_mask;
    switch (fmt) {
    case 0: return tables().rgb332[m_ram[addr]];
    case 1: return tables().alpha8[m_ram[addr]];
    case 2: return tables().int8[m_ram[addr]];
    case 3: return tables().ai44[m_ram[addr]];
    case 4: {
        uint16_t v = m_ram[addr] | (m_ram[(addr + 1) & m_mask] << 8);
        return tables().rgb565[v];
    }
    case 5: {
        uint16_t v = m_ram[addr] | (m_ram[(addr + 1) & m_mask] << 8);
        return tables().argb1555[v];
    }
    case 7: {
        uint16_t v = m_ram[addr] | (m_ram[(addr + 1) & m_mask] << 8);
        return tables().argb4444[v];
    }
    case 6: {
        // ARGB8888
        return m_ram[addr]
            | (m_ram[(addr + 1) & m_mask] << 8)
            | (m_ram[(addr + 2) & m_mask] << 16)
            | (m_ram[(addr + 3) & m_mask] << 24);
    }
    default: return 0xFF000000;
    }
}

} // namespace voodoo
