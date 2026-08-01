# NEC PC-Viper Emulator

Cycle-accurate software emulator of the **NEC PC-Viper** arcade system (VR5432 MIPS IV CPU + Voodoo2 EC GPU + Aureal A3D 2.0 audio).

## System Specifications

| Component | Specification |
|-----------|---------------|
| **CPU** | NEC VR5432 MIPS IV 64-bit RISC (125 MHz) |
| **FPU** | COP1 IEEE-754 (S/D), COP1X fused MADD/MSUB |
| **RAM** | 64 MB SDRAM PC133 (128-bit bus, physical 0x00000000) |
| **GPU** | 3dfx Voodoo2 EC (100 MHz, 16 MB unified SGRAM) |
| **Audio** | Aureal A3D 2.0 DSP (64 ch, 48 kHz, binaural HRTF) |
| **Peripherals** | DVD-ROM DMA, Ethernet, 2x Memory Card slots |
| **Boot ROM** | 256 KB at 0x1FC00000 (reset vector 0xBFC00000) |

## Memory Map (Physical)

```
0x00000000 - 0x03FFFFFF : 64 MB SDRAM
0x10000000 - 0x10FFFFFF : 16 MB Voodoo2 EC (regs + SGRAM + CMDFIFO)
0x14000000 - 0x14000FFF : 4 KB  Aureal A3D
0x1E000000 - 0x1E000FFF : 4 KB  Viper SoC (DVD, Eth, MemCards)
0x1F000000 - 0x1F3FFFFF : 4 MB  Flash
0x1FC00000 - 0x1FC3FFFF : 256 KB Boot ROM
```

MIPS virtual segments:
- KUSEG 0x00000000-0x7FFFFFFF -> phys 0x00000000-0x7FFFFFFF
- KSEG0 0x80000000-0x9FFFFFFF -> phys 0x00000000-0x1FFFFFFF (cached)
- KSEG1 0xA0000000-0xBFFFFFFF -> phys 0x00000000-0x1FFFFFFF (uncached)

## Quick Start

```bash
# Host build (x86_64 Linux, requires gcc, make, pkg-config, libsdl2-dev)
cd pcviper
make all

# Cross-compile firmware/demos (requires mips64-elf-gcc toolchain)
# Available at /opt/libdragon/bin/ or install via libdragon
make firmware/demo3d.bin

# Run static demo (produces cpu3d.ppm)
./pcviper_emulator firmware/demo3d.bin

# Run interactive SDL2 demo (keyboard/gamepad control)
./pcviper_emulator firmware/demo3d.bin --sdl

# Run all tests
make tests

# Full validation suite
./validate.sh
```

### Controls (SDL2 interactive mode)

| Key / Pad | Action |
|-----------|--------|
| Left / Right | Yaw rotation |
| Up / Down | Pitch rotation |
| W / A / S / D | Screen translation (pan) |
| Z / X (or +/-) | Zoom in / out |
| Q / E | Sound azimuth CCW / CW |
| ESC | Quit |
| Left stick | Yaw / Pitch |
| Triggers | Zoom |
| D-Pad | Pan |
| LB / RB | Azimuth |

### Headless Input Override

For CI/automation without SDL window:
```bash
PCVIPER_INPUT_YAW=90 PCVIPER_INPUT_PITCH=45 PCVIPER_INPUT_CAMZ=1.5 \
PCVIPER_INPUT_TX=100 PCVIPER_INPUT_TY=-80 \
./pcviper_emulator firmware/demo3d.bin
```

Variables: `YAW`, `PITCH` (degrees), `CAMZ` (float), `TX`, `TY` (pixels), `AZIMUTH` (degrees).

Auto-quit after N frames (headless):
```bash
PCVIPER_SDL_FRAMES=4 SDL_VIDEODRIVER=dummy ./pcviper_emulator firmware/demo3d.bin --sdl
```

## Demos Included

| Demo | Description | Output |
|------|-------------|--------|
| `demo3d` | Quake-style rotating textured cube (VR5432 FPU geometry + Voodoo2 EC rasterization) | `cpu3d.ppm` |
| `voodoo_demo` | Voodoo2 EC multitextured triangle via MMIO | `voodoo.ppm` |
| `glide_demo` | Glide API textured triangle | `glide.ppm` |
| `voodoo_advanced_demo` | Gouraud shading, distance fog, alpha blend, multitexture, mipmap/trilinear | `advanced.ppm` |
| `a3d_demo` | A-major chord with 3D positional audio (3 channels) | `aureal.wav` |
| `a3d_demo3d_audio` | Firmware-generated sine wave at +/-45 deg azimuth verification | console |

## Validation

