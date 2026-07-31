# Voodoo Rush Custom (SST-96) — GPU 3D

## Visão Geral

O NEC PC-VD usa uma versão customizada do **3dfx Voodoo Rush (SST-96)**, modificada pela NEC em parceria com a 3dfx. O Voodoo Rush é único na linha 3dfx por integrar **2D + 3D no mesmo chip** (diferente do Voodoo 1 que era ONLY 3D, requiring a separate 2D card).

**Clock:** 60 MHz (vs 50 MHz do Voodoo 1 padrão)
**Memória:** 8 MB EDO (4 MB framebuffer + 4 MB texturas)
**TMUs:** 1 (texture mapping unit)
**Pipeline:** 1 pixel/clock com depth + textura simultâneos

## Diferenças do Voodoo 1 Padrão

| Característica | Voodoo 1 (SST-1) | Voodoo Rush (SST-96) | NEC PC-VD |
|---|---|---|---|
| Clock | 50 MHz | 50-60 MHz | **60 MHz** |
| 2D Core | Não (placa separada) | Alliance AT3D integrado | **AT3D modificado** |
| Memória FB | 2-4 MB EDO | 2-4 MB EDO | **4 MB EDO** |
| Memória TMU | 1-4 MB EDO | 1-2 MB EDO | **4 MB EDO** |
| FIFO PCI | 64 entries | 64 entries | **64 entries** |
| Mem FIFO | até 65536 entries | até 65536 | **Custom** |
| Barramento | PCI 33 MHz | PCI 33 MHz | **PCI 33 MHz + dedicado** |
| Resolução 3D | 640×480 @ 16-bit | 640×480 @ 16/32-bit | **640×480 @ 16/32-bit** |
| Saída de vídeo | VGA only | VGA + TV | **RGB + TV (NTSC/PAL)** |
| Register map | 256 regs (standard) | Diferente do V1 | **Custom NEC** |

## Arquitetura Interna

```
┌──────────────────────────────────────────┐
│         Voodoo Rush (SST-96)              │
├──────────────────────────────────────────┤
│  ┌─────────────┐  ┌────────────────────┐ │
│  │ FBI (Frame  │  │  TMU (Texture      │ │
│  │ Buffer     )│  │  Mapping Unit)     │ │
│  │ - Triangle  │  │  - LOD            │ │
│  │ - Rasterizer│  │  - Bilinear filter│ │
│  │ - Depth     │  │  - Palette/NCC    │ │
│  │ - Alpha     │  │  - 8 texel fmts   │ │
│  │ - Fog       │  └────────┬───────────┘ │
│  │ - Dither    │           │             │
│  └──────┬──────┘           │             │
│         │                  │             │
│  ┌──────┴──────────────────┴───────────┐ │
│  │  Pixel Pipeline                     │ │
│  │  (Texture + Blending + Depth Test)  │ │
│  └─────────────────────────────────────┘ │
│                                         │
│  ┌─────────────────────────────────────┐ │
│  │  FIFO Ctrl (PCI FIFO + Mem FIFO)   │ │
│  └─────────────────────────────────────┘ │
└──────────────────────────────────────────┘
```

## Register Map (Baseado em voodoo.cpp linhas 3198-3242+)

### FBI Registers (0x00-0x7F)

| Reg | Nome | Função |
|---|---|---|
| 0x00 | status | Status + FIFO space + vblank |
| 0x04 | — | Reserved |
| 0x08 | vertexAx | Vértice A X (subpixel 16.4) |
| 0x0C | vertexAy | Vértice A Y |
| 0x10 | vertexBx | Vértice B X |
| 0x14 | vertexBy | Vértice B Y |
| 0x18 | vertexCx | Vértice C X |
| 0x1C | vertexCy | Vértice C Y |
| 0x20 | startR | Iterated R start (12.12) |
| 0x24 | startG | Iterated G start |
| 0x28 | startB | Iterated B start |
| 0x2C | startZ | Iterated Z start (20.12) |
| 0x30 | startA | Iterated Alpha start |
| 0x34 | startS | Texture S start (14.18) |
| 0x38 | startT | Texture T start (14.18) |
| 0x3C | startW | W start (2.30 → 16.32) |
| 0x40-0x5C | d?dX | Derivatives X (R,G,B,Z,A,S,T,W) |
| 0x60-0x7C | d?dY | Derivatives Y |
| 0x80 | triangleCMD | Executa triângulo |

### Floating Point Registers (0x80-0xFF)

