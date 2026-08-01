/*
 * main.c - NEC PC-Viper emulator entry point (fase 1: CPU + bus).
 *
 * Carrega a ROM de boot (ou um programa embutido), executa o núcleo
 * VR5432 a partir do vetor de reset 0xBFC00000 e imprime o estado final.
 */
#include "bus.h"
#include "vr5432.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Programa de boot embutido (hand-assembled MIPS IV, little-endian):
 *   lui  $8, 0xDEAD; ori $8, $8, 0xBEEF; lui $9, 0x8000
 *   sd   $8, 0($9);  beq $0, $0, -1; nop
 * Escreve o valor mágico 0xDEADBEEF na SDRAM (físico 0) e faz loop.
 */
static const uint32_t s_builtin_boot[] = {
    0x3C08DEADu,  /* lui  $8, 0xDEAD */
    0x3508BEEFu,  /* ori  $8, $8, 0xBEEF */
    0x3C098000u,  /* lui  $9, 0x8000 */
    0xF5280000u,  /* sd   $8, 0($9) */
    0x1000FFFFu,  /* beq  $0, $0, -1 (loop) */
    0x00000000u,  /* nop (delay slot) */
};

int main(int argc, char** argv) {
    Bus* bus = bus_create();
    if (!bus) {
        fprintf(stderr, "pcviper: failed to allocate bus\n");
        return 1;
    }

    if (argc > 1) {
        if (bus_load_file(bus, argv[1], VIPER_ROM_BASE, VIPER_ROM_SIZE) != 0)
            fprintf(stderr, "pcviper: using built-in boot program\n");
    }

    if (!(bus->rom[0] | bus->rom[1] | bus->rom[2] | bus->rom[3])) {
        memcpy(bus->rom, s_builtin_boot, sizeof(s_builtin_boot));
        printf("pcviper: loaded built-in boot program at 0xBFC00000\n");
    }

    Vr5432 cpu;
    vr5432_reset(&cpu, bus);

    printf("pcviper: running VR5432 (reset vector 0x%08llX)\n",
           (unsigned long long)cpu.pc);

    uint64_t run_cycles = (argc > 2) ? strtoull(argv[2], NULL, 0) : 200;
    vr5432_run(&cpu, run_cycles);

    uint64_t magic = bus_read64(bus, 0x80000000ull);
    printf("pcviper: executed %llu cycles\n", (unsigned long long)cpu.cycles);
    printf("pcviper: PC = 0x%08llX  HI = 0x%016llX  LO = 0x%016llX\n",
           (unsigned long long)cpu.pc, (unsigned long long)cpu.hi,
           (unsigned long long)cpu.lo);
    printf("pcviper: ram[0x80000000] = 0x%016llX\n",
           (unsigned long long)magic);
    printf("pcviper: %s\n",
           (magic == 0x00000000DEADBEEFull) ? "boot OK (magic found)"
                                            : "boot: magic not found");

    bus_destroy(bus);
    return (magic == 0x00000000DEADBEEFull) ? 0 : 1;
}
