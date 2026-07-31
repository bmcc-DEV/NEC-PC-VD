#include "tmu.h"
#include <cstring>
#include <algorithm>

namespace voodoo {

static TexelFormat s_texel;  // file-scope texel format tables

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

uint32_t TexelFormat::lookup8(int fmt, uint8_t val) const {
    switch (fmt) {
    case 0: case 8: return rgb332[val];
    case 2: return alpha8[val];
    case 3: return int8[val];
    case 4: return ai44[val];
    default: return 0xFF000000;
    }
}

uint32_t TexelFormat::lookup16(int fmt, uint16_t val) const {
    switch (fmt) {
    case 10: return rgb565[val];
    case 11: return argb1555[val];
    case 12: return argb4444[val];
    default: return 0xFF000000;
    }
}

// TMU implementation
TMU::TMU() {
    m_regs.fill(0);
    m_palette.fill(0);
}

void TMU::init(int index, uint8_t* ram, uint32_t size) {
    m_index = index;
    m_ram = ram;
    m_mask = size - 1;
    reset();
}

void TMU::reset() {
    m_regs.fill(0);
    m_palette.fill(0);
    m_dirty = true;
}

void TMU::write_reg(int regnum, uint32_t data) {
    if (regnum >= m_regs.size()) return;
    if (m_regs[regnum] != data) {
        m_regs[regnum] = data;
        m_dirty = true;
    }
    // Special: NCC/palette writes
    if (regnum >= reg_nccTable/4 && regnum < reg_nccTable/4 + 48) {
        m_palette[regnum - reg_nccTable/4] = data;
    }
}

uint32_t TMU::read_reg(int regnum) const {
    if (regnum < m_regs.size()) return m_regs[regnum];
    return 0;
}

void TMU::write_texture(uint32_t offset, uint32_t data) {
    if (!m_ram) return;
    int lod = (offset >> 15) & 0xF;
    int tt = (offset >> 7) & 0xFF;
    int ts = (offset << 1) & 0xFF;

    // Simple linear address calculation
    uint32_t addr = (lod << 16) | (tt << 8) | ts;
    addr &= m_mask;

    // Write 4 bytes (little-endian)
    m_ram[addr + 0] = (data >> 0) & 0xFF;
    m_ram[addr + 1] = (data >> 8) & 0xFF;
    m_ram[addr + 2] = (data >> 16) & 0xFF;
    m_ram[addr + 3] = (data >> 24) & 0xFF;
}

uint32_t TMU::lod_base(int lod) const {
    // Simplified LOD base from textureLOD register
    uint32_t texlod = m_regs[reg_textureLOD/4];
    return (texlod >> (lod * 4)) & 0xF;
}

uint32_t TMU::lod_size(int lod) const {
    (void)lod;
    return 256;  // Simplified: fixed 256-byte LOD
}

uint32_t TMU::texel_lookup(uint32_t s, uint32_t t, int lod) const {
    if (!m_ram) return 0xFF000000;

    uint32_t texmode = m_regs[reg_textureMode/4];
    int fmt = texmode & 0xF;

    // Calculate texel address (simplified - no LOD, no bilinear)
    uint32_t addr = (t & 0xFF) * 256 + (s & 0xFF);
    if (fmt < 8) {
        // 8-bit texel
        uint8_t val = m_ram[addr & m_mask];
        return s_texel.lookup8(fmt, val);
    } else {
        uint16_t val = m_ram[(addr * 2) & m_mask] | (m_ram[(addr * 2 + 1) & m_mask] << 8);
        return s_texel.lookup16(fmt, val);
    }
}

void TMU::write_palette(int index, uint32_t data) {
    if (index < 512) m_palette[index] = data;
}

uint32_t TMU::read_palette(int index) const {
    if (index < 512) return m_palette[index];
    return 0;
}

} // namespace voodoo
