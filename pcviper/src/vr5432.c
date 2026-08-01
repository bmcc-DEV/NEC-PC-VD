/*
 * vr5432.c - NEC VR5432 MIPS IV 64-bit CPU core (fase 1).
 *
 * Implementa: GPR 64-bit, HI/LO, COP0 (Status/Cause/EPC/Count/Compare/
 * Config/PRId), COP1 FPU IEEE-754 (single/double, MIPS IV incluindo
 * MADD/MSUB/RECIP/RSQRT), delay slots, branch-likely, exceptions e
 * LL/SC. Execução single-issue com contagem de ciclos.
 */
#include "vr5432.h"
#include <string.h>
#include <math.h>

/* ---------------- field extraction ---------------- */

#define OP(i)     ((i) >> 26)
#define RS(i)     (((i) >> 21) & 0x1F)
#define RT(i)     (((i) >> 16) & 0x1F)
#define RD(i)     (((i) >> 11) & 0x1F)
#define SA(i)     (((i) >> 6) & 0x1F)
#define FUNCT(i)  ((i) & 0x3F)
#define IMM(i)    ((i) & 0xFFFF)
#define TGT(i)    ((i) & 0x03FFFFFF)

static inline int64_t se16(uint32_t v) { return (int16_t)(v & 0xFFFF); }
static inline int64_t se8(uint32_t v)  { return (int8_t)(v & 0xFF); }

/* ---------------- FPU register access ---------------- */

static inline float fpr_s(Vr5432* c, int r) {
    float f;
    memcpy(&f, &c->fpr[r], 4);
    return f;
}

static inline void set_fpr_s(Vr5432* c, int r, float f) {
    memcpy(&c->fpr[r], &f, 4);
}

static inline double fpr_d(Vr5432* c, int r) {
    uint64_t v = (uint64_t)c->fpr[r] | ((uint64_t)c->fpr[r + 1] << 32);
    double d;
    memcpy(&d, &v, 8);
    return d;
}

static inline void set_fpr_d(Vr5432* c, int r, double d) {
    uint64_t v;
    memcpy(&v, &d, 8);
    c->fpr[r] = (uint32_t)v;
    c->fpr[r + 1] = (uint32_t)(v >> 32);
}

static inline int64_t fpr_l(Vr5432* c, int r) {
    return (int64_t)((uint64_t)c->fpr[r] | ((uint64_t)c->fpr[r + 1] << 32));
}

static inline void set_fpr_l(Vr5432* c, int r, int64_t v) {
    c->fpr[r] = (uint32_t)(uint64_t)v;
    c->fpr[r + 1] = (uint32_t)((uint64_t)v >> 32);
}

/* Round to nearest-even (default FCR31 mode) */
static int64_t rnd_nearest(double v) {
    if (isnan(v) || isinf(v)) return INT64_MIN;
    if (v < 0) return -rnd_nearest(-v);
    double f = floor(v);
    double frac = v - f;
    int64_t i = (int64_t)f;
    if (frac > 0.5) return i + 1;
    if (frac < 0.5) return i;
    return (i & 1) ? i + 1 : i;
}

static int32_t cvt_w(double v) {
    if (isnan(v) || isinf(v) || v >= 2147483648.0 || v < -2147483648.0)
        return (int32_t)0x80000000;   /* FPU indefinite value */
    return (int32_t)rnd_nearest(v);
}

static uint64_t cvt_l(double v) {
    if (isnan(v) || isinf(v)) return 0x8000000000000000ull;
    return (uint64_t)rnd_nearest(v);
}

static void fpu_set_flag(Vr5432* c, uint32_t flag) {
    /* flags live in FCR31 bits 6:2, mirrored to cause bits 12:8 */
    c->fcr31 |= flag;
    c->fcr31 |= flag << 6;
}

/* ---------------- exceptions ---------------- */

static void vr5432_exception(Vr5432* c, uint64_t inst_addr, int code,
                             bool in_delay) {
    uint64_t vector = (c->cp0[CP0_STATUS] & SR_BEV) ? 0xBFC00380ull
                                                     : 0x80000180ull;
    if (in_delay) {
        c->cp0[CP0_EPC] = c->branch_pc;
        c->cp0[CP0_CAUSE] |= CAUSE_BD;
    } else {
        c->cp0[CP0_EPC] = inst_addr;
        c->cp0[CP0_CAUSE] &= ~CAUSE_BD;
    }
    c->cp0[CP0_CAUSE] = (c->cp0[CP0_CAUSE] & ~CAUSE_EXC_MASK) |
                        ((uint64_t)code << CAUSE_EXC_SHIFT);
    c->cp0[CP0_STATUS] |= SR_EXL;
    c->pc = vector;
    c->in_delay_slot = false;
}

/* ---------------- load/store helpers ---------------- */

static void set_gpr(Vr5432* c, int r, uint64_t v) {
    if (r != 0) c->gpr[r] = v;
}

static inline uint64_t addr_err_check(Vr5432* c, uint64_t addr, int align,
                                      int code, bool was_delay, uint64_t inst_addr) {
    if (addr & (align - 1)) {
        c->cp0[CP0_BADVADDR] = addr;
        vr5432_exception(c, inst_addr, code, was_delay);
        return 1;
    }
    return 0;
}

/* ---- unaligned word/doubleword load/store (little-endian) ---- */

static uint32_t mips_lwl(Vr5432* c, uint64_t addr, uint32_t rt) {
    uint32_t mem = bus_read32(c->bus, addr & ~3ull);
    switch (addr & 3) {
    case 0: return mem;
    case 1: return (rt & 0x000000FFu) | (mem << 8);
    case 2: return (rt & 0x0000FFFFu) | (mem << 16);
    default: return (rt & 0x00FFFFFFu) | (mem << 24);
    }
}

static uint32_t mips_lwr(Vr5432* c, uint64_t addr, uint32_t rt) {
    uint32_t mem = bus_read32(c->bus, addr & ~3ull);
    switch (addr & 3) {
    case 0: return mem;
    case 1: return (rt & 0xFFFFFF00u) | (mem >> 8);
    case 2: return (rt & 0xFFFF0000u) | (mem >> 16);
    default: return (rt & 0xFF000000u) | (mem >> 24);
    }
}

static uint32_t mips_swl(Vr5432* c, uint64_t addr, uint32_t rt) {
    uint32_t mem = bus_read32(c->bus, addr & ~3ull);
    switch (addr & 3) {
    case 0: return rt;
    case 1: return (mem & 0x000000FFu) | (rt & 0xFFFFFF00u);
    case 2: return (mem & 0x0000FFFFu) | (rt & 0xFFFF0000u);
    default: return (mem & 0x00FFFFFFu) | (rt & 0xFF000000u);
    }
}

static uint32_t mips_swr(Vr5432* c, uint64_t addr, uint32_t rt) {
    uint32_t mem = bus_read32(c->bus, addr & ~3ull);
    switch (addr & 3) {
    case 0: return (mem & 0xFFFFFF00u) | (rt & 0x000000FFu);
    case 1: return (mem & 0xFFFF0000u) | (rt & 0x0000FFFFu);
    case 2: return (mem & 0xFF000000u) | (rt & 0x00FFFFFFu);
    default: return rt;
    }
}

static uint64_t mips_ldl(Vr5432* c, uint64_t addr, uint64_t rt) {
    uint64_t mem = bus_read64(c->bus, addr & ~7ull);
    switch (addr & 7) {
    case 0: return mem;
    case 1: return (rt & 0x00000000000000FFull) | (mem << 8);
    case 2: return (rt & 0x000000000000FFFFull) | (mem << 16);
    case 3: return (rt & 0x0000000000FFFFFFull) | (mem << 24);
    case 4: return (rt & 0x00000000FFFFFFFFull) | (mem << 32);
    case 5: return (rt & 0x000000FFFFFFFFFFull) | (mem << 40);
    case 6: return (rt & 0x0000FFFFFFFFFFFFull) | (mem << 48);
    default: return (rt & 0x00FFFFFFFFFFFFFFull) | (mem << 56);
    }
}

