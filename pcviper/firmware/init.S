    .section .text.start
    .globl _start
_start:
    li $sp, 0x801FF000      # RAM KSEG0, topo da stack
    la $t0, main_c
    jr $t0
    nop
