#include "voodoo2.h"
#include "bus/memory_bus.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace voodoo {

namespace {

inline float as_float(uint32_t v) {
    float f;
    std::memcpy(&f, &v, sizeof(f));
    return f;
}

inline uint32_t as_u32(float f) {
    uint32_t v;
    std::memcpy(&v, &f, sizeof(v));
    return v;
}

inline int iterant8(int64_t v) {
    int c = (int)(v >> 12);
    if (c < 0) c = 0;
    if (c > 255) c = 255;
    return c;
}

inline int lerp8(double v) {
    int c = (int)v;
    if (c < 0) c = 0;
    if (c > 255) c = 255;
    return c;
}

} // namespace

Voodoo2Device::Voodoo2Device() {
    vendor_id = 0x121A;
    device_id = 0x0002;
    revision = 0x02;
    class_code[0] = 0x00;
    class_code[1] = 0x00;
    class_code[2] = 0x03;
    m_cmdfifo.set_device(this);
    reset();
}

Voodoo2Device::~Voodoo2Device() = default;

void Voodoo2Device::reset() {
    m_regs.fill(0);
    m_tmu_regs[0].fill(0);
    m_tmu_regs[1].fill(0);
    m_cmdfifo.set_enable(false);
    m_cmdfifo.set_device(this);

    m_sverts = 0;
    m_svert[0] = m_svert[1] = m_svert[2] = {};

    m_rgboffs[0] = 0;
    m_rgboffs[1] = 0;
    m_rgboffs[2] = ~0;
    m_auxoffs = ~0;
    m_frontbuf = 0;
    m_backbuf = 1;
    m_swaps_pending = 0;
    m_video_changed = true;
    m_height = 480;
    m_xoffs = m_yoffs = 0;
    m_vblank = false;
    m_init_enable = 0;

    // Allocate 12 MB of VRAM: 4 MB framebuffer + 4 MB TMU0 + 4 MB TMU1
    int total = m_fbmem_mb + m_tmumem_mb[0] + m_tmumem_mb[1];
    m_memory = std::make_unique<uint8_t[]>(total * 1024 * 1024 + 4096);
    m_fbram = (uint8_t*)(((uintptr_t)m_memory.get() + 4095) & ~(uintptr_t)4095);
    m_fbmask = m_fbmem_mb * 1024 * 1024 - 1;

    uint8_t* ptr = m_fbram + m_fbmem_mb * 1024 * 1024;
    for (int i = 0; i < 2; i++) {
        m_tmumem[i] = (m_tmumem_mb[i] > 0) ? ptr : nullptr;
        m_tmu[i].init(m_tmumem[i], m_tmumem_mb[i] * 1024 * 1024);
        ptr += m_tmumem_mb[i] * 1024 * 1024;
    }

    // Display configuration defaults
    setup_display_registers();

    // Init CLUT: expand 5-bit values to 8-bit
    for (int i = 0; i < 32; i++) {
        uint32_t v = (i << 3) | (i >> 2);
        m_clut[i] = 0xFF000000 | (v << 16) | (v << 8) | v;
    }
    m_clut[32] = 0x20FFFFFF;
    m_clut_dirty = true;

    recompute_video_memory();
}

void Voodoo2Device::setup_display_registers() {
    // fbiInit1: x tiles (20 * 32 = 640 px wide) + bit 8 + blank (bit 12)
    m_regs[reg_fbiInit1] = (20 << 1) | (1 << 8) | (1 << 12);
    m_regs[reg_fbiInit2] = 0;
    m_regs[reg_fbiInit4] = 1;
    m_regs[reg_fbiInit5] = 1 << 9;   // double-buffered + aux
    m_regs[reg_fbiInit6] = 0;
    m_regs[reg_fbiInit7] = 0;

    // Default clip rectangle (effectively no clipping)
    m_regs[reg_clipLeftRight] = (1023 << 16) | 0;
    m_regs[reg_clipLowYHighY] = (1023 << 16) | 0;
}

