/*
 * firmware.S - NEC PC-Viper POST firmware (MIPS IV, little-endian).
 *
 * Boots at 0xBFC00000 and runs a CPU/FPU/MMIO self-test:
 *   1. 64-bit ALU / shifts
 *   2. multiply / divide (HI/LO)
 *   3. memory store/load
 *   4. branch + delay slot
 *   5. FPU single precision (add + convert)
 *   6. FPU double precision (mul + convert)
 *   7. syscall exception (handler + eret)
 *   8. MMIO: Voodoo2 EC color1, Viper SoC DMA, Aureal A3D channel
 *
 * On success a POST word 0xA3D2_01FF is stored at RAM 0x0000F000
 * (KSEG1 0xA000F000); each failing test leaves its bit clear.
 */

.set noreorder

#define PHYS_RAM(x)    (0xA0000000 + (x))   /* KSEG1 uncached */
#define RAM_POST       0x0000F000
#define RAM_SYSCOUNT   0x0000F100
#define RAM_MEMTEST    0x00008000

#define V2_COLOR1      0xB0000148      /* Voodoo2 EC color1 */
#define SOC_DMA_BASE   0xBE000000
#define A3D_CH0       0xB4000040

    .section .text
    .globl _start

_start:
    li   $s0, 0                /* POST flags accumulator */

    /* ---- 1. 64-bit ALU / shifts ----
     * t2 = dsll32(0x12345678) | 0x12345678 = 0x1234567812345678 */
    li   $t0, 0x12345678
    dsll32 $t0, $t0, 0
    li   $t1, 0x12345678
    or   $t2, $t0, $t1
    li   $t3, 0x12345678
    dsll32 $t3, $t3, 0
    or   $t3, $t3, $t1
    bne  $t2, $t3, .t2
    nop
    ori  $s0, $s0, 0x1          /* ALU ok */

    /* ---- 2. multiply / divide ----
     * dmultu(7, 3) -> lo=21, hi=0; ddivu(21, 3) -> lo=7 */
.t2:
    li   $t0, 7
    li   $t1, 3
    dmultu $t0, $t1
    mflo $t2                    /* 21 */
    mfhi $t3                    /* 0 */
    bnez $t3, .t3
    nop
    li   $t4, 21
    bne  $t2, $t4, .t3
    nop
    ddivu $zero, $t2, $t1
    mflo $t5                    /* 7 */
    li   $t4, 7
    bne  $t5, $t4, .t3
    nop
    ori  $s0, $s0, 0x2          /* mult/div ok */

    /* ---- 3. memory sd/ld roundtrip ---- */
.t3:
    li   $t0, 0xDEADBEEF
    dsll32 $t0, $t0, 0          /* 0xDEADBEEF00000000 */
    li   $t1, 0xBEEF
    or   $t0, $t0, $t1          /* 0xDEADBEEF0000BEEF */
    li   $t1, 0xA0008000
    sd   $t0, 0($t1)
    ld   $t2, 0($t1)
    bne  $t0, $t2, .t4
    nop
    ori  $s0, $s0, 0x4          /* memory ok */

    /* ---- 4. branch + delay slot ---- */
.t4:
    li   $t0, 1
    beq  $t0, $t0, .br_ok
    li   $t1, 0xFFFF            /* delay slot */
    b    .t5
    nop
.br_ok:
    ori  $s0, $s0, 0x8          /* branch ok */

    /* ---- 5. FPU single: 5.0 + 7.0 = 12 ---- */
.t5:
    li   $t0, 5
    mtc1 $t0, $f2
    cvt.s.w $f2, $f2
    li   $t0, 7
    mtc1 $t0, $f4
    cvt.s.w $f4, $f4
    add.s $f6, $f2, $f4
    trunc.w.s $f6, $f6
    mfc1 $t0, $f6
    li   $t1, 12
    bne  $t0, $t1, .t6
    nop
    ori  $s0, $s0, 0x10         /* FPU single ok */

    /* ---- 6. FPU double: 3.0 * 4.0 = 12 ---- */
.t6:
    li   $t0, 3
    mtc1 $t0, $f8
    cvt.d.w $f8, $f8
    li   $t0, 4
    mtc1 $t0, $f10
    cvt.d.w $f10, $f10
    mul.d $f12, $f8, $f10
    trunc.w.d $f12, $f12
    mfc1 $t0, $f12
    li   $t1, 12
    bne  $t0, $t1, .t7
    nop
    ori  $s0, $s0, 0x20         /* FPU double ok */

    /* ---- 7. syscall exception ---- */