static uint64_t mips_ldr(Vr5432* c, uint64_t addr, uint64_t rt) {
    uint64_t mem = bus_read64(c->bus, addr & ~7ull);
    switch (addr & 7) {
    case 0: return mem;
    case 1: return (rt & 0xFFFFFFFFFFFFFF00ull) | (mem >> 8);
    case 2: return (rt & 0xFFFFFFFFFFFF0000ull) | (mem >> 16);
    case 3: return (rt & 0xFFFFFFFFFF000000ull) | (mem >> 24);
    case 4: return (rt & 0xFFFFFFFF00000000ull) | (mem >> 32);
    case 5: return (rt & 0xFFFFFF0000000000ull) | (mem >> 40);
    case 6: return (rt & 0xFFFF000000000000ull) | (mem >> 48);
    default: return (rt & 0xFF00000000000000ull) | (mem >> 56);
    }
}

static uint64_t mips_sdl(Vr5432* c, uint64_t addr, uint64_t rt) {
    uint64_t mem = bus_read64(c->bus, addr & ~7ull);
    switch (addr & 7) {
    case 0: return rt;
    case 1: return (mem & 0x00000000000000FFull) | (rt & 0xFFFFFFFFFFFFFF00ull);
    case 2: return (mem & 0x000000000000FFFFull) | (rt & 0xFFFFFFFFFFFF0000ull);
    case 3: return (mem & 0x0000000000FFFFFFull) | (rt & 0xFFFFFFFFFF000000ull);
    case 4: return (mem & 0x00000000FFFFFFFFull) | (rt & 0xFFFFFFFF00000000ull);
    case 5: return (mem & 0x000000FFFFFFFFFFull) | (rt & 0xFFFFFF0000000000ull);
    case 6: return (mem & 0x0000FFFFFFFFFFFFull) | (rt & 0xFFFF000000000000ull);
    default: return (mem & 0x00FFFFFFFFFFFFFFull) | (rt & 0xFF00000000000000ull);
    }
}

static uint64_t mips_sdr(Vr5432* c, uint64_t addr, uint64_t rt) {
    uint64_t mem = bus_read64(c->bus, addr & ~7ull);
    switch (addr & 7) {
    case 0: return (mem & 0xFFFFFFFFFFFFFF00ull) | (rt & 0x00000000000000FFull);
    case 1: return (mem & 0xFFFFFFFFFFFF0000ull) | (rt & 0x000000000000FFFFull);
    case 2: return (mem & 0xFFFFFFFFFF000000ull) | (rt & 0x0000000000FFFFFFull);
    case 3: return (mem & 0xFFFFFFFF00000000ull) | (rt & 0x00000000FFFFFFFFull);
    case 4: return (mem & 0xFFFFFF0000000000ull) | (rt & 0x000000FFFFFFFFFFull);
    case 5: return (mem & 0xFFFF000000000000ull) | (rt & 0x0000FFFFFFFFFFFFull);
    case 6: return (mem & 0xFF00000000000000ull) | (rt & 0x00FFFFFFFFFFFFFFull);
    default: return rt;
    }
}

/* FPU ordered/unordered comparison (returns the condition bit result) */
static int fpu_compare(double a, double b, uint32_t cond) {
    bool nan = isnan(a) || isnan(b);
    switch (cond & 0xF) {
    case 0:  return 0;                  /* F */
    case 1:  return nan;                /* UN */
    case 2:  return !nan && a == b;     /* EQ */
    case 3:  return nan || a == b;      /* UEQ */
    case 4:  return !nan && a < b;      /* OLT */
    case 5:  return nan || a < b;       /* ULT */
    case 6:  return !nan && a <= b;     /* OLE */
    case 7:  return nan || a <= b;      /* ULE */
    case 8:  return 0;                  /* SF */
    case 9:  return nan;                /* NGLE */
    case 10: return !nan && a == b;     /* SEQ */
    case 11: return nan || a == b;      /* NGL */
    case 12: return !nan && a < b;      /* LT */
    case 13: return nan || a < b;       /* NGE */
    case 14: return !nan && a <= b;     /* LE */
    default: return nan || a <= b;      /* NGT */
    }
}

/* ---------------- core ---------------- */

void vr5432_reset(Vr5432* cpu, Bus* bus) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->bus = bus;
    cpu->pc = 0xBFC00000ull;                 /* reset vector */
    cpu->cp0[CP0_STATUS] = SR_BEV | SR_KSU_KERNEL;
    cpu->cp0[CP0_PRID] = 0x00004300;         /* NEC VR5432 placeholder */
    cpu->cp0[CP0_CONFIG] = 0x0006E180ull;
    cpu->fcr0 = 0x000000F0;
    cpu->fcr31 = 0;
    cpu->in_delay_slot = false;
    cpu->llbit = 0;
    cpu->halted = false;
    cpu->cycles = 0;
    cpu->last_pipe = -1;
    cpu->last_structural = 0;
    cpu->last_issue = 0;
}

uint64_t vr5432_get_gpr(const Vr5432* cpu, int reg) { return cpu->gpr[reg & 31]; }
void vr5432_set_gpr(Vr5432* cpu, int reg, uint64_t value) {
    if (reg) cpu->gpr[reg & 31] = value;
}
uint64_t vr5432_get_pc(const Vr5432* cpu) { return cpu->pc; }
void vr5432_set_pc(Vr5432* cpu, uint64_t pc) { cpu->pc = pc; }
uint64_t vr5432_get_cycles(const Vr5432* cpu) { return cpu->cycles; }
void vr5432_set_fpr_single(Vr5432* cpu, int reg, float value) { set_fpr_s(cpu, reg, value); }
float vr5432_get_fpr_single(const Vr5432* cpu, int reg) { return fpr_s((Vr5432*)cpu, reg); }

/* ------------------------------------------------------------------ */
/* dual-issue superscalar cycle model                                  */
/*                                                                     */
/* Two pipes (INT and FPU). An INT instruction and an independent FPU  */
/* instruction can issue in the same cycle. `cycles` is the completion */
/* cycle of the latest instruction; RAW dependencies stall issue.      */
/* ------------------------------------------------------------------ */

enum { PIPE_INT = 0, PIPE_FPU = 1 };
enum { K_NONE = 0, K_GPR = 1, K_FPR = 2, K_HILO = 3 };

typedef struct {
    int pipe;
    int latency;
    bool structural;
    int dest_kind;
    int dest_reg;
    bool dest_wide;         /* writes FPR pair (fd, fd+1) */
    int nsrc;
    int src_kind[3];
    int src_reg[3];
} InsnInfo;

static void classify_cop1(uint32_t insn, InsnInfo* ii);

