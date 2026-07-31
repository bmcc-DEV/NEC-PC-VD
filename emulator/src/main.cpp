#include "emulator.h"
#include <cstdio>

int main(int argc, char* argv[]) {
    const char* rom_path = nullptr;

    if (argc > 1) {
        rom_path = argv[1];
    } else {
        // Default BIOS path
        rom_path = "roms/pcvd_bios.bin";
    }

    printf("NEC PC-VD Emulator v0.1.0\n");
    printf("ROM: %s\n\n", rom_path ? rom_path : "(none)");
    fflush(stdout);

    Emulator emu;
    if (!emu.init(rom_path)) {
        fprintf(stderr, "Failed to initialize emulator.\n");
        fprintf(stderr, "Try: SDL_VIDEODRIVER=dummy %s\n", argv[0]);
        return 1;
    }

    emu.run();
    emu.shutdown();
    return 0;
}
