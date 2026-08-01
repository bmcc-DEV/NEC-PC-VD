/*
 * vr5432.h - NEC VR5432 MIPS IV 64-bit RISC CPU.
 *
 * MIPS IV 64-bit core: 32 GPRs, HI/LO, PC, COP0 (Status/Cause/EPC/Count/
 * Compare/Config/PRId) and COP1 IEEE-754 FPU (32 FPRs, single/double).
 * Boot vector at 0xBFC00000.
 *
 * Fase 1: execução single-issue semanticamente correta com contagem de
 * ciclos por instrução (a dual-issue superscalar é otimização futura).
 */
#ifndef VIPER_VR5432_H
#define VIPER_VR5432_H

#include <stdint.h>
#include <stdbool.h>
#include "bus.h"

/* COP0 register indices */
enum {
    CP0_INDEX = 0, CP0_RANDOM, CP0_ENTRYLO0, CP0_ENTRYLO1, CP0_CONTEXT,
    CP0_PAGEMASK, CP0_WIRED, CP0_BADVADDR = 8, CP0_COUNT, CP0_ENTRYHI,
    CP0_COMPARE = 11,
    CP0_STATUS = 12, CP0_CAUSE, CP0_EPC, CP0_PRID, CP0_CONFIG,
    CP0_LLADDR, CP0_WATCHLO, CP0_WATCHHI, CP0_ERRCTL = 26, CP0_TAGLO,
    CP0_TAGHI, CP0_ERROREPC = 30, CP0_CACHEERR = 31
};

/* Status register bits */
#define SR_IE       0x00000001
#define SR_EXL      0x00000002
#define SR_ERL      0x00000004
#define SR_KSU_SHIFT 3
#define SR_KSU_MASK  0x00000018
#define SR_KSU_KERNEL 0x00000000
#define SR_IM       0x0000FF00
#define SR_BEV      0x00400000
#define SR_FR       0x04000000
#define SR_CU0      0x10000000
#define SR_CU1      0x20000000

/* Cause register bits */
#define CAUSE_EXC_SHIFT 2
#define CAUSE_EXC_MASK  0x0000007C
#define CAUSE_BD        0x80000000
#define CAUSE_IP7       0x00008000

/* Exception codes */
#define EXC_INT    0
#define EXC_ADEL   4
#define EXC_ADES   5
#define EXC_CPU    6
#define EXC_SYSCALL 8
#define EXC_BREAK  9
#define EXC_OV     12
#define EXC_TRAP   13

/* FCR31 bits */
#define FCR31_ROUND_SHIFT 23
#define FCR31_ROUND_RN    0    /* round to nearest */
#define FCR31_ROUND_RZ    1
#define FCR31_ROUND_RP    2
#define FCR31_ROUND_RM    3
#define FCR31_FLAG_INVALID  0x00000004   /* bit 2 */
#define FCR31_FLAG_DIV0     0x00000008   /* bit 3 */
#define FCR31_FLAG_OVF      0x00000010   /* bit 4 */
#define FCR31_FLAG_UNF      0x00000020   /* bit 5 */
#define FCR31_FLAG_INEXACT  0x00000040   /* bit 6 */
#define FCR31_CC            0x00800000   /* bit 23: condition bit */

typedef struct Vr5432 {
    Bus* bus;

    uint64_t gpr[32];   /* general purpose registers */
    uint64_t hi, lo;
    uint64_t pc;
    uint64_t cp0[32];   /* COP0 */

    uint32_t fpr[32];   /* COP1 FPU (single precision slots; doubles pair up) */
    uint32_t fcr0;
    uint32_t fcr31;

    uint64_t llbit;     /* load-linked bit for LL/SC */

    bool in_delay_slot;      /* current instruction is a branch delay slot */
    bool delay_branch_likely;
    uint64_t branch_pc;      /* address of the branch that set the delay slot */
    uint64_t branch_target;

    bool halted;
    uint64_t cycles;    /* modeled superscalar cycle count */

    /* ---- dual-issue superscalar pipeline model ----
     * Two pipes (INT and FPU); an INT instruction and an independent FPU
     * instruction can issue in the same cycle. `cycles` is the completion
     * cycle of the latest instruction. */
    uint64_t pipe_int_free;   /* cycle when the INT pipe is free */
    uint64_t pipe_fpu_free;   /* cycle when the FPU pipe is free */
    uint64_t gpr_ready[32];   /* cycle when each GPR value is ready */
    uint64_t fpr_ready[32];   /* cycle when each FPR value is ready */
    uint64_t hilo_ready;      /* cycle when HI/LO are ready */
    int last_pipe;            /* 0=INT, 1=FPU, -1 = none */
    int last_structural;
    int last_dest_kind;       /* 0=none, 1=GPR, 2=FPR, 3=HI/LO */
    int last_dest_reg;
    uint64_t last_issue;      /* issue cycle of the previous instruction */
} Vr5432;

void vr5432_reset(Vr5432* cpu, Bus* bus);
void vr5432_step(Vr5432* cpu);
void vr5432_run(Vr5432* cpu, uint64_t max_cycles);

/* Debug / test accessors */
uint64_t vr5432_get_gpr(const Vr5432* cpu, int reg);
void vr5432_set_gpr(Vr5432* cpu, int reg, uint64_t value);
uint64_t vr5432_get_pc(const Vr5432* cpu);
void vr5432_set_pc(Vr5432* cpu, uint64_t pc);
uint64_t vr5432_get_cycles(const Vr5432* cpu);
void vr5432_set_fpr_single(Vr5432* cpu, int reg, float value);
float vr5432_get_fpr_single(const Vr5432* cpu, int reg);

#endif /* VIPER_VR5432_H */
