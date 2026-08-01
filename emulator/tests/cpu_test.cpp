#include "cpu/i386.h"
#include "bus/memory_bus.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

static int g_fail = 0;

static void check(const char* name, uint32_t got, uint32_t want) {
    if (got == want) {
        printf("PASS  %s = 0x%08X\n", name, got);
    } else {
        printf("FAIL  %s = 0x%08X (want 0x%08X)\n", name, got, want);
        g_fail++;
    }
}

int main() {
    MemoryBus mem;
    I386Core cpu(mem);

    // 1MB RAM
    static uint8_t ram[0x10000];
    memset(ram, 0, sizeof(ram));
    mem.add_region(0x00000000, 0x0000FFFF, ram, false);

    // Reset vector region
    static uint8_t rv[16];
    memset(rv, 0x90, sizeof(rv));
    // jmp far 0x0000:0x0000  (EA offset16 seg16)
    rv[0] = 0xEA; rv[1] = 0x00; rv[2] = 0x00; rv[3] = 0x00; rv[4] = 0x00;
    mem.add_region(0xFFFFFFF0, 0xFFFFFFFF, rv, true);

    // Test program
    uint8_t prog[] = {
        // ---- setup segments + stack (CS=0 already via far jump) ----
        0xB8, 0x00, 0x00,          // mov ax, 0
        0x8E, 0xD8,                // mov ds, ax
        0x8E, 0xC0,                // mov es, ax
        0x8E, 0xD0,                // mov ss, ax
        0xBC, 0x00, 0x30,          // mov sp, 0x3000

        // ---- Test 1: AH/CH/DH/BH must not touch ESP/EBP/ESI/EDI ----
        0xB4, 0x12,                // mov ah, 0x12
        0xB5, 0x34,                // mov ch, 0x34
        0xB6, 0x56,                // mov dh, 0x56
        0xB7, 0x78,                // mov bh, 0x78
        0x66, 0x60,                // pushad  (EAX @ [SP+28] = 0x2FFC)

        // ---- Test 2: POP r/m (0x8F) ----
        0x66, 0x68, 0x78, 0x56, 0x34, 0x12,   // push 0x12345678
        0x66, 0x8F, 0x06, 0x00, 0x20,         // pop dword [0x2000]

        // ---- Test 3: INC/DEC reg via 0xFF /0, /1 ----
        0x66, 0xB8, 0x00, 0x01, 0x00, 0x00,  // mov eax, 0x100
        0x66, 0xFF, 0xC0,                    // inc eax
        0x66, 0xFF, 0xC8,                    // dec eax
        0x66, 0x50,                          // push eax -> [0x2FDC]

        // ---- Test 4: 0x80 byte ALU on memory (ADD + ADC) ----
        0xC6, 0x06, 0x10, 0x20, 0x0A,        // mov byte [0x2010], 0x0A
        0xC6, 0x06, 0x11, 0x20, 0x77,        // mov byte [0x2011], 0x77 (sentinel)
        0x80, 0x06, 0x10, 0x20, 0x05,        // add byte [0x2010], 5   -> 0x0F
        0xF9,                                // stc
        0x80, 0x16, 0x10, 0x20, 0x05,        // adc byte [0x2010], 5   -> 0x0F+5+1=0x15

        // ---- Test 5: REP MOVSB with ECX=0 must be a no-op ----
        0xC6, 0x06, 0x00, 0x10, 0x99,        // mov byte [0x1000], 0x99 (source)
        0xC6, 0x06, 0x20, 0x20, 0x33,        // mov byte [0x2020], 0x33 (dest sentinel)
        0x66, 0xB9, 0x00, 0x00, 0x00, 0x00,  // mov ecx, 0
        0xBE, 0x00, 0x10,                    // mov si, 0x1000
        0xBF, 0x20, 0x20,                    // mov di, 0x2020
        0xF3, 0xA4,                          // rep movsb  (no-op)

        // ---- Test 6: REP STOSB ECX=3 ----
        0x66, 0xB9, 0x03, 0x00, 0x00, 0x00,  // mov ecx, 3
        0xB0, 0xAA,                          // mov al, 0xAA
        0xBF, 0x30, 0x20,                    // mov di, 0x2030
        0xF3, 0xAA,                          // rep stosb

        // ---- Test 8: SETcc register + memory (0x0F 0x94) ----
        0x38, 0xC0,                          // cmp al, al   (ZF=1)
        0x0F, 0x94, 0x06, 0x40, 0x20,        // sete byte [0x2040]
        0x0F, 0x94, 0xC0,                    // sete al

        // ---- Test 9: MOVZX 16-bit preserves upper bits ----
        0x66, 0xB8, 0x00, 0x00, 0xFF, 0xFF,  // mov eax, 0xFFFF0000
        0xB3, 0x55,                          // mov bl, 0x55
        0x0F, 0xB6, 0xC3,                    // movzx ax, bl  -> EAX=0xFFFF0055
        0x66, 0x50,                          // push eax -> [0x2FD8]

        // ---- done ----
        0xC6, 0x06, 0x00, 0x22, 0x01,        // mov byte [0x2200], 1
        0xF4                                 // hlt
    };

    memcpy(ram, prog, sizeof(prog));

    cpu.reset();
    int cycles = cpu.execute(500000);

    printf("--- CPU regression test (cycles=%d) ---\n", cycles);

    check("AH (EAX @0x2FFC)", *(uint32_t*)(ram + 0x2FFC), 0x00001200);
    check("CH (ECX @0x2FF8)", *(uint32_t*)(ram + 0x2FF8), 0x00003400);
    check("DH (EDX @0x2FF4)", *(uint32_t*)(ram + 0x2FF4), 0x00005600);
    check("BH (EBX @0x2FF0)", *(uint32_t*)(ram + 0x2FF0), 0x00007800);
    check("POP rm [0x2000]", *(uint32_t*)(ram + 0x2000), 0x12345678);
    check("INC/DEC eax", *(uint32_t*)(ram + 0x2FDC), 0x00000100);
    check("0x80 ADD+ADC byte [0x2010]", *(uint8_t*)(ram + 0x2010), 0x15);
    check("0x80 8-bit write sentinel [0x2011]", *(uint8_t*)(ram + 0x2011), 0x77);
    check("REP MOVSB ECX=0 no-op [0x2020]", *(uint8_t*)(ram + 0x2020), 0x33);
    check("REP STOSB [0x2030]", *(uint8_t*)(ram + 0x2030), 0xAA);
    check("REP STOSB [0x2031]", *(uint8_t*)(ram + 0x2031), 0xAA);
    check("REP STOSB [0x2032]", *(uint8_t*)(ram + 0x2032), 0xAA);
    check("SETcc mem [0x2040]", *(uint8_t*)(ram + 0x2040), 1);
    check("MOVZX 16-bit upper preserved", *(uint32_t*)(ram + 0x2FD8), 0xFFFF0055);
    check("completion marker", *(uint8_t*)(ram + 0x2200), 1);

    printf(g_fail ? "\n%d FAILURES\n" : "\nALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
