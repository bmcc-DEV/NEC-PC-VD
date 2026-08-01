/*
 * cpu_test.c - synthetic tests for the VR5432 MIPS IV core.
 *
 * Each test places a small instruction stream at 0xA0000000 (KSEG1,
 * physical 0) and verifies GPR/FPU/memory state after stepping.
 */
#include "bus.h"
#include "vr5432.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_fail = 0;

static void check(const char* name, uint64_t got, uint64_t want) {
    if (got == want) {
        printf("PASS  %s = 0x%016llX\n", name, (unsigned long long)got);
    } else {
        printf("FAIL  %s = 0x%016llX (want 0x%016llX)\n", name,
               (unsigned long long)got, (unsigned long long)want);
        g_fail++;
    }
}

/* --- encoders --- */
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
static uint32_t cop1x(uint32_t fmt, uint32_t fr, uint32_t fs, uint32_t ft,
                      uint32_t funct) {
    return (0x13u << 26) | (fmt << 21) | (fr << 16) | (fs << 11) | (ft << 6) | funct;
}
static uint32_t cop1_mov(uint32_t rs, uint32_t rt, uint32_t fs) {
    return (0x11u << 26) | (rs << 21) | (rt << 16) | (fs << 11);
}
static uint32_t bcop1(uint32_t tf_likely, int32_t off) {
    return (0x11u << 26) | (0x08u << 21) | (uint32_t)(tf_likely & 0x3) << 16 |
           (uint32_t)(off & 0xFFFF);
}

#define RAM_VADDR 0xA0000000ull   /* KSEG1 -> physical 0 */

typedef struct {
    Vr5432 cpu;
    Bus* bus;
} Ctx;

static Ctx ctx;

static void load_prog(const uint32_t* prog, int count) {
    /* clear the program area so leftover instructions from previous tests
       cannot corrupt execution (everything after the program is NOPs) */
    for (int i = 0; i < 0x400; i++)
        bus_write32(ctx.bus, RAM_VADDR + (uint64_t)i * 4, 0);
    for (int i = 0; i < count; i++)
        bus_write32(ctx.bus, RAM_VADDR + (uint64_t)i * 4, prog[i]);
    vr5432_reset(&ctx.cpu, ctx.bus);
    vr5432_set_pc(&ctx.cpu, RAM_VADDR);
}

static void step_n(int n) {
    for (int i = 0; i < n; i++) vr5432_step(&ctx.cpu);
}

