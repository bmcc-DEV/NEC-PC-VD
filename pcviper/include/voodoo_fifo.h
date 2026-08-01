/*
 * voodoo_fifo.h - Voodoo2 EC command FIFO (CMDFIFO) ring + packet parser.
 *
 * The FIFO lives in off-screen SGRAM between the base/end programmed through
 * cmdFifoBaseAddr. Packets types 0-5 are parsed as they become available.
 */
#ifndef VIPER_VOODOO_FIFO_H
#define VIPER_VOODOO_FIFO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Voodoo2EC Voodoo2EC;

typedef struct V2Cmdfifo {
    Voodoo2EC* device;

    uint8_t* ram;          /* SGRAM pointer */
    uint32_t base;         /* byte offset in SGRAM */
    uint32_t end;          /* byte offset in SGRAM */
    uint32_t size_words;

    bool enable;
    bool count_holes;

    uint32_t rd;           /* read index (words) */
    uint32_t wr;           /* next written word */
    uint32_t depth;        /* contiguous words available */
    uint32_t holes;

    uint32_t address_min;
    uint32_t address_max;
} V2Cmdfifo;

void v2cmdfifo_init(V2Cmdfifo* f, Voodoo2EC* dev);
void v2cmdfifo_configure(V2Cmdfifo* f, uint8_t* ram, uint32_t base, uint32_t end);
void v2cmdfifo_set_enable(V2Cmdfifo* f, bool e);
void v2cmdfifo_set_read_pointer(V2Cmdfifo* f, uint32_t v);
void v2cmdfifo_set_address_min(V2Cmdfifo* f, uint32_t v);
void v2cmdfifo_set_address_max(V2Cmdfifo* f, uint32_t v);
void v2cmdfifo_set_depth(V2Cmdfifo* f, uint32_t v);
void v2cmdfifo_set_holes(V2Cmdfifo* f, uint32_t v);
bool v2cmdfifo_enabled(const V2Cmdfifo* f);
uint32_t v2cmdfifo_read_pointer(const V2Cmdfifo* f);
uint32_t v2cmdfifo_depth(const V2Cmdfifo* f);
uint32_t v2cmdfifo_holes(const V2Cmdfifo* f);

void v2cmdfifo_write(V2Cmdfifo* f, uint32_t addr, uint32_t data);
void v2cmdfifo_write_direct(V2Cmdfifo* f, uint32_t offset, uint32_t data);
uint32_t v2cmdfifo_execute_if_ready(V2Cmdfifo* f);

#endif /* VIPER_VOODOO_FIFO_H */
