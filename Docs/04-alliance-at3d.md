# Alliance AT3D Modificado — GPU 2D

## Visão Geral

O Alliance AT3D é um chip gráfico 2D da Alliance Semiconductor, modificado pela NEC e integrado no **mesmo package** do Voodoo Rush. Ele fornece:

- Aceleração 2D (BITBLT, fills, line draw)
- Video overlay (para FMV)
- Saída de vídeo composta (RGB + TV)

**Framebuffer:** Em SDRAM do sistema via UMA (não na EDO do 3D)
**Resoluções:** 640×480 / 800×600
**Cores:** 8-bit (256 paletizado) ou 16-bit (RGB 565/555)
**Overlay:** YUV→RGB para vídeo CD-ROM

## Arquitetura

```
┌───────────────────────────────────────────────────┐
│           Alliance AT3D (modificado)               │
├───────────────────────────────────────────────────┤
│  ┌─────────────────┐  ┌─────────────────────────┐ │
│  │ 2D Engine       │  │ Video Overlay           │ │
│  │ - BITBLT        │  │ - YUV→RGB              │ │
│  │ - Rectangle fill│  │ - Scaling               │ │
│  │ - Line draw     │  │ - Color space conv.     │ │
│  │ - Clipping      │  └─────────────────────────┘ │
│  └────────┬────────┘                              │
│           │                                       │
│  ┌────────┴──────────────────────────────────────┐│
│  │  Compositor (2D + 3D merge)                   ││
│  │  - Alpha blend 2D sobre 3D                    ││
│  │  - Priority control                           ││
│  └───────────────────────────────────────────────┘│
│                                                  │
│  ┌──────────────────────────────────────────────┐│
│  │  RAMDAC + TV Encoder                        ││
│  │  - ICS GENDAC ICS5342 (clock)               ││
│  │  - NTSC/PAL encoder                         ││
│  └──────────────────────────────────────────────┘│
└───────────────────────────────────────────────────┘
```

## Framebuffer UMA

Diferente da VRAM 3D dedicada (EDO), o framebuffer 2D fica na memória principal:

- **Base:** Configurado via DC_FB_ST_OFFSET (no Display Controller do MediaGX)
- **Formato:** 8-bit (paletizado) ou 16-bit (RGB 565 ou RGB 555)
- **Stride:** Configurado via DC_LINE_DELTA
- **Acesso:** O AT3D acessa via barramento dedicado ao MediaGX

## Modos de Vídeo

### Modo 8-bit (Paletizado)
- 256 cores de uma paleta de 18-bit (6-6-6 via ICS GENDAC)
- Palette index: 1 byte/pixel
- DC_OUTPUT_CFG bit 0 = 1

### Modo 16-bit RGB 565
- R:5, G:6, B:5 — 2 bytes/pixel
- DC_OUTPUT_CFG bit 0 = 0, bit 1 = 0

### Modo 16-bit RGB 555
- R:5, G:5, B:5 (bit 15 = 0) — 2 bytes/pixel
- DC_OUTPUT_CFG bit 0 = 0, bit 1 = 1

## Compositor 2D+3D

O AT3D é responsável por combinar o framebuffer 2D com o output 3D do Voodoo Rush:

1. O 3D renderiza para a VRAM EDO (8 MB)
2. O 2D renderiza para a SDRAM do sistema (UMA)
3. O compositor mescla as duas camadas (geralmente 3D sobre 2D ou 2D sobre 3D com chroma key)
4. O resultado final vai para o RAMDAC → saída RGB/TV

## TV Output

O AT3D inclui um **TV encoder** para saída NTSC/PAL:
- **NTSC:** 640×480 @ 60 Hz, entrelaçado
- **PAL:** 720×576 @ 50 Hz, entrelaçado
- Conversão de progressive para interlaced
- Sincronismo composto ou separado

## Referência no MediaGX

A parte 2D não está num chip separado no MAME — o display controller do MediaGX faz o papel de saída 2D em `mediagx.cpp`:

- `mediagx.cpp` linhas 300-386: `draw_framebuffer()` — 8-bit paletizado e 16-bit
- `mediagx.cpp` linhas 388-408: `draw_cga()` — texto CGA
- `mediagx.cpp` linhas 410-421: `screen_update()` — composição
- `mediagx.cpp` linhas 873-876: `ramdac_map` — RAMDAC palette
