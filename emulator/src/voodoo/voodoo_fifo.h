#pragma once
#include <cstdint>
#include "voodoo_defs.h"

namespace voodoo {

class Voodoo2Device;

// Command FIFO (CMDFIFO) ring buffer + packet parser.
//
// The FIFO lives in off-screen framebuffer memory between the base and end
// addresses programmed through cmdFifoBaseAddr. Software feeds command
// packets (types 0-5) into the ring; the parser executes them as they
// become available.
class Cmdfifo {
public:
    Cmdfifo();
    ~Cmdfifo();

    void set_device(Voodoo2Device* dev) { m_device = dev; }

    void configure(uint8_t* ram, uint32_t base, uint32_t end);
    void set_enable(bool e) { m_enable = e; reset_execution(); }
    bool enabled() const { return m_enable; }

    void set_read_pointer(uint32_t v) { m_rd = v; m_depth = 0; }
    void set_address_min(uint32_t v) { m_address_min = v; }
    void set_address_max(uint32_t v) { m_address_max = v; }
    void set_depth(uint32_t v) { m_depth = v; }
    void set_holes(uint32_t v) { m_holes = v; }

    uint32_t read_pointer() const { return m_rd; }
    uint32_t depth() const { return m_depth; }
    uint32_t holes() const { return m_holes; }

    // LFB-style write into the FIFO region (with hole tracking)
    void write(uint32_t addr, uint32_t data);
    // Direct write at a word offset (register window access)
    void write_direct(uint32_t offset, uint32_t data);

    // Execute available packets until none can run
    uint32_t execute_if_ready();

private:
    uint32_t words_needed(uint32_t command);
    uint32_t packet_type_0(uint32_t command);
    uint32_t packet_type_1(uint32_t command);
    uint32_t packet_type_2(uint32_t command);
    uint32_t packet_type_3(uint32_t command);
    uint32_t packet_type_4(uint32_t command);
    uint32_t packet_type_5(uint32_t command);

    uint32_t read_next();
    float read_next_float();
    void consume(uint32_t count);
    void reset_execution();

    Voodoo2Device* m_device = nullptr;

    uint8_t* m_ram = nullptr;
    uint32_t m_base = 0;        // FIFO base byte offset in framebuffer RAM
    uint32_t m_end = 0;         // FIFO end byte offset in framebuffer RAM
    uint32_t m_size_words = 0;  // ring size in words

    bool m_enable = false;
    bool m_count_holes = false;

    uint32_t m_rd = 0;          // read index (word, relative to base)
    uint32_t m_wr = 0;          // highest written word index (relative to base)
    uint32_t m_depth = 0;       // contiguous words available from m_rd
    uint32_t m_holes = 0;

    uint32_t m_address_min = 0;
    uint32_t m_address_max = 0;
};

} // namespace voodoo