void Voodoo2Device::recompute_video_memory() {
    uint32_t config = bits<6, 1>(m_regs[reg_fbiInit2]);
    if (config == 0)
        config = bits<9, 2>(m_regs[reg_fbiInit5]);

    uint32_t xtiles = bits<1, 5>(m_regs[reg_fbiInit1])
                    | (bits<0, 1>(m_regs[reg_fbiInit6]) << 5);
    int rowpixels = (int)(xtiles * 32);
    if (rowpixels < 640) rowpixels = 640;
    m_rowpixels = rowpixels;

    m_rgboffs[0] = 0;
    uint32_t buf_pages = bits<19, 9>(m_regs[reg_fbiInit2]);
    if (buf_pages == 0) buf_pages = 0x100;   // 1 MB per buffer

    m_rgboffs[1] = buf_pages * 0x1000;
    if (config <= 1) {
        // Double buffered: 2 color buffers + 1 aux
        m_rgboffs[2] = ~0;
        m_auxoffs = 2 * buf_pages * 0x1000;
    } else {
        // Triple buffered
        m_rgboffs[2] = 2 * buf_pages * 0x1000;
        m_auxoffs = (config == 2) ? 3 * buf_pages * 0x1000 : ~0;
    }

    m_video_changed = true;
}

int Voodoo2Device::chipmask_from_offset(uint32_t offset) {
    switch ((offset >> 10) & 3) {
    case 0: return CHIP_FBI;
    case 1: return CHIP_FBI | CHIP_TMU0;
    case 2: return CHIP_FBI | CHIP_TMU1;
    default: return ALL_CHIPS;
    }
}

uint32_t Voodoo2Device::read(uint32_t offset) {
    uint32_t region = offset >> 22;
    if (region == 0) {
        if (m_cmdfifo.enabled() && (offset & 0x200000))
            return 0xFFFFFFFF;
        int chipmask = chipmask_from_offset(offset >> 2);
        int regnum = (offset >> 2) & 0xFF;
        return read_register(regnum, chipmask);
    }
    if (region == 1)
        return lfb_read(offset & 0x3FFFFF);
    return 0xFFFFFFFF;
}

void Voodoo2Device::write(uint32_t offset, uint32_t data, uint32_t mem_mask) {
    uint32_t region = offset >> 22;
    if (region == 0) {
        // CMDFIFO write window
        if (m_cmdfifo.enabled() && (offset & 0x200000)) {
            m_cmdfifo.write_direct((offset >> 2) & 0xFFFF, data);
            return;
        }
        int chipmask = chipmask_from_offset(offset >> 2);
        int regnum = (offset >> 2) & 0xFF;
        write_register(regnum, chipmask, data, mem_mask);
        return;
    }
    if (region == 1) {
        lfb_write(offset & 0x3FFFFF, data, mem_mask);
        return;
    }
    if (region == 2) {
        texture_write(offset & 0x3FFFFF, data);
        return;
    }
}

uint32_t Voodoo2Device::read_register(int regnum, int chipmask) {
    (void)chipmask;
    if (regnum >= reg_tmu_reg_base) {
        if (regnum < reg_tmu_reg_base + reg_tmu_reg_count)
            return m_tmu_regs[0][regnum - reg_tmu_reg_base];
        return 0;
    }
    if (regnum < 0 || regnum >= (int)m_regs.size()) return 0;
    if (regnum == reg_status) {
        uint32_t result = std::min(m_cmdfifo.depth(), 0x3Fu);
        if (m_vblank) result |= 1 << 6;
        result |= m_frontbuf << 10;
        return result;
    }
    if (regnum == reg_vRetrace) return m_vblank ? 0 : 480;
    if (regnum == reg_cmdFifoRdPtr) return m_cmdfifo.read_pointer();
    if (regnum == reg_cmdFifoDepth) return m_cmdfifo.depth();
    if (regnum == reg_cmdFifoHoles) return m_cmdfifo.holes();
    return m_regs[regnum];
}

