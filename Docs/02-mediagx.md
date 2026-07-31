# MediaGXm + Cx5530 — CPU e SoC

## Visão Geral

O MediaGXm é um processador x86 da Cyrix (National Semiconductor) que integra CPU, cache L1 e controladores de memória/display num único chip. É acompanhado pelo **Cx5530** que fornece PCI bridge, áudio e I/O.

**Clock:** 233 MHz
**Core:** 486/Pentium-class com pipeline de 6 estágios
**Cache:** 16 KB L1 unificado (dados + instruções)
**MMX:** Sim (MediaGXm tem suporte MMX)

## Arquitetura Interna

```
┌─────────────────────────────────────────────┐
│             MediaGXm (SoC)                   │
├─────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────────────┐ │
│  │ x86 Core     │  │ Display Controller   │ │
│  │ 233 MHz      │  │ - Framebuffer UMA    │ │
│  │ 16 KB L1     │  │ - CRTC / Timings     │ │
│  │ MMX          │  │ - Cursor HW          │ │
│  └──────┬───────┘  │ - Palette RAMDAC     │ │
│         │          └──────────────────────┘ │
│  ┌──────┴───────┐  ┌──────────────────────┐ │
│  │ Memory Ctrl  │  │ BIU (Bus Interface)  │ │
│  │ - SDRAM      │  │ - PCI master/target  │ │
│  │ - UMA config │  │ - Memória mapeada    │ │
│  │ - DCT        │  │ - GPIO               │ │
│  └──────────────┘  └──────────────────────┘ │
└─────────────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────────────┐
│           Cx5530 Companion                   │
├─────────────────────────────────────────────┤
│  PCI Bridge │ Audio (AD1847-like) │ I/O      │
└─────────────────────────────────────────────┘
```

## Display Controller — Registros

Baseado em `mediagx.cpp` (linhas 227-252):

| Offset | Nome | Descrição |
|---|---|---|
| 0x00 | DC_UNLOCK | Unlock register |
| 0x04 | DC_GENERAL_CFG | Configuração geral |
| 0x08 | DC_TIMING_CFG | Timing (pixel double, vblank status) |
| 0x0c | DC_OUTPUT_CFG | Modo de saída (8/16-bit, RGB 565/555) |
| 0x10 | DC_FB_ST_OFFSET | Framebuffer start offset |
| 0x14 | DC_CB_ST_OFFSET | Cursor buffer start |
| 0x18 | DC_CUR_ST_OFFSET | Cursor start |
| 0x20 | DC_VID_ST_OFFSET | Video overlay start |
| 0x24 | DC_LINE_DELTA | Line stride (bytes por linha) |
| 0x28 | DC_BUF_SIZE | Buffer size |
| 0x30 | DC_H_TIMING_1 | Horizontal timing 1 (width) |
| 0x34 | DC_H_TIMING_2 | Horizontal timing 2 |
| 0x38 | DC_H_TIMING_3 | Horizontal timing 3 |
| 0x3c | DC_FP_H_TIMING | Front porch horizontal |
| 0x40 | DC_V_TIMING_1 | Vertical timing 1 (height) |
| 0x44 | DC_V_TIMING_2 | Vertical timing 2 |
| 0x48 | DC_V_TIMING_3 | Vertical timing 3 |
| 0x4c | DC_FP_V_TIMING | Front porch vertical |
| 0x50 | DC_CURSOR_X | Cursor X position |
| 0x54 | DC_V_LINE_CNT | Vertical line count |
| 0x58 | DC_CURSOR_Y | Cursor Y position |
| 0x5c | DC_SS_LINE_CMP | Screen start line compare |
| 0x70 | DC_PAL_ADDRESS | Palette address |
| 0x74 | DC_PAL_DATA | Palette data |
| 0x78 | DC_DFIFO_DIAG | Display FIFO diag |
| 0x7c | DC_CFIFO_DIAG | Cursor FIFO diag |

Esses registros estão mapeados em **0x40008300** no espaço de memória do MediaGX.

## Memory Controller — Registros

Mapeado em **0x40008400**:

- **Offset 0x20**: Palette access (index/data via RAMDAC)
- Demais offsets: controle de DRAM, timings, UMA config

### UMA (Unified Memory Architecture)

O framebuffer 2D fica na memória principal (SDRAM) em vez de VRAM dedicada:
- Endereço base do framebuffer: definido por DC_FB_ST_OFFSET
- Stride: definido por DC_LINE_DELTA
- O display controller lê diretamente da SDRAM via barramento interno de alta largura de banda

## BIU (Bus Interface Unit)

Registros em **0x40008000**:
- Configuração de barramento PCI
- Mapeamento de memória (ROM shadow, RAM)
- GPIO

## Mapa de Memória (do mediagx.cpp)

| Range | Descrição |
|---|---|
| 0x00000000 — 0x0009FFFF | Main RAM (DOS area) |
| 0x000A0000 — 0x000AFFFF | VGA framebuffer |
| 0x000B0000 — 0x000B7FFF | CGA/Texto |
| 0x000C0000 — 0x000FFFFF | BIOS shadow |
| 0x00100000 — 0x00FFFFFF | Main RAM (extended) |
| 0x40008000 — 0x400080FF | BIU registers |
| 0x40008300 — 0x400083FF | Display controller |
| 0x40008400 — 0x400084FF | Memory controller |
| 0x40800000 — 0x40BFFFFF | VRAM (framebuffer 2D) |
| 0xFFFC0000 — 0xFFFFFFFF | System BIOS |

## I/O Map

| Porta | Dispositivo |
|---|---|
| 0x22/0x23 | Cyrix config registers |
| 0xE8-0xEB | I/O delay |
| 0x378-0x37B | Parallel port (controles) |
| 0x400-0x4FF | Audio AD1847 |

## Config Registers (Cyrix)

Acessados via I/O 0x22 (select) / 0x23 (data):
- Configuram CPU features (cache, MMX, etc.)
- Configuram o controlador de memória
- Configuram o display controller

## Referência MAME

- `mediagx.cpp` linhas 86-224: Classe `mediagx_state`
- `mediagx.cpp` linhas 227-252: Display controller registers
- `mediagx.cpp` linhas 747-772: Address maps
- `mediagx.cpp` linhas 761-772: I/O maps
- `mediagx.cpp` linhas 878-920: Machine config (clock, devices)
