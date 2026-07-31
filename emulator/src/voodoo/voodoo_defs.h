#pragma once
#include <cstdint>

namespace voodoo {

// Register indices (Voodoo 1 / Rush compatible subset)
enum reg : uint32_t {
    reg_status       = 0x000/4,
    reg_vertexAx     = 0x008/4,  reg_vertexAy    = 0x00C/4,
    reg_vertexBx     = 0x010/4,  reg_vertexBy    = 0x014/4,
    reg_vertexCx     = 0x018/4,  reg_vertexCy    = 0x01C/4,
    reg_startR       = 0x020/4,  reg_dRdX        = 0x040/4,  reg_dRdY   = 0x060/4,
    reg_startG       = 0x024/4,  reg_dGdX        = 0x044/4,  reg_dGdY   = 0x064/4,
    reg_startB       = 0x028/4,  reg_dBdX        = 0x048/4,  reg_dBdY   = 0x068/4,
    reg_startZ       = 0x02C/4,  reg_dZdX        = 0x04C/4,  reg_dZdY   = 0x06C/4,
    reg_startA       = 0x030/4,  reg_dAdX        = 0x050/4,  reg_dAdY   = 0x070/4,
    reg_startS       = 0x034/4,  reg_dSdX        = 0x054/4,  reg_dSdY   = 0x074/4,
    reg_startT       = 0x038/4,  reg_dTdX        = 0x058/4,  reg_dTdY   = 0x078/4,
    reg_startW       = 0x03C/4,  reg_dWdX        = 0x05C/4,  reg_dWdY   = 0x07C/4,
    reg_triangleCMD  = 0x080/4,  reg_nopCMD      = 0x084/4,
    reg_fastfillCMD  = 0x088/4,  reg_swapbufferCMD=0x08C/4,
    reg_fogTable     = 0x0E0/4,

    // Floating-point equivalents (offset + 0x80)
    reg_fvertexAx    = 0x088/4,  reg_fvertexAy   = 0x08C/4,
    reg_fstartR      = 0x0A0/4,  reg_fstartG     = 0x0A4/4,
    reg_fstartB      = 0x0A8/4,  reg_fstartZ     = 0x0AC/4,

    // FBI config registers (using explicit dword offsets)
    reg_fbiInit0     = 64,  reg_fbiInit1    = 65,
    reg_fbiInit2     = 66,  reg_fbiInit3    = 67,
    reg_fbiInit4     = 68,  reg_fbiInit5    = 69,
    reg_fbiInit6     = 70,

    reg_fbzMode      = 74,  reg_fbzColorPath= 75,
    reg_lfbMode      = 76,
    reg_clipLeft     = 77,  reg_clipRight   = 78,
    reg_clipTop      = 79,  reg_clipBottom  = 80,
    reg_color0       = 82,  reg_color1      = 83,
    reg_fogColor     = 84,  reg_alphaMode   = 85,
    reg_fogMode      = 86,  reg_chromaKey   = 87,
    reg_zaColor      = 88,  reg_stipple     = 90,
    reg_clutData     = 91,  reg_dacData     = 92,
    reg_vRetrace     = 93,

    // Statistics
    reg_fbiPixelsIn  = 0x178/4,  reg_fbiPixelsOut= 0x17C/4,
    reg_fbiChromaFail= 0x184/4,  reg_fbiZfuncFail= 0x188/4,
    reg_fbiAfuncFail = 0x18C/4,  reg_fbiTrianglesOut=0x190/4,

    // TMU registers
    reg_textureMode  = 0x200/4,  reg_textureLOD    = 0x204/4,
    reg_textureDetail= 0x208/4,
    reg_nccTable     = 0x210/4,
    reg_palette      = 0x240/4,
};

// Helper to extract bitfields from a 32-bit register value
template<int S, int L>
inline uint32_t bits(uint32_t v) { return (v >> S) & ((1 << L) - 1); }
template<int S, int L>
inline void set_bits(uint32_t& v, uint32_t val) {
    v = (v & ~(((1 << L) - 1) << S)) | ((val & ((1 << L) - 1)) << S);
}

// FIFO type flags
enum fifo_flags : uint32_t {
    TYPE_REGISTER  = 0x00000000,
    TYPE_LFB       = 0x00400000,
    TYPE_TEXTURE   = 0x00800000,
    TYPE_MASK      = 0x00C00000,
    NO_16_31       = 0x10000000,
    NO_0_15        = 0x20000000,
    FLAGS_MASK     = 0xF0000000,
};

// Chip mask bits
enum chipmask : uint32_t {
    CHIP_FBI  = 1,
    CHIP_TMU0 = 2,
    CHIP_TMU1 = 4,
    ALL_CHIPS = 7,
};

} // namespace voodoo