void Voodoo2Device::write_register(int regnum, int chipmask, uint32_t data, uint32_t mask) {
    if (regnum < 0 || regnum >= (int)m_regs.size()) return;

    // TMU register bank
    if (regnum >= reg_tmu_reg_base) {
        int idx = regnum - reg_tmu_reg_base;
        for (int tmu = 0; tmu < 2; tmu++) {
            int cm = (tmu == 0) ? CHIP_TMU0 : CHIP_TMU1;
            if (chipmask & cm) {
                if (mask != 0xFFFFFFFF)
                    m_tmu_regs[tmu][idx] = (m_tmu_regs[tmu][idx] & ~mask) | (data & mask);
                else
                    m_tmu_regs[tmu][idx] = data;
            }
        }
        return;
    }

    // Float -> fixed conversion for the float triangle registers
    if (regnum >= reg_fvertexAx && regnum <= reg_fdWdY) {
        float f = as_float(data);
        int scale = (regnum <= reg_fvertexCy) ? 16 : 4096;
        int target;
        if (regnum <= reg_fvertexCy) {
            static const int fvertmap[] = {
                reg_vertexAx, reg_vertexAy, reg_vertexBx, reg_vertexBy,
                reg_vertexCx, reg_vertexCy,
            };
            target = fvertmap[regnum - reg_fvertexAx];
        } else {
            static const int fiterantmap[] = {
                reg_startR, reg_startG, reg_startB, reg_startZ, reg_startA,
                reg_startS, reg_startT, reg_startW,
                reg_dRdX, reg_dGdX, reg_dBdX, reg_dZdX, reg_dAdX,
                reg_dSdX, reg_dTdX, reg_dWdX,
                reg_dRdY, reg_dGdY, reg_dBdY, reg_dZdY, reg_dAdY,
                reg_dSdY, reg_dTdY, reg_dWdY,
            };
            target = fiterantmap[regnum - reg_fstartR];
        }
        m_regs[target] = (uint32_t)(int64_t)(f * (float)scale);
        return;
    }

    if (mask != 0xFFFFFFFF)
        m_regs[regnum] = (m_regs[regnum] & ~mask) | (data & mask);
    else
        m_regs[regnum] = data;

    switch (regnum) {
    case reg_fbiInit0:
        if (data & 0x02000000) reset();
        break;
    case reg_fbiInit1:
    case reg_fbiInit2:
    case reg_fbiInit5:
    case reg_fbiInit6:
        recompute_video_memory();
        break;
    case reg_fbiInit7:
        m_cmdfifo.set_enable((data & 1) != 0);
        break;
    case reg_cmdFifoBaseAddr: {
        uint32_t base = bits<0, 10>(data) << 12;
        uint32_t end = (bits<16, 10>(data) + 1) << 12;
        if (end > m_fbmem_mb * 1024 * 1024)
            end = m_fbmem_mb * 1024 * 1024;
        m_cmdfifo.configure(m_fbram, base, end);
        break;
    }
    case reg_cmdFifoRdPtr:
        m_cmdfifo.set_read_pointer(data);
        break;
    case reg_cmdFifoAMin:
        m_cmdfifo.set_address_min(data);
        break;
    case reg_cmdFifoAMax:
        m_cmdfifo.set_address_max(data);
        break;
    case reg_cmdFifoDepth:
        m_cmdfifo.set_depth(data);
        break;
    case reg_cmdFifoHoles:
        m_cmdfifo.set_holes(data);
        break;
    case reg_clutData:
        if (!(m_regs[reg_fbiInit1] & (1 << 8))) {
            int idx = (data >> 24) & 0xFF;
            if (idx <= 32) {
                m_clut[idx] = data;
                m_clut_dirty = true;
            }
        }
        break;
    case reg_swapbufferCMD:
        m_swaps_pending++;
        if (!(data & 1)) {
            m_frontbuf = (m_frontbuf + 1) % 2;
            m_backbuf = (m_backbuf + 1) % 2;
            m_video_changed = true;
            m_swaps_pending--;
        }
        break;
    case reg_fastfillCMD:
        draw_fastfill();
        break;
    case reg_triangleCMD:
    case reg_ftriangleCMD:
        draw_triangle_legacy();
        break;
    case reg_sARGB:
        m_regs[reg_sAlpha] = as_u32((float)((data >> 24) & 0xFF));
        m_regs[reg_sRed] = as_u32((float)((data >> 16) & 0xFF));
        m_regs[reg_sGreen] = as_u32((float)((data >> 8) & 0xFF));
        m_regs[reg_sBlue] = as_u32((float)(data & 0xFF));
        break;
    case reg_sBeginTriCMD:
        begin_triangle();
        break;
    case reg_sDrawTriCMD:
        draw_triangle_setup();
        break;
    case reg_bltCommand:
        do_blit();
        break;
    default:
        break;
    }
}

uint32_t Voodoo2Device::cmdfifo_register_w(uint32_t regnum, uint32_t data) {
    int chipmask = chipmask_from_offset(regnum);
    write_register(regnum & 0xFF, chipmask, data, 0xFFFFFFFF);
    return 0;
}