static void classify_insn(uint32_t insn, InsnInfo* ii) {
    memset(ii, 0, sizeof(*ii));
    ii->pipe = PIPE_INT;
    ii->latency = 1;
    ii->dest_kind = K_NONE;
    ii->dest_reg = -1;
    uint32_t op = insn >> 26;
    uint32_t rs = (insn >> 21) & 0x1F;
    uint32_t rt = (insn >> 16) & 0x1F;
    uint32_t rd = (insn >> 11) & 0x1F;
    uint32_t sa = (insn >> 6) & 0x1F;
    uint32_t funct = insn & 0x3F;

    switch (op) {
    case 0x00:
        switch (funct) {
        case 0x00: case 0x02: case 0x03:          /* SLL/SRL/SRA */
        case 0x38: case 0x3A: case 0x3B:
        case 0x3C: case 0x3E: case 0x3F:          /* DSLL/DSRL/DSRA(+32) */
            ii->dest_kind = K_GPR; ii->dest_reg = rd;
            ii->src_kind[0] = K_GPR; ii->src_reg[0] = rt; ii->nsrc = 1;
            break;
        case 0x04: case 0x06: case 0x07:
        case 0x14: case 0x16: case 0x17:          /* SLLV/SRLV/SRAV/DSLLV/... */
            ii->dest_kind = K_GPR; ii->dest_reg = rd;
            ii->src_kind[0] = K_GPR; ii->src_reg[0] = rt;
            ii->src_kind[1] = K_GPR; ii->src_reg[1] = rs; ii->nsrc = 2;
            break;
        case 0x08: case 0x09:                     /* JR/JALR */
            ii->structural = true; ii->latency = 2;
            ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
            break;
        case 0x0A: case 0x0B:                     /* MOVZ/MOVN */
            ii->dest_kind = K_GPR; ii->dest_reg = rd;
            ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs;
            ii->src_kind[1] = K_GPR; ii->src_reg[1] = rt; ii->nsrc = 2;
            break;
        case 0x0C: case 0x0D: case 0x0F:         /* SYSCALL/BREAK/SYNC */
            ii->structural = true;
            break;
        case 0x10: case 0x12:                     /* MFHI/MFLO */
            ii->dest_kind = K_GPR; ii->dest_reg = rd;
            ii->src_kind[0] = K_HILO; ii->nsrc = 1;
            break;
        case 0x11: case 0x13:                     /* MTHI/MTLO */
            ii->dest_kind = K_HILO;
            ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
            break;
        case 0x18: case 0x19: case 0x1C: case 0x1D:   /* MULT/MULTU/DMULT/DMULTU */
            ii->latency = 5; ii->dest_kind = K_HILO;
            ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs;
            ii->src_kind[1] = K_GPR; ii->src_reg[1] = rt; ii->nsrc = 2;
            break;
        case 0x1A: case 0x1B:                     /* DIV/DIVU */
            ii->latency = 36; ii->dest_kind = K_HILO;
            ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs;
            ii->src_kind[1] = K_GPR; ii->src_reg[1] = rt; ii->nsrc = 2;
            break;
        case 0x1E: case 0x1F:                     /* DDIV/DDIVU */
            ii->latency = 68; ii->dest_kind = K_HILO;
            ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs;
            ii->src_kind[1] = K_GPR; ii->src_reg[1] = rt; ii->nsrc = 2;
            break;
        case 0x20: case 0x21: case 0x22: case 0x23:
        case 0x24: case 0x25: case 0x26: case 0x27:
        case 0x2A: case 0x2B: case 0x2C: case 0x2D:
        case 0x2E: case 0x2F:
            ii->dest_kind = K_GPR; ii->dest_reg = rd;
            ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs;
            ii->src_kind[1] = K_GPR; ii->src_reg[1] = rt; ii->nsrc = 2;
            break;
        case 0x30: case 0x31: case 0x32: case 0x33:
        case 0x34: case 0x36:                     /* traps */
            ii->structural = true;
            break;
        default:
            break;
        }
        break;
    case 0x01:                                    /* REGIMM branches */
        ii->structural = true; ii->latency = 2;
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
        break;
    case 0x02: case 0x03:                         /* J/JAL */
        ii->structural = true; ii->latency = 2;
        break;
    case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x14: case 0x15: case 0x16: case 0x17:   /* branches */
        ii->structural = true; ii->latency = 2;
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
        if (op == 0x04 || op == 0x05 || op == 0x14 || op == 0x15) {
            ii->src_kind[1] = K_GPR; ii->src_reg[1] = rt; ii->nsrc = 2;
        }
        break;
    case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x18: case 0x19:                         /* ADDI/ADDIU/.../DADDI/DADDIU */
        ii->dest_kind = K_GPR; ii->dest_reg = rt;
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
        break;
    case 0x0C: case 0x0D: case 0x0E:             /* ANDI/ORI/XORI */
        ii->dest_kind = K_GPR; ii->dest_reg = rt;
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
        break;
    case 0x0F:                                    /* LUI */
        ii->dest_kind = K_GPR; ii->dest_reg = rt;
        break;
    case 0x10:                                    /* COP0 */
        if (rs == 0x10) ii->structural = true;    /* TLB/ERET */
        else { ii->latency = 2; if (rs == 0x00) { ii->dest_kind = K_GPR; ii->dest_reg = rt; } }
        break;
    case 0x11:                                    /* COP1 */
        classify_cop1(insn, ii);
        break;
    case 0x13: {                                  /* COP1X MADD/MSUB */
        uint32_t fn = insn & 0x3F;
        ii->pipe = PIPE_FPU;
        ii->latency = 4;
        ii->dest_kind = K_FPR; ii->dest_reg = sa;
        ii->dest_wide = (fn == 0x21 || fn == 0x29 || fn == 0x2D || fn == 0x31);
        ii->src_kind[0] = K_FPR; ii->src_reg[0] = rs;    /* fr */
        ii->src_kind[1] = K_FPR; ii->src_reg[1] = rt;    /* ft */
        ii->src_kind[2] = K_FPR; ii->src_reg[2] = rd; ii->nsrc = 3;  /* fs */
        break;
    }
    case 0x1A: case 0x1B:
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x26: case 0x27:
    case 0x30: case 0x37:                         /* loads (LD=0x37 R4300) */
        ii->latency = 2;
        ii->dest_kind = K_GPR; ii->dest_reg = rt;
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
        break;
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x2C: case 0x2D: case 0x2E: case 0x2F:
    case 0x38: case 0x3F:                         /* stores (SC=0x38, SD=0x3F R4300) */
        ii->structural = true;
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
        if (op != 0x2F) { ii->src_kind[1] = K_GPR; ii->src_reg[1] = rt; ii->nsrc = 2; }
        break;
    case 0x31:                                    /* LWC1 */
        ii->pipe = PIPE_FPU; ii->latency = 2;
        ii->dest_kind = K_FPR; ii->dest_reg = rt;
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
        break;
    case 0x35:                                    /* LDC1 (R4300) */
        ii->pipe = PIPE_FPU; ii->latency = 3;
        ii->dest_kind = K_FPR; ii->dest_reg = rt; ii->dest_wide = true;
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs; ii->nsrc = 1;
        break;
    case 0x39: case 0x3D:                         /* SWC1/SDC1 (R4300) */
        ii->structural = true;
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rs;
        ii->src_kind[1] = K_FPR; ii->src_reg[1] = rt; ii->nsrc = 2;
        break;
    default:
        break;
    }
}

