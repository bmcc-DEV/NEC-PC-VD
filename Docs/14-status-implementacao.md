# Status da Implementação — 29 Jul 2026

## Teste de funcionamento

```
$ ./build/nec-pc-vd roms/pcvd_bios.bin
NEC PC-VD Emulator v0.1.0
ROM: roms/pcvd_bios.bin
Loaded ROM: roms/pcvd_bios.bin (262144 bytes) at 0xFFFC0000-0xFFFFFFFF
BIOS shadowed to 0xC0000 (262144 bytes)
Reset vector byte at 0xFFFFFFF0: 0xEA
Code byte at 0xFC000: 0x8C
```

O emulador boota, executa o test ROM e renderiza o framebuffer em loop até 60 FPS.

## O que está funcionando ✅

### Núcleo x86 (1031 linhas)
- Boot completo: reset vector (0xFFFFFFF0) → real-mode → LGDT → MOV CR0 → far jump → PM 32-bit
- Real mode 16-bit: mov, add, sub, and, or, xor, cmp, shifts, push/pop, etc.
- Protected mode 32-bit com parsing correto de descritores GDT
- I/O ports, interrupts, HLT
- ~17 bugs corrigidos (fetch sem CS base, operand size, double modrm_addr, etc.)

### Sistema
- MemoryBus: regiões + handlers de escrita
- PCI Bus
- Display Controller MediaGX: configurável, framebuffer 2D em UMA (0x40800000)
- Composição 2D + 3D

### Voodoo Rush 3D (novo, 5 arquivos, ~620 linhas)
- `voodoo_defs.h`: Register map (256+ regs), bitfields, FIFO flags
- `voodoo_fifo.cpp/h`: PCI FIFO (64 entries) + Memory FIFO
- `tmu.cpp/h`: Texture Mapping Unit com 8 formatos de texel
- `voodoo_rush.cpp/h`: Device principal — reset, alloc, regs, LFB, FIFO, display

## Bugs corrigidos (resumo)

| Bug | Sintoma | Correção |
|---|---|---|
| `modrm_addr` relia ModRM | LGDT, ADD com endereço errado | `modrm_addr_ea()` para callers com ModRM já parseado |
| `C7 MOV rm,imm` ordem invertida | Display writes iam para addr 0 | Address calculado ANTES do immediate |
| `MOV Sreg` em PM | DS base = 0x100 (seg<<4) em vez de 0 | Ler descritor da GDT |
| `0x8C` invertia source/dest | CS podia ser alterado | Reg = source segment |
| `op_alu_imm` AL vs AX/EAX | `or al,1` escrevia em EAX | Separar AL (8-bit) de AX/EAX |
| fetch/modrm sem CS base | Instruções lidas de addr errado | `m_seg_base[1] + m_eip` |

## Arquitetura atual

```
src/
├── main.cpp              (entry point)
├── emulator.cpp/.h       (core: init, run, step_frame)
├── cpu/i386.cpp/.h       (x86 interpreter, ~1000 lines)
├── mediagx/
│   ├── mediagx.cpp/.h    (SoC wrapper)
│   ├── display_ctrl.cpp/.h (2D)
│   └── mem_ctrl.cpp/.h
├── bus/
│   ├── memory_bus.cpp/.h (regions + handlers)
│   └── pci_bus.cpp/.h
├── voodoo/
│   ├── voodoo_defs.h     (register map)
│   ├── voodoo_fifo.cpp/.h (FIFO system)
│   ├── tmu.cpp/.h         (Texture Mapping Unit)
│   └── voodoo_rush.cpp/.h (main 3D device)
│   └── renderer/          (planned)
├── frontend/
│   ├── sdl_window.cpp/.h (SDL2)
│   └── opengl_output.cpp/.h (placeholder)
└── devices/               (CD-ROM, memcard, controls — planned)
```

## Próximo passo recomendado

**Triangle rasterizer** — implementar o scan converter e pixel pipeline do Voodoo
Rush. Baseado no `voodoo.cpp` do MAME (~1200 linhas de rasterização com
texture mapping, alpha blending, depth testing, fog, dither, stipple).
