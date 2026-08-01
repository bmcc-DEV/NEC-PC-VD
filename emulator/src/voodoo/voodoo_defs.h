#pragma once
#include <cstdint>

namespace voodoo {

// Register indices are DWORD offsets (byte offset / 4) in the register window.
// Register addresses follow the 3dfx Voodoo 2 (SST-2) layout.
enum reg : uint32_t {
    reg_status          = 0x000/4,  reg_intrCtrl        = 0x004/4,
    reg_vertexAx        = 0x008/4,  reg_vertexAy        = 0x00C/4,
    reg_vertexBx        = 0x010/4,  reg_vertexBy        = 0x014/4,
    reg_vertexCx        = 0x018/4,  reg_vertexCy        = 0x01C/4,
    reg_startR          = 0x020/4,  reg_startG          = 0x024/4,
    reg_startB          = 0x028/4,  reg_startZ          = 0x02C/4,
    reg_startA          = 0x030/4,  reg_startS          = 0x034/4,
    reg_startT          = 0x038/4,  reg_startW          = 0x03C/4,
    reg_dRdX            = 0x040/4,  reg_dGdX            = 0x044/4,
    reg_dBdX            = 0x048/4,  reg_dZdX            = 0x04C/4,
    reg_dAdX            = 0x050/4,  reg_dSdX            = 0x054/4,
    reg_dTdX            = 0x058/4,  reg_dWdX            = 0x05C/4,
    reg_dRdY            = 0x060/4,  reg_dGdY            = 0x064/4,
    reg_dBdY            = 0x068/4,  reg_dZdY            = 0x06C/4,
    reg_dAdY            = 0x070/4,  reg_dSdY            = 0x074/4,
    reg_dTdY            = 0x078/4,  reg_dWdY            = 0x07C/4,
    reg_triangleCMD     = 0x080/4,

    reg_fvertexAx       = 0x088/4,  reg_fvertexAy       = 0x08C/4,
    reg_fvertexBx       = 0x090/4,  reg_fvertexBy       = 0x094/4,
    reg_fvertexCx       = 0x098/4,  reg_fvertexCy       = 0x09C/4,
    reg_fstartR         = 0x0A0/4,  reg_fstartG         = 0x0A4/4,
    reg_fstartB         = 0x0A8/4,  reg_fstartZ         = 0x0AC/4,
    reg_fstartA         = 0x0B0/4,  reg_fstartS         = 0x0B4/4,
    reg_fstartT         = 0x0B8/4,  reg_fstartW         = 0x0BC/4,
    reg_fdRdX           = 0x0C0/4,  reg_fdGdX           = 0x0C4/4,
    reg_fdBdX           = 0x0C8/4,  reg_fdZdX           = 0x0CC/4,
    reg_fdAdX           = 0x0D0/4,  reg_fdSdX           = 0x0D4/4,
    reg_fdTdX           = 0x0D8/4,  reg_fdWdX           = 0x0DC/4,
    reg_fdRdY           = 0x0E0/4,  reg_fdGdY           = 0x0E4/4,
    reg_fdBdY           = 0x0E8/4,  reg_fdZdY           = 0x0EC/4,
    reg_fdAdY           = 0x0F0/4,  reg_fdSdY           = 0x0F4/4,
    reg_fdTdY           = 0x0F8/4,  reg_fdWdY           = 0x0FC/4,
    reg_ftriangleCMD    = 0x100/4,

    reg_fbzColorPath    = 0x104/4,  reg_fogMode         = 0x108/4,
    reg_alphaMode       = 0x10C/4,  reg_fbzMode         = 0x110/4,
    reg_lfbMode         = 0x114/4,
    reg_clipLeftRight   = 0x118/4,  reg_clipLowYHighY   = 0x11C/4,
    reg_nopCMD          = 0x120/4,  reg_fastfillCMD     = 0x124/4,
    reg_swapbufferCMD   = 0x128/4,  reg_fogColor        = 0x12C/4,
    reg_zaColor         = 0x130/4,  reg_chromaKey       = 0x134/4,
    reg_chromaRange     = 0x138/4,  reg_userIntrCMD     = 0x13C/4,
    reg_stipple         = 0x140/4,  reg_color0          = 0x144/4,
    reg_color1          = 0x148/4,
    reg_fogTable        = 0x160/4,

    reg_cmdFifoBaseAddr = 0x1E0/4,  reg_cmdFifoBump     = 0x1E4/4,
    reg_cmdFifoRdPtr    = 0x1E8/4,  reg_cmdFifoAMin     = 0x1EC/4,
    reg_cmdFifoAMax     = 0x1F0/4,  reg_cmdFifoDepth    = 0x1F4/4,
    reg_cmdFifoHoles    = 0x1F8/4,

