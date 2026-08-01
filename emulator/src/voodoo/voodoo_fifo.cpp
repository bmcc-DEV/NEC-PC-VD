#include "voodoo_fifo.h"
#include "voodoo2.h"
#include <bit>
#include <cstring>

namespace voodoo {

Cmdfifo::Cmdfifo() = default;
Cmdfifo::~Cmdfifo() = default;

void Cmdfifo::configure(uint8_t* ram, uint32_t base, uint32_t end) {
    m_ram = ram;
    m_base = base;
    m_end = end;
    m_size_words = (end - base) / 4;
    reset_execution();
}

void Cmdfifo::reset_execution() {
    m_rd = 0;
    m_wr = 0;
    m_depth = 0;
    m_holes = 0;
}

void Cmdfifo::write(uint32_t addr, uint32_t data) {
    if (!m_enable || !m_ram) return;
    if (addr < m_base || addr >= m_end) return;

    uint32_t wo = (addr - m_base) / 4;
    // Store the word little-endian at the ring slot
    uint32_t slot = wo % m_size_words;
    m_ram[m_base + slot * 4 + 0] = (data >> 0) & 0xFF;
    m_ram[m_base + slot * 4 + 1] = (data >> 8) & 0xFF;
    m_ram[m_base + slot * 4 + 2] = (data >> 16) & 0xFF;
    m_ram[m_base + slot * 4 + 3] = (data >> 24) & 0xFF;

    // In-order writes extend the contiguous depth
    if (wo == m_rd + m_depth)
        m_depth++;
    if (wo >= m_wr)
        m_wr = wo + 1;

    execute_if_ready();
}

void Cmdfifo::write_direct(uint32_t offset, uint32_t data) {
    if (!m_enable || !m_ram) return;

    uint32_t slot = offset % m_size_words;
    m_ram[m_base + slot * 4 + 0] = (data >> 0) & 0xFF;
    m_ram[m_base + slot * 4 + 1] = (data >> 8) & 0xFF;
    m_ram[m_base + slot * 4 + 2] = (data >> 16) & 0xFF;
    m_ram[m_base + slot * 4 + 3] = (data >> 24) & 0xFF;

    if (offset == m_rd + m_depth)
        m_depth++;
    if (offset >= m_wr)
        m_wr = offset + 1;

    execute_if_ready();
}

uint32_t Cmdfifo::read_next() {
    uint32_t slot = m_rd % m_size_words;
    uint32_t v = m_ram[m_base + slot * 4 + 0]
        | (m_ram[m_base + slot * 4 + 1] << 8)
        | (m_ram[m_base + slot * 4 + 2] << 16)
        | (m_ram[m_base + slot * 4 + 3] << 24);
    m_rd++;
    if (m_depth > 0) m_depth--;
    return v;
}

static inline float as_float(uint32_t v) {
    float f;
    __builtin_memcpy(&f, &v, sizeof(f));
    return f;
}

float Cmdfifo::read_next_float() {
    return as_float(read_next());
}

void Cmdfifo::consume(uint32_t count) {
    for (uint32_t i = 0; i < count; i++) read_next();
}

uint32_t Cmdfifo::words_needed(uint32_t command) {
    switch (command & 7) {
    case 0:
        // Packet type 0: 1 or 2 words (JMP AGP uses 2)
        return bits<3, 3>(command) == 4 ? 2 : 1;
    case 1:
        // Packet type 1: 1 + N words
        return 1 + bits<16, 16>(command);
    case 2:
        // Packet type 2: 1 + popcount(mask) words
        return 1 + (uint32_t)std::popcount(bits<3, 29>(command));
    case 3: {
        // Packet type 3: 1 + count*words_per_vertex + dummies
        uint32_t count = bits<6, 4>(command);
        uint32_t wpv = 2;  // X/Y
        if (bits<28, 1>(command)) {
            wpv += (bits<10, 2>(command) != 0) ? 1 : 0;      // packed ARGB
        } else {
            wpv += 3 * bits<10, 1>(command) + bits<11, 1>(command);  // RGB + A
        }
        wpv += bits<12, 1>(command);   // Z
        wpv += bits<13, 1>(command);   // Wb
        wpv += bits<14, 1>(command);   // W0
        wpv += 2 * bits<15, 1>(command); // S0/T0
        wpv += bits<16, 1>(command);   // W1
        wpv += 2 * bits<17, 1>(command); // S1/T1
        return 1 + count * wpv + bits<29, 3>(command);
    }
    case 4:
        // Packet type 4: 1 + popcount(mask) + dummies
        return 1 + (uint32_t)std::popcount(bits<15, 14>(command)) + bits<29, 3>(command);
    case 5:
        // Packet type 5: 2 + N words
        return 2 + bits<3, 19>(command);
    default:
        return 1;
    }
}

uint32_t Cmdfifo::execute_if_ready() {
    if (!m_enable || !m_device) return 0;

    while (m_depth > 0) {
        uint32_t slot = m_rd % m_size_words;
        uint32_t cmd = m_ram[m_base + slot * 4 + 0]
            | (m_ram[m_base + slot * 4 + 1] << 8)
            | (m_ram[m_base + slot * 4 + 2] << 16)
            | (m_ram[m_base + slot * 4 + 3] << 24);

        uint32_t needed = words_needed(cmd);
        if (m_depth < needed) break;

        // Consume the packet header word
        read_next();

        uint32_t type = cmd & 7;
        uint32_t cycles = 0;
        switch (type) {
        case 0: cycles = packet_type_0(cmd); break;
        case 1: cycles = packet_type_1(cmd); break;
        case 2: cycles = packet_type_2(cmd); break;
        case 3: cycles = packet_type_3(cmd); break;
        case 4: cycles = packet_type_4(cmd); break;
        case 5: cycles = packet_type_5(cmd); break;
        default:
            break;
        }
        (void)cycles;
    }
    return 0;
}

uint32_t Cmdfifo::packet_type_0(uint32_t command) {
    // Function: 0=NOP, 1=JSR, 2=RET, 3=JMP LOCAL, 4=JMP AGP
    uint32_t func = bits<3, 3>(command);
    uint32_t target = bits<6, 23>(command) << 2;   // byte address in framebuffer

    switch (func) {
    case 3: // JMP LOCAL
        if (target >= m_base && target < m_end) {
            m_rd = (target - m_base) / 4;
            m_depth = (m_wr > m_rd) ? (m_wr - m_rd) : 0;
        }
        break;
    case 4: // JMP AGP uses a second word
        read_next();
        break;
    default: // NOP / JSR / RET: header already consumed
        break;
    }
    return 0;
}

uint32_t Cmdfifo::packet_type_1(uint32_t command) {
    uint32_t count = bits<16, 16>(command);
    uint32_t inc = bits<15, 1>(command);
    uint32_t target = bits<3, 12>(command);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t data = read_next();
        if (m_device) m_device->cmdfifo_register_w(target, data);
        target += inc;
    }
    return 0;
}

