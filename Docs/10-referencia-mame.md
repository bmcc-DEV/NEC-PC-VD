# Referência do Código MAME

Este documento mapeia o código MAME existente para o emulador standalone. O código MAME (BSD-3-Clause) serve como blueprint.

## mediagx.cpp (1039 linhas) — Ville Linde

### Estrutura

| Linhas | Conteúdo | Uso no Emulador |
|---|---|---|
| 1-67 | Header, comentários, hardware list | Documentação |
| 69-84 | Includes | Similar, adaptar nomes |
| 86-224 | `mediagx_state` class | Classe `MediagxSoC` |
| 227-252 | Display controller register defines | `display_ctrl.h` |
| 259-265 | CGA palette | Não usar (PC-VD usa 2D RGB) |
| 267-274 | `video_start()` | Inicialização do display |
| 276-298 | `draw_char()` | Não usar (sem CGA) |
| 300-386 | `draw_framebuffer()` | **AT3D framebuffer output** |
| 388-408 | `draw_cga()` | Não usar |
| 410-421 | `screen_update()` | **Compositor 2D** |
| 423-449 | `disp_ctrl_r/w()` | **Display controller** |
| 452-486 | `memory_ctrl_r/w()` | **Memory controller** |
| 490-508 | `biu_ctrl_r/w()` | **BIU** |
| 510-512 | `bios_ram_w()` | BIOS write-protect |
| 514-540 | `io20_r/w()` | Cyrix config regs |
| 542-655 | `parallel_port_r/w()` | **Controles** (adaptar) |
| 657-742 | AD1847 audio | **Áudio Cx5530** |
| 747-759 | `mediagx_map()` | **Memory map** |
| 761-772 | `mediagx_io()` | **I/O map** |
| 776-796 | GFX decode (CGA font) | Não usar |
| 798-852 | INPUT_PORTS | **Controles** (mapeamento) |
| 854-871 | `machine_start/reset()` | Inicialização |
| 873-920 | `mediagx()` machine config | Config System |
| 923-997 | Init + speedups | Init + debug |
| 1001-1015 | ROM definitions | ROM loading |
| 1038-1039 | GAME entries | Driver entry |

### Funções-Chave para Reimplementar

```cpp
// Display controller — essencial para saída 2D
void draw_framebuffer(bitmap_rgb32 &bitmap, ...);  // linha 300
uint32_t disp_ctrl_r(offs_t offset);                // linha 423
void disp_ctrl_w(offs_t offset, uint32_t data, ...);// linha 445

// Memory controller — UMA config
uint32_t memory_ctrl_r(offs_t offset);              // linha 452
void memory_ctrl_w(offs_t offset, uint32_t data, ...);// linha 457

// Audio — AD1847 codec
TIMER_DEVICE_CALLBACK_MEMBER(sound_timer_callback); // linha 659
void ad1847_reg_write(int reg, uint8_t data);       // linha 671
uint32_t ad1847_r(offs_t offset);                   // linha 710
void ad1847_w(offs_t offset, ...);                  // linha 720
```

---

## voodoo.cpp (3411 linhas) — Aaron Giles

### Estrutura

