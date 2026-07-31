# Periféricos — CD-ROM, Memory Cards, Controles

## CD-ROM 12×

### Especificação
- **Velocidade:** 12× (1.8 MB/s teórico)
- **Formatos:** ISO9660 + modo proprietário NEC
- **Interface:** Proprietária NEC (não IDE/ATAPI)
- **Opcional:** DVD-ROM em revisão posterior

### Modos de Leitura
1. **ISO9660 Padrão**: Setores 2048 bytes, modo 1
2. **Modo Proprietário NEC**: Provavelmente setores maiores ou raw data (2352 bytes) para dados não-ISO (FMV, assets)

### DMA
- O CD-ROM provavelmente usa DMA para transferir dados direto para a SDRAM do sistema
- Interrupção quando o buffer está cheio

## Memory Cards (2× 1 MB)

### Especificação
- **Capacidade:** 1 MB cada
- **Interface:** Serial (estilo PlayStation)
- **Persistência:** Bateria interna

### Formato (Estimado)
- Provavelmente usa estrutura similar a Memory Cards de console:
  - **Block size:** 8 KB ou 16 KB
  - **File allocation:** FAT-like simples ou diretório plano
  - **Header:** Magic + checksum

### Emulação
- Salvar em arquivos `.nvm` (raw dump do conteúdo)
- 2 arquivos: `memcard0.nvm`, `memcard1.nvm`
- Tamanho: 1 MB cada

## Controles

### Especificação
- **Digital:** D-pad + 6 botões (A, B, C, X, Y, Z)
- **Analógico:** 4 eixos (2 sticks analógicos)
- **Interface:** Porta proprietária (adaptador PC disponível)

### Protocolo (Estimado)
Baseado na porta paralela do MediaGX (similar ao Atari):
- **Porta:** 0x378-0x37B (parallel port)
- **Protocolo:** Serial over parallel
  - Write byte 0x18-0x1B: Reset pointer
  - Write byte 0x20+: General purpose output
  - Read: Dados do controle via nibbles

### Mapeamento (do mediagx.cpp linhas 798-852)
```
Porta paralela:
  Write: 0x18-0x1B → Reset pointer para 0-3
  Write: 0x20-0x2F → General purpose output (low nibble)
  Write: 0x30-0x3F → General purpose output (high nibble)
  Write: 0x40-0x4F → Coin counters
  Write: 0x50-0x5F → Kickers
  Write: 0x60-0x6F → Watchdog reset
  Write: 0x70-0xFF → Advance pointer
  Read: Nibble data (12 bits: 3 nibbles por controle)
```

### Mapeamento Proposto para PC-VD

| Controle | Mapeamento PC-VD |
|---|---|
| 4 eixos analógicos | 2 sticks (left X/Y, right X/Y) |
| D-Pad | Direcionais |
| Botão A / B / C | 3 botões face |
| Botão X / Y / Z | 3 botões ombro/gatilho |
| Start / Select | Botões de sistema |

## I/O Mapa Estimado

| Porta | Função |
|---|---|
| 0x1F0-0x1F7 | CD-ROM data/command (IDE-like?) |
| 0x3F0-0x3F7 | CD-ROM aux status |
| 0x378-0x37B | Controles (via parallel port) |
| 0x?? | Memory Card slot 1 |
| 0x?? | Memory Card slot 2 |
| 0x?? | TV system (NTSC/PAL) |
| 0x?? | System control (reset, etc.) |

## Referência MAME

- `mediagx.cpp` linhas 542-655: parallel_port_r/w (controles)
- `mediagx.cpp` linhas 798-852: INPUT_PORTS (mapeamento)
- `mediagx.cpp` linhas 1001-1031: ROM / DISK definitions