uint32_t Cmdfifo::packet_type_2(uint32_t command) {
    // 2D register mask write
    for (uint32_t bit = 3; bit <= 31; bit++) {
        if (command & (1u << bit)) {
            uint32_t data = read_next();
            if (m_device) m_device->cmdfifo_2d_w(bit - 3, data);
        }
    }
    return 0;
}

uint32_t Cmdfifo::packet_type_3(uint32_t command) {
    uint32_t count = bits<6, 4>(command);
    uint32_t code = bits<3, 3>(command);

    if (m_device) m_device->cmdfifo_set_setup_mode(bits<10, 8>(command) | (bits<22, 4>(command) << 16));

    // Parse each vertex per the setup flags
    for (uint32_t i = 0; i < count; i++) {
        SetupVertex sv = {};
        sv.x = read_next_float();
        sv.y = read_next_float();

        if (bits<28, 1>(command)) {
            // Packed ARGB
            if (bits<10, 2>(command) != 0) {
                uint32_t argb = read_next();
                if (bits<10, 1>(command)) {
                    sv.r = (float)((argb >> 16) & 0xFF);
                    sv.g = (float)((argb >> 8) & 0xFF);
                    sv.b = (float)(argb & 0xFF);
                }
                if (bits<11, 1>(command)) sv.a = (float)((argb >> 24) & 0xFF);
            }
        } else {
            if (bits<10, 1>(command)) { sv.r = read_next_float(); sv.g = read_next_float(); sv.b = read_next_float(); }
            if (bits<11, 1>(command)) sv.a = read_next_float();
        }
        if (bits<12, 1>(command)) sv.z = read_next_float();
        if (bits<13, 1>(command)) sv.wb = sv.w0 = sv.w1 = read_next_float();
        if (bits<14, 1>(command)) sv.w0 = sv.w1 = read_next_float();
        if (bits<15, 1>(command)) { sv.s0 = sv.s1 = read_next_float(); sv.t0 = sv.t1 = read_next_float(); }
        if (bits<16, 1>(command)) sv.w1 = read_next_float();
        if (bits<17, 1>(command)) { sv.s1 = read_next_float(); sv.t1 = read_next_float(); }

        if (m_device) m_device->cmdfifo_triangle_vertex(sv, command, i);
    }

    consume(bits<29, 3>(command));
    return 0;
}

uint32_t Cmdfifo::packet_type_4(uint32_t command) {
    uint32_t target = bits<3, 12>(command);
    for (uint32_t bit = 15; bit <= 28; bit++) {
        if (command & (1u << bit)) {
            uint32_t data = read_next();
            if (m_device) m_device->cmdfifo_register_w(target, data);
        }
        target++;
    }
    consume(bits<29, 3>(command));
    return 0;
}

uint32_t Cmdfifo::packet_type_5(uint32_t command) {
    uint32_t count = bits<3, 19>(command);
    uint32_t target = read_next() / 4;

    switch (bits<30, 2>(command)) {
    case 0: // Linear framebuffer
        for (uint32_t i = 0; i < count; i++) {
            uint32_t data = read_next();
            if (m_device) m_device->cmdfifo_lfb_w(target++, data);
        }
        break;
    case 3: // Texture port
        for (uint32_t i = 0; i < count; i++) {
            uint32_t data = read_next();
            if (m_device) m_device->cmdfifo_texture_w(target++, data);
        }
        break;
    default:
        for (uint32_t i = 0; i < count; i++) read_next();
        break;
    }
    return 0;
}

} // namespace voodoo
