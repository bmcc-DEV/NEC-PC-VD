#pragma once
#include <cstdint>
#include <functional>
#include <array>

class MemoryBus;

class I386Core {
public:
    I386Core(MemoryBus& mem);

    void reset();
    void set_cpuid(uint32_t sig, uint32_t feature);

    int execute(int max_cycles);
    uint32_t pc() const { return m_eip; }

    void signal_interrupt(int vector);
    void spin_until_interrupt();
    void reset_spinning();

    // Para callbacks do sistema
    void set_io_read_handler(uint16_t port, std::function<uint8_t(uint16_t)> handler);
    void set_io_write_handler(uint16_t port, std::function<void(uint16_t, uint8_t)> handler);

private:
    MemoryBus& m_mem;

    // Registradores
    uint32_t m_eax, m_ecx, m_edx, m_ebx;
    uint32_t m_esp, m_ebp, m_esi, m_edi;
    uint32_t m_eip;
    uint32_t m_eflags;

    // Segment registers
    uint16_t m_seg[6]; // ES, CS, SS, DS, FS, GS
    uint32_t m_seg_base[6];
    uint32_t m_seg_limit[6];

    // Modo
    bool m_protected_mode = false;
    bool m_spinning = false;
    bool m_halted = false;

    // Descriptor tables
    uint16_t m_gdtr_limit = 0;
    uint32_t m_gdtr_base = 0;

    // I/O helpers
    uint8_t io_read_byte(uint16_t port);
    void io_write_byte(uint16_t port, uint8_t data);

    // I/O handlers
    std::function<uint8_t(uint16_t)> m_io_read[65536];
    std::function<void(uint16_t, uint8_t)> m_io_write[65536];

    // Register helpers
    uint8_t _rm8(int rm);
    uint16_t _rm16(int rm);
    uint32_t _rm32(int rm);
    void _wm8(int rm, uint8_t v);
    void _wm16(int rm, uint16_t v);
    void _wm32(int rm, uint32_t v);

    uint8_t modrm_byte();
    uint32_t modrm_addr_ea(int seg_reg, int mod, int rm);

    // Fetch
    uint32_t fetch8();
    uint32_t fetch16();
    uint32_t fetch32();

    // Memória
    uint32_t read32_seg(int seg, uint32_t offset);
    uint16_t read16_seg(int seg, uint32_t offset);
    uint8_t  read8_seg(int seg, uint32_t offset);
    void write32_seg(int seg, uint32_t offset, uint32_t data);
    void write16_seg(int seg, uint32_t offset, uint16_t data);
    void write8_seg(int seg, uint32_t offset, uint8_t data);

    uint32_t read32(uint32_t addr);
    uint16_t read16(uint32_t addr);
    uint8_t  read8(uint32_t addr);
    void write32(uint32_t addr, uint32_t data);
    void write16(uint32_t addr, uint16_t data);
    void write8(uint32_t addr, uint8_t data);

    // Flags
    uint32_t get_flags();
    void set_flags(uint32_t f);
    void set_sz8(uint8_t v);
    void set_sz16(uint16_t v);
    void set_sz32(uint32_t v);
    void set_cf_add32(uint32_t a, uint32_t b);
    void set_cf_sub32(uint32_t a, uint32_t b);
    void set_of_add32(uint32_t a, uint32_t b, uint32_t r);
    void set_of_sub32(uint32_t a, uint32_t b, uint32_t r);
    int parity8(uint8_t v);

    // ModRM / SIB
    uint32_t modrm_addr(int seg_reg);
    int modrm_reg();
    int modrm_rm();

    // Current opcode
    int m_op = 0;
    int m_op2 = 0;

    // Prefix states
    int m_seg_prefix = -1;
    bool m_lock_prefix = false;
    bool m_rep_prefix = false;
    int m_op_size = 32;
    int m_addr_size = 32;

    void decode_prefixes();
    int decode_one();

    // Opcodes
    int op_mov_rm_r();
    int op_mov_imm();
    int op_mov_mem_acc();
    int op_mov_acc_mem();
    int op_mov_sreg();
    int op_mov_sreg_rm();
    int op_add_rm_r();
    int op_sub_rm_r();
    int op_cmp_rm_r();
    int op_and_rm_r();
    int op_or_rm_r();
    int op_xor_rm_r();
    int op_test_rm_r();
    int op_inc_dec();
    int op_push_pop();
    int op_jmp();
    int op_jcc();
    int op_call();
    int op_ret();
    int op_int();
    int op_shift_rotate();
    int op_alu_imm();
    int op_arith_rm_imm();
    int op_lea();
    int op_xchg();
    int op_nop();
    int op_esc_nop();
    int op_two_byte();
    int op_setcc();
    int op_cmovcc();
    int op2_mov_cr();
    int op2_mov_dr();
    int op2_shrd_shld();
    int op2_bt();
    int op2_bts();
    int op2_imul();
    int op2_cpuid();
    int op2_cmpxchg();
};