.t7:
    li   $t1, 0xA000F100
    sw   $zero, 0($t1)
    syscall
    nop
    lw   $t2, 0($t1)            /* expect 1 after eret */
    li   $t3, 1
    bne  $t2, $t3, .t8
    nop
    ori  $s0, $s0, 0x40         /* syscall ok */

    /* ---- 8. timer interrupt (Count/Compare -> IP7) ----
     * program Compare ~100 cycles ahead, enable IE+IM7 and spin until
     * the shared exception handler raises the interrupt flag. */
.t8:
    li   $t1, 0xA000F200
    sw   $zero, 0($t1)          /* clear the interrupt flag */
    mfc0 $t0, $9                /* Count */
    addiu $t0, $t0, 100
    mtc0 $t0, $11               /* Compare = Count + 100 */
    mfc0 $t0, $12               /* Status */
    ori  $t0, $t0, 0x8001       /* IM7 + IE */
    mtc0 $t0, $12
    li   $t2, 0                 /* timeout guard */
.wait_int:
    lw   $t3, 0($t1)
    bnez $t3, .int_ok
    nop
    addiu $t2, $t2, 1
    slti $t3, $t2, 4000
    bnez $t3, .wait_int
    nop
    b    .t9                    /* timeout: leave the bit clear */
    nop
.int_ok:
    ori  $s0, $s0, 0x100        /* interrupt ok */

    /* ---- 9. MMIO: Voodoo2 EC color1 ---- */
.t9:
    li   $t0, 0xB0000148
    li   $t1, 0x00FF0000
    sw   $t1, 0($t0)

    /* ---- MMIO: Viper SoC DMA (DVD LBA 0 -> RAM 0x100000) ---- */
    li   $t0, 0xBE000000
    sw   $zero, 0x04($t0)       /* DMA_SRC */
    li   $t1, 0x100000
    sw   $t1, 0x08($t0)         /* DMA_DST */
    li   $t1, 4096
    sw   $t1, 0x0C($t0)         /* DMA_SIZE */
    li   $t1, 1
    sw   $t1, 0x00($t0)         /* DMA_CTRL start */

    /* ---- MMIO: Aureal A3D channel 0 ---- */
    li   $t0, 0xB4000040
    li   $t1, 0x100000
    sw   $t1, 0x08($t0)         /* SRC = RAM (DVD data) */
    li   $t1, 4096
    sw   $t1, 0x0C($t0)         /* LEN */
    li   $t1, 0x10000
    sw   $t1, 0x04($t0)         /* PITCH */
    li   $t1, 0xFFFF
    sw   $t1, 0x14($t0)         /* VOL_L */
    sw   $t1, 0x18($t0)         /* VOL_R */
    li   $t1, 0xC2340000        /* AZIMUTH = -45.0f */
    sw   $t1, 0x28($t0)
    li   $t1, 3
    sw   $t1, 0x00($t0)         /* CTRL enable + loop */
    ori  $s0, $s0, 0x80         /* MMIO ok */

    /* ---- store POST code and loop ---- */
    li   $t0, 0xA3D20000
    or   $t0, $t0, $s0
    li   $t1, 0xA000F000
    sw   $t0, 0($t1)

1:  b    1b
    nop

    /* ---- general exception vector (BEV=1 -> 0xBFC00380) ---- */
    .org 0x380
_exception_vector:
    mfc0 $k0, $13               /* Cause */
    andi $k1, $k0, 0x7C         /* EXCode << 2 */
    li   $k0, 0x20              /* EXC_SYSCALL (8) << 2 */
    bne  $k1, $k0, .handler_int
    nop
    /* syscall: skip the instruction and count it */
    mfc0 $k0, $14               /* EPC */
    daddiu $k0, $k0, 4
    mtc0 $k0, $14
    li   $k1, 0xA000F100
    lw   $k0, 0($k1)
    addiu $k0, $k0, 1
    sw   $k0, 0($k1)
    eret
.handler_int:
    /* interrupt: clear IP7, raise the flag, return to the same EPC */
    mfc0 $k0, $13
    andi $k0, $k0, 0x7FFF
    mtc0 $k0, $13
    li   $k1, 0xA000F200
    li   $k0, 1
    sw   $k0, 0($k1)
    eret
