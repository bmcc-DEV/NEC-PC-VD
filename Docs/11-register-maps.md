# Mapas de Registradores Detalhados

## MediaGX — Display Controller (0x40008300)

Baseado em `mediagx.cpp` linhas 227-252.

### DC_GENERAL_CFG (0x04)

```
Bit     Função
31-24   Reserved
23-20   Display enable
19-16   Pipe select
15-8    Reserved
7-0     General config
```

### DC_OUTPUT_CFG (0x0C)

```
Bit     Função
0       0 = 16-bit mode, 1 = 8-bit mode
1       0 = RGB 565, 1 = RGB 555 (16-bit mode)
2       TV enable
3       NTSC/PAL select
7-4     Reserved
```

### DC_H_TIMING_1 (0x30) — Width

```
Bits 10-0: Horizontal display width - 1
Bit 15: Pixel double (0 = normal, 1 = double)
```

### DC_V_TIMING_1 (0x40) — Height

```
Bits 10-0: Vertical display height - 1
```

### DC_LINE_DELTA (0x24)

```
Bits 9-0: Line stride (bytes per line) / 4
```

### DC_FB_ST_OFFSET (0x10)

```
Bits 21-0: Framebuffer start offset in VRAM (em bytes, alinhado 4 KB?)
```

### DC_PAL_ADDRESS (0x70)

```
Bits 7-0: Palette index (0-255)
```

### DC_PAL_DATA (0x74)

```
Bits 5-0: Red (6-bit)
Bits 13-8: Green (6-bit)
Bits 21-16: Blue (6-bit)
→ 18-bit color (6-6-6)
```

---

## Voodoo Rush — Register Table

**Base:** `voodoo.cpp` linhas 3198-3242+.

Endereços relativos ao base do Voodoo no espaço PCI.

### Status Register (0x00) — Leitura

```
Bit     Função
5-0     PCI FIFO free space (em entries / 2)
6       Vertical retrace (0 = in vis area, 1 = vblank)
7       FBI graphics engine busy
8       TREX busy
9       Overall busy
11-10   Displayed buffer (0=front, 1=back, 2=third)
27-12   Memory FIFO free space
30-28   Pending swaps count
31      PCI interrupt pending
```

### fbiInit0 (0x20)

```
Bit     Função
0       Enable hardware init
1       Enable PCI FIFO
2       Swizzle register writes
3       Enable memory FIFO
4       Stall PCI for HWM
5       LFB → memory FIFO
6       Texture → memory FIFO
11-8    PCI FIFO empty entries LWM
15-12   Memory FIFO HWM (×32)
19-16   Memory FIFO write burst HWM
25-24   Graphics reset / FIFO reset
```

### fbiInit1 (0x24)

```
Bit     Função
3-0     Number of 64-pixel tiles in X minus 1
7-4     Number of 32-pixel tiles in X (/2)
8       Video timing reset (enable CLUT writes)
12      Software blank
19-16   DAC type
24      Tile count MSB
```

### fbiInit2 (0x28)

```
Bit     Função
2-0     Reserved
5-3     Number of 64-pixel tiles in Y minus 1
6       Enable triple buffer
10-8    Reserved
19-11   Video buffer offset (in 4 KB pages)
```

### fbzMode (0x32)

```
Bit     Função
1-0     Draw buffer (0=front, 1=back, 2=aux)
3-2     Depth buffer select
5-4     Enable depth test
7-6     Depth function
9-8     Enable alpha test
11-10   Alpha function
13-12   Enable alpha blending
15-14   Blend function
17-16   Fog enable / mode
19-18   Dither enable / mode
21-20   Stipple enable / pattern
23-22   Y origin (0=top, 1=bottom)
24      Enable chroma key
25      Enable alpha planes
```

### lfbMode (0x34)

```
Bit     Função
3-0     Write format (0-15)
5-4     Read buffer select (0-2)
7-6     Write buffer select (0-2)
9-8     RGBA lanes (0=ARGB, 1=ABGR, 2=RGBA, 3=BGRA)
10      Enable pixel pipeline in LFB writes
11      Word swap writes
12      Byte swizzle writes
13      Word swap reads
14      Byte swizzle reads
15      Y origin for LFB
```

### Triangle Setup Registers

**Vertex (16.4 fixed point):**

| Reg | Bits | Descrição |
|---|---|---|
| vertexAx (0x08) | 15-0 | Vértice A X (subpixel) |
| vertexAy (0x0C) | 15-0 | Vértice A Y |
| vertexBx (0x10) | 15-0 | Vértice B X |
| vertexBy (0x14) | 15-0 | Vértice B Y |
| vertexCx (0x18) | 15-0 | Vértice C X |
| vertexCy (0x1C) | 15-0 | Vértice C Y |

**Derivatives (vários bits):**

| Reg | Bits | Descrição |
|---|---|---|
| startR (0x20) | 23-0 | R start value (12.12) |
| dRdX (0x40) | 23-0 | dR/dX |
| dRdY (0x60) | 23-0 | dR/dY |
| startZ (0x2C) | 31-0 | Z start (20.12) |
| startA (0x30) | 23-0 | Alpha start (12.12) |
| startS (0x34) | 31-0 | Texture S start (14.18 → signed) |
| startT (0x38) | 31-0 | Texture T start |
| startW (0x3C) | 31-0 | W start (2.30 → 16.32) |

### Commands

| Reg | Escrita | Ação |
|---|---|---|
| triangleCMD (0x80) | qualquer | Executa triângulo |
| nopCMD (0x84) | bit 0 = reset counters, bit 1 = reset triangle count | NOP |
| fastfillCMD (0x88) | qualquer | Fast fill (clear) |
| swapbufferCMD (0x8C) | bit 0 = sync to vblank, bits 8-1 = swap count | Swap buffers |

---

## Audio AD1847 / Cx5530

### Data Format Register (0x08)

```
Bit     Função
0       0 = 16-bit linear, 1 = 8-bit unsigned
1-3     Sample rate divisor
4       0 = stereo, 1 = mono
5       0 = linear, 1 = companded
6       0 = 16-bit, 1 = 8-bit (override)
7       Reserved
```

### Interface Register (0x09)

```
Bit     Função
0       Playback enable
1       Record enable
2       Auto-calibrate
3       Power down
```

---

## PCI Config (Voodoo Rush)

Via Cx5530 PCI bridge (acesso via I/O 0xCF8/0xCFC).

| Register | Valor | Descrição |
|---|---|---|
| Vendor ID | 0x121A | 3dfx Interactive |
| Device ID | 0x0001 | Voodoo 1 / Rush |
| Revision | 0x02 | — |
| Class Code | 0x000000 | Non-VGA backward compat |
| BAR0 | 16 MB | Memory-mapped registers + FB + tex |

## Referências

- `mediagx.cpp` linhas 227-252: Display ctrl regs
- `voodoo.cpp` linhas 3198-3242+: Voodoo register table
- `voodoo.cpp` linhas 1974-2008: Status register
- `voodoo.cpp` linhas 1221-1239: Memory FIFO config (fbiInit4)
- `voodoo_pci.cpp` linhas 110-131: PCI config
