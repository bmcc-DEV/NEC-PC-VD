# Visão Geral do Hardware — NEC PC-VD

```
┌──────────────────────────────────────────────────────────┐
│                     NEC PC-VD                            │
├──────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────────────────────┐      │
│  │  MediaGXm    │  │  Voodoo Rush Custom (SST-96)  │      │
│  │  @ 233 MHz   │  │  + Alliance AT3D (package)   │      │
│  │  Core 486/P5 │  │  3D @ 60 MHz / 2D integrado  │      │
│  │  16 KB L1    │  │  8 MB EDO (4 FB + 4 TMU)     │      │
│  └──────┬───────┘  └──────────────┬───────────────┘      │
│         │                         │                       │
│         └─────────┬───────────────┘                       │
│                   │ Bus dedicado PCI-like @ 33 MHz        │
│  ┌────────────────┴──────────────────────────────────┐    │
│  │              Cx5530 Companion                      │    │
│  │  ┌──────────────┐  ┌──────────┐  ┌──────────────┐ │    │
│  │  │ Mem Ctrl     │  │ Audio    │  │ I/O          │ │    │
│  │  │ UMA + SDRAM  │  │ DSP 16b  │  │ CD-ROM       │ │    │
│  │  │ 32 MB (64MB) │  │ 4ch ADPCM│  │ MemCard x2   │ │    │
│  │  └──────────────┘  └──────────┘  │ Controles    │ │    │
│  │                                  └──────────────┘ │    │
│  └────────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

## Especificações Técnicas

| Componente | Especificação | Notas |
|---|---|---|
| **CPU** | Cyrix MediaGXm @ 233 MHz (MMX) | Core 486/Pentium-class, 16 KB L1 unificado |
| **Coprocessador** | MediaGX + Cx5530 companion | Mem, PCI, áudio, I/O integrados |
| **GPU 3D** | NEC Voodoo Rush Custom (SST-96 + TMU) | 60 MHz, custom NEC + 3dfx |
| **GPU 2D** | Alliance AT3D modificado | Mesmo package do 3D |
| **RAM** | 32 MB SDRAM (exp. 64 MB) | Shared com framebuffer 2D (UMA) |
| **VRAM 3D** | 8 MB EDO dedicada | 4 MB FB + 4 MB texturas |
| **Resolução 2D** | 640×480 / 800×600 | RGB + TV (NTSC/PAL) |
| **Resolução 3D** | 640×480 @ 16/32-bit | — |
| **Áudio** | DSP 16-bit estéreo + 4ch ADPCM | Via Cx5530 |
| **Mídia** | CD-ROM 12× (ISO9660 + NEC) | Opcional DVD-ROM |
| **Armazenamento** | 2× Memory Card (1 MB cada) | Persistente, estilo PS1 |
| **Controle** | Digital + analógico (4 eixos) + 6 botões | Porta proprietária |
| **Barramento** | PCI 33 MHz + bus dedicado GPU↔CPU | Baixa latência |
| **SO** | Runtime freestanding + SDK C | Boot direto para game_main() |
| **API Gráfica** | Glide-like simplificada + Display List 2D | Compatível subset Glide |

## Peculiaridades

1. **UMA (Unified Memory Architecture)**: O framebuffer 2D fica na SDRAM do sistema, gerenciado pelo Display Controller do MediaGX via DCT (Display Compression Technology)
2. **VRAM dedicada 3D**: Diferente do Voodoo Rush original (que compartilhava barramento com 2D), a NEC separou a memória EDO para o 3D, eliminando disputa de barramento
3. **Custom Voodoo Rush**: A NEC modificou o SST-96 da 3dfx, provavelmente alterando o mapa de registros e o barramento para o AT3D
4. **Sem SO**: O console não roda Windows — boot direto para o jogo via runtime freestanding
5. **API Glide-like**: Compatível com subset do Glide da 3dfx + primitivas 2D aceleradas
6. **CD-ROM proprietário**: Usa modo NEC próprio além do ISO9660 padrão
7. **TV Output**: Saída RGB + TV (NTSC/PAL) — provavelmente via ICS GENDAC

## Clock Domains

| Domain | Frequência | Dispositivos |
|---|---|---|
| CPU Core | 233 MHz | MediaGXm |
| Bus PCI | 33 MHz | Cx5530, Voodoo Rush, periféricos |
| GPU 3D | 60 MHz | Voodoo Rush (SST-96 + TMU) |
| GPU 2D | ? | Alliance AT3D |
| Audio | 16.9344 / 24.576 MHz | AD1847 / Cx5530 DSP |
| Video DAC | ? | ICS GENDAC ICS5342 |
| RAMDAC | ? | ICS GENDAC (provavelmente 80-135 MHz) |
