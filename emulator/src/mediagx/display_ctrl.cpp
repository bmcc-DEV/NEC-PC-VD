#include "display_ctrl.h"
#include <cstdio>
#include <algorithm>

enum {
    DC_UNLOCK       = 0x00/4,
    DC_GENERAL_CFG  = 0x04/4,
    DC_TIMING_CFG   = 0x08/4,
    DC_OUTPUT_CFG   = 0x0C/4,
    DC_FB_ST_OFFSET = 0x10/4,
    DC_CB_ST_OFFSET = 0x14/4,
    DC_CUR_ST_OFFSET= 0x18/4,
    DC_VID_ST_OFFSET= 0x20/4,
    DC_LINE_DELTA   = 0x24/4,
    DC_BUF_SIZE     = 0x28/4,
    DC_H_TIMING_1   = 0x30/4,
    DC_H_TIMING_2   = 0x34/4,
    DC_H_TIMING_3   = 0x38/4,
    DC_FP_H_TIMING  = 0x3C/4,
    DC_V_TIMING_1   = 0x40/4,
    DC_V_TIMING_2   = 0x44/4,
    DC_V_TIMING_3   = 0x48/4,
    DC_FP_V_TIMING  = 0x4C/4,
    DC_CURSOR_X     = 0x50/4,
    DC_V_LINE_CNT   = 0x54/4,
    DC_CURSOR_Y     = 0x58/4,
    DC_SS_LINE_CMP  = 0x5C/4,
    DC_PAL_ADDRESS  = 0x70/4,
    DC_PAL_DATA     = 0x74/4,
    DC_DFIFO_DIAG   = 0x78/4,
    DC_CFIFO_DIAG   = 0x7C/4,
};

DisplayController::DisplayController() {
    reset();
}

void DisplayController::reset() {
    m_regs.fill(0);
    m_frame_width = 640;
    m_frame_height = 480;
    m_pal_index = 0;
    m_pal_byte = 0;
}

void DisplayController::write(int offset, uint32_t data, uint32_t mask) {
    if (mask != 0xFFFFFFFF) {
        uint32_t old = m_regs[offset];
        for (int i = 0; i < 4; i++) {
            if (mask & (0xFF << (i * 8)))
                m_regs[offset] = (old & ~(0xFF << (i * 8))) | (data & (0xFF << (i * 8)));
        }
    } else {
        m_regs[offset] = data;
    }

    if (offset == DC_PAL_ADDRESS) {
        // DC_PAL_ADDRESS holds the color index (0..255)
        m_regs[offset] = data & 0xFF;
        m_pal_index = data & 0xFF;
        m_pal_byte = 0;
    } else if (offset == DC_PAL_DATA) {
        // Three consecutive byte writes fill R,G,B of the current color index
        m_palette[m_pal_index * 3 + m_pal_byte] = data & 0xFF;
        m_pal_byte = (m_pal_byte + 1) % 3;
        if (m_pal_byte == 0) m_pal_index = (m_pal_index + 1) & 0xFF;
        m_regs[DC_PAL_ADDRESS] = m_pal_index;
    } else if (offset == DC_H_TIMING_1) {
        int w = (data & 0x7FF) + 1;
        if (m_regs[DC_TIMING_CFG] & 0x8000) w >>= 1;
        m_frame_width = std::min<uint32_t>(w + 4, 800);
    } else if (offset == DC_V_TIMING_1) {
        m_frame_height = std::min<uint32_t>((data & 0x7FF) + 1, 600);
    }
}

uint32_t DisplayController::read(int offset) {
    if (offset == DC_TIMING_CFG) {
        return m_regs[offset];
    }
    return m_regs[offset];
}

void DisplayController::render(uint32_t* output_buf, int out_width, int out_height) {
    if (!m_framebuffer) return;

    if (is_8bit())
        render_8bit(output_buf, out_width, out_height);
    else if ((m_regs[DC_OUTPUT_CFG] & 0x2) == 0)
        render_16bit_565(output_buf, out_width, out_height);
    else
        render_16bit_555(output_buf, out_width, out_height);
}

void DisplayController::render_8bit(uint32_t* out, int ow, int oh) {
    uint32_t fb_start = m_regs[DC_FB_ST_OFFSET];
    int stride_pixels = stride();
    int w = std::min(m_frame_width, ow);
    int h = std::min(m_frame_height, oh);

    for (int y = 0; y < h; y++) {
        auto* src = m_framebuffer + fb_start + y * stride_pixels;
        auto* dst = out + y * ow;
        for (int x = 0; x < w; x++) {
            int c = src[x];
            int r = m_palette[c * 3 + 0] << 2;
            int g = m_palette[c * 3 + 1] << 2;
            int b = m_palette[c * 3 + 2] << 2;
            dst[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
}

void DisplayController::render_16bit_565(uint32_t* out, int ow, int oh) {
    uint32_t fb_start = m_regs[DC_FB_ST_OFFSET];
    int stride_bytes = stride();
    int w = std::min(m_frame_width, ow);
    int h = std::min(m_frame_height, oh);

    for (int y = 0; y < h; y++) {
        auto* src = m_framebuffer + fb_start + y * stride_bytes;
        auto* dst = out + y * ow;
        for (int x = 0; x < w; x++) {
            uint16_t c = src[x * 2] | (src[x * 2 + 1] << 8);
            int r = ((c >> 11) & 0x1F) << 3;
            int g = ((c >> 5) & 0x3F) << 2;
            int b = (c & 0x1F) << 3;
            dst[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
}

void DisplayController::render_16bit_555(uint32_t* out, int ow, int oh) {
    uint32_t fb_start = m_regs[DC_FB_ST_OFFSET];
    int stride_bytes = stride();
    int w = std::min(m_frame_width, ow);
    int h = std::min(m_frame_height, oh);

    for (int y = 0; y < h; y++) {
        auto* src = m_framebuffer + fb_start + y * stride_bytes;
        auto* dst = out + y * ow;
        for (int x = 0; x < w; x++) {
            uint16_t c = src[x * 2] | (src[x * 2 + 1] << 8);
            int r = ((c >> 10) & 0x1F) << 3;
            int g = ((c >> 5) & 0x1F) << 3;
            int b = (c & 0x1F) << 3;
            dst[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }
    }
}