A partir de 0x80, versões floating point (IEEE 754) dos mesmos registros:
- `fvertexAx`, `fvertexAy`, etc. (offset 0x08 + 0x80)
- `fstartR`, `fdRdX`, etc. (offset 0x20 + 0x80)

### Config Registers

- **fbiInit0** (0x20): Init, FIFO config, memory FIFO enable
- **fbiInit1** (0x24): Video configuração, tiles X
- **fbiInit2** (0x28): Buffer offset, triple buffer
- **fbiInit3** (0x2C): Y origin, register remap
- **fbiInit4** (0x30): Memory FIFO rows
- **fbzMode** (0x32): Depth/alpha mode, dither, stipple
- **fbzColorPath** (0x33): Color path (subpixel adjust, etc.)
- **lfbMode** (0x34): LFB format, byte/word swapping
- **clipLeft/Right/Top/Bottom** (0x38-0x3B)
- **color0/color1** (0x3C-0x3D): Fog/line colors
- **zaColor** (0x3E): Depth/alpha clear value

### Texture Registers (TMU)

- **textureMode** (0x10): Format, NCC table, 8-downld
- **textureLOD** (0x11): LOD, tdirect, swizzle
- **textureDetail** (0x12): Detail, LOD bias
- **nccTable** (0x14-0x1F): NCC/palette tables
- **palette** (0x20-0x27): 256-entry palette
- **chromaKey** (0x2C): Chroma key color
- **fogTable** (0x30-0x3F): Fog table (32 entries)

## FIFO System

O Voodoo Rush usa dois níveis de FIFO:

```
PCI Write → PCI FIFO (64 entries) → Memory FIFO (em FB RAM)
                                        ↓
                              Execute (triangle, LFB, texture)
```

### PCI FIFO
- 64 entries de 32-bit (pares offset+data)
- Watermarks: LWM (low water mark) para stall
- Tipos: Register, LFB Write, Texture Write

### Memory FIFO
- Armazenado no final da FB RAM
- Tamanho configurável (até 65536 entries)
- HWM (high water mark) para stall

### Stalling
- CPU é stalled via trigger/interrupt quando FIFO enche
- Resume quando FIFO drena abaixo do LWM

## Pipeline de Renderização

1. **Setup**: CPU escreve vertices + derivatives → registers
2. **Triangle CMD**: Write a triangleCMD dispara o rasterizer
3. **Scan Conversion**: Edge walking, span generation
4. **Texture**: TMU fetch + bilinear filter (ou ponto)
5. **Blending**: Alpha, fog, chroma key, dither
6. **Depth Test**: Z-buffer (16 ou 24-bit)
7. **Write**: Pixel para framebuffer

### Clock Cycles
- Setup: ~48 clocks
- Clear: 2 pixels/clock (RGB + depth)
- Render: 1 pixel/clock (textured + depth)

## Resolução e Video Timing

A resolução 3D do PC-VD é **640×480 @ 60 Hz** (NTSC) ou **720×576 @ 50 Hz** (PAL):
- hSyncOn/hSyncOff: configurável via hSync registers
- vSyncOn/vSyncOff: configurável via vSync registers
- O Voodoo Rush auto-detects standard/medium/VGA sync rates

## Referência MAME

- `voodoo.cpp` linhas 1-106: Header com specs e TODO
- `voodoo.cpp` linhas 138-164: float_to_int32/int64 (conversão para fixed-point)
- `voodoo.cpp` linhas 228-248: s_alias_map (remap de registros)
- `voodoo.cpp` linhas 259-307: shared_tables (texel formats RGB332, RGB565, etc.)
- `voodoo.cpp` linhas 314-461: tmu_state (texture upload, palette/NCC)
- `voodoo.cpp` linhas 468-545: memory_fifo (add/remove/configure)
- `voodoo.cpp` linhas 665-683: generic_voodoo_device
- `voodoo.cpp` linhas 703-744: voodoo_1_device (construtor, membros)
- `voodoo.cpp` linhas 760-780: core_map (memory map do Voodoo 1)
- `voodoo.cpp` linhas 900-1030: device_start (alocação, init)
- `voodoo.cpp` linhas 1247-1296: add_to_fifo (PCI→FIFO)
- `voodoo.cpp` linhas 1304-1344: flush_fifos (execução)
- `voodoo.cpp` linhas 1352-1400: execute_fifos (dispatch)
- `voodoo.cpp` linhas 2888-3029: triangle() (rasterização)
- `voodoo.cpp` linhas 3198-3242+: Register table
