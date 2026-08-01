#pragma once
#include <cstdint>
#include <array>

namespace voodoo {

struct TexelFormat {
    std::array<uint32_t, 256>    rgb332;
    std::array<uint32_t, 256>    alpha8;
    std::array<uint32_t, 256>    int8;
    std::array<uint32_t, 256>    ai44;
    std::array<uint32_t, 65536>  rgb565;
    std::array<uint32_t, 65536>  argb1555;
    std::array<uint32_t, 65536>  argb4444;

    TexelFormat();
};

// Texture unit: owns the TMU RAM and performs texel lookups for a single TMU.
class TMU {
public:
    TMU();

    void init(uint8_t* ram, uint32_t size);
    void reset();
    bool has_ram() const { return m_ram != nullptr; }

    // Fetch a texel for the given format, s/t coordinates and texture base.
    // s/t are fixed-point 14.18 values scaled to the texture dimensions.
    uint32_t texel_lookup(uint32_t fmt, int lod, uint32_t s, uint32_t t, uint32_t texbase) const;

private:
    uint8_t* m_ram = nullptr;
    uint32_t m_mask = 0;

    static const TexelFormat& tables();
};

} // namespace voodoo
