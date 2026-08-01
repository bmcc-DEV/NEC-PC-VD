/*
 * pipeline_test.c - VR5432 dual-issue superscalar cycle model tests.
 *
 * Verifies the modeled cycle counts: INT throughput, INT+FPU dual issue,
 * RAW stalls (register + load-use), FPU latency and HI/LO latency.
 */
#include "bus.h"
#include "vr5432.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_fail = 0;

static void check(const char* name, uint64_t got, uint64_t want) {
    if (got == want) printf("PASS  %s = %llu\n", name, (unsigned long long)got);
    else { printf("FAIL  %s = %llu (want %llu)\n", name,
                  (unsigned long long)got, (unsigned long long)want); g_fail++; }
}

static uint32_t rtype(uint32_t op, uint32_t rs, uint32_t rt, uint32_t rd,
                      uint32_t sa, uint32_t funct) {
    return (op << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct;
}
static uint32_t itype(uint32_t op, uint32_t rs, uint32_t rt, uint32_t imm) {
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF);
}
static uint32_t cop1(uint32_t fmt, uint32_t ft, uint32_t fs, uint32_t fd,
                     uint32_t funct) {
    return (0x11u << 26) | (fmt << 21) | (ft << 16) | (fs << 11) | (fd << 6) | funct;
}

#define RAM_VADDR 0xA0000000ull

static Vr5432 cpu;
static Bus* bus;

static void load_prog(const uint32_t* prog, int count) {
    for (int i = 0; i < 0x400; i++)
        bus_write32(bus, RAM_VADDR + (uint64_t)i * 4, 0);
    for (int i = 0; i < count; i++)
        bus_write32(bus, RAM_VADDR + (uint64_t)i * 4, prog[i]);
    vr5432_reset(&cpu, bus);
    vr5432_set_pc(&cpu, RAM_VADDR);
}

int main(void) {
    bus = bus_create();

    /* ---- t1: 6 independent integer ADDU (latency 1 each) ---- */
    {
        uint32_t p[] = {
            rtype(0, 9, 10, 8, 0, 0x21), rtype(0, 9, 10, 11, 0, 0x21),
            rtype(0, 9, 10, 12, 0, 0x21), rtype(0, 9, 10, 13, 0, 0x21),
            rtype(0, 9, 10, 14, 0, 0x21), rtype(0, 9, 10, 15, 0, 0x21),
        };
        load_prog(p, 6);
        for (int i = 0; i < 6; i++) vr5432_step(&cpu);
        check("t1 6xADDU cycles", vr5432_get_cycles(&cpu), 6);
    }

    /* ---- t2: INT/FPU alternating dual-issue ----
     * ADDU, ADD.S (latency 2), ADDU, ADD.S: the FPU overlaps the INT pipe
     * in cycles 0 and 2 -> 4 cycles for 4 instructions. */
    {
        uint32_t p[] = {
            rtype(0, 9, 10, 8, 0, 0x21),     /* ADDU $8, $9, $10 */
            cop1(0x10, 4, 2, 0, 0x00),       /* ADD.S $f0, $f2, $f4 */
            rtype(0, 9, 10, 11, 0, 0x21),    /* ADDU $11, $9, $10 */
            cop1(0x10, 10, 8, 6, 0x00),      /* ADD.S $f6, $f8, $f10 */
        };
        load_prog(p, 4);
        for (int i = 0; i < 4; i++) vr5432_step(&cpu);
        check("t2 INT+FPU dual-issue", vr5432_get_cycles(&cpu), 4);
    }

    /* ---- t3: RAW GPR dependency stalls ----
     * ADDU $1,$2,$3 ; ADDU $4,$1,$0: second waits for $1 (ready at 1) */
    {
        uint32_t p[] = {
            rtype(0, 2, 3, 1, 0, 0x21),
            rtype(0, 1, 0, 4, 0, 0x21),
        };
        load_prog(p, 2);
        for (int i = 0; i < 2; i++) vr5432_step(&cpu);
        check("t3 RAW GPR stall", vr5432_get_cycles(&cpu), 2);
    }

    /* ---- t4: load-use stall ----
     * LW $1, 0($2) (latency 2) ; ADDU $3, $1, $0: waits until $1 ready at 2 */
    {
        uint32_t p[] = {
            itype(0x23, 2, 1, 0),     /* lw $1, 0($2) */
            rtype(0, 1, 0, 3, 0, 0x21),
        };
        load_prog(p, 2);
        for (int i = 0; i < 2; i++) vr5432_step(&cpu);
        check("t4 load-use stall", vr5432_get_cycles(&cpu), 3);
    }

    /* ---- t5: FPU dependency ----
     * ADD.S $f0,$f2,$f4 ; ADD.S $f6,$f0,$f8: second reads $f0 ready at 2 */
    {
        uint32_t p[] = {
            cop1(0x10, 4, 2, 0, 0x00),   /* ADD.S $f0, $f2, $f4 */
            cop1(0x10, 8, 0, 6, 0x00),   /* ADD.S $f6, $f0, $f8 */
        };
        load_prog(p, 2);
        for (int i = 0; i < 2; i++) vr5432_step(&cpu);
        check("t5 FPU dependency", vr5432_get_cycles(&cpu), 4);
    }

    /* ---- t6: MULT -> MFLO latency ----
     * MULT $2,$3 ; MFLO $4: waits for HI/LO (ready at 5) */
    {
        uint32_t p[] = {
            rtype(0, 2, 3, 0, 0, 0x18),  /* mult $2, $3 */
            rtype(0, 0, 0, 4, 0, 0x12),  /* mflo $4 */
        };
        load_prog(p, 2);
        for (int i = 0; i < 2; i++) vr5432_step(&cpu);
        check("t6 MULT->MFLO latency", vr5432_get_cycles(&cpu), 6);
    }

    /* ---- t7: 64-bit DIV latency (68 cycles) ---- */
    {
        uint32_t p[] = {
            rtype(0, 2, 3, 0, 0, 0x1F),  /* ddivu $2, $3 */
        };
        load_prog(p, 1);
        vr5432_step(&cpu);
        check("t7 DDIV latency", vr5432_get_cycles(&cpu), 68);
    }

    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    bus_destroy(bus);
    return g_fail ? 1 : 0;
}
