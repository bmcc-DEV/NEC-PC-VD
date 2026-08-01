/*
 * voodoo_fifo.c - Voodoo2 EC command FIFO (CMDFIFO) implementation.
 */
#include "voodoo_fifo.h"
#include "voodoo2_ec.h"
#include <string.h>
#include <stdint.h>

static uint32_t popcount32(uint32_t v) {
    uint32_t c = 0;
    while (v) { c += v & 1; v >>= 1; }
    return c;
}

static inline float as_float(uint32_t v) {
    float f;
    memcpy(&f, &v, 4);
    return f;
}

void v2cmdfifo_init(V2Cmdfifo* f, Voodoo2EC* dev) {
    memset(f, 0, sizeof(*f));
    f->device = dev;
}

void v2cmdfifo_configure(V2Cmdfifo* f, uint8_t* ram, uint32_t base, uint32_t end) {
    f->ram = ram;
    f->base = base;
    f->end = end;
    f->size_words = (end - base) / 4;
    f->rd = f->wr = f->depth = f->holes = 0;
}

void v2cmdfifo_set_enable(V2Cmdfifo* f, bool e) {
    f->enable = e;
    f->rd = f->wr = f->depth = f->holes = 0;
}

void v2cmdfifo_set_read_pointer(V2Cmdfifo* f, uint32_t v) { f->rd = v; f->depth = 0; }
void v2cmdfifo_set_address_min(V2Cmdfifo* f, uint32_t v) { f->address_min = v; }
void v2cmdfifo_set_address_max(V2Cmdfifo* f, uint32_t v) { f->address_max = v; }
void v2cmdfifo_set_depth(V2Cmdfifo* f, uint32_t v) { f->depth = v; }
void v2cmdfifo_set_holes(V2Cmdfifo* f, uint32_t v) { f->holes = v; }
bool v2cmdfifo_enabled(const V2Cmdfifo* f) { return f->enable; }
uint32_t v2cmdfifo_read_pointer(const V2Cmdfifo* f) { return f->rd; }
uint32_t v2cmdfifo_depth(const V2Cmdfifo* f) { return f->depth; }
uint32_t v2cmdfifo_holes(const V2Cmdfifo* f) { return f->holes; }

static uint32_t rd_word(const V2Cmdfifo* f, uint32_t idx) {
    uint32_t slot = idx % f->size_words;
    uint8_t* p = f->ram + f->base + slot * 4;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr_word(const V2Cmdfifo* f, uint32_t idx, uint32_t data) {
    uint32_t slot = idx % f->size_words;
    uint8_t* p = f->ram + f->base + slot * 4;
    p[0] = (uint8_t)(data >> 0);
    p[1] = (uint8_t)(data >> 8);
    p[2] = (uint8_t)(data >> 16);
    p[3] = (uint8_t)(data >> 24);
}

void v2cmdfifo_write(V2Cmdfifo* f, uint32_t addr, uint32_t data) {
    if (!f->enable || !f->ram) return;
    if (addr < f->base || addr >= f->end) return;
    uint32_t wo = (addr - f->base) / 4;
    wr_word(f, wo, data);
    if (wo == f->rd + f->depth) f->depth++;
    if (wo >= f->wr) f->wr = wo + 1;
    v2cmdfifo_execute_if_ready(f);
}

void v2cmdfifo_write_direct(V2Cmdfifo* f, uint32_t offset, uint32_t data) {
    if (!f->enable || !f->ram) return;
    wr_word(f, offset, data);
    if (offset == f->rd + f->depth) f->depth++;
    if (offset >= f->wr) f->wr = offset + 1;
    v2cmdfifo_execute_if_ready(f);
}

static uint32_t next_word(V2Cmdfifo* f) {
    uint32_t v = rd_word(f, f->rd);
    f->rd++;
    if (f->depth > 0) f->depth--;
    return v;
}

static float next_float(V2Cmdfifo* f) { return as_float(next_word(f)); }

static void consume(V2Cmdfifo* f, uint32_t count) {
    while (count--) next_word(f);
}

static uint32_t words_needed(uint32_t cmd) {
    switch (cmd & 7) {
    case 0: return ((cmd >> 3) & 7) == 4 ? 2 : 1;
    case 1: return 1 + ((cmd >> 16) & 0xFFFF);
    case 2: return 1 + popcount32((cmd >> 3) & 0x1FFFFFFF);
    case 3: {
        uint32_t count = (cmd >> 6) & 0xF;
        uint32_t wpv = 2;
        if ((cmd >> 28) & 1) wpv += (((cmd >> 10) & 3) != 0) ? 1 : 0;
        else wpv += 3 * ((cmd >> 10) & 1) + ((cmd >> 11) & 1);
        wpv += (cmd >> 12) & 1;
        wpv += (cmd >> 13) & 1;
        wpv += (cmd >> 14) & 1;
        wpv += 2 * ((cmd >> 15) & 1);
        wpv += (cmd >> 16) & 1;
        wpv += 2 * ((cmd >> 17) & 1);
        return 1 + count * wpv + ((cmd >> 29) & 7);
    }
    case 4: return 1 + popcount32((cmd >> 15) & 0x3FFF) + ((cmd >> 29) & 7);
    case 5: return 2 + ((cmd >> 3) & 0x7FFFF);
    default: return 1;
    }
}

static uint32_t packet_type_0(V2Cmdfifo* f, uint32_t cmd);
static uint32_t packet_type_1(V2Cmdfifo* f, uint32_t cmd);
static uint32_t packet_type_2(V2Cmdfifo* f, uint32_t cmd);
static uint32_t packet_type_3(V2Cmdfifo* f, uint32_t cmd);
static uint32_t packet_type_4(V2Cmdfifo* f, uint32_t cmd);
static uint32_t packet_type_5(V2Cmdfifo* f, uint32_t cmd);

uint32_t v2cmdfifo_execute_if_ready(V2Cmdfifo* f) {
    if (!f->enable || !f->device) return 0;
    while (f->depth > 0) {
        uint32_t cmd = rd_word(f, f->rd);
        uint32_t needed = words_needed(cmd);
        if (f->depth < needed) break;
        next_word(f);   /* consume header */
        switch (cmd & 7) {
        case 0: packet_type_0(f, cmd); break;
        case 1: packet_type_1(f, cmd); break;
        case 2: packet_type_2(f, cmd); break;
        case 3: packet_type_3(f, cmd); break;
        case 4: packet_type_4(f, cmd); break;
        case 5: packet_type_5(f, cmd); break;
        default: break;
        }
    }
    return 0;
}

/* --- packet handlers (callbacks into the device) --- */

static uint32_t packet_type_0(V2Cmdfifo* f, uint32_t cmd) {
    uint32_t func = (cmd >> 3) & 7;
    uint32_t target = ((cmd >> 6) & 0x7FFFFF) << 2;
    if (func == 3) {   /* JMP LOCAL */
        if (target >= f->base && target < f->end) {
            f->rd = (target - f->base) / 4;
            f->depth = (f->wr > f->rd) ? (f->wr - f->rd) : 0;
        }
    } else if (func == 4) {   /* JMP AGP uses a second word */
        next_word(f);
    }
    return 0;
}

static uint32_t packet_type_1(V2Cmdfifo* f, uint32_t cmd) {
    uint32_t count = (cmd >> 16) & 0xFFFF;
    uint32_t inc = (cmd >> 15) & 1;
    uint32_t target = (cmd >> 3) & 0xFFF;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t data = next_word(f);
        voodoo2ec_cmdfifo_reg_write(f->device, target, data);
        target += inc;
    }
    return 0;
}

