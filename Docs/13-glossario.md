# Glossário

## A

**AD1847** — Codec de áudio da Analog Devices (16-bit estéreo, usado no MediaGX/Atari). Referência para o DSP do Cx5530.

**ADPCM** — Adaptive Differential Pulse Code Modulation. Compressão de áudio 4:1 usada nos 4 canais de SFX.

**AT3D** — Alliance AT3D, chip gráfico 2D modificado pela NEC, integrado no mesmo package do Voodoo Rush.

## B

**BIU** — Bus Interface Unit. Gerencia o barramento PCI e memória no MediaGX.

## C

**Cx5510/Cx5530** — Companion chips da Cyrix para o MediaGX. Cx5530 adiciona PCI bridge + áudio + I/O.

**CLUT** — Color Look-Up Table. Paleta de 256 cores (mapeada para 16-bit via interpolação no Voodoo).

## D

**DCT** — Display Compression Technology. Tecnologia do MediaGX para compressão do framebuffer UMA.

## F

**FBI** — Frame Buffer Interface. Sub-sistema do Voodoo que gerencia o framebuffer, rasterização e blending.

**FIFO** — First-In, First-Out. Buffer de comandos entre CPU e GPU (PCI FIFO + Memory FIFO).

## G

**GENDAC ICS5342** — RAMDAC da ICS usado no MediaGX (interface entre framebuffer digital e saída analógica RGB).

## L

**LFB** — Linear Frame Buffer. Acesso direto (linear) ao framebuffer 3D via espaço de memória.

**LOD** — Level of Detail. Mipmaps para texturas (distância).

**LWM/HWM** — Low Water Mark / High Water Mark. Limiares de água do FIFO para controle de stall.

## M

**MediaGXm** — CPU x86 da Cyrix/NS com controladores integrados. Core 486/Pentium, 16 KB L1, MMX, 233 MHz.

**Memory FIFO** — FIFO secundário armazenado na própria FB RAM (expansão do PCI FIFO on-chip).

## N

**NCC** — NEC Color Compression (ou Nearest Color Coding). Formato de textura comprimida do Voodoo.

## P

**PCI FIFO** — FIFO primário de 64 entries dentro do chip Voodoo.

## R

**RAMDAC** — RAM Digital-to-Analog Converter. Converte dados digitais do framebuffer para sinal analógico RGB.

**Rush** — Codinome 3dfx para o Voodoo Rush (SST-96), único chip 2D+3D integrado da 3dfx.

## S

**SST-96** — Designação 3dfx para o Voodoo Rush.

## T

**TMU** — Texture Mapping Unit. Unidade de mapeamento de textura (filtro bilinear, LOD, palette/NCC).

## U

**UMA** — Unified Memory Architecture. Framebuffer 2D na memória principal (SDRAM) em vez de VRAM dedicada.

## V

**Voodoo 1 (SST-1)** — Primeira geração 3dfx (só 3D, precisa de placa 2D separada).

**Voodoo Rush (SST-96)** — Segunda geração 3dfx (2D+3D integrado, AT3D). Base do chip custom NEC.