static void classify_cop1(uint32_t insn, InsnInfo* ii) {
    uint32_t fmt = (insn >> 21) & 0x1F;
    uint32_t rt = (insn >> 16) & 0x1F;
    uint32_t fs = (insn >> 11) & 0x1F;
    uint32_t fd = (insn >> 6) & 0x1F;
    uint32_t funct = insn & 0x3F;
    ii->pipe = PIPE_FPU;

    switch (fmt) {
    case 0x00: case 0x01:                        /* MFC1/DMFC1 */
        ii->latency = 2; ii->dest_kind = K_GPR; ii->dest_reg = rt;
        ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs; ii->nsrc = 1;
        break;
    case 0x02: case 0x06:                        /* CFC1/CTC1 */
        ii->latency = 2;
        break;
    case 0x04: case 0x05:                        /* MTC1/DMTC1 */
        ii->latency = 2;
        ii->dest_kind = K_FPR; ii->dest_reg = fs;
        ii->dest_wide = (fmt == 0x05);
        ii->src_kind[0] = K_GPR; ii->src_reg[0] = rt; ii->nsrc = 1;
        break;
    case 0x08:                                    /* BC1 */
        ii->structural = true; ii->latency = 2;
        break;
    case 0x10: case 0x11: {
        bool d = (fmt == 0x11);
        ii->dest_kind = K_FPR; ii->dest_reg = fd;
        switch (funct) {
        case 0x00: case 0x01:                     /* ADD/SUB */
            ii->latency = d ? 3 : 2;
            ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs;
            ii->src_kind[1] = K_FPR; ii->src_reg[1] = rt; ii->nsrc = 2;
            break;
        case 0x02:                                /* MUL */
            ii->latency = d ? 6 : 5;
            ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs;
            ii->src_kind[1] = K_FPR; ii->src_reg[1] = rt; ii->nsrc = 2;
            break;
        case 0x03:                                /* DIV */
            ii->latency = d ? 18 : 12;
            ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs;
            ii->src_kind[1] = K_FPR; ii->src_reg[1] = rt; ii->nsrc = 2;
            break;
        case 0x04:                                /* SQRT */
            ii->latency = d ? 34 : 20;
            ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs; ii->nsrc = 1;
            break;
            case 0x05: case 0x06: case 0x07:         /* ABS/MOV/NEG */
                ii->latency = 1;
                ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs; ii->nsrc = 1;
                break;
        case 0x11:                                    /* MOVT.S */
            ii->latency = 1;
            ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs; ii->nsrc = 1;
            ii->dest_kind = K_FPR; ii->dest_reg = fd;
            break;
        case 0x31:                                    /* MOVT.D */
            ii->latency = 1;
            ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs; ii->nsrc = 1;
            ii->dest_kind = K_FPR; ii->dest_reg = fd; ii->dest_wide = true;
            break;
        case 0x08: case 0x09: case 0x0A: case 0x0B:   /* ROUND.L/TRUNC.L/CEIL.L/FLOOR.L */
        case 0x0C: case 0x0D: case 0x0E: case 0x0F:   /* ROUND.W/TRUNC.W/CEIL.W/FLOOR.W */
        case 0x14: case 0x15: case 0x16:              /* CVT.D.S / CVT.W.S / CVT.L.S */
        case 0x20: case 0x21: case 0x22:              /* CVT.S.D / CVT.W.D / CVT.L.D */
            ii->latency = 2;
            ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs; ii->nsrc = 1;
            break;
        case 0x45: case 0x4A:                    /* RECIP.S / RSQRT.S */
            ii->latency = 13;
            ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs; ii->nsrc = 1;
            break;
        case 0x46: case 0x4B:                    /* RECIP.D / RSQRT.D */
            ii->latency = 26;
            ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs; ii->nsrc = 1;
            break;
        default:
            if ((funct >= 0x30 && funct <= 0x3F) || (funct >= 0x48 && funct <= 0x4F)) {
                /* C.cond */
                ii->latency = 1; ii->dest_kind = K_NONE;
                ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs;
                ii->src_kind[1] = K_FPR; ii->src_reg[1] = rt; ii->nsrc = 2;
            }
            break;
        }
        ii->dest_wide = d;
        if (funct == 0x08 || funct == 0x09 || funct == 0x0A || funct == 0x0B ||
            funct == 0x16 || funct == 0x22)
            ii->dest_wide = true;                 /* .L and .D results are FPR pairs */
        break;
    }
    case 0x14:                                    /* W conversions */
        ii->latency = 2;
        ii->dest_kind = K_FPR; ii->dest_reg = fd;
        ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs; ii->nsrc = 1;
        ii->dest_wide = (funct == 0x22);          /* CVT.L.W -> pair */
        break;
    case 0x15:                                    /* L conversions */
        ii->latency = 2;
        ii->dest_kind = K_FPR; ii->dest_reg = fd;
        ii->src_kind[0] = K_FPR; ii->src_reg[0] = fs;
        ii->src_kind[1] = K_FPR; ii->src_reg[1] = fs + 1; ii->nsrc = 2;
        ii->dest_wide = (funct == 0x21);          /* CVT.D.L -> pair */
        break;
    default:
        break;
    }
}

static void account_pipeline(Vr5432* c, const InsnInfo* ii) {
    uint64_t ready = 0;
    for (int i = 0; i < ii->nsrc; i++) {
        uint64_t r = 0;
        switch (ii->src_kind[i]) {
        case K_GPR: r = c->gpr_ready[ii->src_reg[i] & 31]; break;
        case K_FPR: r = c->fpr_ready[ii->src_reg[i] & 31]; break;
        case K_HILO: r = c->hilo_ready; break;
        default: break;
        }
        if (r > ready) ready = r;
    }

    uint64_t pipe_free = (ii->pipe == PIPE_FPU) ? c->pipe_fpu_free : c->pipe_int_free;
    uint64_t issue;

    /* dual issue: an INT instruction and an independent FPU instruction
     * share a cycle (opposite pipes, both free at the previous issue) */
    if (!ii->structural && !c->last_structural && c->last_pipe >= 0 &&
        ii->pipe != c->last_pipe && ready <= c->last_issue &&
        pipe_free <= c->last_issue) {
        issue = c->last_issue;
    } else {
        issue = c->cycles;
        if (pipe_free > issue) issue = pipe_free;
        if (ready > issue) issue = ready;
    }

    uint64_t finish = issue + ii->latency;
    if (ii->pipe == PIPE_FPU) c->pipe_fpu_free = finish;
    else c->pipe_int_free = finish;
    if (finish > c->cycles) c->cycles = finish;

    switch (ii->dest_kind) {
    case K_GPR: c->gpr_ready[ii->dest_reg & 31] = finish; break;
    case K_FPR:
        c->fpr_ready[ii->dest_reg & 31] = finish;
        if (ii->dest_wide) c->fpr_ready[(ii->dest_reg + 1) & 31] = finish;
        break;
    case K_HILO: c->hilo_ready = finish; break;
    default: break;
    }

    c->last_pipe = ii->pipe;
    c->last_structural = ii->structural;
    c->last_dest_kind = ii->dest_kind;
    c->last_dest_reg = ii->dest_reg;
    c->last_issue = issue;
}


