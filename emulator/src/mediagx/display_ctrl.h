#pragma once
#include <cstdint>
#include <array>
#include "bus/memory_bus.h"

class DisplayController {
public:
    DisplayController();

    void reset();

    uint32_t read(int offset);
    void write(int offset, uint32_t data, uint32_t mask = 0xFFFFFFFF);

    int width() const { return m_frame_width; }
    int height() const { return m_frame_height; }
    bool is_8bit() const { return (m_regs[0x0C/4] & 1) != 0; }
    int stride() const { return (m_regs[0x24/4] & 0x3FF) * 4; }
    uint32_t fb_offset() const { return m_regs[0x10/4]; }

    void set_framebuffer(uint8_t* fb) { m_framebuffer = fb; }
    void render(uint32_t* output_buf, int out_width, int out_height);

private:
    std::array<uint32_t, 64> m_regs;
    int m_frame_width = 640;
    int m_frame_height = 480;
    uint8_t* m_framebuffer = nullptr;
    uint8_t m_palette[768]{};
    uint8_t m_pal_index = 0;
    uint8_t m_pal_byte = 0;

    void render_8bit(uint32_t* out, int ow, int oh);
    void render_16bit_565(uint32_t* out, int ow, int oh);
    void render_16bit_555(uint32_t* out, int ow, int oh);
};