    reg_fbiInit4        = 0x200/4,  reg_vRetrace        = 0x204/4,
    reg_backPorch       = 0x208/4,  reg_videoDimensions = 0x20C/4,
    reg_fbiInit0        = 0x210/4,  reg_fbiInit1        = 0x214/4,
    reg_fbiInit2        = 0x218/4,  reg_fbiInit3        = 0x21C/4,
    reg_hSync           = 0x220/4,  reg_vSync           = 0x224/4,
    reg_clutData        = 0x228/4,  reg_dacData         = 0x22C/4,
    reg_maxRgbDelta     = 0x230/4,  reg_hBorder         = 0x234/4,
    reg_vBorder         = 0x238/4,  reg_borderColor     = 0x23C/4,
    reg_hvRetrace       = 0x240/4,
    reg_fbiInit5        = 0x244/4,  reg_fbiInit6        = 0x248/4,
    reg_fbiInit7        = 0x24C/4,
    reg_fbiSwapHistory  = 0x258/4,  reg_fbiTrianglesOut = 0x25C/4,

    // Hardware triangle setup engine
    reg_sSetupMode      = 0x260/4,  reg_sVx             = 0x264/4,
    reg_sVy             = 0x268/4,  reg_sARGB           = 0x26C/4,
    reg_sRed            = 0x270/4,  reg_sGreen          = 0x274/4,
    reg_sBlue           = 0x278/4,  reg_sAlpha          = 0x27C/4,
    reg_sVz             = 0x280/4,  reg_sWb             = 0x284/4,
    reg_sWtmu0          = 0x288/4,  reg_sS_W0           = 0x28C/4,
    reg_sT_W0           = 0x290/4,  reg_sWtmu1          = 0x294/4,
    reg_sS_W1           = 0x298/4,  reg_sT_W1           = 0x29C/4,
    reg_sDrawTriCMD     = 0x2A0/4,  reg_sBeginTriCMD    = 0x2A4/4,

    // 2D BitBLT engine
    reg_bltSrcBaseAddr  = 0x2C0/4,  reg_bltDstBaseAddr  = 0x2C4/4,
    reg_bltXYStrides    = 0x2C8/4,  reg_bltSrcChromaRange = 0x2CC/4,
    reg_bltDstChromaRange = 0x2D0/4,
    reg_bltClipX        = 0x2D4/4,  reg_bltClipY        = 0x2D8/4,
    reg_bltSrcXY        = 0x2E0/4,  reg_bltDstXY        = 0x2E4/4,
    reg_bltSize         = 0x2E8/4,  reg_bltRop          = 0x2EC/4,
    reg_bltColor        = 0x2F0/4,  reg_bltCommand      = 0x2F8/4,
    reg_bltData         = 0x2FC/4,

    // Per-TMU texture registers (0x300-0x3FF). Stored per TMU.
    reg_textureMode     = 0x300/4,  reg_tLOD            = 0x304/4,
    reg_tDetail         = 0x308/4,  reg_texBaseAddr     = 0x30C/4,
    reg_texBaseAddr_1   = 0x310/4,  reg_texBaseAddr_2   = 0x314/4,
    reg_texBaseAddr_3_8 = 0x318/4,  reg_trexInit0       = 0x31C/4,
    reg_trexInit1       = 0x320/4,
    reg_nccTable0       = 0x324/4,  reg_nccTable1       = 0x354/4,
    reg_palette         = 0x354/4,
    reg_tmu_reg_base    = 0x300/4,
    reg_tmu_reg_count   = 0x100/4,   // 64 dwords per TMU
};

// Helper to extract bitfields from a 32-bit register value
template<int S, int L>
inline uint32_t bits(uint32_t v) { return (v >> S) & ((1 << L) - 1); }

// Chip mask bits
enum chipmask : uint32_t {
    CHIP_FBI  = 1,
    CHIP_TMU0 = 2,
    CHIP_TMU1 = 4,
    ALL_CHIPS = 7,
};

// One accumulated vertex for the hardware triangle setup engine / CMDFIFO
struct SetupVertex {
    float x = 0, y = 0;
    float r = 0, g = 0, b = 0, a = 0;
    float z = 0, wb = 0;
    float w0 = 0, s0 = 0, t0 = 0;
    float w1 = 0, s1 = 0, t1 = 0;
};

} // namespace voodoo