static void execute(Vr5432* c, uint32_t insn, uint64_t inst_addr, bool was_delay) {
    uint32_t op = OP(insn);
    uint32_t rs = RS(insn), rt = RT(insn), rd = RD(insn), sa = SA(insn);
    uint32_t funct = FUNCT(insn);
    uint64_t target, addr;

    switch (op) {
    /* ---------------- SPECIAL (0x00) ---------------- */
    case 0x00:
        switch (funct) {
        case 0x00: set_gpr(c, rd, c->gpr[rt] << sa); break;                    /* SLL */
        case 0x02: set_gpr(c, rd, c->gpr[rt] >> sa); break;                    /* SRL */
        case 0x03: set_gpr(c, rd, (int64_t)c->gpr[rt] >> sa); break;           /* SRA */
        case 0x04: set_gpr(c, rd, c->gpr[rt] << (c->gpr[rs] & 0x1F)); break;   /* SLLV */
        case 0x06: set_gpr(c, rd, c->gpr[rt] >> (c->gpr[rs] & 0x1F)); break;   /* SRLV */
        case 0x07: set_gpr(c, rd, (int64_t)c->gpr[rt] >> (c->gpr[rs] & 0x1F)); break; /* SRAV */
        case 0x08: /* JR */
            target = c->gpr[rs];
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = target;
            break;
        case 0x09: /* JALR */
            target = c->gpr[rs];
            set_gpr(c, rd, inst_addr + 8);
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = target;
            break;
        case 0x0A: /* MOVZ */
            if (c->gpr[rt] == 0) set_gpr(c, rd, c->gpr[rs]);
            break;
        case 0x0B: /* MOVN */
            if (c->gpr[rt] != 0) set_gpr(c, rd, c->gpr[rs]);
            break;
        case 0x0C: /* SYSCALL */
            vr5432_exception(c, inst_addr, EXC_SYSCALL, was_delay);
            break;
        case 0x0D: /* BREAK */
            vr5432_exception(c, inst_addr, EXC_BREAK, was_delay);
            break;
        case 0x0F: /* SYNC */ break;
        case 0x10: set_gpr(c, rd, c->hi); break;   /* MFHI */
        case 0x11: c->hi = c->gpr[rs]; break;      /* MTHI */
        case 0x12: set_gpr(c, rd, c->lo); break;   /* MFLO */
        case 0x13: c->lo = c->gpr[rs]; break;      /* MTLO */
        case 0x14: set_gpr(c, rd, c->gpr[rt] << (c->gpr[rs] & 0x3F)); break;   /* DSLLV */
        case 0x16: set_gpr(c, rd, c->gpr[rt] >> (c->gpr[rs] & 0x3F)); break;   /* DSRLV */
        case 0x17: set_gpr(c, rd, (int64_t)c->gpr[rt] >> (c->gpr[rs] & 0x3F)); break; /* DSRAV */
        case 0x18: { /* MULT */
            int64_t r = (int64_t)(int32_t)c->gpr[rs] * (int64_t)(int32_t)c->gpr[rt];
            c->lo = (uint64_t)(int64_t)r; c->hi = (uint64_t)((int64_t)r >> 32);
        break; }
        case 0x19: { /* MULTU */
            uint64_t r = (uint64_t)(uint32_t)c->gpr[rs] * (uint64_t)(uint32_t)c->gpr[rt];
            c->lo = r & 0xFFFFFFFF; c->hi = r >> 32;
        break; }
        case 0x1A: /* DIV */
            if ((int32_t)c->gpr[rt] != 0) {
                int32_t a = (int32_t)c->gpr[rs], b = (int32_t)c->gpr[rt];
                if (a == INT32_MIN && b == -1) {
                    c->lo = (uint32_t)INT32_MIN; c->hi = 0;
                } else {
                    c->lo = (uint32_t)(a / b);
                    c->hi = (uint32_t)(a % b);
                }
            }
        break;
        case 0x1B: /* DIVU */
            if ((uint32_t)c->gpr[rt] != 0) {
                c->lo = (uint32_t)c->gpr[rs] / (uint32_t)c->gpr[rt];
                c->hi = (uint32_t)c->gpr[rs] % (uint32_t)c->gpr[rt];
            }
        break;
        case 0x1C: { /* DMULT */
            __int128_t r = (__int128_t)(int64_t)c->gpr[rs] * (int64_t)c->gpr[rt];
            c->lo = (uint64_t)r; c->hi = (uint64_t)(r >> 64);
        break; }
        case 0x1D: { /* DMULTU */
            uint64_t a = c->gpr[rs], b = c->gpr[rt];
            __uint128_t r = (__uint128_t)a * b;
            c->lo = (uint64_t)r; c->hi = (uint64_t)(r >> 64);
        break; }
        case 0x1E: /* DDIV */
            if ((int64_t)c->gpr[rt] != 0) {
                int64_t a = (int64_t)c->gpr[rs], b = (int64_t)c->gpr[rt];
                c->lo = (uint64_t)(a / b); c->hi = (uint64_t)(a % b);
            }
        break;
        case 0x1F: /* DDIVU */
            if (c->gpr[rt] != 0) {
                c->lo = c->gpr[rs] / c->gpr[rt];
                c->hi = c->gpr[rs] % c->gpr[rt];
            }
        break;
        case 0x20: { /* ADD */
            int64_t a = (int32_t)c->gpr[rs], b = (int32_t)c->gpr[rt];
            int64_t r = a + b;
            if (((a ^ r) & (b ^ r) & 0x80000000ull) == 0x80000000ull)
                vr5432_exception(c, inst_addr, EXC_OV, was_delay);
            else set_gpr(c, rd, (uint32_t)r);
            break; }
        case 0x21: set_gpr(c, rd, (uint32_t)(c->gpr[rs] + c->gpr[rt])); break;  /* ADDU */
        case 0x22: { /* SUB */
            int64_t a = (int32_t)c->gpr[rs], b = (int32_t)c->gpr[rt];
            int64_t r = a - b;
            if (((a ^ r) & (~b ^ r) & 0x80000000ull) == 0x80000000ull)
                vr5432_exception(c, inst_addr, EXC_OV, was_delay);
            else set_gpr(c, rd, (uint32_t)r);
            break; }
        case 0x23: set_gpr(c, rd, (uint32_t)(c->gpr[rs] - c->gpr[rt])); break;  /* SUBU */
        case 0x24: set_gpr(c, rd, c->gpr[rs] & c->gpr[rt]); break;   /* AND */
        case 0x25: set_gpr(c, rd, c->gpr[rs] | c->gpr[rt]); break;   /* OR */
        case 0x26: set_gpr(c, rd, c->gpr[rs] ^ c->gpr[rt]); break;   /* XOR */
        case 0x27: set_gpr(c, rd, ~(c->gpr[rs] | c->gpr[rt])); break; /* NOR */
        case 0x2A: set_gpr(c, rd, (int64_t)c->gpr[rs] < (int64_t)c->gpr[rt]); break; /* SLT */
        case 0x2B: set_gpr(c, rd, c->gpr[rs] < c->gpr[rt]); break;   /* SLTU */
        case 0x2C: { /* DADD */
            int64_t a = (int64_t)c->gpr[rs], b = (int64_t)c->gpr[rt];
            int64_t r = a + b;
            if (((a ^ r) & (b ^ r)) < 0)
                vr5432_exception(c, inst_addr, EXC_OV, was_delay);
            else set_gpr(c, rd, (uint64_t)r);
            break; }
        case 0x2D: set_gpr(c, rd, c->gpr[rs] + c->gpr[rt]); break;   /* DADDU */
        case 0x2E: { /* DSUB */
            int64_t a = (int64_t)c->gpr[rs], b = (int64_t)c->gpr[rt];
            int64_t r = a - b;
            if (((a ^ r) & (~b ^ r)) < 0)
                vr5432_exception(c, inst_addr, EXC_OV, was_delay);
            else set_gpr(c, rd, (uint64_t)r);
            break; }
        case 0x2F: set_gpr(c, rd, c->gpr[rs] - c->gpr[rt]); break;   /* DSUBU */
        case 0x30: /* TGE */
            if ((int64_t)c->gpr[rs] >= (int64_t)c->gpr[rt])
                vr5432_exception(c, inst_addr, EXC_TRAP, was_delay);
            break;
        case 0x31: /* TGEU */
            if (c->gpr[rs] >= c->gpr[rt])
                vr5432_exception(c, inst_addr, EXC_TRAP, was_delay);
            break;
        case 0x32: /* TLT */
            if ((int64_t)c->gpr[rs] < (int64_t)c->gpr[rt])
                vr5432_exception(c, inst_addr, EXC_TRAP, was_delay);
            break;
        case 0x33: /* TLTU */
            if (c->gpr[rs] < c->gpr[rt])
                vr5432_exception(c, inst_addr, EXC_TRAP, was_delay);
            break;
        case 0x34: /* TEQ */
            if (c->gpr[rs] == c->gpr[rt])
                vr5432_exception(c, inst_addr, EXC_TRAP, was_delay);
            break;
        case 0x36: /* TNE */
            if (c->gpr[rs] != c->gpr[rt])
                vr5432_exception(c, inst_addr, EXC_TRAP, was_delay);
            break;
        case 0x38: set_gpr(c, rd, c->gpr[rt] << sa); break;     /* DSLL */
        case 0x3A: set_gpr(c, rd, c->gpr[rt] >> sa); break;     /* DSRL */
        case 0x3B: set_gpr(c, rd, (int64_t)c->gpr[rt] >> sa); break; /* DSRA */
        case 0x3C: set_gpr(c, rd, c->gpr[rt] << (sa + 32)); break;   /* DSLL32 */
        case 0x3E: set_gpr(c, rd, c->gpr[rt] >> (sa + 32)); break;   /* DSRL32 */
        case 0x3F: set_gpr(c, rd, (int64_t)c->gpr[rt] >> (sa + 32)); break; /* DSRA32 */
        default:
            break;
        }
        break;

    /* ---------------- REGIMM (0x01) ---------------- */
    case 0x01: {
        bool likely = (rt & 0x20) != 0;
        int64_t off = se16(insn) << 2;
        uint64_t tgt = inst_addr + 4 + (uint64_t)off;
        bool taken = false;
        switch (rt & 0x1F) {
        case 0x00: taken = (int64_t)c->gpr[rs] < 0; break;          /* BLTZ */
        case 0x01: taken = (int64_t)c->gpr[rs] >= 0; break;         /* BGEZ */
        case 0x10:                                                  /* BLTZAL */
            if ((int64_t)c->gpr[rs] < 0) { taken = true; }
            set_gpr(c, 31, inst_addr + 8);
            break;
        case 0x11:                                                  /* BGEZAL */
            if ((int64_t)c->gpr[rs] >= 0) { taken = true; }
            set_gpr(c, 31, inst_addr + 8);
            break;
        default: break;
        }
        if (taken) {
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = tgt;
        } else if (likely) {
            c->pc += 4;   /* annul the delay slot */
        }
        break;
    }

    case 0x02: /* J */
        c->in_delay_slot = true;
        c->branch_pc = inst_addr;
        c->branch_target = (inst_addr & 0xF0000000ull) | ((uint64_t)TGT(insn) << 2);
        break;
    case 0x03: /* JAL */
        set_gpr(c, 31, inst_addr + 8);
        c->in_delay_slot = true;
        c->branch_pc = inst_addr;
        c->branch_target = (inst_addr & 0xF0000000ull) | ((uint64_t)TGT(insn) << 2);
        break;

    case 0x04: /* BEQ */
        if (c->gpr[rs] == c->gpr[rt]) {
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = inst_addr + 4 + (uint64_t)(se16(insn) << 2);
        }
        break;
    case 0x05: /* BNE */
        if (c->gpr[rs] != c->gpr[rt]) {
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = inst_addr + 4 + (uint64_t)(se16(insn) << 2);
        }
        break;
    case 0x06: /* BLEZ */
        if ((int64_t)c->gpr[rs] <= 0) {
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = inst_addr + 4 + (uint64_t)(se16(insn) << 2);
        }
        break;
    case 0x07: /* BGTZ */
        if ((int64_t)c->gpr[rs] > 0) {
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = inst_addr + 4 + (uint64_t)(se16(insn) << 2);
        }
        break;

    case 0x08: { /* ADDI */
        int64_t a = (int32_t)c->gpr[rs], b = se16(insn);
        int64_t r = a + b;
        if (((a ^ r) & (b ^ r) & 0x80000000ull) == 0x80000000ull)
            vr5432_exception(c, inst_addr, EXC_OV, was_delay);
        else set_gpr(c, rt, (uint32_t)r);
        break; }
    case 0x09: set_gpr(c, rt, (uint32_t)(c->gpr[rs] + (uint64_t)se16(insn))); break; /* ADDIU */
    case 0x0A: set_gpr(c, rt, (int64_t)c->gpr[rs] < se16(insn)); break;  /* SLTI */
    case 0x0B: set_gpr(c, rt, c->gpr[rs] < (uint64_t)se16(insn)); break; /* SLTIU */
    case 0x0C: set_gpr(c, rt, c->gpr[rs] & (uint64_t)IMM(insn)); break;  /* ANDI */
    case 0x0D: set_gpr(c, rt, c->gpr[rs] | (uint64_t)IMM(insn)); break;  /* ORI */
    case 0x0E: set_gpr(c, rt, c->gpr[rs] ^ (uint64_t)IMM(insn)); break;  /* XORI */
    case 0x0F: set_gpr(c, rt, (uint64_t)IMM(insn) << 16); break;         /* LUI */

    case 0x14: /* BEQL */
        if (c->gpr[rs] == c->gpr[rt]) {
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = inst_addr + 4 + (uint64_t)(se16(insn) << 2);
        } else {
            c->pc += 4;
        }
        break;
    case 0x15: /* BNEL */
        if (c->gpr[rs] != c->gpr[rt]) {
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = inst_addr + 4 + (uint64_t)(se16(insn) << 2);
        } else {
            c->pc += 4;
        }
        break;
    case 0x16: /* BLEZL */
        if ((int64_t)c->gpr[rs] <= 0) {
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = inst_addr + 4 + (uint64_t)(se16(insn) << 2);
        } else {
            c->pc += 4;
        }
        break;
    case 0x17: /* BGTZL */
        if ((int64_t)c->gpr[rs] > 0) {
            c->in_delay_slot = true;
            c->branch_pc = inst_addr;
            c->branch_target = inst_addr + 4 + (uint64_t)(se16(insn) << 2);
        } else {
            c->pc += 4;
        }
        break;

    case 0x18: { /* DADDI */
        int64_t a = (int64_t)c->gpr[rs], b = se16(insn);
        int64_t r = a + b;
        if (((a ^ r) & (b ^ r)) < 0)
            vr5432_exception(c, inst_addr, EXC_OV, was_delay);
        else set_gpr(c, rt, (uint64_t)r);
        break; }
    case 0x19: set_gpr(c, rt, c->gpr[rs] + (uint64_t)se16(insn)); break; /* DADDIU */

    /* ---------------- loads ---------------- */
    case 0x20: /* LB */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        set_gpr(c, rt, (uint64_t)se8(bus_read8(c->bus, addr)));
        break;
    case 0x21: /* LH */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 2, EXC_ADEL, was_delay, inst_addr)) break;
        set_gpr(c, rt, (uint64_t)se16(bus_read16(c->bus, addr)));
        break;
    case 0x22: { /* LWL */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        set_gpr(c, rt, mips_lwl(c, addr, (uint32_t)c->gpr[rt]));
        break; }
    case 0x23: /* LW */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 4, EXC_ADEL, was_delay, inst_addr)) break;
        set_gpr(c, rt, (uint64_t)(int64_t)(int32_t)bus_read32(c->bus, addr));
        break;
    case 0x24: /* LBU */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        set_gpr(c, rt, bus_read8(c->bus, addr));
        break;
    case 0x25: /* LHU */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 2, EXC_ADEL, was_delay, inst_addr)) break;
        set_gpr(c, rt, bus_read16(c->bus, addr));
        break;
    case 0x26: { /* LWR */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        set_gpr(c, rt, mips_lwr(c, addr, (uint32_t)c->gpr[rt]));
        break; }
    case 0x27: /* LWU */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 4, EXC_ADEL, was_delay, inst_addr)) break;
        set_gpr(c, rt, bus_read32(c->bus, addr));
        break;
    case 0x1A: { /* LDL */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        set_gpr(c, rt, mips_ldl(c, addr, c->gpr[rt]));
        break; }
    case 0x1B: { /* LDR */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        set_gpr(c, rt, mips_ldr(c, addr, c->gpr[rt]));
        break; }
    case 0x30: /* LL */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 4, EXC_ADEL, was_delay, inst_addr)) break;
        set_gpr(c, rt, (uint64_t)(int64_t)(int32_t)bus_read32(c->bus, addr));
        c->llbit = 1;
        c->cp0[CP0_LLADDR] = addr;
        break;
    case 0x37: /* LD (R4300 encoding) */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 8, EXC_ADEL, was_delay, inst_addr)) break;
        set_gpr(c, rt, bus_read64(c->bus, addr));
        break;

    /* ---------------- stores ---------------- */
    case 0x28: /* SB */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        bus_write8(c->bus, addr, (uint8_t)c->gpr[rt]);
        break;
    case 0x29: /* SH */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 2, EXC_ADES, was_delay, inst_addr)) break;
        bus_write16(c->bus, addr, (uint16_t)c->gpr[rt]);
        break;
    case 0x2A: { /* SWL */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        bus_write32(c->bus, addr & ~3ull, mips_swl(c, addr, (uint32_t)c->gpr[rt]));
        break; }
    case 0x2B: /* SW */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 4, EXC_ADES, was_delay, inst_addr)) break;
        bus_write32(c->bus, addr, (uint32_t)c->gpr[rt]);
        break;
    case 0x2C: { /* SDL */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        bus_write64(c->bus, addr & ~7ull, mips_sdl(c, addr, c->gpr[rt]));
        break; }
    case 0x2D: { /* SDR */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        bus_write64(c->bus, addr & ~7ull, mips_sdr(c, addr, c->gpr[rt]));
        break; }
    case 0x2E: { /* SWR */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        bus_write32(c->bus, addr & ~3ull, mips_swr(c, addr, (uint32_t)c->gpr[rt]));
        break; }
    case 0x2F: /* CACHE */ break;
    case 0x38: /* SC (R4300 encoding) */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 4, EXC_ADES, was_delay, inst_addr)) break;
        if (c->llbit) {
            bus_write32(c->bus, addr, (uint32_t)c->gpr[rt]);
            set_gpr(c, rt, 1);
        } else {
            set_gpr(c, rt, 0);
        }
        c->llbit = 0;
        break;
    case 0x3F: /* SD (R4300 encoding) */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 8, EXC_ADES, was_delay, inst_addr)) break;
        bus_write64(c->bus, addr, c->gpr[rt]);
        break;

    /* ---------------- COP0 ---------------- */
    case 0x10:
        if (rs == 0x00) {   /* MFC0 */
            set_gpr(c, rt, c->cp0[rd & 0x1F]);
        } else if (rs == 0x04) { /* MTC0 */
            switch (rd & 0x1F) {
            case CP0_STATUS:
                c->cp0[CP0_STATUS] = c->gpr[rt];   /* full write keeps BEV/IM/IE */
                break;
            case CP0_COUNT:
                c->cp0[CP0_COUNT] = c->gpr[rt];
                c->cp0[CP0_CAUSE] &= ~CAUSE_IP7;
                break;
            default:
                c->cp0[rd & 0x1F] = c->gpr[rt];
                break;
            }
        } else if (rs == 0x10) {
            if (funct == 0x18) {  /* ERET */
                c->llbit = 0;
                if (c->cp0[CP0_STATUS] & SR_ERL) {
                    c->pc = c->cp0[CP0_ERROREPC];
                    c->cp0[CP0_STATUS] &= ~SR_ERL;
                } else {
                    c->pc = c->cp0[CP0_EPC];
                    c->cp0[CP0_STATUS] &= ~SR_EXL;
                }
                c->in_delay_slot = false;
            }
            /* TLB ops (TLBR/TLBWI/TLBWR/TLBP) are no-ops in phase 1 */
        }
        break;

    /* ---------------- COP1 FPU ---------------- */
    case 0x11: {
        uint32_t fmt = rs;
        if (fmt == 0x00) {   /* MFC1 */
            set_gpr(c, rt, (uint64_t)(int64_t)(int32_t)c->fpr[rd]);
        } else if (fmt == 0x01) { /* DMFC1 */
            uint64_t v = (uint64_t)c->fpr[rd] | ((uint64_t)c->fpr[rd + 1] << 32);
            set_gpr(c, rt, v);
        } else if (fmt == 0x02) { /* CFC1 */
            set_gpr(c, rt, (rd == 0) ? c->fcr0 : c->fcr31);
        } else if (fmt == 0x04) { /* MTC1 */
            c->fpr[rd] = (uint32_t)c->gpr[rt];
        } else if (fmt == 0x05) { /* DMTC1 */
            c->fpr[rd] = (uint32_t)c->gpr[rt];
            c->fpr[rd + 1] = (uint32_t)(c->gpr[rt] >> 32);
        } else if (fmt == 0x06) { /* CTC1 */
            if (rd == 1) c->fcr31 = (uint32_t)c->gpr[rt];
        } else if (fmt == 0x08) { /* BC1 */
            bool likely = (rt & 0x02) != 0;
            bool tf = (rt & 0x01) != 0;
            bool cc = (c->fcr31 & FCR31_CC) != 0;
            bool taken = tf ? cc : !cc;
            if (taken) {
                c->in_delay_slot = true;
                c->branch_pc = inst_addr;
                c->branch_target = inst_addr + 4 + (uint64_t)(se16(insn) << 2);
            } else if (likely) {
                c->pc += 4;
            }
        } else if (fmt == 0x10 || fmt == 0x11) {
            /* single/double arithmetic + conversions.
             * COP1 R-type: ft = bits[20:16] = RT(i), fs = bits[15:11] = RD(i),
             *              fd = bits[10:6] = SA(i) */
            bool is_d = (fmt == 0x11);
            double fa, fb, fr;
            int fs = (int)rd;
            int ft = (int)rt;
            int fd = (int)sa;
            switch (funct) {
            case 0x00: /* ADD */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                fb = is_d ? fpr_d(c, ft) : fpr_s(c, ft);
                fr = fa + fb;
                if (is_d) set_fpr_d(c, fd, fr); else set_fpr_s(c, fd, (float)fr);
        break;
            case 0x01: /* SUB */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                fb = is_d ? fpr_d(c, ft) : fpr_s(c, ft);
                fr = fa - fb;
                if (is_d) set_fpr_d(c, fd, fr); else set_fpr_s(c, fd, (float)fr);
        break;
            case 0x02: /* MUL */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                fb = is_d ? fpr_d(c, ft) : fpr_s(c, ft);
                fr = fa * fb;
                if (is_d) set_fpr_d(c, fd, fr); else set_fpr_s(c, fd, (float)fr);
        break;
            case 0x03: /* DIV */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                fb = is_d ? fpr_d(c, ft) : fpr_s(c, ft);
                if (fb == 0.0) fpu_set_flag(c, FCR31_FLAG_DIV0);
                fr = fa / fb;
                if (is_d) set_fpr_d(c, fd, fr); else set_fpr_s(c, fd, (float)fr);
        break;
            case 0x04: /* SQRT */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                fr = sqrt(fa);
                if (is_d) set_fpr_d(c, fd, fr); else set_fpr_s(c, fd, (float)fr);
        break;
            case 0x05: /* ABS */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                fr = fabs(fa);
                if (is_d) set_fpr_d(c, fd, fr); else set_fpr_s(c, fd, (float)fr);
                break;
            case 0x06: /* MOV */
                if (is_d) set_fpr_d(c, fd, fpr_d(c, fs));
                else set_fpr_s(c, fd, fpr_s(c, fs));
                break;
            case 0x11: /* MOVT (move if FPU condition true) */
                if (c->fcr31 & FCR31_CC) {
                    if (is_d) set_fpr_d(c, fd, fpr_d(c, fs));
                    else set_fpr_s(c, fd, fpr_s(c, fs));
                }
                break;
            case 0x31: /* MOVT.D */
                if (c->fcr31 & FCR31_CC)
                    set_fpr_d(c, fd, fpr_d(c, fs));
                break;
            case 0x07: /* NEG */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                fr = -fa;
                if (is_d) set_fpr_d(c, fd, fr); else set_fpr_s(c, fd, (float)fr);
                break;
            case 0x0C: /* ROUND.W */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                c->fpr[fd] = (uint32_t)cvt_w(fa);
                break;
            case 0x0D: /* TRUNC.W */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                c->fpr[fd] = (uint32_t)(int32_t)trunc(fa);
                break;
            case 0x0E: /* CEIL.W */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                c->fpr[fd] = (uint32_t)(int32_t)ceil(fa);
                break;
            case 0x0F: /* FLOOR.W */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                c->fpr[fd] = (uint32_t)(int32_t)floor(fa);
                break;
            case 0x08: /* ROUND.L */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                set_fpr_l(c, fd, rnd_nearest(fa));
                break;
            case 0x09: /* TRUNC.L */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                set_fpr_l(c, fd, (int64_t)trunc(fa));
                break;
            case 0x0A: /* CEIL.L */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                set_fpr_l(c, fd, (int64_t)ceil(fa));
                break;
            case 0x0B: /* FLOOR.L */
                fa = is_d ? fpr_d(c, fs) : fpr_s(c, fs);
                set_fpr_l(c, fd, (int64_t)floor(fa));
                break;
            case 0x14: /* CVT.D.S */
                set_fpr_d(c, fd, (double)fpr_s(c, fs));
                break;
            case 0x15: /* CVT.W.S */
                c->fpr[fd] = (uint32_t)cvt_w(fpr_s(c, fs));
                break;
            case 0x16: /* CVT.L.S */
                set_fpr_l(c, fd, (int64_t)cvt_l(fpr_s(c, fs)));
                break;
            case 0x20: /* CVT.S.D */
                set_fpr_s(c, fd, (float)fpr_d(c, fs));
                break;
            case 0x21: /* CVT.W.D */
                c->fpr[fd] = (uint32_t)cvt_w(fpr_d(c, fs));
                break;
            case 0x22: /* CVT.L.D */
                set_fpr_l(c, fd, (int64_t)cvt_l(fpr_d(c, fs)));
                break;
            case 0x45: /* RECIP.S */
                set_fpr_s(c, fd, 1.0f / fpr_s(c, fs));
        break;
            case 0x4A: /* RSQRT.S */
                set_fpr_s(c, fd, 1.0f / sqrtf(fpr_s(c, fs)));
        break;
            case 0x46: /* RECIP.D */
                set_fpr_d(c, fd, 1.0 / fpr_d(c, fs));
        break;
            case 0x4B: /* RSQRT.D */
                set_fpr_d(c, fd, 1.0 / sqrt(fpr_d(c, fs)));
        break;
            default:
                /* C.cond.S = 0x30-0x3F, C.cond.D = 0x48-0x4F */
                if (funct >= 0x30 && funct <= 0x3F) {
                    uint32_t cond = funct & 0x0F;
                    double a = fpr_s(c, fs), b = fpr_s(c, ft);
                    c->fcr31 = (c->fcr31 & ~FCR31_CC) |
                               (fpu_compare(a, b, cond) ? FCR31_CC : 0);
                } else if (funct >= 0x48 && funct <= 0x4F) {
                    uint32_t cond = funct & 0x0F;
                    double a = fpr_d(c, fs), b = fpr_d(c, ft);
                    c->fcr31 = (c->fcr31 & ~FCR31_CC) |
                               (fpu_compare(a, b, cond) ? FCR31_CC : 0);
                }
                break;
            }
        } else if (fmt == 0x14) {  /* W: conversions (fs = bits[15:11]) */
            int fs = (int)rd, fd = (int)sa;
            switch (funct) {
            case 0x20: set_fpr_s(c, fd, (float)(int32_t)c->fpr[fs]); break;  /* CVT.S.W */
            case 0x21: set_fpr_d(c, fd, (double)(int32_t)c->fpr[fs]); break; /* CVT.D.W */
            case 0x22: set_fpr_d(c, fd, (double)(int64_t)(int32_t)c->fpr[fs]); break; /* CVT.L.W */
            default: break;
            }
        } else if (fmt == 0x15) {  /* L: conversions (fs = bits[15:11]) */
            int fs = (int)rd, fd = (int)sa;
            switch (funct) {
            case 0x20: set_fpr_s(c, fd, (float)fpr_l(c, fs)); break;   /* CVT.S.L */
            case 0x21: set_fpr_d(c, fd, (double)fpr_l(c, fs)); break;  /* CVT.D.L */
            case 0x22: c->fpr[fd] = (uint32_t)(int64_t)trunc((double)fpr_l(c, fs)); break; /* CVT.W.L */
            default: break;
            }
        }
        break;
    }

    /* ---------------- COP1X (MIPS IV MADD/MSUB) ---------------- */
    case 0x13: {
        /* COP1X fields: fr=bits[25:21], ft=bits[20:16], fs=bits[15:11],
         * fd=bits[10:6] (MIPS IV, verified against llvm-mc) */
        int fr = rs, ft = rt, fs = rd, fd = sa;
        switch (funct) {
        case 0x20: /* MADD.S */
            set_fpr_s(c, fd, (float)(fpr_s(c, fr) + fpr_s(c, fs) * fpr_s(c, ft)));
            break;
        case 0x28: /* MSUB.S */
            set_fpr_s(c, fd, (float)(fpr_s(c, fr) - fpr_s(c, fs) * fpr_s(c, ft)));
            break;
        case 0x2C: /* NMADD.S */
            set_fpr_s(c, fd, (float)-(fpr_s(c, fr) + fpr_s(c, fs) * fpr_s(c, ft)));
            break;
        case 0x30: /* NMSUB.S */
            set_fpr_s(c, fd, (float)-(fpr_s(c, fr) - fpr_s(c, fs) * fpr_s(c, ft)));
            break;
        case 0x21: /* MADD.D */
            set_fpr_d(c, fd, fpr_d(c, fr) + fpr_d(c, fs) * fpr_d(c, ft));
            break;
        case 0x29: /* MSUB.D */
            set_fpr_d(c, fd, fpr_d(c, fr) - fpr_d(c, fs) * fpr_d(c, ft));
            break;
        case 0x2D: /* NMADD.D */
            set_fpr_d(c, fd, -(fpr_d(c, fr) + fpr_d(c, fs) * fpr_d(c, ft)));
            break;
        case 0x31: /* NMSUB.D */
            set_fpr_d(c, fd, -(fpr_d(c, fr) - fpr_d(c, fs) * fpr_d(c, ft)));
            break;
        default:
            break;
        }
        break;
    }

    /* ---------------- LWC1 / LDC1 / SWC1 / SDC1 ---------------- */
    case 0x31: /* LWC1 */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 4, EXC_ADEL, was_delay, inst_addr)) break;
        c->fpr[rt] = bus_read32(c->bus, addr);
        break;
    case 0x35: /* LDC1 (R4300 encoding) */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 8, EXC_ADEL, was_delay, inst_addr)) break;
        c->fpr[rt] = (uint32_t)bus_read64(c->bus, addr);
        c->fpr[rt + 1] = (uint32_t)(bus_read64(c->bus, addr) >> 32);
        break;
    case 0x39: /* SWC1 (R4300 encoding) */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 4, EXC_ADES, was_delay, inst_addr)) break;
        bus_write32(c->bus, addr, c->fpr[rt]);
        break;
    case 0x3D: /* SDC1 (R4300 encoding) */
        addr = c->gpr[rs] + (uint64_t)se16(insn);
        if (addr_err_check(c, addr, 8, EXC_ADES, was_delay, inst_addr)) break;
        bus_write64(c->bus, addr, (uint64_t)c->fpr[rt] |
                    ((uint64_t)c->fpr[rt + 1] << 32));
        break;

    default:
        break;   /* unknown opcode: ignore for phase 1 */
    }
}

