# Áudio — Cx5530 DSP

## Visão Geral

O áudio do NEC PC-VD é fornecido pelo **companion Cx5530** que inclui um DSP 16-bit estéreo com suporte a 4 canais ADPCM. O codec é compatível/similar ao **Analog Devices AD1847** (usado no Atari MediaGX).

**DSP:** 16-bit estéreo
**Canais:** 4 canais ADPCM (sample-based)
**Streaming:** Suporte a CD-DA (áudio CD-ROM)
**Frequências:** Múltiplas taxas de sample (5.5 kHz — 48 kHz)

## AD1847 / Cx5530 Codec

O AD1847 (usado no Atari MediaGX) serve como referência para o DSP do Cx5530:

### Registros (via I/O 0x400-0x4FF)

| Porta | Função |
|---|---|
| 0x400-0x403 | Audio data (L/R 16-bit DMA) |
| 0x40C | Status/timing |
| 0x40C+3 | Register select + data write |

### Register Select (AD1847)

| Reg | Nome | Descrição |
|---|---|---|
| 0 | Left Input | Volume/gain |
| 1 | Right Input | Volume/gain |
| 2 | Left Aux | Volume/gain |
| 3 | Right Aux | Volume/gain |
| 4 | Left Output | Volume/gain |
| 5 | Right Output | Volume/gain |
| 6 | Loopback | Controle |
| 7 | Data Format | Sample rate, formato |
| 8 | Interface | Interface config |
| 9-15 | Pin Control | GPIO |

### Data Format Register (Reg 8)

Controla sample rate e formato:
```
Bit 0:    0 = 16-bit, 1 = 8-bit
Bit 1-3:  Divisor de clock (sample rate)
Bit 4:    Stereo/Mono
Bit 5:    Companded (A-law/μ-law)
```

### Sample Rates

O AD1847 usa dois clocks:
- **16.9344 MHz** (clock base)
- **24.576 MHz** (clock alternativo)

A taxa de sample é derivada como: `clock / divide_factor[bits 1-3]`

| Divisor | Fator | @ 16.9344 MHz | @ 24.576 MHz |
|---|---|---|---|
| 0 | 3072 | 5.5125 kHz | 8.0 kHz |
| 1 | 1536 | 11.025 kHz | 16.0 kHz |
| 2 | 896 | 18.9 kHz | 27.43 kHz |
| 3 | 768 | 22.05 kHz | 32.0 kHz |
| 4 | 448 | 37.8 kHz | 54.86 kHz |
| 5 | 384 | 44.1 kHz | 64.0 kHz |
| 6 | 512 | 33.075 kHz | 48.0 kHz |
| 7 | 2560 | 6.615 kHz | 9.6 kHz |

## 4 Canais ADPCM

Além do PCM estéreo, o Cx5530 suporta 4 canais ADPCM:
- Compressão 4:1 (16-bit → 4-bit nibbles)
- Usado para efeitos sonoros (SFX)
- Mistura com o stream PCM na saída final

## Fluxo de Áudio

```
CD-ROM (CD-DA) ──┐
                  ├── Mixer ──→ DSP 16-bit ──→ DAC ──→ Audio Out
ADPCM CH 1-4 ────┘
       ↑
  DMA do PCM (game samples)
```

O áudio é sampleado por timer (a cada 10ms no MAME) e transferido via DMA: 
- `mediagx.cpp` linhas 659-669: `sound_timer_callback` — transferência DMA
- `mediagx.cpp` linhas 671-708: `ad1847_reg_write` — configuração
- `mediagx.cpp` linhas 720-742: `ad1847_w` — escrita de dados PCM
- `mediagx.cpp` linhas 855-857: Alocação de buffers DMA (65536 samples cada canal)

## Referência MAME

- `mediagx.cpp` linhas 657-742: AD1847 completo (timer, reg write, read, write)
- `mediagx.cpp` linhas 855-857: DMA buffers (m_dacl, m_dacr)
- `mediagx.cpp` linhas 860-871: Reset (timer adjust)
- `mediagx.cpp` linhas 897-918: Machine config (DMADAC devices)