`./validate.sh` runs:
1. Clean build (no warnings)
2. All unit test suites (88 tests): CPU, Voodoo2 EC, Aureal A3D, Viper SoC, Glide, Pipeline, Voodoo Advanced
3. POST firmware self-test (MIPS IV assembly) -> `0xA3D201FF` READY
4. Demo3d interactive input path (zoom override + SDL2 headless)
5. Full emulator run with all subsystems
6. Artifact checks (PPM size/header, WAV RIFF)

Expected: **88 PASS / 0 FAIL**

## Project Structure

```
pcviper/
├── include/           # Public headers
│   ├── vr5432.h       # VR5432 CPU state & API
│   ├── voodoo2_ec.h   # Voodoo2 EC registers & API
│   ├── aureal_a3d.h   # Aureal A3D registers & API
│   ├── bus.h          # Memory bus & MMIO
│   ├── viper_system.h # Viper SoC peripherals
│   └── glide.h        # Glide API subset
├── src/               # Emulator core
│   ├── vr5432.c       # MIPS IV interpreter (COP0/COP1/COP1X)
│   ├── voodoo2_ec.c   # Voodoo2 EC (SGRAM, setup engine, CMDFIFO, rasterizer)
│   ├── aureal_a3d.c   # A3D 2.0 (HRTF, reverb, chorus, biquad)
│   ├── bus.c          # Physical memory map & MMIO bridge
│   ├── viper_system.c # DVD DMA, Ethernet, Memory Cards
│   ├── glide_api.c    # Glide 2.x subset implementation
│   └── main.c         # Entry point, SDL2 window, demo orchestration
├── firmware/
│   ├── demo3d.c       # MIPS IV freestanding C (cube + A3D audio)
│   ├── init.S         # Boot stub (_start -> main_c)
│   └── link.ld        # Link script (load at 0x1FC00000)
├── tests/             # Unit tests (run via make tests)
└── validate.sh        # End-to-end validation script
```

## Key Implementation Notes

### VR5432 COP1X MADD/MSUB Fix
The MIPS IV COP1X fused multiply-add instructions (`MADD.S/D`, `MSUB.S/D`, `NMADD.S/D`, `NMSUB.S/D`) use funct codes `0x20/0x28/0x2C/0x30` (not `0x00`) with register fields:
```
fr = bits[25:21], ft = bits[20:16], fs = bits[15:11], fd = bits[10:6]
```
Previously misdecoded (shifted fields + wrong funct) causing silent NOPs that broke demo3d geometry.

### Voodoo2 EC Rasterizer
- 16 MB unified SGRAM for framebuffers + textures
- Hardware triangle setup engine (SARGB, SVX/Y, SWB, S/W/T for TMU0/1)
- Per-pixel perspective-correct texture mapping (s/w, t/w)
- **Advanced features** (added in `voodoo2ec_rasterize`):
  - Gouraud shading (per-vertex RGB interpolation)
  - Distance fog via 256-entry `fogTable` or linear mode (`fogDepth = 1-w`)
  - Alpha blending (`alphaMode`): src-over, sources = const/vertex/tex0/tex1/one
  - Mipmapping with trilinear LOD (derivative-based, bilinear + LOD blend)

### A3D 2.0 Positional Audio
- 64 hardware channels, 48 kHz, 16-bit PCM
- Simplified binaural HRTF: ITD (Woodworth) + ILD (equal-power pan) + elevation attenuation
- Doppler pitch (16.16 fixed), per-channel biquad, global Schroeder reverb + LFO chorus
- Stereo mix output (SDL2 audio queue or WAV file)

### Host<->Firmware Input Block
32-byte structure at physical 0x00000800 (KSEG0 0x80000800):
```c
struct ViperInput {
    float cy, sy;      // cos/sin of yaw
    float cx, sx;      // cos/sin of pitch
    float camz;        // camera distance (zoom)
    float tx, ty;      // screen translation (pixels)
    float azimuth;     // sound source azimuth (-180..180 deg)
    uint32_t magic;    // 0xA3D2EC01 when valid
};
```
Host writes cos/sin + camz/tx/ty/azimuth each frame; firmware does full FPU vertex transform.

## Cross-Toolchain

Firmware requires MIPS IV toolchain:
```bash
# Via libdragon (used by validate.sh)
export PATH=/opt/libdragon/bin:$PATH
mips64-elf-gcc -EL -mips4 -mhard-float -ffreestanding \
    -nostdlib -mno-abicalls -G0 -O2 -T firmware/link.ld \
    -o firmware/demo3d.elf firmware/init.S firmware/demo3d.c
mips64-elf-objcopy -O binary firmware/demo3d.elf firmware/demo3d.bin
```

## License

MIT