void vr5432_step(Vr5432* c) {
    uint64_t inst_addr = c->pc;
    bool was_delay = c->in_delay_slot;

    uint32_t insn = bus_read32(c->bus, inst_addr);

    if (was_delay) {
        c->pc = c->branch_target;
        c->in_delay_slot = false;
    } else {
        c->pc = inst_addr + 4;
    }

    execute(c, insn, inst_addr, was_delay);

    /* superscalar dual-issue cycle accounting */
    InsnInfo ii;
    classify_insn(insn, &ii);
    account_pipeline(c, &ii);

    /* COP0 count/compare timer */
    uint64_t count = c->cp0[CP0_COUNT] + 1;
    c->cp0[CP0_COUNT] = count;
    if (count == c->cp0[CP0_COMPARE]) c->cp0[CP0_CAUSE] |= CAUSE_IP7;
}

void vr5432_run(Vr5432* c, uint64_t max_cycles) {
    while (!c->halted && c->cycles < max_cycles) {
        /* take a pending interrupt at an instruction boundary */
        uint32_t sr = (uint32_t)c->cp0[CP0_STATUS];
        if ((sr & SR_IE) && !(sr & (SR_EXL | SR_ERL))) {
            uint32_t pend = (uint32_t)(c->cp0[CP0_CAUSE] & 0xFF00) & (sr & SR_IM);
            if (pend && !c->in_delay_slot) {
                vr5432_exception(c, c->pc, EXC_INT, false);
                continue;
            }
        }
        vr5432_step(c);
    }
}