static uint32_t packet_type_2(V2Cmdfifo* f, uint32_t cmd) {
    for (uint32_t bit = 3; bit <= 31; bit++) {
        if (cmd & (1u << bit)) {
            uint32_t data = next_word(f);
            voodoo2ec_cmdfifo_2d_write(f->device, bit - 3, data);
        }
    }
    return 0;
}

static uint32_t packet_type_3(V2Cmdfifo* f, uint32_t cmd) {
    uint32_t count = (cmd >> 6) & 0xF;
    voodoo2ec_cmdfifo_set_setup_mode(f->device,
        ((cmd >> 10) & 0xFF) | (((cmd >> 22) & 0xF) << 16));

    for (uint32_t i = 0; i < count; i++) {
        V2SetupVertex sv;
        memset(&sv, 0, sizeof(sv));
        sv.x = next_float(f);
        sv.y = next_float(f);

        if ((cmd >> 28) & 1) {
            if (((cmd >> 10) & 3) != 0) {
                uint32_t argb = next_word(f);
                if ((cmd >> 10) & 1) {
                    sv.r = (float)((argb >> 16) & 0xFF);
                    sv.g = (float)((argb >> 8) & 0xFF);
                    sv.b = (float)(argb & 0xFF);
                }
                if ((cmd >> 11) & 1) sv.a = (float)((argb >> 24) & 0xFF);
            }
        } else {
            if ((cmd >> 10) & 1) {
                sv.r = next_float(f); sv.g = next_float(f); sv.b = next_float(f);
            }
            if ((cmd >> 11) & 1) sv.a = next_float(f);
        }
        if ((cmd >> 12) & 1) sv.z = next_float(f);
        if ((cmd >> 13) & 1) sv.wb = sv.w0 = sv.w1 = next_float(f);
        if ((cmd >> 14) & 1) sv.w0 = sv.w1 = next_float(f);
        if ((cmd >> 15) & 1) {
            sv.s0 = sv.s1 = next_float(f);
            sv.t0 = sv.t1 = next_float(f);
        }
        if ((cmd >> 16) & 1) sv.w1 = next_float(f);
        if ((cmd >> 17) & 1) {
            sv.s1 = next_float(f);
            sv.t1 = next_float(f);
        }
        voodoo2ec_cmdfifo_triangle_vertex(f->device, &sv, cmd, i);
    }
    consume(f, (cmd >> 29) & 7);
    return 0;
}

static uint32_t packet_type_4(V2Cmdfifo* f, uint32_t cmd) {
    uint32_t target = (cmd >> 3) & 0xFFF;
    for (uint32_t bit = 15; bit <= 28; bit++) {
        if (cmd & (1u << bit)) {
            uint32_t data = next_word(f);
            voodoo2ec_cmdfifo_reg_write(f->device, target, data);
        }
        target++;
    }
    consume(f, (cmd >> 29) & 7);
    return 0;
}

static uint32_t packet_type_5(V2Cmdfifo* f, uint32_t cmd) {
    uint32_t count = (cmd >> 3) & 0x7FFFF;
    uint32_t target = next_word(f) / 4;
    switch ((cmd >> 30) & 3) {
    case 0: /* LFB */
        for (uint32_t i = 0; i < count; i++)
            voodoo2ec_cmdfifo_lfb_write(f->device, target++, next_word(f));
        break;
    case 3: /* texture port */
        for (uint32_t i = 0; i < count; i++)
            voodoo2ec_cmdfifo_texture_write(f->device, target++, next_word(f));
        break;
    default:
        consume(f, count);
        break;
    }
    return 0;
}
