#include "voodoo_rush.h"
#include "bus/memory_bus.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

namespace voodoo {

VoodooRushDevice::VoodooRushDevice()
    : m_regs{}
    , m_tmu0_regs{}
    , m_pci_fifo() {
    m_pci_fifo.configure(m_pci_fifo_mem, 64*2);
}

VoodooRushDevice::~VoodooRushDevice() = default;

void VoodooRushDevice::reset() {
    m_regs.fill(0);
    m_tmu0_regs.fill(0);
    m_pci_fifo.reset();
    m_mem_fifo.reset();

    m_rgboffs[0] = m_rgboffs[1] = m_rgboffs[2] = 0;
    m_auxoffs = ~0;
    m_frontbuf = 0;
    m_backbuf = 1;
    m_swaps_pending = 0;
    m_video_changed = true;
    m_width = 512;
    m_height = 384;
    m_xoffs = m_yoffs = 0;

    m_init_enable = 0;
    m_regs[reg_fbiInit0] = (1 << 4) | (0x10 << 6);
    m_regs[reg_fbiInit1] = (1 << 1) | (1 << 8) | (1 << 12) | (2 << 20);
    m_regs[reg_fbiInit2] = (1 << 6) | (0x100 << 23);
    m_regs[reg_fbiInit3] = (2 << 13) | (0xf << 17);
    m_regs[reg_fbiInit4] = 1;

    m_stall_state = NOT_STALLED;

    // Init CLUT: expand 5-bit values to 8-bit so the default gradient is visible
    for (int i = 0; i < 32; i++) {
        uint32_t v = (i << 3) | (i >> 2);
        m_clut[i] = 0xFF000000 | (v << 16) | (v << 8) | v;
    }
    m_clut[32] = 0x20FFFFFF;
    m_clut_dirty = true;

    // Allocate memory
    int total = m_fbmem_mb + m_tmumem_mb[0] + m_tmumem_mb[1];
    m_memory = std::make_unique<uint8_t[]>(total * 1024 * 1024 + 4096);
    m_fbram = (uint8_t*)(((uintptr_t)m_memory.get() + 4095) & ~4095);
    m_fbmask = m_fbmem_mb * 1024 * 1024 - 1;

    uint8_t* ptr = m_fbram + m_fbmem_mb * 1024 * 1024;
    for (int i = 0; i < 2; i++) {
        m_tmumem[i] = (m_tmumem_mb[i] > 0) ? ptr : nullptr;
        ptr += m_tmumem_mb[i] * 1024 * 1024;
    }

    recompute_video_memory();
}

// Register access helpers
uint32_t VoodooRushDevice::read(uint32_t offset) {
    uint32_t region = offset >> 22;
    if (region == 0) return read_register(offset & 0xFF);
    if (region == 1) return lfb_read(offset & 0x3FFFFF);
    return 0xFFFFFFFF;
}

void VoodooRushDevice::write(uint32_t offset, uint32_t data, uint32_t mem_mask) {
    uint32_t region = offset >> 22;
    if (region == 0) {
        uint32_t regnum = offset & 0xFF;
        if (m_init_enable & 2) {
            // FIFO mode
            add_to_fifo(TYPE_REGISTER | (regnum), data);
        } else {
            write_register(regnum, data);
        }
    } else if (region == 1) {
        if (m_init_enable & 2) {
            add_to_fifo(TYPE_LFB | (offset & 0x3FFFFF), data);
        } else {
            lfb_write(offset & 0x3FFFFF, data, mem_mask);
        }
    }
}

void VoodooRushDevice::write_register(int regnum, uint32_t data) {
    if (!m_mem) return;
    if (regnum < 0 || regnum >= (int)m_regs.size()) return;
    m_regs[regnum] = data;

    // Handle special registers
    switch (regnum) {
    case reg_fbiInit0:
        if (data & 0x02000000) reset();
        break;
    case reg_fbiInit1:
    case reg_fbiInit2:
        recompute_video_memory();
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
    case reg_triangleCMD:
        draw_triangle();
        break;
    case reg_fastfillCMD:
        draw_fastfill();
        break;
    case reg_swapbufferCMD:
        m_swaps_pending++;
        // In Voodoo 1/Rush, swap is immediate (no vblank sync without processing)
        if (!(data & 1)) {
            // Immediate swap
            m_frontbuf = (m_frontbuf + 1) % 2;
            m_backbuf = (m_backbuf + 1) % 2;
            m_video_changed = true;
            m_swaps_pending--;
        }
        break;
    }
}

void VoodooRushDevice::recompute_video_memory() {
    uint32_t config = bits<6,1>(m_regs[reg_fbiInit2]);
    uint32_t xtiles = bits<0,4>(m_regs[reg_fbiInit1])
                    | (bits<24,1>(m_regs[reg_fbiInit1]) << 4);
    int rowpixels = xtiles * 64;
    if (rowpixels < 640) rowpixels = 640;
    m_rowpixels = rowpixels;

    m_rgboffs[0] = 0;
    uint32_t buf_pages = bits<19,9>(m_regs[reg_fbiInit2]);
    m_rgboffs[1] = buf_pages * 0x1000;

    if (config == 0) {
        // Double buffered: 2 color + 1 aux
        m_rgboffs[2] = ~0;
        m_auxoffs = 2 * buf_pages * 0x1000;
    } else {
        // Triple buffered
        m_rgboffs[2] = 2 * buf_pages * 0x1000;
        m_auxoffs = (config == 2) ? 3 * buf_pages * 0x1000 : ~0;
    }

    m_video_changed = true;
}

uint16_t* VoodooRushDevice::front_buffer() {
    if (m_rgboffs[m_frontbuf] == ~0) return nullptr;
    return (uint16_t*)(m_fbram + m_rgboffs[m_frontbuf]);
}

uint16_t* VoodooRushDevice::back_buffer() {
    if (m_rgboffs[m_backbuf] == ~0) return nullptr;
    return (uint16_t*)(m_fbram + m_rgboffs[m_backbuf]);
}

uint16_t* VoodooRushDevice::aux_buffer() {
    if (m_auxoffs == ~0) return nullptr;
    return (uint16_t*)(m_fbram + m_auxoffs);
}

uint16_t* VoodooRushDevice::draw_buffer(int buf) {
    if (buf < 3 && m_rgboffs[buf] != ~0)
        return (uint16_t*)(m_fbram + m_rgboffs[buf]);
    return front_buffer();
}

void VoodooRushDevice::add_to_fifo(uint32_t offset, uint32_t data) {
    m_pci_fifo.push(offset, data);
    // Check for memory FIFO spill (simplified - no memory FIFO for now)
}

void VoodooRushDevice::flush_fifos() {
    while (!m_pci_fifo.empty()) {
        uint32_t offset = m_pci_fifo.remove();
        uint32_t data = m_pci_fifo.remove();

        uint32_t type = offset & TYPE_MASK;
        uint32_t addr = offset & ~(TYPE_MASK | FLAGS_MASK);

        switch (type) {
        case TYPE_REGISTER:
            write_register(addr & 0xFF, data);
            break;
        case TYPE_LFB:
            lfb_write(addr, data, 0xFFFFFFFF);
            break;
        case TYPE_TEXTURE:
            // TMU texture write
            if (m_tmu0_regs.size() > 0) {
                int tmunum = bits<19,2>(offset);
                // Simplified texture store
            }
            break;
        }
    }
}

bool VoodooRushDevice::operation_pending() {
    return !m_pci_fifo.empty() || !m_mem_fifo.empty();
}

// LFB access
uint32_t VoodooRushDevice::lfb_read(uint32_t offset) {
    uint32_t x = offset % m_rowpixels;
    uint32_t y = offset / m_rowpixels;
    if (y >= (uint32_t)m_height) return 0xFFFFFFFF;
    uint16_t* buf = front_buffer();
    if (!buf) return 0xFFFFFFFF;
    uint32_t maxwords = (m_fbmem_mb * 1024 * 1024) / 2;
    if (y * m_rowpixels + x + 1 >= maxwords) return 0xFFFFFFFF;
    buf += y * m_rowpixels + x;
    return buf[0] | (buf[1] << 16);
}

void VoodooRushDevice::lfb_write(uint32_t offset, uint32_t data, uint32_t mem_mask) {
    uint32_t x = offset % m_rowpixels;
    uint32_t y = offset / m_rowpixels;
    if (y >= (uint32_t)m_height) return;
    uint16_t* buf = back_buffer();
    if (!buf) return;
    uint32_t maxwords = (m_fbmem_mb * 1024 * 1024) / 2;
    if (y * m_rowpixels + x + 1 >= maxwords) return;
    buf += y * m_rowpixels + x;
    if (mem_mask & 0xFFFF) buf[0] = data & 0xFFFF;
    if (mem_mask & 0xFFFF0000) buf[1] = (data >> 16) & 0xFFFF;
}

// Status register
uint32_t VoodooRushDevice::read_status() {
    uint32_t result = std::min(m_pci_fifo.space() / 2, 0x3F) << 0;
    if (m_vblank) result |= 1 << 6;
    if (operation_pending()) result |= (1 << 7) | (1 << 8) | (1 << 9);
    result |= m_frontbuf << 10;
    result |= std::min(m_swaps_pending, 7) << 28;
    return result;
}

uint32_t VoodooRushDevice::read_register(int regnum) {
    switch (regnum) {
    case reg_status: return read_status();
    case reg_vRetrace: return m_vblank ? 0 : 480;  // simplified
    default:
        if (regnum < (int)m_regs.size()) return m_regs[regnum];
        return 0;
    }
}

// Display output
void VoodooRushDevice::draw_framebuffer(uint32_t* out, int w, int h) {
    if (!m_fbram) return;
    auto* buf = front_buffer();
    if (!buf) return;

    // Recompute pens if CLUT changed
    if (m_clut_dirty) {
        uint8_t rt[32], gt[64], bt[32];
        for (int i = 0; i < 32; i++) {
            rt[i] = ((m_clut[i] >> 16) & 0xFF);
            bt[i] = (m_clut[i] & 0xFF);
        }
        for (int i = 0; i < 64; i++) {
            gt[i] = ((m_clut[i/2] >> 8) & 0xFF);
        }
        for (uint32_t pen = 0; pen < 65536; pen++) {
            int r = bits<11,5>(pen), g = bits<5,6>(pen), b = bits<0,5>(pen);
            m_pen[pen] = 0xFF000000 | (rt[r] << 16) | (gt[g] << 8) | bt[b];
        }
        m_clut_dirty = false;
    }

    // Copy front buffer to output with pen lookup.
    // Row stride follows the LFB pitch (rowpixels), matching the addressing
    // used by lfb_read/lfb_write.
    for (int y = 0; y < h && y < m_height; y++) {
        int sy = y + m_yoffs;
        if (sy < 0 || sy >= m_height) continue;
        auto* src = buf + sy * m_rowpixels - m_xoffs;
        auto* dst = out + y * w;
        for (int x = 0; x < w && x < m_rowpixels; x++) {
            dst[x] = m_pen[src[x]];
        }
    }
}

int VoodooRushDevice::update(uint32_t* rgb_buffer, int width, int height) {
    // Flush pending FIFO operations
    if (operation_pending())
        flush_fifos();

    if (!m_fbram || !rgb_buffer) return 0;

    // Clear output to transparent so undrawn pixels let the 2D framebuffer show
    std::fill_n(rgb_buffer, width * height, 0x00000000);

    if (!(m_regs[reg_fbiInit1] & (1 << 12))) {
        // Not blanked - draw
        draw_framebuffer(rgb_buffer, width, height);
    }

    bool changed = m_video_changed;
    m_video_changed = false;
    return changed ? 1 : 0;
}

uint16_t VoodooRushDevice::pack_rgb565(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t VoodooRushDevice::pack_rgb555(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

void VoodooRushDevice::draw_fastfill() {
    if (!m_fbram) return;
    uint16_t* buf = back_buffer();
    if (!buf) return;

    uint32_t color = m_regs[reg_color1];
    int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    bool is555 = (m_regs[reg_fbzColorPath] & 1) != 0;
    uint16_t pix = is555 ? pack_rgb555(r, g, b) : pack_rgb565(r, g, b);

    int32_t left = m_regs[reg_clipLeft], right = m_regs[reg_clipRight];
    int32_t top = m_regs[reg_clipTop], bottom = m_regs[reg_clipBottom];

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

void VoodooRushDevice::draw_triangle() {
    if (!m_fbram) return;
    uint16_t* buf = back_buffer();
    if (!buf) return;

    // Vertices are signed 12.4 fixed point (1/16 pixel)
    int16_t ax = (int16_t)(m_regs[reg_vertexAx] & 0xFFFF);
    int16_t ay = (int16_t)(m_regs[reg_vertexAy] & 0xFFFF);
    int16_t bx = (int16_t)(m_regs[reg_vertexBx] & 0xFFFF);
    int16_t by = (int16_t)(m_regs[reg_vertexBy] & 0xFFFF);
    int16_t cx = (int16_t)(m_regs[reg_vertexCx] & 0xFFFF);
    int16_t cy = (int16_t)(m_regs[reg_vertexCy] & 0xFFFF);

    double x0 = ax / 16.0, y0 = ay / 16.0;
    double x1 = bx / 16.0, y1 = by / 16.0;
    double x2 = cx / 16.0, y2 = cy / 16.0;

    // Signed-area test (works for both winding orders)
    double area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
    if (area == 0.0) return;

    // Color iterants are x.12 fixed point; 8-bit color = value >> 12
    double r0 = (double)(int32_t)m_regs[reg_startR];
    double g0 = (double)(int32_t)m_regs[reg_startG];
    double b0 = (double)(int32_t)m_regs[reg_startB];
    double dRdx = (double)(int32_t)m_regs[reg_dRdX], dRdy = (double)(int32_t)m_regs[reg_dRdY];
    double dGdx = (double)(int32_t)m_regs[reg_dGdX], dGdy = (double)(int32_t)m_regs[reg_dGdY];
    double dBdx = (double)(int32_t)m_regs[reg_dBdX], dBdy = (double)(int32_t)m_regs[reg_dBdY];

    auto iterant8 = [](double v) -> int {
        int c = (int)(v / 4096.0);
        if (c < 0) c = 0;
        if (c > 255) c = 255;
        return c;
    };

    // Color at each vertex
    auto vcol = [&](int16_t vx, int16_t vy) -> uint32_t {
        double dx = vx - ax, dy = vy - ay;
        int r = iterant8(r0 + dRdx * dx / 16.0 + dRdy * dy / 16.0);
        int g = iterant8(g0 + dGdx * dx / 16.0 + dGdy * dy / 16.0);
        int b = iterant8(b0 + dBdx * dx / 16.0 + dBdy * dy / 16.0);
        return (uint32_t)((r << 16) | (g << 8) | b);
    };

    uint32_t c0 = vcol(ax, ay);
    uint32_t c1 = vcol(bx, by);
    uint32_t c2 = vcol(cx, cy);

    bool constant_rgb = (m_regs[reg_fbzColorPath] & 0x02) != 0;
    bool is555 = (m_regs[reg_fbzColorPath] & 1) != 0;

    // Bounding box
    int minx = std::max(0, (int)std::min({x0, x1, x2}));
    int maxx = std::min(m_rowpixels - 1, (int)std::max({x0, x1, x2}));
    int miny = std::max(0, (int)std::min({y0, y1, y2}));
    int maxy = std::min(m_height - 1, (int)std::max({y0, y1, y2}));

    // Optional clip rectangle (defaults of 0 disable clipping)
    int32_t cl = m_regs[reg_clipLeft], cr = m_regs[reg_clipRight];
    int32_t ct = m_regs[reg_clipTop], cb = m_regs[reg_clipBottom];
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
                auto lerp8 = [](double v) -> int {
                    int c = (int)v;
                    if (c < 0) c = 0;
                    if (c > 255) c = 255;
                    return c;
                };
                r = lerp8((c0 >> 16) * w0 + (c1 >> 16) * w1 + (c2 >> 16) * w2);
                g = lerp8(((c0 >> 8) & 0xFF) * w0 + ((c1 >> 8) & 0xFF) * w1 + ((c2 >> 8) & 0xFF) * w2);
                b = lerp8((c0 & 0xFF) * w0 + (c1 & 0xFF) * w1 + (c2 & 0xFF) * w2);
            }
            row[x] = is555 ? pack_rgb555(r, g, b) : pack_rgb565(r, g, b);
        }
    }
    m_video_changed = true;
}

} // namespace voodoo