| Linhas | Conteúdo | Uso |
|---|---|---|
| 1-106 | Header, specs, TODO | Documentação |
| 108-116 | TODO list | Atenção |
| 118-125 | Includes | Similar |
| 138-164 | `float_to_int32()` | **Conversão FP→fixed** |
| 167-199 | `float_to_int64()` | **Conversão FP→fixed** |
| 210-222 | `register_save()` | Save states |
| 229-248 | `s_alias_map` | Register aliasing |
| 259-307 | `shared_tables` | **Texel formats** |
| 314-327 | `tmu_state` class | **TMU** |
| 334-343 | `tmu_state::init()` | TMU init |
| 375-410 | `ncc_w()` | **NCC/palette** |
| 418-461 | `prepare_texture()` | **Texture setup** |
| 468-545 | `memory_fifo` | **FIFO system** |
| 556-659 | `debug_stats` | Debug |
| 665-683 | `generic_voodoo_device` | Base class |
| 703-744 | `voodoo_1_device` | **Classe principal** |
| 760-780 | `core_map()` | **Memory map** |
| 788-826 | `read()/write()` | MMIO |
| 834-839 | `set_init_enable()` | Init |
| 846-893 | `update()` | **Frame output** |
| 900-1030 | `device_start()` | **Init/alloc** |
| 1076-1082 | `soft_reset()` | Reset |
| 1215-1239 | `recompute_fbmem_fifo()` | FIFO config |
| 1247-1296 | `add_to_fifo()` | **PCI→FIFO** |
| 1304-1344 | `flush_fifos()` | **FIFO exec** |
| 1352-1400 | `execute_fifos()` | **FIFO dispatch** |
| 1408-1416 | `map_register_r()` | Register read |
| 1435-1475 | `map_register_w()` | **Register write** |
| 1482-1504 | `map_lfb_w/texture_w()` | LFB/tex write |
| 1512-1562 | `internal_lfb_r()` | **LFB read** |
| 1570-1710 | `internal_lfb_w()` | **LFB write** |
| 1719-1878 | `expand_lfb_data()` | **Pixel expand** |
| 1886-1944 | `internal_texture_w()` | **Texture upload** |
| 1951-2271 | Register handlers | **Register R/W** |
| 2279-2282 | `reg_triangle_w()` | Triangle cmd |
| 2289-2302 | `reg_nop_w()` | NOP cmd |
| 2309-2327 | `reg_fastfill_w()` | Fast fill |
| 2334-2351 | `reg_swapbuffer_w()` | Swap buffers |
| 2358-2362 | `reg_fogtable_w()` | Fog table |
| 2369-2395 | `reg_fbiinit_w()` | Init regs |
| 2403-2424 | `reg_video_w()` | Video timing |
| 2527-2604 | VBLANK timers | Vsync |
| 2612-2663 | `swap_buffers()` | **Buffer swap** |
| 2672-2680 | `rotate_buffers()` | Buffer rotate |
| 2687-2737 | `update_common()` | **Framebuffer→bitmap** |
| 2746-2799 | `recompute_video_timing()` | **Video timing** |
| 2807-2881 | `recompute_video_memory()` | **VRAM layout** |
| 2888-3029 | `triangle()` | **Triangle raster** |
| 3198-3242+ | Register table | **256 reg entries** |

### Funções-Chave para Reimplementar

```cpp
// FIFO system — coração da comunicação
void add_to_fifo(u32 offset, u32 data, u32 mem_mask); // linha 1247
void flush_fifos(attotime current_time);                // linha 1304
u32 execute_fifos();                                    // linha 1352

// Triangle rasterization
s32 triangle();                                         // linha 2888

// Texture
void internal_texture_w(offs_t offset, u32 data);      // linha 1886
void internal_lfb_w(offs_t offset, u32 data, ...);     // linha 1570

// Framebuffer
int update_common(bitmap_rgb32 &bitmap, ...);           // linha 2687
void swap_buffers();                                    // linha 2612
```

---

## voodoo_pci.cpp (353 linhas) — Ted Green

| Linhas | Conteúdo | Uso |
|---|---|---|
| 9-17 | `Voodoo 1 PCI mconfig` | PCI device setup |
| 56-60 | `config_map()` | PCI config |
| 62-66 | `voodoo_pci_device` class | Base PCI class |
| 68-71 | Voodoo 1 PCI ctor | Constructor |
| 100-108 | `device_start()` (base) | Init |
| 110-131 | Voodoo 1 PCI start | **PCI IDs, BARs** |
| 232-235 | `screen_update()` | Output |
| 238-281 | `pcictrl_r/w()` | **PCI ctrl regs** |
| 283-293 | VGA r/w | VGA legacy |

### Funções-Chave

```cpp
void device_start();             // PCI init
u32 pcictrl_r(offs_t offset, ...);  // PCI control read
void pcictrl_w(offs_t offset, ...); // PCI control write
```