uint32_t Voodoo2Device::cmdfifo_2d_w(uint32_t index, uint32_t data) {
    uint32_t regnum = reg_bltSrcBaseAddr + index;
    if (regnum <= reg_bltData)
        write_register(regnum, CHIP_FBI, data, 0xFFFFFFFF);
    return 0;
}

void Voodoo2Device::cmdfifo_lfb_w(uint32_t offset, uint32_t data) {
    lfb_write(offset, data, 0xFFFFFFFF);
}

void Voodoo2Device::cmdfifo_texture_w(uint32_t offset, uint32_t data) {
    texture_write(offset, data);
}

void Voodoo2Device::cmdfifo_triangle_vertex(const SetupVertex& sv, uint32_t command, uint32_t index) {
    uint32_t code = bits<3, 3>(command);
    bool fan = bits<22, 1>(command) != 0;

    if ((code == 1 && index == 0) || (code == 0 && index % 3 == 0)) {
        m_sverts = 1;
        m_svert[0] = m_svert[1] = m_svert[2] = sv;
    } else {
        if (!fan)
            m_svert[0] = m_svert[1];
        m_svert[1] = m_svert[2];
        m_svert[2] = sv;
        if (++m_sverts >= 3)
            setup_and_draw_triangle();
    }
}

uint16_t* Voodoo2Device::front_buffer() {
    if (m_rgboffs[m_frontbuf] == ~0) return nullptr;
    return (uint16_t*)(m_fbram + m_rgboffs[m_frontbuf]);
}

uint16_t* Voodoo2Device::back_buffer() {
    if (m_rgboffs[m_backbuf] == ~0) return nullptr;
    return (uint16_t*)(m_fbram + m_rgboffs[m_backbuf]);
}

uint16_t* Voodoo2Device::draw_buffer(int buf) {
    if (buf < 3 && m_rgboffs[buf] != ~0)
        return (uint16_t*)(m_fbram + m_rgboffs[buf]);
    return front_buffer();
}

uint32_t Voodoo2Device::lfb_read(uint32_t offset) {
    uint32_t x = offset % (uint32_t)m_rowpixels;
    uint32_t y = offset / (uint32_t)m_rowpixels;
    uint16_t* buf = front_buffer();
    if (!buf) return 0xFFFFFFFF;
    uint32_t maxwords = (m_fbmem_mb * 1024 * 1024) / 2;
    if (y >= (uint32_t)m_height || y * (uint32_t)m_rowpixels + x + 1 >= maxwords)
        return 0xFFFFFFFF;
    buf += y * m_rowpixels + x;
    return buf[0] | (buf[1] << 16);
}

void Voodoo2Device::lfb_write(uint32_t offset, uint32_t data, uint32_t mask) {
    uint32_t x = offset % (uint32_t)m_rowpixels;
    uint32_t y = offset / (uint32_t)m_rowpixels;
    uint16_t* buf = back_buffer();
    if (!buf) return;
    uint32_t maxwords = (m_fbmem_mb * 1024 * 1024) / 2;
    if (y >= (uint32_t)m_height || y * (uint32_t)m_rowpixels + x + 1 >= maxwords)
        return;
    buf += y * m_rowpixels + x;
    if (mask & 0xFFFF) buf[0] = data & 0xFFFF;
    if (mask & 0xFFFF0000) buf[1] = (data >> 16) & 0xFFFF;
}

void Voodoo2Device::texture_write(uint32_t offset, uint32_t data) {
    int tmu = (offset >> 21) & 1;
    uint32_t addr = offset & 0x1FFFFF;
    if (!m_tmumem[tmu]) return;
    uint32_t size = m_tmumem_mb[tmu] * 1024 * 1024;
    addr &= size - 1;
    m_tmumem[tmu][addr + 0] = (data >> 0) & 0xFF;
    m_tmumem[tmu][addr + 1] = (data >> 8) & 0xFF;
    m_tmumem[tmu][addr + 2] = (data >> 16) & 0xFF;
    m_tmumem[tmu][addr + 3] = (data >> 24) & 0xFF;
}

