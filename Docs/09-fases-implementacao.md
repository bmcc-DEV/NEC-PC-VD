# Fases de Implementação

## Visão Geral

```
Fase 1 ──→ Fase 2 ──→ Fase 3 ──→ Fase 4 ──→ Fase 5 ──→ Fase 6
 CPU/SoC    Voodoo 3D    AT3D 2D    Áudio     Periféricos  Boot
```

Cada fase produz um marco testável.

---

## Fase 1: CPU + MediaGX Base

**Objetivo:** CPU x86 executando código, display controller mostrando framebuffer.

### Tarefas

- [ ] CPU x86 core (i386 compat):
  - Load/store de memória via callbacks
  - I/O ports via callbacks
  - Interrupções (IRQ)
- [ ] MediaGX SoC:
  - BIU registers (0x40008000)
  - Display controller (0x40008300)
  - Memory controller (0x40008400)
  - UMA framebuffer em RAM
- [ ] Mapa de memória:
  - RAM (0x00000000 — 0x00FFFFFF)
  - I/O mappings
  - BIOS ROM (0xFFFC0000)
- [ ] Frontend SDL:
  - Janela básica
  - Polling de eventos
  - Mostrar framebuffer 2D (8/16-bit)

### Teste
- Carregar BIOS/ROM, CPU deve executar
- Display mostra garbage ou BIOS POST

### Referência MAME
- `mediagx.cpp` linhas 747-772 (maps)
- `mediagx.cpp` linhas 267-421 (video)
- `mediagx.cpp` linhas 878-920 (machine config)

---

## Fase 2: Voodoo Rush 3D

**Objetivo:** Renderizar triângulos 3D texturizados no framebuffer.

### Tarefas

- [ ] Voodoo register table (256 registers)
- [ ] PCI FIFO + Memory FIFO
- [ ] LFB reads/writes (framebuffer acesso linear)
- [ ] Texture uploads (TMU)
- [ ] Triangle setup + rasterizer
- [ ] Pixel pipeline (texture, depth, alpha, fog, dither)
- [ ] Buffer swapping + VBLANK
- [ ] OpenGL output para display

### Sub-fases

#### 2a: Register + FIFO
- Implementar register read/write handlers
- PCI FIFO: add_to_fifo, flush_fifos, execute_fifos
- Stalling mechanism

#### 2b: Framebuffer + Texture
- LFB reads/writes (modos raw + pipeline)
- Texture upload (internal_texture_w)
- Texel format conversion (RGB332, RGB565, ARGB1555, etc.)

#### 2c: Triangle Rasterizer
- Scan converter (edge walking)
- Parameter interpolation (R,G,B,A,Z,S,T,W)
- Texture sampling (bilinear/point)
- Alpha blending, depth testing, fog, chroma key

### Teste
- Executar demo 3D simples via CPU
- Ver triângulo colorido/texturizado na tela

### Referência MAME
- `voodoo.cpp` linhas 1247-1296 (FIFO add)
- `voodoo.cpp` linhas 1352-1400 (FIFO execute)
- `voodoo.cpp` linhas 1570-1710 (LFB write)
- `voodoo.cpp` linhas 1886-1944 (texture write)
- `voodoo.cpp` linhas 2888-3029 (triangle)

---

## Fase 3: Alliance AT3D 2D

**Objetivo:** Camada 2D funcional + composição com 3D.

### Tarefas

- [ ] Modos de vídeo 8-bit (paletizado) e 16-bit
- [ ] 2D acceleration: BITBLT, fills, line draw
- [ ] Video overlay (YUV→RGB)
- [ ] Compositor 2D+3D
- [ ] TV output (NTSC/PAL)
- [ ] RAMDAC / ICS GENDAC

### Teste
- Menu 2D do jogo visível
- Overlay de FMV funcional

### Referência
- `mediagx.cpp` linhas 300-386 (draw_framebuffer)
- `mediagx.cpp` linhas 873-876 (ramdac_map)

---

## Fase 4: Áudio Cx5530

**Objetivo:** Som funcional (PCM + ADPCM).

### Tarefas

- [ ] AD1847-like codec registers
- [ ] DMA de áudio (PCM estéreo)
- [ ] 4 canais ADPCM (decoder)
- [ ] CD-DA streaming
- [ ] Mixer + volume
- [ ] SDL audio output

### Teste
- Bips/BooPs do BIOS/jogo
- Música de fundo

### Referência
- `mediagx.cpp` linhas 657-742 (AD1847)

---

## Fase 5: Periféricos

**Objetivo:** CD-ROM, Memory Cards, controles.

### Tarefas

- [ ] CD-ROM:
  - Leitura ISO9660
  - Modo proprietário NEC
  - CD-DA áudio
- [ ] Memory Cards:
  - Formato de dados
  - Load/save de arquivos .nvm
- [ ] Controles:
  - Mapear teclado/joystick para controles PC-VD
  - Protocolo de polling
- [ ] I/O restante

### Teste
- Jogo detecta controles
- Save/Load funcional

---

## Fase 6: Boot + Runtime

**Objetivo:** Boot completo de jogo real.

### Tarefas

- [ ] BIOS NEC (dump real)
- [ ] Boot sequence:
  - Reset vector → ROM
  - Shadow copy
  - Init hardware
  - Read CD boot sector
  - Load game → RAM
- [ ] Runtime freestanding:
  - Setup de interrupções
  - Timer
  - CD-ROM driver
  - Memory card driver
- [ ] Jump para `game_main()`

### Teste
- Jogo real bootando e rodando
- Título na tela, música tocando, controles funcionando

---

## Cronograma Estimado

| Fase | Complexidade | Tempo Est. | Depende de |
|---|---|---|---|
| 1: CPU + SoC | Muito Alta | 4-6 semanas | — |
| 2a: Voodoo Regs | Média | 1-2 semanas | Fase 1 |
| 2b: Voodoo FB/Tex | Média | 1-2 semanas | Fase 2a |
| 2c: Voodoo Raster | Alta | 3-4 semanas | Fase 2b |
| 3: AT3D 2D | Média | 2-3 semanas | Fase 1 |
| 4: Áudio | Baixa | 1 semana | Fase 1 |
| 5: Periféricos | Média | 2-3 semanas | Fase 1 |
| 6: Boot | Média | 1-2 semanas | Fase 1-5 |

**Total estimado:** 4-6 meses (paralelizando Fase 2, 3, 4)
