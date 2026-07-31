# Mapa de Memória — NEC PC-VD

## Memória do Sistema (SDRAM — 32/64 MB)

Mapeamento baseado no MediaGX (referência `mediagx.cpp` linhas 747-759):

```
0x00000000 ┌──────────────────────┐
           │  Main RAM            │ 640 KB (DOS compat)
           │  (0x00000-0x9FFFF)   │
0x000A0000 ├──────────────────────┤
           │  VGA Framebuffer     │ 64 KB
0x000B0000 ├──────────────────────┤
           │  CGA/Text Buffer     │ 32 KB
0x000C0000 ├──────────────────────┤
           │  BIOS Shadow / ROM   │ 256 KB
0x00100000 ├──────────────────────┤
           │  Main RAM (extended)  │ ~31 MB (ou ~63 MB)
           │                       │
0x00FFFFFF └──────────────────────┘

0x40008000 ┌──────────────────────┐
           │  BIU Registers       │ 256 bytes (0xFF)
0x40008300 ├──────────────────────┤
           │  Display Controller  │ 256 bytes
0x40008400 ├──────────────────────┤
           │  Memory Controller   │ 256 bytes
0x40800000 ├──────────────────────┤
           │  2D Framebuffer      │ 4 MB (UMA)
           │  (VRAM UMA)          │
0x40BFFFFF └──────────────────────┘

0xFFFC0000 ┌──────────────────────┐
           │  System BIOS / ROM   │ 256 KB
0xFFFFFFFF └──────────────────────┘
```

## VRAM 3D (EDO — 8 MB dedicada)

Não mapeada no espaço da CPU diretamente! Acessada via:
- **Registers** do Voodoo Rush (mapeados no space PCI)
- **LFB** (Linear Frame Buffer) acesso direto
- **Texture** upload via portas dedicadas

```
VRAM 3D (8 MB EDO):
  ┌─────────────────────┐
  │  Framebuffer 3D     │ 4 MB
  │  - Color buffer 0   │ (front)
  │  - Color buffer 1   │ (back)
  │  - Depth/Alpha buf  │ (Z-buffer)
  ├─────────────────────┤
  │  Texture RAM        │ 4 MB
  │  - TMU textures     │
  │  - Mipmaps          │
  │  - Palette/NCC      │
  └─────────────────────┘
```

Layout configurável via `fbiInit1` e `fbiInit2`:
- **Triple buffer:** 3 color + 0 depth, ou 3 color + 1 depth
- **Double buffer:** 2 color + 1 depth (padrão)
- **Tile size:** 64×16 pixels por tile

## Mapeamento PCI (Voodoo Rush)

O Voodoo Rush é mapeado no espaço PCI do MediaGX (32 MB):

```
0x000000 ┌──────────────────────┐
         │  Registers           │ 4 MB
         │  (00ab----ccrrrrrr--)│
         │  a=alt reg map       │
         │  b=byte swizzle      │
         │  c=chip mask         │
         │  r=register index    │
0x3FFFFF ├──────────────────────┤
0x400000 │  LFB (Linear FB)    │ 4 MB
         │  (16-bit ou 32-bit)  │
0x7FFFFF ├──────────────────────┤
0x800000 │  Texture Memory      │ 4 MB
         │  (LOD + Y + X)       │
0xFFFFFF └──────────────────────┘
```

## I/O Ports

```
0x0022-0x0023   Cyrix Config Registers
0x00E8-0x00EB   I/O Delay (nop)
0x01F0-0x01F7   CD-ROM Data/Command
0x0220-0x022F   Audio (alternativo?)
0x0378-0x037B   Parallel Port (Controles)
0x03BC-0x03BF   LPT
0x03F0-0x03F7   CD-ROM Aux
0x0400-0x04FF   Audio AD1847 / Cx5530 DSP
0x0CF8-0x0CFF   PCI Config

--- Estimados (não confirmados) ---
0x??            Memory Card slot 1
0x??            Memory Card slot 2
0x??            TV System (NTSC/PAL)
0x??            System Control
```

## BIOS / Boot ROM

- **Tamanho:** 256 KB
- **Localização:** 0xFFFC0000 — 0xFFFFFFFF
- **Tipo:** ROM (copiada para BIOS shadow em 0x000C0000 no reset)
- **Conteúdo:** Runtime freestanding + bibliotecas SDk

### Boot Sequence

1. CPU começa em 0xFFFFFFF0 (reset vector → ROM)
2. BIOS copia runtime para RAM (shadow)
3. Inicializa hardware (MediaGX, Voodoo, áudio)
4. Lê CD-ROM (setor de boot proprietário)
5. Carrega game binary para RAM
6. Jump para `game_main()` (endereço fixo ou via header)

## Referência MAME

- `mediagx.cpp` linhas 747-759: `mediagx_map()` — address map completo
- `mediagx.cpp` linhas 761-772: `mediagx_io()` — I/O map
- `voodoo.cpp` linhas 760-780: `core_map()` — Voodoo 1 memory map
- `voodoo.cpp` linhas 2800-2881: `recompute_video_memory()` — VRAM layout