uint16_t Voodoo2Device::pack_rgb565(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t Voodoo2Device::pack_rgb555(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

void Voodoo2Device::draw_fastfill() {
    uint16_t* buf = back_buffer();
    if (!buf) return;

    uint32_t color = m_regs[reg_color1];
    int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    bool is555 = (m_regs[reg_fbzColorPath] & 1) != 0;
    uint16_t pix = is555 ? pack_rgb555(r, g, b) : pack_rgb565(r, g, b);

    int32_t left = bits<0, 12>(m_regs[reg_clipLeftRight]);
    int32_t right = bits<16, 12>(m_regs[reg_clipLeftRight]);
    int32_t top = bits<0, 12>(m_regs[reg_clipLowYHighY]);
    int32_t bottom = bits<16, 12>(m_regs[reg_clipLowYHighY]);

    if (left <= right && top <= bottom) {
        if (left < 0) left = 0;
        if (top < 0) top = 0;
        if (right >= m_rowpixels) right = m_rowpixels - 1;
        if (bottom >= m_height) bottom = m_height - 1;
        for (int y = top; y <= bottom; y++) {
            uint16_t* row = buf + (uint32_t)y * m_rowpixels;
            for (int x = left; x <= right; x++) row[x] = pix;
        }
    } else {
        for (int y = 0; y < m_height; y++) {
            uint16_t* row = buf + (uint32_t)y * m_rowpixels;
            for (int x = 0; x < m_rowpixels; x++) row[x] = pix;
        }
    }
    m_video_changed = true;
}

void Voodoo2Device::begin_triangle() {
    SetupVertex& sv = m_svert[2];
    sv.x = as_float(m_regs[reg_sVx]);
    sv.y = as_float(m_regs[reg_sVy]);
    sv.wb = as_float(m_regs[reg_sWb]);
    sv.w0 = as_float(m_regs[reg_sWtmu0]);
    sv.s0 = as_float(m_regs[reg_sS_W0]);
    sv.t0 = as_float(m_regs[reg_sT_W0]);
    sv.w1 = as_float(m_regs[reg_sWtmu1]);
    sv.s1 = as_float(m_regs[reg_sS_W1]);
    sv.t1 = as_float(m_regs[reg_sT_W1]);
    sv.a = as_float(m_regs[reg_sAlpha]);
    sv.r = as_float(m_regs[reg_sRed]);
    sv.g = as_float(m_regs[reg_sGreen]);
    sv.b = as_float(m_regs[reg_sBlue]);
    m_svert[0] = m_svert[1] = sv;
    m_sverts = 1;
}

void Voodoo2Device::draw_triangle_setup() {
    // strip mode: shuffle vertex 1 down to 0
    if (!(m_regs[reg_sSetupMode] & (1 << 16)))
        m_svert[0] = m_svert[1];
    m_svert[1] = m_svert[2];

    SetupVertex& sv = m_svert[2];
    sv.x = as_float(m_regs[reg_sVx]);
    sv.y = as_float(m_regs[reg_sVy]);
    sv.wb = as_float(m_regs[reg_sWb]);
    sv.w0 = as_float(m_regs[reg_sWtmu0]);
    sv.s0 = as_float(m_regs[reg_sS_W0]);
    sv.t0 = as_float(m_regs[reg_sT_W0]);
    sv.w1 = as_float(m_regs[reg_sWtmu1]);
    sv.s1 = as_float(m_regs[reg_sS_W1]);
    sv.t1 = as_float(m_regs[reg_sT_W1]);
    sv.a = as_float(m_regs[reg_sAlpha]);
    sv.r = as_float(m_regs[reg_sRed]);
    sv.g = as_float(m_regs[reg_sGreen]);
    sv.b = as_float(m_regs[reg_sBlue]);

    if (++m_sverts >= 3)
        setup_and_draw_triangle();
}

void Voodoo2Device::setup_and_draw_triangle() {
    SetupVertex sv0 = m_svert[0];
    SetupVertex sv1 = m_svert[1];
    SetupVertex sv2 = m_svert[2];

    double divisor = (sv0.x - sv1.x) * (sv0.y - sv2.y) - (sv0.x - sv2.x) * (sv0.y - sv1.y);

    uint32_t smode = m_regs[reg_sSetupMode];
    if (smode & (1 << 17)) {   // enable culling
        int culling_sign = (smode >> 18) & 1;
        int divisor_sign = (divisor < 0);
        if (!(smode & (1 << 16)) && !(smode & (1 << 19)))  // strips + ping pong
            culling_sign ^= (m_sverts - 3) & 1;
        if (divisor_sign == culling_sign)
            return;
    }

    bool use_tex0 = (smode & (1 << 5)) != 0;   // setup S0/T0
    bool use_tex1 = (smode & (1 << 7)) != 0;   // setup S1/T1
    rasterize(sv0, sv1, sv2, use_tex0, use_tex1);
}

void Voodoo2Device::draw_triangle_legacy() {
    int16_t ax = (int16_t)(m_regs[reg_vertexAx] & 0xFFFF);
    int16_t ay = (int16_t)(m_regs[reg_vertexAy] & 0xFFFF);
    int16_t bx = (int16_t)(m_regs[reg_vertexBx] & 0xFFFF);
    int16_t by = (int16_t)(m_regs[reg_vertexBy] & 0xFFFF);
    int16_t cx = (int16_t)(m_regs[reg_vertexCx] & 0xFFFF);
    int16_t cy = (int16_t)(m_regs[reg_vertexCy] & 0xFFFF);

    double r0 = (double)(int32_t)m_regs[reg_startR];
    double g0 = (double)(int32_t)m_regs[reg_startG];
    double b0 = (double)(int32_t)m_regs[reg_startB];
    double dRdx = (double)(int32_t)m_regs[reg_dRdX], dRdy = (double)(int32_t)m_regs[reg_dRdY];
    double dGdx = (double)(int32_t)m_regs[reg_dGdX], dGdy = (double)(int32_t)m_regs[reg_dGdY];
    double dBdx = (double)(int32_t)m_regs[reg_dBdX], dBdy = (double)(int32_t)m_regs[reg_dBdY];

    auto vcol = [&](int16_t vx, int16_t vy) {
        double dx = vx - ax, dy = vy - ay;
        SetupVertex v;
        v.r = (float)iterant8((int64_t)(r0 + dRdx * dx / 16.0 + dRdy * dy / 16.0));
        v.g = (float)iterant8((int64_t)(g0 + dGdx * dx / 16.0 + dGdy * dy / 16.0));
        v.b = (float)iterant8((int64_t)(b0 + dBdx * dx / 16.0 + dBdy * dy / 16.0));
        return v;
    };

    SetupVertex v0 = vcol(ax, ay);
    v0.x = ax / 16.0;
    v0.y = ay / 16.0;
    SetupVertex v1 = vcol(bx, by);
    v1.x = bx / 16.0;
    v1.y = by / 16.0;
    SetupVertex v2 = vcol(cx, cy);
    v2.x = cx / 16.0;
    v2.y = cy / 16.0;

    rasterize(v0, v1, v2, false, false);
}

uint32_t Voodoo2Device::sample_texel(int tmu, uint32_t fmt, int lod, uint32_t s, uint32_t t, uint32_t texbase) {
    return m_tmu[tmu].texel_lookup(fmt, lod, s, t, texbase);
}

void Voodoo2Device::rasterize(const SetupVertex& v0in, const SetupVertex& v1in, const SetupVertex& v2in,
                              bool use_tex0, bool use_tex1) {
    uint16_t* buf = back_buffer();
    if (!buf) return;
    uint32_t maxwords = (m_fbmem_mb * 1024 * 1024) / 2;

    double x0 = v0in.x, y0 = v0in.y;
    double x1 = v1in.x, y1 = v1in.y;
    double x2 = v2in.x, y2 = v2in.y;

    double area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0.0) return;

    bool is555 = (m_regs[reg_fbzColorPath] & 1) != 0;
    bool constant_rgb = (m_regs[reg_fbzColorPath] & 2) != 0;

    // TMU configuration
    bool tex0_ok = use_tex0 && m_tmu[0].has_ram();
    bool tex1_ok = use_tex1 && m_tmu[1].has_ram();
    uint32_t fmt0 = tex0_ok ? ((m_tmu_regs[0][0] >> 2) & 7) : 0;
    uint32_t fmt1 = tex1_ok ? ((m_tmu_regs[1][0] >> 2) & 7) : 0;
    int lod0 = tex0_ok ? (int)((m_tmu_regs[0][reg_tLOD - reg_tmu_reg_base] >> 3) & 0x1F) : 0;
    int lod1 = tex1_ok ? (int)((m_tmu_regs[1][reg_tLOD - reg_tmu_reg_base] >> 3) & 0x1F) : 0;
    uint32_t tbase0 = tex0_ok ? (m_tmu_regs[0][reg_texBaseAddr - reg_tmu_reg_base] & 0xFFFFF) : 0;
    uint32_t tbase1 = tex1_ok ? (m_tmu_regs[1][reg_texBaseAddr - reg_tmu_reg_base] & 0xFFFFF) : 0;

    // Bounding box
    int minx = std::max(0, (int)std::min({x0, x1, x2}));
    int maxx = std::min(m_rowpixels - 1, (int)std::max({x0, x1, x2}));
    int miny = std::max(0, (int)std::min({y0, y1, y2}));
    int maxy = std::min(m_height - 1, (int)std::max({y0, y1, y2}));

    // Clip rectangle
    int32_t cl = bits<0, 12>(m_regs[reg_clipLeftRight]);
    int32_t cr = bits<16, 12>(m_regs[reg_clipLeftRight]);
    int32_t ct = bits<0, 12>(m_regs[reg_clipLowYHighY]);
    int32_t cb = bits<16, 12>(m_regs[reg_clipLowYHighY]);
    if (cl <= cr && ct <= cb) {
        minx = std::max(minx, (int)cl);
        maxx = std::min(maxx, (int)cr);
        miny = std::max(miny, (int)ct);
        maxy = std::min(maxy, (int)cb);
    }
    if (minx > maxx || miny > maxy) return;

    for (int y = miny; y <= maxy; y++) {
        uint16_t* row = buf + (uint32_t)y * m_rowpixels;
        double py = y + 0.5;
        for (int x = minx; x <= maxx; x++) {
            double px = x + 0.5;
            double w0 = ((x1 - px) * (y2 - py) - (y1 - py) * (x2 - px)) / area;
            double w1 = ((x2 - px) * (y0 - py) - (y2 - py) * (x0 - px)) / area;
            double w2 = ((x0 - px) * (y1 - py) - (y0 - py) * (x1 - px)) / area;
            if (w0 < 0.0 || w1 < 0.0 || w2 < 0.0) continue;

            int r, g, b;
            if (constant_rgb) {
                uint32_t col = m_regs[reg_color1];
                r = (col >> 16) & 0xFF; g = (col >> 8) & 0xFF; b = col & 0xFF;
            } else {
                r = lerp8(v0in.r * w0 + v1in.r * w1 + v2in.r * w2);
                g = lerp8(v0in.g * w0 + v1in.g * w1 + v2in.g * w2);
                b = lerp8(v0in.b * w0 + v1in.b * w1 + v2in.b * w2);
            }

            uint32_t col = 0xFF000000 | (r << 16) | (g << 8) | b;

            if (tex0_ok) {
                double sw = v0in.s0 * v0in.w0 * w0 + v1in.s0 * v1in.w0 * w1 + v2in.s0 * v2in.w0 * w2;
                double tw = v0in.t0 * v0in.w0 * w0 + v1in.t0 * v1in.w0 * w1 + v2in.t0 * v2in.w0 * w2;
                double ww = v0in.w0 * w0 + v1in.w0 * w1 + v2in.w0 * w2;
                if (ww > 1e-6) {
                    col = sample_texel(0, fmt0, lod0, (uint32_t)(sw / ww), (uint32_t)(tw / ww), tbase0);
                }
            }
            if (tex1_ok) {
                double sw = v0in.s1 * v0in.w1 * w0 + v1in.s1 * v1in.w1 * w1 + v2in.s1 * v2in.w1 * w2;
                double tw = v0in.t1 * v0in.w1 * w0 + v1in.t1 * v1in.w1 * w1 + v2in.t1 * v2in.w1 * w2;
                double ww = v0in.w1 * w0 + v1in.w1 * w1 + v2in.w1 * w2;
                if (ww > 1e-6) {
                    uint32_t tex1 = sample_texel(1, fmt1, lod1, (uint32_t)(sw / ww), (uint32_t)(tw / ww), tbase1);
                    int cr = ((col >> 16) & 0xFF) * ((tex1 >> 16) & 0xFF) / 255;
                    int cg = ((col >> 8) & 0xFF) * ((tex1 >> 8) & 0xFF) / 255;
                    int cbb = (col & 0xFF) * (tex1 & 0xFF) / 255;
                    col = 0xFF000000 | (cr << 16) | (cg << 8) | cbb;
                }
            }

            uint32_t idx = (uint32_t)y * m_rowpixels + x;
            if (idx + 1 >= maxwords) continue;
            row[x] = is555 ? pack_rgb555((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF)
                           : pack_rgb565((col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF);
        }
    }
    m_video_changed = true;
}

void Voodoo2Device::do_blit() {
    // Simple 16-bit BLT: copy a w*h block from source to destination
    uint16_t* fb = (uint16_t*)m_fbram;
    uint32_t maxwords = (m_fbmem_mb * 1024 * 1024) / 2;

    uint32_t src = m_regs[reg_bltSrcBaseAddr];
    uint32_t dst = m_regs[reg_bltDstBaseAddr];
    int xstride = m_regs[reg_bltXYStrides] & 0xFFFF;
    int ystride = (m_regs[reg_bltXYStrides] >> 16) & 0xFFFF;
    int sx = m_regs[reg_bltSrcXY] & 0xFFFF;
    int sy = (m_regs[reg_bltSrcXY] >> 16) & 0xFFF;
    int dx = m_regs[reg_bltDstXY] & 0xFFFF;
    int dy = (m_regs[reg_bltDstXY] >> 16) & 0xFFF;
    int w = m_regs[reg_bltSize] & 0xFFF;
    int h = (m_regs[reg_bltSize] >> 16) & 0xFFF;
    uint32_t color = m_regs[reg_bltColor];

    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            uint32_t saddr = (src + (sy + yy) * ystride + (sx + xx) * 2) / 2;
            uint32_t daddr = (dst + (dy + yy) * ystride + (dx + xx) * 2) / 2;
            if (saddr >= maxwords || daddr >= maxwords) continue;
            uint16_t v = (sx + xx < w) ? fb[saddr] : (uint16_t)(color & 0xFFFF);
            fb[daddr] = v;
        }
    }
    m_video_changed = true;
}

void Voodoo2Device::draw_framebuffer(uint32_t* out, int w, int h) {
    if (!m_fbram) return;
    auto* buf = front_buffer();
    if (!buf) return;

    // Recompute pens if CLUT changed
    if (m_clut_dirty) {
        uint8_t rt[32], gt[64], bt[32];
        for (int i = 0; i < 32; i++) {
            rt[i] = (m_clut[i] >> 16) & 0xFF;
            bt[i] = m_clut[i] & 0xFF;
        }
        for (int i = 0; i < 64; i++)
            gt[i] = (m_clut[i / 2] >> 8) & 0xFF;
        for (uint32_t pen = 0; pen < 65536; pen++) {
            int r = bits<11, 5>(pen), g = bits<5, 6>(pen), b = bits<0, 5>(pen);
            m_pen[pen] = 0xFF000000 | (rt[r] << 16) | (gt[g] << 8) | bt[b];
        }
        m_clut_dirty = false;
    }

    for (int y = 0; y < h && y < m_height; y++) {
        int sy = y + m_yoffs;
        if (sy < 0 || sy >= m_height) continue;
        auto* src = buf + sy * m_rowpixels - m_xoffs;
        auto* dst = out + y * w;
        for (int x = 0; x < w && x < m_rowpixels; x++)
            dst[x] = m_pen[src[x]];
    }
}

int Voodoo2Device::update(uint32_t* rgb_buffer, int width, int height) {
    if (!m_fbram || !rgb_buffer) return 0;

    // Clear output to transparent so undrawn pixels let the 2D framebuffer show
    std::fill_n(rgb_buffer, (size_t)width * height, 0x00000000);

    if (!(m_regs[reg_fbiInit1] & (1 << 12))) {
        // Not blanked - draw
        draw_framebuffer(rgb_buffer, width, height);
    }

    bool changed = m_video_changed;
    m_video_changed = false;
    return changed ? 1 : 0;
}

uint32_t Voodoo2Device::config_read(int reg, uint32_t mask) {
    (void)mask;
    switch (reg) {
    case 0x00: return (device_id << 16) | vendor_id;
    case 0x04: return command | (status << 16);
    case 0x08: return (class_code[0] << 16) | (class_code[1] << 8) | class_code[2] | (revision);
    case 0x0C: return 0x10;   // cache line size / latency timer
    case 0x10: return bar[0];
    case 0x3C: return irq_line;
    default: return 0;
    }
}

void Voodoo2Device::config_write(int reg, uint32_t data, uint32_t mask) {
    (void)data;
    (void)mask;
    switch (reg) {
    case 0x10:
        bar[0] = data & ~0xF;   // keep the BAR (4 MB aligned)
        break;
    default:
        break;
    }
}

} // namespace voodoo
