# Arquitetura do Emulador — NEC PC-VD

## Stack Tecnológico

- **Linguagem:** C++20 (compatível com MAME)
- **Frontend:** SDL2 (janela, input, áudio)
- **Renderização:** OpenGL 3.3+ (para output do Voodoo)
- **Build:** CMake
- **CPU Core:** Extraído do MAME (i386) ou [[https://github.com/80021/softce|SoftCe]]

## Estrutura de Diretórios

```
nec-pcvd/
├── CMakeLists.txt
├── src/
│   ├── main.cpp              # Entry point, SDL loop
│   ├── emulator.cpp/h        # Core emulator class
│   ├── cpu/
│   │   ├── i386.cpp/h        # CPU x86 (386+ compat)
│   │   └── cpuid.cpp/h       # CPUID (MediaGX)
│   ├── mediagx/
│   │   ├── mediagx.cpp/h     # MediaGX SoC wrapper
│   │   ├── display_ctrl.cpp/h# Display controller regs
│   │   └── mem_ctrl.cpp/h    # Memory controller UMA
│   ├── voodoo/
│   │   ├── voodoo.cpp/h      # Voodoo 1 core (adaptado)
│   │   ├── voodoo_regs.cpp/h # Register table + handlers
│   │   ├── voodoo_fifo.cpp/h # PCI + Memory FIFO
│   │   ├── tmu.cpp/h         # Texture Mapping Unit
│   │   └── renderer/
│   │       ├── voodoo_renderer.cpp/h  # Main renderer
│   │       ├── rasterizer.cpp/h      # Scan converter
│   │       └── pixel_pipeline.cpp/h  # Per-pixel ops
│   ├── at3d/
│   │   └── at3d.cpp/h        # Alliance AT3D 2D
│   ├── audio/
│   │   └── cx5530_audio.cpp/h# DSP + ADPCM
│   ├── devices/
│   │   ├── cdrom.cpp/h       # NEC CD-ROM
│   │   ├── memcard.cpp/h     # Memory Card
│   │   └── controller.cpp/h  # Game controller
│   ├── bus/
│   │   └── pci_bus.cpp/h     # PCI bus
│   └── frontend/
│       ├── sdl_window.cpp/h  # SDL2 window + input
│       └── opengl_output.cpp/h  # OpenGL rendering
├── roms/                     # BIOS + ISOs
└── scripts/
    └── extract_mame.sh       # Script para extrair partes do MAME
```

## Ciclo de Emulação

```
while (running) {
    // CPU
    cpu_cycles = cpu_step(quantum);       // Executa N instruções x86
    
    // Voodoo FIFO flush
    if (voodoo.has_pending_ops())
        voodoo.flush_fifos(current_time);
    
    // Voodoo Render
    if (voodoo.frame_pending())
        voodoo.render_frame();
    
    // Video output
    if (screen.needs_update())
        composite_video();                 // 2D (AT3D) + 3D blend
    
    // Audio
    audio.mix_and_output();
    
    // Input / Events
    SDL_PollEvents();
    controller.update();
    
    // Sync
    frame_limit(60);
}
```

## Diagrama de Classes

```
┌──────────────────────────────────────────────────────────┐
│ Emulator                                                 │
│  - CPU: i386_core                                        │
│  - MediaGX: MediagxSoC                                   │
│  - Voodoo: VoodooRushDevice                              │
│  - AT3D: AllianceAT3D                                    │
│  - Audio: Cx5530Audio                                    │
│  - CDROM: NecCDROM                                       │
│  - PCI: PciBus                                           │
│  - Frontend: SdlWindow                                   │
└──────────────────────────────────────────────────────────┘
         │
         ├── i386_core ──┬── read_mem(addr)
         │               ├── write_mem(addr, data)
         │               ├── read_io(port)
         │               └── write_io(port, data)
         │
         ├── PciBus ─────┬── register_device(dev)
         │               ├── config_read(bus, dev, fn, reg)
         │               └── config_write(...)
         │
         ├── VoodooRush ─┬── write(offset, data) [mmio]
         │               ├── read(offset) [mmio]
         │               ├── fifo_add(offset, data)
         │               ├── flush_fifos(time)
         │               ├── triangle() → cycles
         │               └── update(bitmap) → frame
         │
         ├── MediagxSoC ─┬── display_ctrl_write(offset, data)
         │               ├── mem_ctrl_write(offset, data)
         │               └── biu_write(offset, data)
         │
         └── SdlWindow ──┬── handle_events()
                         ├── render_video()
                         └── audio_callback()
```

## Componentes por Fonte

### Do MAME (`MaMe Files/`)
- **`voodoo.cpp`** (3411 linhas): Renderer, TMU, regs, FIFO, triangle — ~90% do 3D
- **`mediagx.cpp`** (1039 linhas): Display ctrl, mem ctrl, audio AD1847 — ~60% do SoC
- **`voodoo_pci.cpp`** (353 linhas): PCI interface — busca de barramentos

### Dos PDFs (`FIFO/`)
- **`gxmdb_v20.pdf`**: Register map do MediaGX (timings, display, memória)
- **`Voodoo1_Rush_SST-96_Spec_r2.2_199911.pdf`**: Register map do Rush (diferenças do V1)
- **`voodoo_graphics.pdf`**: Pipeline gráfico 3dfx

### Implementação Própria
- CPU x86 core (separado do MAME devido a licenciamento)
- AT3D 2D (não existe no MAME)
- CD-ROM NEC (proprietário)
- Memory Cards
- Controles
- Frontend SDL/OpenGL

## Licenciamento

O código do MAME é BSD-3-Clause, permitindo reuso. No entanto:
- Manter atribuições de copyright (Ville Linde, Aaron Giles, Ted Green)
- CPU core do MAME é GPL — usar implementação separada (SoftCe ou reescrita)
- O emulador standalone pode ser BSD-3-Clause ou MIT

## Referências

- [[03-voodoo-rush]] — Detalhes do Voodoo Rush
- [[02-mediagx]] — Detalhes do MediaGX
- [[09-fases-implementacao]] — Fases de implementação
- [[10-referencia-mame]] — Referência detalhada do MAME
- [[12-fifo-protocol]] — Protocolo FIFO