int main(void) {
    ctx.bus = bus_create();
    if (!ctx.bus) { fprintf(stderr, "no bus\n"); return 1; }

    /* ---- Test 1: 64-bit ALU / shifts ---- */
    {
        uint32_t p[] = {
            itype(0x0F, 0, 1, 0x1234),              /* lui  $1, 0x1234 */
            itype(0x0D, 1, 1, 0x5678),              /* ori  $1, $1, 0x5678 */
            rtype(0x00, 0, 1, 2, 0, 0x3C),          /* dsll32 $2, $1, 0 */
            rtype(0x00, 1, 2, 3, 0, 0x25),          /* or   $3, $1, $2 */
            rtype(0x00, 0, 3, 4, 3, 0x3B),          /* dsra $4, $3, 3 */
        };
        load_prog(p, 5);
        step_n(5);
        check("t1 lui/ori", vr5432_get_gpr(&ctx.cpu, 1), 0x0000000012345678ull);
        check("t1 dsll32", vr5432_get_gpr(&ctx.cpu, 2), 0x1234567800000000ull);
        check("t1 or", vr5432_get_gpr(&ctx.cpu, 3), 0x1234567812345678ull);
        check("t1 dsra 3", vr5432_get_gpr(&ctx.cpu, 4), 0x02468ACF02468ACFull);
    }

    /* ---- Test 2: SD/LD 64-bit memory ---- */
    {
        uint32_t p[] = {
            itype(0x0F, 0, 8, 0xA000),              /* lui  $8, 0xA000 */
            itype(0x19, 8, 8, 0x20),                /* daddiu $8, $8, 0x20 */
            itype(0x0F, 0, 9, 0xDEAD),              /* lui  $9, 0xDEAD */
            itype(0x0D, 9, 9, 0xBEEF),              /* ori  $9, $9, 0xBEEF */
            itype(0x3F, 8, 9, 0),                   /* sd   $9, 0($8) (R4300: 0x3F) */
            itype(0x37, 8, 10, 0),                  /* ld   $10, 0($8) (R4300: 0x37) */
        };
        load_prog(p, 6);
        step_n(6);
        check("t2 sd/ld roundtrip", vr5432_get_gpr(&ctx.cpu, 10),
              0x00000000DEADBEEFull);
        check("t2 memory", bus_read64(ctx.bus, RAM_VADDR + 0x20),
              0x00000000DEADBEEFull);
    }

    /* ---- Test 3: LWL/LWR unaligned word loads ---- */
    {
        uint32_t p[] = {
            itype(0x0F, 0, 1, 0xA000),              /* lui  $1, 0xA000 */
            itype(0x19, 1, 1, 0x31),                /* daddiu $1, $1, 0x31 */
            itype(0x0F, 0, 2, 0xFFFF),              /* lui  $2, 0xFFFF */
            itype(0x0D, 2, 2, 0xFFFF),              /* ori  $2, $2, 0xFFFF */
            itype(0x22, 1, 2, 0),                   /* lwl  $2, 0($1) */
        };
        load_prog(p, 5);
        bus_write32(ctx.bus, RAM_VADDR + 0x30, 0x11223344);
        step_n(5);
        check("t3 lwl", vr5432_get_gpr(&ctx.cpu, 2), 0x00000000223344FFull);
    }
    {
        uint32_t p[] = {
            itype(0x0F, 0, 1, 0xA000),              /* lui  $1, 0xA000 */
            itype(0x19, 1, 1, 0x32),                /* daddiu $1, $1, 0x32 */
            itype(0x0F, 0, 2, 0xFFFF),              /* lui  $2, 0xFFFF */
            itype(0x0D, 2, 2, 0xFFFF),              /* ori  $2, $2, 0xFFFF */
            itype(0x26, 1, 2, 0),                   /* lwr  $2, 0($1) */
        };
        load_prog(p, 5);
        bus_write32(ctx.bus, RAM_VADDR + 0x30, 0x11223344);
        step_n(5);
        check("t3 lwr", vr5432_get_gpr(&ctx.cpu, 2), 0x00000000FFFF1122ull);
    }

    /* ---- Test 4: BEQ delay slot ---- */
    {
        uint32_t p[] = {
            itype(0x04, 0, 0, 2),                   /* beq $0,$0,+2 */
            rtype(0x00, 0, 0, 0, 0, 0),             /* nop (delay slot) */
            itype(0x19, 0, 2, 1),                   /* daddiu $2, $0, 1 (skipped) */
            itype(0x19, 0, 3, 2),                   /* daddiu $3, $0, 2 */
        };
        load_prog(p, 4);
        step_n(3);
        check("t4 delay slot skipped", vr5432_get_gpr(&ctx.cpu, 2), 0);
        check("t4 branch landing", vr5432_get_gpr(&ctx.cpu, 3), 2);
        check("t4 pc after branch", vr5432_get_pc(&ctx.cpu),
              RAM_VADDR + 4 * 4);
    }

    /* ---- Test 5: JALR/JR with return address ---- */
    {
        uint32_t p[] = {
            itype(0x19, 0, 31, 0),                  /* daddiu $31, $0, 0 */
            itype(0x0F, 0, 1, 0xA000),              /* lui  $1, 0xA000 */
            itype(0x19, 1, 1, 0x24),                /* daddiu $1, $1, 0x24 */
            rtype(0x00, 1, 0, 2, 0, 0x09),          /* jalr $2, $1 */
            rtype(0x00, 0, 0, 0, 0, 0),             /* nop */
            itype(0x19, 0, 3, 1),                   /* skipped */
            rtype(0x00, 0, 0, 0, 0, 0),
            rtype(0x00, 0, 0, 0, 0, 0),
            rtype(0x00, 0, 0, 0, 0, 0),
            itype(0x19, 0, 4, 0xAA),                /* daddiu $4, $0, 0xAA */
        };
        load_prog(p, 10);
        step_n(7);
        check("t5 jalr return addr", vr5432_get_gpr(&ctx.cpu, 2),
              RAM_VADDR + 5 * 4);
        check("t5 landing", vr5432_get_gpr(&ctx.cpu, 4), 0xAA);
        check("t5 skipped instr", vr5432_get_gpr(&ctx.cpu, 3), 0);
    }

    /* ---- Test 6: MULT/DIV ---- */
    {
        uint32_t p[] = {
            itype(0x19, 0, 1, 7),                   /* daddiu $1, $0, 7 */
            itype(0x19, 0, 2, 3),                   /* daddiu $2, $0, 3 */
            rtype(0x00, 1, 2, 0, 0, 0x18),          /* mult $1, $2 */
            rtype(0x00, 0, 0, 3, 0, 0x10),          /* mfhi $3 */
            rtype(0x00, 0, 0, 4, 0, 0x12),          /* mflo $4 */
            rtype(0x00, 1, 2, 0, 0, 0x1A),          /* div  $1, $2 */
            rtype(0x00, 0, 0, 5, 0, 0x12),          /* mflo $5 */
            rtype(0x00, 0, 0, 6, 0, 0x10),          /* mfhi $6 */
        };
        load_prog(p, 8);
        step_n(8);
        check("t6 mult lo", vr5432_get_gpr(&ctx.cpu, 4), 21);
        check("t6 mult hi", vr5432_get_gpr(&ctx.cpu, 3), 0);
        check("t6 div lo", vr5432_get_gpr(&ctx.cpu, 5), 2);
        check("t6 div hi", vr5432_get_gpr(&ctx.cpu, 6), 1);
    }

    /* ---- Test 7: FPU arithmetic + MADD.S ---- */
    {
        uint32_t p[] = {
            itype(0x19, 0, 1, 5),                   /* daddiu $1, $0, 5 */
            cop1_mov(0x04, 1, 0),                   /* mtc1 $1, $0 */
            cop1(0x14, 0, 0, 2, 0x20),              /* cvt.s.w $2, $0 */
            itype(0x19, 0, 1, 7),                   /* daddiu $1, $0, 7 */
            cop1_mov(0x04, 1, 4),                   /* mtc1 $1, $4 */
            cop1(0x14, 0, 4, 6, 0x20),              /* cvt.s.w $6, $4 (word 7 -> 7.0f) */
            cop1(0x14, 0, 4, 4, 0x20),              /* cvt.s.w $4, $4 (word 7 -> 7.0f) */
            cop1(0x10, 2, 6, 8, 0x00),              /* add.s $8, $2, $6 (fs=2,ft=6) */
            cop1x(0x10, 4, 2, 6, 0x00),             /* madd.s f4 = f4 + f2*f6 */
            cop1(0x10, 0, 8, 10, 0x0D),             /* trunc.w.s $10, $8 */
            cop1_mov(0x00, 9, 10),                  /* mfc1 $9, $10 */
            cop1(0x10, 0, 4, 12, 0x0D),             /* trunc.w.s $12, $4 */
            cop1_mov(0x00, 11, 12),                 /* mfc1 $11, $12 */
        };
        load_prog(p, 13);
        step_n(13);
        check("t7 add.s 5+7 -> 12", vr5432_get_gpr(&ctx.cpu, 9), 12);
        check("t7 madd.s 7+5*7 -> 42", vr5432_get_gpr(&ctx.cpu, 11), 42);
    }

    /* ---- Test 8: FPU compare + BC1 ---- */
    {
        uint32_t p[] = {
            itype(0x19, 0, 1, 5),                   /* daddiu $1, $0, 5 */
            cop1_mov(0x04, 1, 0),                   /* mtc1 $1, $0 */
            cop1(0x14, 0, 0, 2, 0x20),              /* cvt.s.w $2, $0 */
            itype(0x19, 0, 1, 7),                   /* daddiu $1, $0, 7 */
            cop1_mov(0x04, 1, 4),                   /* mtc1 $1, $4 */
            cop1(0x14, 0, 4, 6, 0x20),              /* cvt.s.w $6, $4 */
            cop1(0x10, 6, 2, 0, 0x3C),              /* c.olt.s fs=2, ft=6 (5<7) */
            bcop1(1, 1),                            /* bc1t +1 -> index 9 */
            itype(0x19, 0, 3, 1),                   /* skipped */
            itype(0x19, 0, 3, 0x55),                /* daddiu $3, $0, 0x55 */
        };
        load_prog(p, 10);
        step_n(10);
        check("t8 bc1t taken", vr5432_get_gpr(&ctx.cpu, 3), 0x55);
    }

    /* ---- Test 9: SYSCALL exception + ERET ---- */
    {
        uint32_t p[] = {
            itype(0x19, 0, 1, 0x1234),              /* daddiu $1, $0, 0x1234 */
            rtype(0x00, 0, 0, 0, 0, 0x0C),          /* syscall */
            itype(0x19, 0, 2, 1),                   /* daddiu $2, $0, 1 (skipped) */
        };
        load_prog(p, 3);
        /* ERET handler at the BEV general exception vector */
        bus_write32(ctx.bus, 0xBFC00380ull, 0x42000018ull);   /* eret */
        step_n(2);                                  /* daddiu + syscall */
        check("t9 exception vector", vr5432_get_pc(&ctx.cpu), 0xBFC00380ull);
        check("t9 EPC", ctx.cpu.cp0[CP0_EPC], RAM_VADDR + 4);
        check("t9 EXL set", ctx.cpu.cp0[CP0_STATUS] & SR_EXL, SR_EXL);
        step_n(1);                                  /* eret */
        check("t9 pc after eret", vr5432_get_pc(&ctx.cpu), RAM_VADDR + 4);
        check("t9 EXL cleared", ctx.cpu.cp0[CP0_STATUS] & SR_EXL, 0);
    }

    /* ---- Test 10: branch-likely annulling ---- */
    {
        uint32_t p[] = {
            itype(0x15, 0, 1, 2),                   /* bnel $0, $1, +2 (not taken) */
            itype(0x19, 0, 2, 0xAA),                /* annulled delay slot */
            itype(0x19, 0, 3, 0xBB),                /* daddiu $3, $0, 0xBB */
        };
        load_prog(p, 3);
        step_n(3);
        check("t10 annulled", vr5432_get_gpr(&ctx.cpu, 2), 0);
        check("t10 next", vr5432_get_gpr(&ctx.cpu, 3), 0xBB);
    }

    /* ---- Test 11: LL/SC ---- */
    {
        uint32_t p[] = {
            itype(0x19, 0, 1, 0x55),                /* daddiu $1, $0, 0x55 */
            itype(0x0F, 0, 8, 0xA000),              /* lui  $8, 0xA000 */
            itype(0x19, 8, 8, 0x40),                /* daddiu $8, $8, 0x40 */
            itype(0x30, 8, 3, 0),                   /* ll   $3, 0($8) */
            itype(0x38, 8, 1, 0),                   /* sc   $1, 0($8) (R4300: 0x38) */
        };
        load_prog(p, 5);
        step_n(5);
        check("t11 sc succeeded", vr5432_get_gpr(&ctx.cpu, 1), 1);
        check("t11 memory written", bus_read32(ctx.bus, RAM_VADDR + 0x40), 0x55);
    }

    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    bus_destroy(ctx.bus);
    return g_fail ? 1 : 0;
}
