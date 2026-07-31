# Protocolo FIFO — Voodoo Rush

## Visão Geral

O Voodoo Rush usa um sistema de FIFO (First-In, First-Out) para bufferizar comandos da CPU para a GPU. Isso permite:

1. **Pipeline assíncrono**: CPU não precisa esperar a GPU terminar cada operação
2. **Burst writes**: Múltiplos writes consecutivos sem stall
3. **Prioridade**: Memory FIFO tem prioridade sobre PCI FIFO na execução

## Arquitetura

```
CPU Write (PCI)
     │
     ▼
┌──────────────┐    ┌──────────────────┐
│  PCI FIFO    │───→│  Memory FIFO     │
│  64 entries  │    │  (em FB RAM)     │
│  (on-chip)   │    │  até 65536 entry │
└──────────────┘    └────────┬─────────┘
                            │
                            ▼
                     ┌──────────────┐
                     │  Execute     │
                     │  (process)   │
                     └──────────────┘
```

### Tipos de Dados no FIFO

Cada entry no FIFO é um par **offset + data** (2 × 32-bit):

| Tipo | Offset [31-0] | Descrição |
|---|---|---|
| **Register** | `00ab----ccrrrrrr` | a=alt reg, b=swizzle, c=chipmask, r=regnum |
| **LFB Write** | `01yyyyyyyyxxxxx` | Y=linha, X=coluna (acesso linear ao FB) |
| **Texture** | `1-ccllllttttttttsssssss` | c=chip, l=LOD, t=Y, s=X |

### Flags no Offset

Para LFB writes, flags indicam o byte mask:

```
Bit 30: NO_16_31 (não escrever bytes 16-31)
Bit 29: NO_0_15  (não escrever bytes 0-15)
Bits 28-22: TYPE (0=register, 1=LFB, 2=texture, 3=texture alt)
```

## Adicionando ao FIFO

Fluxo de `add_to_fifo()` (voodoo.cpp linha 1247):

```
add_to_fifo(offset, data, mem_mask):
  1. Ajusta offset com flags NO_16_31 / NO_0_15 baseado em mem_mask
  2. Adiciona offset + data ao PCI FIFO
  3. Se memory FIFO habilitado E PCI FIFO space <= LWM:
     a. Move entries do PCI FIFO para memory FIFO
     b. Verifica tipos permitidos (LFB → FIFO, Texture → FIFO)
     c. Se FIFO acima do HWM → stall CPU
  4. Se PCI FIFO space <= 2 × LWM → stall CPU
```

## Executando o FIFO

Fluxo de `execute_fifos()` (voodoo.cpp linha 1352):

```
execute_fifos():
  loop:
    1. Prioriza memory FIFO sobre PCI FIFO
    2. Se vazio, return 0 (sem mais trabalho)
    3. Remove offset + data do FIFO
    4. Switch pelo tipo:
       - TYPE_REGISTER: write ao register handler
         Se cycles > 0, return cycles
       - TYPE_TEXTURE: internal_texture_w()
       - TYPE_LFB: internal_lfb_w() com mem_mask
```

## Stalling

### Quando Stalla
1. **PCI FIFO LWM**: PCI FIFO tem ≤ 2 × LWM entries livres
2. **Memory FIFO HWM**: Memory FIFO tem ≥ 2 × 32 × HWM entries

### Como Stalla
```
stall_cpu(STALLED_UNTIL_FIFO_LWM):
  1. Seta stall_state = STALLED_UNTIL_FIFO_LWM
  2. Se tem callback de stall → chama callback(true)
  3. Senão → cpu.spin_until_trigger(trigger)
  4. Timer de resume agenda check_stalled_cpu()
```

### Resume
```
check_stalled_cpu():
  1. Flush FIFOs (executar o que der)
  2. Se STALLED_UNTIL_FIFO_LWM:
     - Memory FIFO habilitado? memory_fifo.items < HWM threshold → resume
     - PCI FIFO? pci_fifo.space > 2 × LWM → resume
  3. Se STALLED_UNTIL_FIFO_EMPTY:
     - FIFOs vazios → resume
  4. Se resume → trigga CPU para continuar
```

## Implementação no Emulador

### memory_fifo class

```cpp
class MemoryFIFO {
    u32* m_base;     // Base do buffer (FB RAM)
    int m_size;      // Tamanho em entries
    int m_in;        // Write pointer
    int m_out;       // Read pointer
    
    void configure(u32* base, u32 size);
    void add(u32 data);
    u32  remove();
    bool empty() const { return m_in == m_out; }
    int  space() const;  // Entries livres
    int  items() const;  // Entries ocupados
};
```

### Filtros de Tipo

```cpp
// Controla quais tipos vão para memory FIFO
valid[0] = true;  // Register sempre
valid[1] = fbiInit0.lfb_to_memory_fifo();     // LFB
valid[2] = fbiInit0.texmem_to_memory_fifo();   // Texture
valid[3] = fbiInit0.texmem_to_memory_fifo();   // Texture (alt)

// Verifica tipo: (offset >> 22) & 3
type = (offset >> 22) & 3;
can_go_to_memory_fifo = valid[type];
```

### Configuração

Os parâmetros do FIFO vêm de:
- **fbiInit0**: enable_memory_fifo, lfb_to_memory_fifo, texmem_to_memory_fifo
- **fbiInit0**: stall_pcie_for_hwm, pci_fifo_lwm, memory_fifo_hwm
- **fbiInit4**: memory_fifo_start_row, memory_fifo_stop_row (localização na FB RAM)

## Referência MAME

- `voodoo.cpp` linhas 468-545: `memory_fifo` class
- `voodoo.cpp` linhas 1215-1239: `recompute_fbmem_fifo()`
- `voodoo.cpp` linhas 1247-1296: `add_to_fifo()`
- `voodoo.cpp` linhas 1304-1344: `flush_fifos()`
- `voodoo.cpp` linhas 1352-1400: `execute_fifos()`
- `voodoo.cpp` linhas 3097-3147: `check_stalled_cpu()`
- `voodoo.cpp` linhas 3155-3173: `stall_cpu()`
