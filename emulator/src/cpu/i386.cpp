#include "i386.h"
#include "bus/memory_bus.h"
#include <cstdio>
#include <cstring>

#define FLAG_CF 0x001
#define FLAG_PF 0x004
#define FLAG_AF 0x010
#define FLAG_ZF 0x040
#define FLAG_SF 0x080
#define FLAG_TF 0x100
#define FLAG_IF 0x200
#define FLAG_DF 0x400
#define FLAG_OF 0x800

I386Core::I386Core(MemoryBus& mem) : m_mem(mem) {
    for (int i = 0; i < 65536; i++) { m_io_read[i] = nullptr; m_io_write[i] = nullptr; }
}

void I386Core::set_io_read_handler(uint16_t port, std::function<uint8_t(uint16_t)> h) { m_io_read[port] = std::move(h); }
void I386Core::set_io_write_handler(uint16_t port, std::function<void(uint16_t, uint8_t)> h) { m_io_write[port] = std::move(h); }

void I386Core::reset() {
    m_eax = m_ecx = m_edx = m_ebx = 0;
    m_esp = m_ebp = m_esi = m_edi = 0;
    m_eip = 0xFFF0; m_eflags = 0x2;
    for (int i = 0; i < 6; i++) { m_seg[i] = 0; m_seg_base[i] = 0; m_seg_limit[i] = 0xFFFF; }
    m_seg[1] = 0xF000; m_seg_base[1] = 0xFFFF0000; m_seg_limit[1] = 0xFFFF;
    m_protected_mode = false; m_spinning = false; m_halted = false;
}

void I386Core::set_cpuid(uint32_t sig, uint32_t feature) {}
void I386Core::signal_interrupt(int vector) {
    if (m_eflags & FLAG_IF) {
        m_spinning = false;
        m_esp -= 4; write32(m_esp, m_eflags);
        m_esp -= 4; write32(m_esp, m_seg[1]);
        m_esp -= 4; write32(m_esp, m_eip);
        m_eflags &= ~(FLAG_IF | FLAG_TF);
        uint32_t a = read16(vector * 4), s = read16(vector * 4 + 2);
        m_eip = a; m_seg[1] = s; m_seg_base[1] = s << 4;
    }
}

void I386Core::spin_until_interrupt() { m_spinning = true; }
void I386Core::reset_spinning() { m_spinning = false; if (m_halted) m_spinning = true; }

uint32_t I386Core::fetch8() { uint32_t v = read8(m_seg_base[1] + m_eip); m_eip++; return v; }
uint32_t I386Core::fetch16() { uint32_t v = read16(m_seg_base[1] + m_eip); m_eip += 2; return v; }
uint32_t I386Core::fetch32() { uint32_t v = read32(m_seg_base[1] + m_eip); m_eip += 4; return v; }

uint32_t I386Core::read32(uint32_t a) { return m_mem.read32(a); }
uint16_t I386Core::read16(uint32_t a) { return m_mem.read16(a); }
uint8_t  I386Core::read8(uint32_t a)  { return m_mem.read8(a); }
void I386Core::write32(uint32_t a, uint32_t d) { m_mem.write32(a, d); }
void I386Core::write16(uint32_t a, uint16_t d) { m_mem.write16(a, d); }
void I386Core::write8(uint32_t a, uint8_t d)   { m_mem.write8(a, d); }

uint32_t I386Core::read32_seg(int s, uint32_t o) { return read32(m_seg_base[s] + o); }
uint16_t I386Core::read16_seg(int s, uint32_t o) { return read16(m_seg_base[s] + o); }
uint8_t  I386Core::read8_seg(int s, uint32_t o)  { return read8(m_seg_base[s] + o); }
void I386Core::write32_seg(int s, uint32_t o, uint32_t d) { write32(m_seg_base[s] + o, d); }
void I386Core::write16_seg(int s, uint32_t o, uint16_t d) { write16(m_seg_base[s] + o, d); }
void I386Core::write8_seg(int s, uint32_t o, uint8_t d)   { write8(m_seg_base[s] + o, d); }

// Flags
uint32_t I386Core::get_flags() { return m_eflags; }
void I386Core::set_flags(uint32_t f) { m_eflags = (f & 0x0FFF) | 0x2; }
void I386Core::set_sz8(uint8_t v) {
    m_eflags = (m_eflags & ~(FLAG_ZF|FLAG_SF|FLAG_PF)) | ((v == 0) ? FLAG_ZF : 0) | ((v & 0x80) ? FLAG_SF : 0);
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; if (!(v & 1)) m_eflags |= FLAG_PF;
}
void I386Core::set_sz16(uint16_t v) {
    m_eflags = (m_eflags & ~(FLAG_ZF|FLAG_SF|FLAG_PF)) | ((v == 0) ? FLAG_ZF : 0) | ((v & 0x8000) ? FLAG_SF : 0);
    uint8_t p = v & 0xFF; p ^= p >> 4; p ^= p >> 2; p ^= p >> 1; if (!(p & 1)) m_eflags |= FLAG_PF;
}
void I386Core::set_sz32(uint32_t v) {
    m_eflags = (m_eflags & ~(FLAG_ZF|FLAG_SF|FLAG_PF)) | ((v == 0) ? FLAG_ZF : 0) | ((v & 0x80000000) ? FLAG_SF : 0);
    uint8_t p = v & 0xFF; p ^= p >> 4; p ^= p >> 2; p ^= p >> 1; if (!(p & 1)) m_eflags |= FLAG_PF;
}

// ModRM (reads from CS:EIP)
uint8_t I386Core::modrm_byte() { uint8_t v = read8(m_seg_base[1] + m_eip); m_eip++; return v; }
int I386Core::modrm_reg() { return (read8(m_seg_base[1] + m_eip) >> 3) & 7; }
int I386Core::modrm_rm() { return read8(m_seg_base[1] + m_eip) & 7; }

    uint32_t I386Core::modrm_addr(int seg_reg) {
    uint8_t modrm = read8(m_seg_base[1] + m_eip);
    int mod = (modrm >> 6) & 3, rm = modrm & 7;
    m_eip++;
    if (mod == 3) return 0;
    uint32_t base = 0; int32_t disp = 0;
    if (m_addr_size == 32) {
        if (rm == 4) {
            uint8_t sib = read8(m_seg_base[1] + m_eip); m_eip++;
            int scale = (sib >> 6) & 3, idx = (sib >> 3) & 7, base_reg = sib & 7;
            base = ((uint32_t*)&m_eax)[base_reg];
            if (idx != 4) base += ((uint32_t*)&m_eax)[idx] << scale;
        } else {
            base = ((uint32_t*)&m_eax)[rm];
        }
        if (mod == 0) { if (rm == 5) { base = 0; disp = (int32_t)fetch32(); } }
        else if (mod == 1) { disp = (int8_t)fetch8(); }
        else { disp = (int32_t)fetch32(); }
    } else {
        switch (rm) {
            case 0: base = m_ebx + m_esi; break;
            case 1: base = m_ebx + m_edi; break;
            case 2: base = m_ebp + m_esi; break;
            case 3: base = m_ebp + m_edi; break;
            case 4: base = m_esi; break;
            case 5: base = m_edi; break;
            case 6: if (mod != 0) base = m_ebp; break;
            case 7: base = m_ebx; break;
        }
        base &= 0xFFFF;
        if (mod == 0) { if (rm == 6) disp = fetch16(); }
        else if (mod == 1) disp = (int8_t)fetch8();
        else disp = (int16_t)fetch16();
    }
    return base + disp + m_seg_base[seg_reg];
}

// Version for callers that already parsed mod/rm
uint32_t I386Core::modrm_addr_ea(int seg_reg, int mod, int rm) {
    if (mod == 3) return 0;
    uint32_t base = 0; int32_t disp = 0;
    if (m_addr_size == 32) {
        if (rm == 4) {
            uint8_t sib = read8(m_seg_base[1] + m_eip); m_eip++;
            int scale = (sib >> 6) & 3, idx = (sib >> 3) & 7, base_reg = sib & 7;
            base = ((uint32_t*)&m_eax)[base_reg];
            if (idx != 4) base += ((uint32_t*)&m_eax)[idx] << scale;
        } else {
            base = ((uint32_t*)&m_eax)[rm];
        }
        if (mod == 0) { if (rm == 5) { base = 0; disp = (int32_t)fetch32(); } }
        else if (mod == 1) { disp = (int8_t)fetch8(); }
        else { disp = (int32_t)fetch32(); }
    } else {
        switch (rm) {
            case 0: base = m_ebx + m_esi; break;
            case 1: base = m_ebx + m_edi; break;
            case 2: base = m_ebp + m_esi; break;
            case 3: base = m_ebp + m_edi; break;
            case 4: base = m_esi; break;
            case 5: base = m_edi; break;
            case 6: if (mod != 0) base = m_ebp; break;
            case 7: base = m_ebx; break;
        }
        base &= 0xFFFF;
        if (mod == 0) { if (rm == 6) disp = fetch16(); }
        else if (mod == 1) disp = (int8_t)fetch8();
        else disp = (int16_t)fetch16();
    }
    return base + disp + m_seg_base[seg_reg];
}

// I/O
uint8_t I386Core::io_read_byte(uint16_t port) { return m_io_read[port] ? m_io_read[port](port) : 0xFF; }
void I386Core::io_write_byte(uint16_t port, uint8_t data) { if (m_io_write[port]) m_io_write[port](port, data); }

uint8_t I386Core::_rm8(int rm) {
    // Register numbering: EAX=0, ECX=1, EDX=2, EBX=3, ESP=4, EBP=5, ESI=6, EDI=7
    // 8-bit registers: 0-3 = AL/CL/DL/BL (low byte), 4-7 = AH/CH/DH/BH (high byte)
    if (rm >= 4) return (uint8_t)(((uint32_t*)&m_eax)[rm - 4] >> 8);
    return ((uint8_t*)&m_eax)[rm * 4];
}
uint16_t I386Core::_rm16(int rm) { return ((uint16_t*)&m_eax)[rm * 2]; }
uint32_t I386Core::_rm32(int rm) { return ((uint32_t*)&m_eax)[rm]; }
void I386Core::_wm8(int rm, uint8_t v) {
    if (rm >= 4) {
        uint32_t& r = ((uint32_t*)&m_eax)[rm - 4];
        r = (r & 0xFFFF00FF) | ((uint32_t)v << 8);
    } else {
        ((uint8_t*)&m_eax)[rm * 4] = v;
    }
}
void I386Core::_wm16(int rm, uint16_t v) { ((uint16_t*)&m_eax)[rm * 2] = v; }
void I386Core::_wm32(int rm, uint32_t v) { ((uint32_t*)&m_eax)[rm] = v; }

int I386Core::execute(int max_cycles) {
    if (m_spinning) return 0;
    int cycles = 0;
    while (cycles < max_cycles && !m_spinning) {
        m_seg_prefix = -1; m_lock_prefix = false; m_rep_prefix = false;

        // Decode prefixes
        m_op_size = m_protected_mode ? 32 : 16;
        m_addr_size = m_protected_mode ? 32 : 16;

        while (true) {
            uint8_t p = read8(m_seg_base[1] + m_eip);
            if (p == 0x66) { m_op_size = (m_op_size == 32) ? 16 : 32; m_eip++; continue; }
            if (p == 0x67) { m_addr_size = (m_addr_size == 32) ? 16 : 32; m_eip++; continue; }
            if (p == 0x2E) { m_seg_prefix = 1; m_eip++; continue; }
            if (p == 0x3E) { m_seg_prefix = 0; m_eip++; continue; }
            if (p == 0x26) { m_seg_prefix = 3; m_eip++; continue; }
            if (p == 0x36) { m_seg_prefix = 2; m_eip++; continue; }
            if (p == 0x64) { m_seg_prefix = 4; m_eip++; continue; }
            if (p == 0x65) { m_seg_prefix = 5; m_eip++; continue; }
            if (p == 0xF0) { m_lock_prefix = true; m_eip++; continue; }
            if (p == 0xF2 || p == 0xF3) { m_rep_prefix = true; m_eip++; continue; }
            break;
        }
        cycles += decode_one();
    }
    return cycles;
}

// Forward declarations are handled by the switch-based dispatch.

// Include the opcode implementations from the generated file
// For now, inline decode_one as the main dispatcher
int I386Core::decode_one() {
    // make variables available to opcode helpers
    auto &s = *this;
    int cycles = 1;

    uint8_t op = fetch8();

    // Handlers with helper macro to avoid repetition
    // Most handlers are implemented inline in the switch for simplicity

    switch (op) {
    case 0x00: case 0x01: case 0x02: case 0x03: {
        // ADD rm, r / ADD r, rm
        bool to_rm = (op & 2) == 0;
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = (to_rm && !is_reg) ? modrm_addr_ea(seg, mod, rm) : 0;
        if (m_op_size == 32) {
            uint32_t v = _rm32(to_rm ? reg : rm);
            uint32_t dst = to_rm ? (is_reg ? _rm32(rm) : read32(maddr)) : v;
            uint32_t res = dst + v;
            uint64_t sum = (uint64_t)dst + v;
            m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF)) | ((sum >> 32) ? FLAG_CF : 0);
            int32_t sd = dst, sv = v, sr = res;
            if ((sd > 0 && sv > 0 && sr < 0) || (sd < 0 && sv < 0 && sr >= 0)) m_eflags |= FLAG_OF;
            set_sz32(res);
            if (to_rm) { if (is_reg) _wm32(rm, res); else write32(maddr, res); }
            else { _wm32(reg, res); }
        } else if (m_op_size == 16) {
            uint16_t v = _rm16(to_rm ? reg : rm);
            uint16_t dst = to_rm ? (is_reg ? _rm16(rm) : read16(maddr)) : v;
            uint16_t res = dst + v;
            uint32_t sum = (uint32_t)dst + v;
            m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF)) | ((sum >> 16) ? FLAG_CF : 0);
            set_sz16(res);
            if (to_rm) { if (is_reg) _wm16(rm, res); else write16(maddr, res); }
            else { _wm16(reg, res); }
        }
        cycles = 2;
        break;
    }
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x24: case 0x2C: case 0x34: case 0x3C: {
        // ALU AL, imm8 (always 8-bit)
        int alu_op = (op >> 3) & 7;
        uint8_t imm = fetch8();
        uint8_t al = m_eax & 0xFF;
        uint8_t res8 = 0;
        switch (alu_op) {
            case 0: res8 = al + imm; m_eflags = (m_eflags & ~FLAG_CF) | (((uint16_t)al + imm) >> 8); set_sz8(res8); m_eax = (m_eax & 0xFFFFFF00) | res8; break;
            case 1: res8 = al | imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz8(res8); m_eax = (m_eax & 0xFFFFFF00) | res8; break;
            case 4: res8 = al & imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz8(res8); m_eax = (m_eax & 0xFFFFFF00) | res8; break;
            case 5: res8 = al - imm; m_eflags = (m_eflags & ~FLAG_CF) | ((al < imm) ? FLAG_CF : 0); set_sz8(res8); m_eax = (m_eax & 0xFFFFFF00) | res8; break;
            case 6: res8 = al ^ imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz8(res8); m_eax = (m_eax & 0xFFFFFF00) | res8; break;
            case 7: res8 = al - imm; m_eflags = (m_eflags & ~FLAG_CF) | ((al < imm) ? FLAG_CF : 0); set_sz8(res8); break;
        }
        break;
    }
    case 0x05: case 0x0D: case 0x15: case 0x1D:
    case 0x25: case 0x2D: case 0x35: case 0x3D: {
        int alu_op = (op >> 3) & 7;
        uint32_t imm;
        if (m_op_size == 32) imm = fetch32();
        else imm = fetch16();
        uint32_t res = 0, a = m_eax;
        switch (alu_op) {
            case 0: res = a + imm; { uint64_t s = (uint64_t)a + imm; m_eflags = (m_eflags & ~FLAG_CF) | ((s >> 32) ? FLAG_CF : 0); m_eax = (uint32_t)res; set_sz32(res); } break;
            case 1: res = a | imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res); m_eax = res; break;
            case 4: res = a & imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res); m_eax = res; break;
            case 5: res = a - imm; m_eflags = (m_eflags & ~FLAG_CF) | ((a < imm) ? FLAG_CF : 0); m_eax = (uint32_t)res; set_sz32(res); break;
            case 6: res = a ^ imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res); m_eax = res; break;
            case 7: res = a - imm; m_eflags = (m_eflags & ~FLAG_CF) | ((a < imm) ? FLAG_CF : 0); set_sz32(res); break;
        }
        break;
    }
    case 0x06: if (m_op_size == 32) { m_esp -= 4; write32(m_esp, m_seg[3]); } else { m_esp -= 2; write16(m_esp, m_seg[3]); } break;
    case 0x07: if (m_op_size == 32) { m_seg[3] = read32(m_esp) & 0xFFFF; m_esp += 4; } else { m_seg[3] = read16(m_esp); m_esp += 2; } break;
    case 0x0E: if (m_op_size == 32) { m_esp -= 4; write32(m_esp, m_seg[1]); } else { m_esp -= 2; write16(m_esp, m_seg[1]); } break;
    case 0x16: if (m_op_size == 32) { m_esp -= 4; write32(m_esp, m_seg[2]); } else { m_esp -= 2; write16(m_esp, m_seg[2]); } break;
    case 0x17: if (m_op_size == 32) { m_seg[2] = read32(m_esp) & 0xFFFF; m_esp += 4; } else { m_seg[2] = read16(m_esp); m_esp += 2; } break;
    case 0x1E: if (m_op_size == 32) { m_esp -= 4; write32(m_esp, m_seg[0]); } else { m_esp -= 2; write16(m_esp, m_seg[0]); } break;
    case 0x1F: if (m_op_size == 32) { m_seg[0] = read32(m_esp) & 0xFFFF; m_esp += 4; } else { m_seg[0] = read16(m_esp); m_esp += 2; } break;

    case 0x08: case 0x09: case 0x0A: case 0x0B: {
        bool to_rm = (op & 2) == 0;
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = (to_rm && !is_reg) ? modrm_addr_ea(seg, mod, rm) : 0;
        if (m_op_size == 32) {
            uint32_t v = _rm32(to_rm ? reg : rm);
            uint32_t dst = to_rm ? (is_reg ? _rm32(rm) : read32(maddr)) : v;
            uint32_t res = dst | v;
            m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res);
            if (to_rm) { if (is_reg) _wm32(rm, res); else write32(maddr, res); }
            else { _wm32(reg, res); }
        } else if (m_op_size == 16) {
            uint16_t v = _rm16(to_rm ? reg : rm);
            uint16_t dst = to_rm ? (is_reg ? _rm16(rm) : read16(maddr)) : v;
            uint16_t res = dst | v;
            m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(res);
            if (to_rm) { if (is_reg) _wm16(rm, res); else write16(maddr, res); }
            else { _wm16(reg, res); }
        }
        cycles = 2; break;
    }

    case 0x20: case 0x21: case 0x22: case 0x23: {
        bool to_rm = (op & 2) == 0;
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = (to_rm && !is_reg) ? modrm_addr_ea(seg, mod, rm) : 0;
        if (m_op_size == 32) {
            uint32_t v = _rm32(to_rm ? reg : rm);
            uint32_t dst = to_rm ? (is_reg ? _rm32(rm) : read32(maddr)) : v;
            uint32_t res = dst & v;
            m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res);
            if (to_rm) { if (is_reg) _wm32(rm, res); else write32(maddr, res); }
            else { _wm32(reg, res); }
        } else if (m_op_size == 16) {
            uint16_t v = _rm16(to_rm ? reg : rm);
            uint16_t dst = to_rm ? (is_reg ? _rm16(rm) : read16(maddr)) : v;
            uint16_t res = dst & v;
            m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(res);
            if (to_rm) { if (is_reg) _wm16(rm, res); else write16(maddr, res); }
            else { _wm16(reg, res); }
        }
        cycles = 2; break;
    }

    case 0x28: case 0x29: case 0x2A: case 0x2B: {
        bool to_rm = (op & 2) == 0;
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = (to_rm && !is_reg) ? modrm_addr_ea(seg, mod, rm) : 0;
        if (m_op_size == 32) {
            uint32_t v = _rm32(to_rm ? reg : rm);
            uint32_t dst = to_rm ? (is_reg ? _rm32(rm) : read32(maddr)) : v;
            uint32_t res = dst - v;
            m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF)) | ((dst < v) ? FLAG_CF : 0);
            set_sz32(res);
            if (to_rm) { if (is_reg) _wm32(rm, res); else write32(maddr, res); }
            else { _wm32(reg, res); }
        } else if (m_op_size == 16) {
            uint16_t v = _rm16(to_rm ? reg : rm);
            uint16_t dst = to_rm ? (is_reg ? _rm16(rm) : read16(maddr)) : v;
            uint16_t res = dst - v;
            m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF)) | ((dst < v) ? FLAG_CF : 0);
            set_sz16(res);
            if (to_rm) { if (is_reg) _wm16(rm, res); else write16(maddr, res); }
            else { _wm16(reg, res); }
        }
        cycles = 2; break;
    }

    case 0x30: case 0x31: case 0x32: case 0x33: {
        bool to_rm = (op & 2) == 0;
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = (to_rm && !is_reg) ? modrm_addr_ea(seg, mod, rm) : 0;
        if (m_op_size == 32) {
            uint32_t v = _rm32(to_rm ? reg : rm);
            uint32_t dst = to_rm ? (is_reg ? _rm32(rm) : read32(maddr)) : v;
            uint32_t res = dst ^ v;
            m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res);
            if (to_rm) { if (is_reg) _wm32(rm, res); else write32(maddr, res); }
            else { _wm32(reg, res); }
        } else if (m_op_size == 16) {
            uint16_t v = _rm16(to_rm ? reg : rm);
            uint16_t dst = to_rm ? (is_reg ? _rm16(rm) : read16(maddr)) : v;
            uint16_t res = dst ^ v;
            m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(res);
            if (to_rm) { if (is_reg) _wm16(rm, res); else write16(maddr, res); }
            else { _wm16(reg, res); }
        }
        cycles = 2; break;
    }

    case 0x38: case 0x39: case 0x3A: case 0x3B: {
        bool to_rm = (op & 2) == 0;
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = !is_reg ? modrm_addr_ea(seg, mod, rm) : 0;
        if (m_op_size == 32) {
            uint32_t v = _rm32(reg);
            uint32_t dst = is_reg ? _rm32(rm) : read32(maddr);
            m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF)) | ((dst < v) ? FLAG_CF : 0);
            set_sz32(dst - v);
        } else if (m_op_size == 16) {
            uint16_t v = _rm16(reg);
            uint16_t dst = is_reg ? _rm16(rm) : read16(maddr);
            m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF|FLAG_ZF|FLAG_SF|FLAG_PF)) | ((dst < v) ? FLAG_CF : 0);
            set_sz16(dst - v);
        }
        cycles = 2; break;
    }

    case 0x40 ... 0x47: { int r = op & 7; if (m_op_size == 32) { uint32_t v = _rm32(r) + 1; _wm32(r, v); set_sz32(v); } else if (m_op_size == 16) { uint16_t v = _rm16(r) + 1; _wm16(r, v); set_sz16(v); } else { uint8_t v = _rm8(r) + 1; _wm8(r, v); set_sz8(v); } } break;
    case 0x48 ... 0x4F: { int r = op & 7; if (m_op_size == 32) { uint32_t v = _rm32(r) - 1; _wm32(r, v); set_sz32(v); } else if (m_op_size == 16) { uint16_t v = _rm16(r) - 1; _wm16(r, v); set_sz16(v); } else { uint8_t v = _rm8(r) - 1; _wm8(r, v); set_sz8(v); } } break;

    case 0x50 ... 0x57: { int r = op & 7; if (m_op_size == 32) { m_esp -= 4; write32(m_esp, _rm32(r)); } else { m_esp -= 2; write16(m_esp, _rm16(r)); } } break;
    case 0x58 ... 0x5F: { int r = op & 7; if (m_op_size == 32) { _wm32(r, read32(m_esp)); m_esp += 4; } else { _wm16(r, read16(m_esp)); m_esp += 2; } } break;

    case 0x60: {
        if (m_op_size == 32) {
            uint32_t old_esp = m_esp;
            m_esp -= 4; write32(m_esp, m_eax); m_esp -= 4; write32(m_esp, m_ecx);
            m_esp -= 4; write32(m_esp, m_edx); m_esp -= 4; write32(m_esp, m_ebx);
            m_esp -= 4; write32(m_esp, old_esp); m_esp -= 4; write32(m_esp, m_ebp);
            m_esp -= 4; write32(m_esp, m_esi); m_esp -= 4; write32(m_esp, m_edi);
        } else {
            uint16_t old_sp = m_esp;
            m_esp -= 2; write16(m_esp, m_eax); m_esp -= 2; write16(m_esp, m_ecx);
            m_esp -= 2; write16(m_esp, m_edx); m_esp -= 2; write16(m_esp, m_ebx);
            m_esp -= 2; write16(m_esp, old_sp); m_esp -= 2; write16(m_esp, m_ebp);
            m_esp -= 2; write16(m_esp, m_esi); m_esp -= 2; write16(m_esp, m_edi);
        }
        cycles = 2; break;
    }
    case 0x61: {
        if (m_op_size == 32) {
            m_edi = read32(m_esp); m_esp += 4; m_esi = read32(m_esp); m_esp += 4;
            m_ebp = read32(m_esp); m_esp += 4; m_esp += 4;
            m_ebx = read32(m_esp); m_esp += 4; m_edx = read32(m_esp); m_esp += 4;
            m_ecx = read32(m_esp); m_esp += 4; m_eax = read32(m_esp); m_esp += 4;
        } else {
            m_edi = read16(m_esp); m_esp += 2; m_esi = read16(m_esp); m_esp += 2;
            m_ebp = read16(m_esp); m_esp += 2; m_esp += 2;
            m_ebx = read16(m_esp); m_esp += 2; m_edx = read16(m_esp); m_esp += 2;
            m_ecx = read16(m_esp); m_esp += 2; m_eax = read16(m_esp); m_esp += 2;
        }
        cycles = 2; break;
    }

    case 0x68: {
        if (m_op_size == 32) { uint32_t v = fetch32(); m_esp -= 4; write32(m_esp, v); }
        else { uint16_t v = fetch16(); m_esp -= 2; write16(m_esp, v); }
        break;
    }
    case 0x6A: {
        int8_t v = fetch8();
        if (m_op_size == 32) { m_esp -= 4; write32(m_esp, (int32_t)v); }
        else { m_esp -= 2; write16(m_esp, (int16_t)v); }
        break;
    }

    case 0x70 ... 0x7F: {
        int8_t disp = fetch8();
        bool taken = false;
        switch (op & 0xF) {
            case 0: taken = (m_eflags & FLAG_OF); break;
            case 1: taken = !(m_eflags & FLAG_OF); break;
            case 2: taken = (m_eflags & FLAG_CF); break;
            case 3: taken = !(m_eflags & FLAG_CF); break;
            case 4: taken = (m_eflags & FLAG_ZF); break;
            case 5: taken = !(m_eflags & FLAG_ZF); break;
            case 6: taken = (m_eflags & (FLAG_CF|FLAG_ZF)); break;
            case 7: taken = !(m_eflags & (FLAG_CF|FLAG_ZF)); break;
            case 8: taken = (m_eflags & FLAG_SF); break;
            case 9: taken = !(m_eflags & FLAG_SF); break;
            case 0xA: taken = (m_eflags & FLAG_PF); break;
            case 0xB: taken = !(m_eflags & FLAG_PF); break;
            case 0xC: taken = ((m_eflags & FLAG_SF) != 0) != ((m_eflags & FLAG_OF) != 0); break;
            case 0xD: taken = ((m_eflags & FLAG_SF) != 0) == ((m_eflags & FLAG_OF) != 0); break;
            case 0xE: taken = (m_eflags & FLAG_ZF) || ((m_eflags & FLAG_SF) != (m_eflags & FLAG_OF)); break;
            case 0xF: taken = !(m_eflags & FLAG_ZF) && ((m_eflags & FLAG_SF) == (m_eflags & FLAG_OF)); break;
        }
        if (taken) m_eip += disp;
        cycles = 2; break;
    }

    case 0x80: case 0x81: case 0x82: case 0x83: {
        // 0x80/0x82: ALU r/m8, imm8
        // 0x81:      ALU r/m16/32, imm16/32
        // 0x83:      ALU r/m16/32, imm8 (sign-extended)
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = !is_reg ? modrm_addr_ea(seg, mod, rm) : 0;

        if (op == 0x80 || op == 0x82) {
            // 8-bit ALU: operand size is always 1 byte
            int8_t imm = fetch8();
            uint8_t v = is_reg ? _rm8(rm) : read8(maddr);
            uint8_t res = 0;
            switch (reg) {
            case 0: { // ADD
                uint16_t s = (uint16_t)v + (uint8_t)imm;
                res = (uint8_t)s;
                m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF)) | ((s >> 8) ? FLAG_CF : 0);
                if (((v & 0x80) == ((uint8_t)imm & 0x80)) && ((res & 0x80) != (v & 0x80))) m_eflags |= FLAG_OF;
                set_sz8(res);
                break;
            }
            case 1: res = v | (uint8_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz8(res); break;
            case 2: { // ADC
                uint32_t cf = (m_eflags & FLAG_CF) ? 1 : 0;
                uint32_t s = (uint32_t)v + (uint8_t)imm + cf;
                res = (uint8_t)s;
                m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF)) | ((s >> 8) ? FLAG_CF : 0);
                set_sz8(res);
                break;
            }
            case 3: { // SBB
                uint32_t cf = (m_eflags & FLAG_CF) ? 1 : 0;
                int32_t s = (int32_t)v - (int8_t)imm - (int32_t)cf;
                res = (uint8_t)s;
                m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0);
                set_sz8(res);
                break;
            }
            case 4: res = v & (uint8_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz8(res); break;
            case 5: { // SUB
                int32_t s = (int32_t)v - (int8_t)imm;
                res = (uint8_t)s;
                m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0);
                set_sz8(res);
                break;
            }
            case 6: res = v ^ (uint8_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz8(res); break;
            case 7: { // CMP
                int32_t s = (int32_t)v - (int8_t)imm;
                m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0);
                set_sz8((uint8_t)s);
                break;
            }
            }
            if (reg != 7) { if (is_reg) _wm8(rm, res); else write8(maddr, res); }
            cycles = 3; break;
        }

        if (op == 0x83) {
            // 16/32-bit ALU with sign-extended imm8
            int32_t imm = (int8_t)fetch8();
            if (m_op_size == 32) {
                uint32_t v = is_reg ? _rm32(rm) : read32(maddr);
                uint32_t res = 0;
                switch (reg) {
                case 0: { uint64_t s = (uint64_t)v + (uint32_t)imm; res = v + imm; m_eflags = (m_eflags & ~FLAG_CF) | ((s >> 32) ? FLAG_CF : 0); set_sz32(res); } break;
                case 1: res = v | (uint32_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res); break;
                case 2: { uint64_t s = (uint64_t)v + (uint32_t)imm + ((m_eflags & FLAG_CF) ? 1 : 0); res = v + imm + ((m_eflags & FLAG_CF) ? 1 : 0); m_eflags = (m_eflags & ~FLAG_CF) | ((s >> 32) ? FLAG_CF : 0); set_sz32(res); } break;
                case 3: { int64_t s = (int64_t)(int32_t)v - (int32_t)imm - ((m_eflags & FLAG_CF) ? 1 : 0); res = (uint32_t)s; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz32(res); } break;
                case 4: res = v & (uint32_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res); break;
                case 5: { int64_t s = (int64_t)(int32_t)v - (int32_t)imm; res = (uint32_t)s; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz32(res); } break;
                case 6: res = v ^ (uint32_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res); break;
                case 7: { int64_t s = (int64_t)(int32_t)v - (int32_t)imm; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz32((uint32_t)s); } break;
                }
                if (reg != 7) { if (is_reg) _wm32(rm, res); else write32(maddr, res); }
            } else {
                uint16_t v = is_reg ? _rm16(rm) : read16(maddr);
                uint16_t res = 0;
                switch (reg) {
                case 0: { uint32_t s = (uint32_t)v + (uint32_t)(int16_t)imm; res = v + (int16_t)imm; m_eflags = (m_eflags & ~FLAG_CF) | ((s >> 16) ? FLAG_CF : 0); set_sz16(res); } break;
                case 1: res = v | (uint16_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(res); break;
                case 2: { uint32_t s = (uint32_t)v + (uint32_t)(int16_t)imm + ((m_eflags & FLAG_CF) ? 1 : 0); res = v + (int16_t)imm + ((m_eflags & FLAG_CF) ? 1 : 0); m_eflags = (m_eflags & ~FLAG_CF) | ((s >> 16) ? FLAG_CF : 0); set_sz16(res); } break;
                case 3: { int32_t s = (int32_t)(int16_t)v - (int32_t)(int16_t)imm - ((m_eflags & FLAG_CF) ? 1 : 0); res = (uint16_t)s; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz16(res); } break;
                case 4: res = v & (uint16_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(res); break;
                case 5: { int32_t s = (int32_t)(int16_t)v - (int32_t)(int16_t)imm; res = (uint16_t)s; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz16(res); } break;
                case 6: res = v ^ (uint16_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(res); break;
                case 7: { int32_t s = (int32_t)(int16_t)v - (int32_t)(int16_t)imm; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz16((uint16_t)s); } break;
                }
                if (reg != 7) { if (is_reg) _wm16(rm, res); else write16(maddr, res); }
            }
            cycles = 3; break;
        }

        // 0x81: ALU r/m16/32, imm16/32
        if (m_op_size == 32) {
            int32_t imm = fetch32();
            uint32_t v = is_reg ? _rm32(rm) : read32(maddr);
            uint32_t res = 0;
            switch (reg) {
            case 0: { uint64_t s = (uint64_t)v + (uint32_t)imm; res = v + imm; m_eflags = (m_eflags & ~FLAG_CF) | ((s >> 32) ? FLAG_CF : 0); set_sz32(res); } break;
            case 1: res = v | (uint32_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res); break;
            case 2: { uint64_t s = (uint64_t)v + (uint32_t)imm + ((m_eflags & FLAG_CF) ? 1 : 0); res = v + imm + ((m_eflags & FLAG_CF) ? 1 : 0); m_eflags = (m_eflags & ~FLAG_CF) | ((s >> 32) ? FLAG_CF : 0); set_sz32(res); } break;
            case 3: { int64_t s = (int64_t)(int32_t)v - (int32_t)imm - ((m_eflags & FLAG_CF) ? 1 : 0); res = (uint32_t)s; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz32(res); } break;
            case 4: res = v & (uint32_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res); break;
            case 5: { int64_t s = (int64_t)(int32_t)v - (int32_t)imm; res = (uint32_t)s; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz32(res); } break;
            case 6: res = v ^ (uint32_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(res); break;
            case 7: { int64_t s = (int64_t)(int32_t)v - (int32_t)imm; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz32((uint32_t)s); } break;
            }
            if (reg != 7) { if (is_reg) _wm32(rm, res); else write32(maddr, res); }
        } else {
            int16_t imm = fetch16();
            uint16_t v = is_reg ? _rm16(rm) : read16(maddr);
            uint16_t res = 0;
            switch (reg) {
            case 0: { uint32_t s = (uint32_t)v + (uint16_t)imm; res = v + imm; m_eflags = (m_eflags & ~FLAG_CF) | ((s >> 16) ? FLAG_CF : 0); set_sz16(res); } break;
            case 1: res = v | (uint16_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(res); break;
            case 2: { uint32_t s = (uint32_t)v + (uint16_t)imm + ((m_eflags & FLAG_CF) ? 1 : 0); res = v + imm + ((m_eflags & FLAG_CF) ? 1 : 0); m_eflags = (m_eflags & ~FLAG_CF) | ((s >> 16) ? FLAG_CF : 0); set_sz16(res); } break;
            case 3: { int32_t s = (int32_t)(int16_t)v - (int32_t)(int16_t)imm - ((m_eflags & FLAG_CF) ? 1 : 0); res = (uint16_t)s; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz16(res); } break;
            case 4: res = v & (uint16_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(res); break;
            case 5: { int32_t s = (int32_t)(int16_t)v - (int32_t)(int16_t)imm; res = (uint16_t)s; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz16(res); } break;
            case 6: res = v ^ (uint16_t)imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(res); break;
            case 7: { int32_t s = (int32_t)(int16_t)v - (int32_t)(int16_t)imm; m_eflags = (m_eflags & ~FLAG_CF) | (s < 0 ? FLAG_CF : 0); set_sz16((uint16_t)s); } break;
            }
            if (reg != 7) { if (is_reg) _wm16(rm, res); else write16(maddr, res); }
        }
        cycles = 3; break;
    }

    case 0x84: case 0x85: {
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = !is_reg ? modrm_addr_ea(seg, mod, rm) : 0;
        if (m_op_size == 32) {
            uint32_t r = _rm32(reg) & (is_reg ? _rm32(rm) : read32(maddr));
            m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(r);
        } else if (m_op_size == 16) {
            uint16_t r = _rm16(reg) & (is_reg ? _rm16(rm) : read16(maddr));
            m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz16(r);
        }
        cycles = 2; break;
    }

    case 0x86: case 0x87: {
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = is_reg ? 0 : modrm_addr_ea(seg, mod, rm);
        if (m_op_size == 32) {
            uint32_t v1 = _rm32(reg);
            uint32_t v2 = is_reg ? _rm32(rm) : read32(maddr);
            if (is_reg) _wm32(rm, v1); else write32(maddr, v1);
            _wm32(reg, v2);
        } else if (m_op_size == 16) {
            uint16_t v1 = _rm16(reg);
            uint16_t v2 = is_reg ? _rm16(rm) : read16(maddr);
            if (is_reg) _wm16(rm, v1); else write16(maddr, v1);
            _wm16(reg, v2);
        }
        cycles = 3; break;
    }

    case 0x88: case 0x89: case 0x8A: case 0x8B: {
        bool to_rm = (op & 1) == 0;
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = (to_rm && !is_reg) ? modrm_addr_ea(seg, mod, rm) : 0;
        if (m_op_size == 32) {
            uint32_t v = _rm32(to_rm ? reg : rm);
            if (to_rm) { if (is_reg) _wm32(rm, v); else write32(maddr, v); }
            else { _wm32(reg, is_reg ? _rm32(rm) : read32(modrm_addr_ea(seg, mod, rm))); }
        } else if (m_op_size == 16) {
            uint16_t v = _rm16(to_rm ? reg : rm);
            if (to_rm) { if (is_reg) _wm16(rm, v); else write16(maddr, v); }
            else { _wm16(reg, is_reg ? _rm16(rm) : read16(modrm_addr_ea(seg, mod, rm))); }
        } else {
            uint8_t v = _rm8(to_rm ? reg : rm);
            if (to_rm) { if (is_reg) _wm8(rm, v); else write8(modrm_addr_ea(seg, mod, rm), v); }
            else { _wm8(reg, is_reg ? _rm8(rm) : read8(modrm_addr_ea(seg, mod, rm))); }
        }
        cycles = 2; break;
    }

    case 0x8C: {
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, sreg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        if (mod == 3) _wm16(rm, m_seg[sreg]);
        else write16(modrm_addr_ea(seg, mod, rm), m_seg[sreg]);
        break;
    }
    case 0x8D: {
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        if (mod != 3) {
            uint32_t addr = modrm_addr_ea(seg, mod, rm) - m_seg_base[seg];
            if (m_op_size == 32) _wm32(reg, addr);
            else _wm16(reg, addr & 0xFFFF);
        }
        cycles = 2; break;
    }
    case 0x8E: {
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        uint16_t v;
        if (mod == 3) v = _rm16(rm);
        else v = read16(modrm_addr_ea(seg, mod, rm));
        m_seg[reg] = v;
        if (m_protected_mode) {
            // Read segment descriptor from GDT to get base
            if ((v & 0xFFFC) != 0) {
                int idx = v >> 3;
                if (idx * 8 + 7 <= m_gdtr_limit) {
                    uint32_t dl = read32(m_gdtr_base + idx * 8);
                    uint32_t dh = read32(m_gdtr_base + idx * 8 + 4);
                    m_seg_base[reg] = (dl >> 16) | ((dh & 0xFF) << 16) | (dh & 0xFF000000);
                }
            }
        } else {
            m_seg_base[reg] = (uint32_t)v << 4;
        }
        break;
    }

    case 0x8F: {
        // POP r/m16/32
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        if (m_op_size == 32) {
            uint32_t v = read32(m_esp); m_esp += 4;
            if (is_reg) _wm32(rm, v); else write32(modrm_addr_ea(seg, mod, rm), v);
        } else {
            uint16_t v = read16(m_esp); m_esp += 2;
            if (is_reg) _wm16(rm, v); else write16(modrm_addr_ea(seg, mod, rm), v);
        }
        cycles = 4; break;
    }

    case 0x90: break; // NOP
    case 0x91 ... 0x97: {
        int r = op & 7;
        if (m_op_size == 32) { uint32_t t = _rm32(r); _wm32(r, m_eax); m_eax = t; }
        else { uint16_t t = _rm16(r); _wm16(r, m_eax & 0xFFFF); m_eax = (m_eax & 0xFFFF0000) | t; }
        cycles = 2; break;
    }
    case 0x98: { if (m_op_size == 32) m_eax = (int16_t)(m_eax & 0xFFFF); else m_eax = (int8_t)(m_eax & 0xFF); } break;
    case 0x99: { if (m_op_size == 32) m_edx = (m_eax & 0x80000000) ? 0xFFFFFFFF : 0; else m_edx = (m_edx & 0xFFFF0000) | ((m_eax & 0x8000) ? 0xFFFF : 0); } break;
    case 0x9C: { m_esp -= 4; write32(m_esp, get_flags()); } break;
    case 0x9D: { set_flags(read32(m_esp)); m_esp += 4; } break;
    case 0x9E: { m_eflags = (m_eflags & 0xFFFFFF00) | (m_eax & 0xFF) | 0x2; } break;
    case 0x9F: { m_eax = (m_eax & 0xFFFFFF00) | (m_eflags & 0xFF); } break;

    case 0xA0: case 0xA1: {
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        if (m_addr_size == 32) { uint32_t addr = fetch32(); if (m_op_size == 32) m_eax = read32(addr + m_seg_base[seg]); else if (m_op_size == 16) m_eax = (m_eax & 0xFFFF0000) | read16(addr + m_seg_base[seg]); else m_eax = (m_eax & 0xFFFFFF00) | read8(addr + m_seg_base[seg]); }
        else { uint16_t addr = fetch16(); if (m_op_size == 32) m_eax = read32(addr + m_seg_base[seg]); else if (m_op_size == 16) m_eax = (m_eax & 0xFFFF0000) | read16(addr + m_seg_base[seg]); else m_eax = (m_eax & 0xFFFFFF00) | read8(addr + m_seg_base[seg]); }
        break;
    }
    case 0xA2: case 0xA3: {
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        if (m_addr_size == 32) { uint32_t addr = fetch32(); if (m_op_size == 32) write32(addr + m_seg_base[seg], m_eax); else if (m_op_size == 16) write16(addr + m_seg_base[seg], m_eax & 0xFFFF); else write8(addr + m_seg_base[seg], m_eax & 0xFF); }
        else { uint16_t addr = fetch16(); if (m_op_size == 32) write32(addr + m_seg_base[seg], m_eax); else if (m_op_size == 16) write16(addr + m_seg_base[seg], m_eax & 0xFFFF); else write8(addr + m_seg_base[seg], m_eax & 0xFF); }
        break;
    }
    case 0xA4: case 0xA5: {
        // 0xA4 = MOVSB (always 1 byte), 0xA5 = MOVSW/MOVSD (2/4 bytes)
        int sz = (op == 0xA4) ? 1 : ((m_op_size == 32) ? 4 : 2);
        if (!m_rep_prefix || m_ecx != 0) {
            do {
                if (sz == 4) { uint32_t v = read32_seg((m_seg_prefix>=0)?m_seg_prefix:0, m_esi); write32_seg(3, m_edi, v); }
                else if (sz == 2) { uint16_t v = read16_seg((m_seg_prefix>=0)?m_seg_prefix:0, m_esi); write16_seg(3, m_edi, v); }
                else { uint8_t v = read8_seg((m_seg_prefix>=0)?m_seg_prefix:0, m_esi); write8_seg(3, m_edi, v); }
                if (m_eflags & FLAG_DF) { m_esi -= sz; m_edi -= sz; } else { m_esi += sz; m_edi += sz; }
                if (m_rep_prefix) --m_ecx;
            } while (m_rep_prefix && m_ecx != 0);
        }
        m_rep_prefix = false; cycles = 3; break;
    }
    case 0xAA: case 0xAB: {
        // 0xAA = STOSB (always 1 byte), 0xAB = STOSW/STOSD (2/4 bytes)
        int sz = (op == 0xAA) ? 1 : ((m_op_size == 32) ? 4 : 2);
        if (!m_rep_prefix || m_ecx != 0) {
            do {
                if (sz == 4) write32_seg(3, m_edi, m_eax);
                else if (sz == 2) write16_seg(3, m_edi, m_eax & 0xFFFF);
                else write8_seg(3, m_edi, m_eax & 0xFF);
                if (m_eflags & FLAG_DF) m_edi -= sz; else m_edi += sz;
                if (m_rep_prefix) --m_ecx;
            } while (m_rep_prefix && m_ecx != 0);
        }
        m_rep_prefix = false; cycles = 3; break;
    }
    case 0xAC: case 0xAD: {
        // 0xAC = LODSB (always 1 byte), 0xAD = LODSW/LODSD (2/4 bytes)
        int sz = (op == 0xAC) ? 1 : ((m_op_size == 32) ? 4 : 2);
        if (!m_rep_prefix || m_ecx != 0) {
            do {
                if (sz == 4) m_eax = read32_seg((m_seg_prefix>=0)?m_seg_prefix:0, m_esi);
                else if (sz == 2) m_eax = (m_eax & 0xFFFF0000) | read16_seg((m_seg_prefix>=0)?m_seg_prefix:0, m_esi);
                else m_eax = (m_eax & 0xFFFFFF00) | read8_seg((m_seg_prefix>=0)?m_seg_prefix:0, m_esi);
                if (m_eflags & FLAG_DF) m_esi -= sz; else m_esi += sz;
                if (m_rep_prefix) --m_ecx;
            } while (m_rep_prefix && m_ecx != 0);
        }
        m_rep_prefix = false; cycles = 3; break;
    }

    case 0xB0 ... 0xB7: { uint8_t v = fetch8(); _wm8(op & 7, v); } break;
    case 0xB8 ... 0xBF: { int r = op & 7; if (m_op_size == 32) { _wm32(r, fetch32()); } else { _wm16(r, fetch16()); } } break;

    case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
        // Shift/rotate
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = !is_reg ? modrm_addr_ea(seg, mod, rm) : 0;
        int count;
        if (op == 0xD0 || op == 0xD1) count = 1;
        else if (op == 0xD2 || op == 0xD3) count = m_ecx & 0xFF;
        else count = fetch8();
        if (count == 0) break;
        if (m_op_size == 32) {
            uint32_t v = is_reg ? _rm32(rm) : read32(maddr);
            uint32_t res = v;
            if (reg == 0) { res = (v << count) | (v >> (32 - count)); m_eflags = (m_eflags & ~FLAG_CF) | ((v >> (32 - count)) & 1); } // ROL
            else if (reg == 1) { res = (v >> count) | (v << (32 - count)); m_eflags = (m_eflags & ~FLAG_CF) | ((v >> (count - 1)) & 1); } // ROR
            else if (reg == 4) { res = v << count; m_eflags = (m_eflags & ~FLAG_CF) | ((v >> (32 - count)) & 1); } // SHL
            else if (reg == 5) { res = (int32_t)v >> count; m_eflags = (m_eflags & ~FLAG_CF) | ((v >> (count - 1)) & 1); } // SAR
            else if (reg == 7) { res = v >> count; m_eflags = (m_eflags & ~FLAG_CF) | ((v >> (count - 1)) & 1); } // SHR
            if (is_reg) _wm32(rm, res); else write32(maddr, res);
        } else if (m_op_size == 16) {
            uint16_t v = is_reg ? _rm16(rm) : read16(maddr);
            uint16_t res = v;
            if (reg == 0) { res = (v << count) | (v >> (16 - count)); m_eflags = (m_eflags & ~FLAG_CF) | ((v >> (16 - count)) & 1); }
            else if (reg == 4) { res = v << count; m_eflags = (m_eflags & ~FLAG_CF) | ((v >> (16 - count)) & 1); }
            else if (reg == 5) { res = (int16_t)v >> count; m_eflags = (m_eflags & ~FLAG_CF) | ((v >> (count - 1)) & 1); }
            else if (reg == 7) { res = v >> count; m_eflags = (m_eflags & ~FLAG_CF) | ((v >> (count - 1)) & 1); }
            if (is_reg) _wm16(rm, res); else write16(maddr, res);
        }
        cycles = 2; break;
    }

    case 0xC2: { uint16_t pop = fetch16(); m_eip = read32(m_esp); m_esp += 4 + pop; cycles = 4; break; }
    case 0xC3: { m_eip = read32(m_esp); m_esp += 4; cycles = 4; break; }

    case 0xC6: case 0xC7: {
        // 0xC6 = MOV r/m8, imm8 (always 8-bit); 0xC7 = MOV r/m16/32, imm16/32
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        if (op == 0xC6) {
            uint32_t a = is_reg ? 0 : modrm_addr_ea(seg, mod, rm);
            uint8_t imm = fetch8();
            if (is_reg) _wm8(rm, imm);
            else write8(a, imm);
        } else if (m_op_size == 32) {
            uint32_t a = modrm_addr_ea(seg, mod, rm);
            uint32_t imm = fetch32();
            if (is_reg) _wm32(rm, imm);
            else write32(a, imm);
        } else {
            uint16_t a = modrm_addr_ea(seg, mod, rm);
            uint16_t imm = fetch16();
            if (is_reg) _wm16(rm, imm);
            else write16(a, imm);
        }
        break;
    }

    case 0xC9: { m_esp = m_ebp; m_ebp = read32(m_esp); m_esp += 4; cycles = 2; break; }

    case 0xCC: signal_interrupt(3); cycles = 4; break;
    case 0xCD: { uint8_t v = fetch8(); signal_interrupt(v); cycles = 4; } break;

    case 0xD7: {
        uint8_t v = read8_seg((m_seg_prefix>=0)?m_seg_prefix:0, m_ebx + (m_eax & 0xFF));
        m_eax = (m_eax & 0xFFFFFF00) | v;
        break;
    }

    case 0xE0: { int8_t d = fetch8(); m_ecx--; if (m_ecx != 0 && !(m_eflags & FLAG_ZF)) m_eip += d; cycles = 2; break; }
    case 0xE1: { int8_t d = fetch8(); m_ecx--; if (m_ecx != 0 && (m_eflags & FLAG_ZF)) m_eip += d; cycles = 2; break; }
    case 0xE2: { int8_t d = fetch8(); m_ecx--; if (m_ecx != 0) m_eip += d; cycles = 2; break; }
    case 0xE3: { int8_t d = fetch8(); if ((m_addr_size==32 && m_ecx==0) || (m_addr_size==16 && (m_ecx&0xFFFF)==0)) m_eip += d; cycles = 2; break; }

    case 0xE4: { uint8_t p = fetch8(); m_eax = (m_eax & 0xFFFFFF00) | io_read_byte(p); cycles = 2; break; }
    case 0xE5: { uint8_t p = fetch8(); if (m_op_size == 32) m_eax = io_read_byte(p)|(io_read_byte(p+1)<<8)|(io_read_byte(p+2)<<16)|(io_read_byte(p+3)<<24); else m_eax = (m_eax & 0xFFFF0000) | io_read_byte(p) | (io_read_byte(p+1)<<8); cycles = 2; break; }
    case 0xE6: { uint8_t p = fetch8(); io_write_byte(p, m_eax & 0xFF); cycles = 2; break; }
    case 0xE7: { uint8_t p = fetch8(); for (int i = 0; i < (m_op_size == 32 ? 4 : 2); i++) io_write_byte(p + i, (m_eax >> (i*8)) & 0xFF); cycles = 2; break; }

    case 0xE8: {
        uint32_t target;
        if (m_op_size == 32) target = m_eip + (int32_t)fetch32();
        else target = m_eip + (int16_t)fetch16();
        if (m_op_size == 32) { m_esp -= 4; write32(m_esp, m_eip); }
        else { m_esp -= 2; write16(m_esp, m_eip & 0xFFFF); }
        m_eip = target;
        cycles = 3; break;
    }
    case 0xE9: {
        if (m_op_size == 32) m_eip += (int32_t)fetch32();
        else m_eip += (int16_t)fetch16();
        cycles = 2; break;
    }
    case 0xEA: {
        uint32_t off; uint16_t seg;
        if (m_op_size == 32) { off = fetch32(); seg = fetch16(); }
        else { off = fetch16(); seg = fetch16(); }
        if (m_protected_mode) {
            int idx = (seg >> 3);
            if (idx > 0 && idx * 8 + 7 <= m_gdtr_limit) {
                uint32_t dl = read32(m_gdtr_base + idx * 8);
                uint32_t dh = read32(m_gdtr_base + idx * 8 + 4);
                uint32_t base = (dl >> 16) | ((dh & 0xFF) << 16) | (dh & 0xFF000000);
                m_seg[1] = seg; m_seg_base[1] = base;
                if (dh & 0x00400000) m_op_size = 32; else m_op_size = 16;
                m_eip = off;
            }
        } else {
            m_eip = off; m_seg[1] = seg; m_seg_base[1] = (uint32_t)seg << 4;
        }
        cycles = 3; break;
    }
    case 0xEB: { m_eip += (int8_t)fetch8(); cycles = 2; break; }

    case 0xEC: { uint16_t p = m_edx & 0xFFFF; m_eax = (m_eax & 0xFFFFFF00) | io_read_byte(p); cycles = 2; break; }
    case 0xED: { uint16_t p = m_edx & 0xFFFF; if (m_op_size==32) m_eax=io_read_byte(p)|(io_read_byte(p+1)<<8)|(io_read_byte(p+2)<<16)|(io_read_byte(p+3)<<24); else m_eax=(m_eax&0xFFFF0000)|io_read_byte(p)|(io_read_byte(p+1)<<8); cycles=2; break; }
    case 0xEE: { io_write_byte(m_edx & 0xFFFF, m_eax & 0xFF); cycles = 2; break; }
    case 0xEF: { uint16_t p = m_edx & 0xFFFF; for (int i = 0; i < (m_op_size==32?4:2); i++) io_write_byte(p+i, (m_eax>>(i*8))&0xFF); cycles = 2; break; }

    case 0xF4: m_halted = true; m_spinning = true; break;
    case 0xF5: m_eflags ^= FLAG_CF; break;
    case 0xF6: case 0xF7: {
        uint8_t modrm = modrm_byte();
        int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
        int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
        bool is_reg = (mod == 3);
        uint32_t maddr = !is_reg ? modrm_addr_ea(seg, mod, rm) : 0;
        if (reg == 0) { // TEST
            if (m_op_size == 32) {
                uint32_t imm = fetch32();
                uint32_t v = is_reg ? _rm32(rm) : read32(maddr);
                uint32_t r = v & imm; m_eflags &= ~(FLAG_CF|FLAG_OF); set_sz32(r);
            }
            break;
        }
        if (reg == 3) { // MUL
            if (m_op_size == 32) {
                uint32_t v = is_reg ? _rm32(rm) : read32(maddr);
                uint64_t r = (uint64_t)(uint32_t)m_eax * v;
                m_eax = r & 0xFFFFFFFF; m_edx = r >> 32;
                m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF)) | ((m_edx) ? FLAG_CF|FLAG_OF : 0);
            } else if (m_op_size == 16) {
                uint16_t v = is_reg ? _rm16(rm) : read16(maddr);
                uint32_t r = (uint32_t)(m_eax & 0xFFFF) * v;
                m_eax = (m_eax & 0xFFFF0000) | (r & 0xFFFF);
                m_edx = (m_edx & 0xFFFF0000) | ((r >> 16) & 0xFFFF);
                m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF)) | ((r & 0xFFFF0000) ? FLAG_CF|FLAG_OF : 0);
            }
            cycles = 4; break;
        }
        if (reg == 5) { // DIV
            if (m_op_size == 32) {
                uint32_t v = is_reg ? _rm32(rm) : read32(maddr);
                if (v) { uint64_t n = ((uint64_t)m_edx << 32) | m_eax; m_eax = n / v; m_edx = n % v; }
            }
            cycles = 4; break;
        }
        if (reg == 2) { // NOT
            if (m_op_size == 32) { uint32_t v = is_reg ? _rm32(rm) : read32(maddr); if (is_reg) _wm32(rm, ~v); else write32(maddr, ~v); }
            break;
        }
        if (reg == 1) { // NEG
            if (m_op_size == 32) { uint32_t v = is_reg ? _rm32(rm) : read32(maddr); if (is_reg) _wm32(rm, -v); else write32(maddr, -v); }
            break;
        }
        break;
    }
    case 0xF8: m_eflags &= ~FLAG_CF; break;
    case 0xF9: m_eflags |= FLAG_CF; break;
    case 0xFA: m_eflags &= ~FLAG_IF; break;
    case 0xFB: m_eflags |= FLAG_IF; break;
    case 0xFC: m_eflags &= ~FLAG_DF; break;
    case 0xFD: m_eflags |= FLAG_DF; break;

    case 0xFE: case 0xFF:
        {
            uint8_t modrm = modrm_byte();
            int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
            int seg = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
            bool is_reg = (mod == 3);

            if (op == 0xFE) {
                // 0xFE: only INC/DEC r/m8 are valid
                if (reg == 0 || reg == 1) {
                    uint32_t maddr = is_reg ? 0 : modrm_addr_ea(seg, mod, rm);
                    if (reg == 0) {
                        uint8_t v = (is_reg ? _rm8(rm) : read8(maddr)) + 1;
                        if (is_reg) _wm8(rm, v); else write8(maddr, v);
                        set_sz8(v);
                    } else {
                        uint8_t v = (is_reg ? _rm8(rm) : read8(maddr)) - 1;
                        if (is_reg) _wm8(rm, v); else write8(maddr, v);
                        set_sz8(v);
                    }
                    cycles = 2;
                }
                break;
            }

            if (reg == 2 || reg == 4 || reg == 6 || reg == 0 || reg == 1) {
                uint32_t maddr = is_reg ? 0 : modrm_addr_ea(seg, mod, rm);
                if (reg == 2) { // CALL r/m
                    uint32_t target = (m_op_size == 32) ? (is_reg ? _rm32(rm) : read32(maddr))
                                                        : (is_reg ? _rm16(rm) : read16(maddr));
                    if (m_op_size == 32) { m_esp -= 4; write32(m_esp, m_eip); }
                    else { m_esp -= 2; write16(m_esp, m_eip & 0xFFFF); }
                    m_eip = target; cycles = 4;
                } else if (reg == 4) { // JMP r/m
                    m_eip = (m_op_size == 32) ? (is_reg ? _rm32(rm) : read32(maddr))
                                              : (is_reg ? _rm16(rm) : read16(maddr));
                    cycles = 3;
                } else if (reg == 6) { // PUSH r/m
                    if (m_op_size == 32) {
                        uint32_t v = is_reg ? _rm32(rm) : read32(maddr);
                        m_esp -= 4; write32(m_esp, v);
                    } else {
                        uint16_t v = is_reg ? _rm16(rm) : read16(maddr);
                        m_esp -= 2; write16(m_esp, v);
                    }
                } else if (reg == 0) {
                    if (m_op_size == 32) { uint32_t v = is_reg ? _rm32(rm) : read32(maddr); v++; if (is_reg) _wm32(rm, v); else write32(maddr, v); set_sz32(v); }
                    else if (m_op_size == 16) { uint16_t v = is_reg ? _rm16(rm) : read16(maddr); v++; if (is_reg) _wm16(rm, v); else write16(maddr, v); set_sz16(v); }
                    else { uint8_t v = is_reg ? _rm8(rm) : read8(maddr); v++; if (is_reg) _wm8(rm, v); else write8(maddr, v); set_sz8(v); }
                } else if (reg == 1) {
                    if (m_op_size == 32) { uint32_t v = is_reg ? _rm32(rm) : read32(maddr); v--; if (is_reg) _wm32(rm, v); else write32(maddr, v); set_sz32(v); }
                    else if (m_op_size == 16) { uint16_t v = is_reg ? _rm16(rm) : read16(maddr); v--; if (is_reg) _wm16(rm, v); else write16(maddr, v); set_sz16(v); }
                    else { uint8_t v = is_reg ? _rm8(rm) : read8(maddr); v--; if (is_reg) _wm8(rm, v); else write8(maddr, v); set_sz8(v); }
                }
            }
            break;
        }
    
    case 0x0F: {
        // Two-byte opcodes
        uint8_t op2 = fetch8();
        switch (op2) {
        case 0x01: case 0x00: {
            uint8_t modrm = modrm_byte();
            int mod2 = (modrm >> 6) & 3, reg2 = (modrm >> 3) & 7, rm2 = modrm & 7;
            int seg2 = (m_seg_prefix >= 0) ? m_seg_prefix : 0;
            if (op2 == 0x01 && reg2 == 2) {
                uint32_t a = modrm_addr_ea(seg2, mod2, rm2);
                m_gdtr_limit = read16(a); m_gdtr_base = read32(a + 2);
            } else if (op2 == 0x01 && reg2 == 3) {
                modrm_addr_ea(seg2, mod2, rm2);
            }
            break;
        }
        case 0x06: break; // SLDT
        case 0x07: break; // SGDT
        case 0x08: break; case 0x09: break; case 0x0B: break;
        case 0x20: {
            uint8_t modrm = modrm_byte();
            int reg2 = (modrm >> 3) & 7, rm2 = modrm & 7;
            _wm32(reg2, 0); break; // MOV reg, CRn — return 0
        }
        case 0x21: { modrm_byte(); break; } // MOV from DR
        case 0x22: {
            uint8_t modrm = modrm_byte();
            int reg2 = (modrm >> 3) & 7, rm2 = modrm & 7;
            if (reg2 == 0) m_protected_mode = (_rm32(rm2) & 1) != 0;
            break;
        }
        case 0x23: { modrm_byte(); break; } // MOV to DR
        case 0x31: break; // RDTSC
        case 0x80 ... 0x8F: {
            // Jcc near: displacement is 32-bit in 32-bit operand mode, 16-bit otherwise
            int32_t disp = (m_op_size == 32) ? (int32_t)fetch32() : (int16_t)fetch16();
            bool taken = false;
            switch (op2 & 0xF) {
                case 0: taken = (m_eflags & FLAG_OF); break;
                case 1: taken = !(m_eflags & FLAG_OF); break;
                case 2: taken = (m_eflags & FLAG_CF); break;
                case 3: taken = !(m_eflags & FLAG_CF); break;
                case 4: taken = (m_eflags & FLAG_ZF); break;
                case 5: taken = !(m_eflags & FLAG_ZF); break;
                case 6: taken = (m_eflags & (FLAG_CF|FLAG_ZF)); break;
                case 7: taken = !(m_eflags & (FLAG_CF|FLAG_ZF)); break;
                case 8: taken = (m_eflags & FLAG_SF); break;
                case 9: taken = !(m_eflags & FLAG_SF); break;
                case 0xA: taken = (m_eflags & FLAG_PF); break;
                case 0xB: taken = !(m_eflags & FLAG_PF); break;
                case 0xC: taken = ((m_eflags & FLAG_SF)!=0) != ((m_eflags & FLAG_OF)!=0); break;
                case 0xD: taken = ((m_eflags & FLAG_SF)!=0) == ((m_eflags & FLAG_OF)!=0); break;
                case 0xE: taken = (m_eflags & FLAG_ZF) || ((m_eflags & FLAG_SF) != (m_eflags & FLAG_OF)); break;
                case 0xF: taken = !(m_eflags & FLAG_ZF) && ((m_eflags & FLAG_SF) == (m_eflags & FLAG_OF)); break;
            }
            if (taken) m_eip += disp;
            cycles = 2; break;
        }
        case 0x90 ... 0x9F: {
            uint8_t modrm = modrm_byte();
            int mod2 = (modrm >> 6) & 3, rm2 = modrm & 7, seg2 = (m_seg_prefix>=0)?m_seg_prefix:0;
            bool cond = false; int cc = op2 & 0xF;
            switch (cc) {
                case 0: cond = (m_eflags & FLAG_OF); break;
                case 1: cond = !(m_eflags & FLAG_OF); break;
                case 2: cond = (m_eflags & FLAG_CF); break;
                case 3: cond = !(m_eflags & FLAG_CF); break;
                case 4: cond = (m_eflags & FLAG_ZF); break;
                case 5: cond = !(m_eflags & FLAG_ZF); break;
                case 6: cond = (m_eflags & (FLAG_CF|FLAG_ZF)); break;
                case 7: cond = !(m_eflags & (FLAG_CF|FLAG_ZF)); break;
                case 8: cond = (m_eflags & FLAG_SF); break;
                case 9: cond = !(m_eflags & FLAG_SF); break;
                case 0xA: cond = (m_eflags & FLAG_PF); break;
                case 0xB: cond = !(m_eflags & FLAG_PF); break;
                case 0xC: cond = ((m_eflags & FLAG_SF)!=0) != ((m_eflags & FLAG_OF)!=0); break;
                case 0xD: cond = ((m_eflags & FLAG_SF)!=0) == ((m_eflags & FLAG_OF)!=0); break;
                case 0xE: cond = (m_eflags & FLAG_ZF) || ((m_eflags & FLAG_SF)!=(m_eflags & FLAG_OF)); break;
                case 0xF: cond = !(m_eflags & FLAG_ZF) && ((m_eflags & FLAG_SF)==(m_eflags & FLAG_OF)); break;
            }
            uint8_t val = cond ? 1 : 0;
            if (mod2 == 3) _wm8(rm2, val);
            else write8(modrm_addr_ea(seg2, mod2, rm2), val);
            break;
        }
        case 0xA0: { m_esp -= 4; write32(m_esp, m_seg[4] << 16); break; } // PUSH FS
        case 0xA1: { m_seg[4] = read32(m_esp) >> 16; m_esp += 4; break; } // POP FS
        case 0xA2: { m_eax = 1; break; } // CPUID
        case 0xA3: { modrm_byte(); m_eflags &= ~FLAG_CF; break; } // BT
        case 0xA8: { m_esp -= 4; write32(m_esp, m_seg[5] << 16); break; } // PUSH GS
        case 0xA9: { m_seg[5] = read32(m_esp) >> 16; m_esp += 4; break; } // POP GS
        case 0xAB: { modrm_byte(); break; } // BTS
        case 0xAF: {
            uint8_t modrm = modrm_byte();
            int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7;
            int seg = (m_seg_prefix>=0)?m_seg_prefix:0;
            uint32_t v = (mod == 3) ? _rm32(rm) : read32(modrm_addr_ea(seg, mod, rm));
            int64_t res = (int64_t)(int32_t)_rm32(reg) * (int64_t)(int32_t)v;
            _wm32(reg, res & 0xFFFFFFFF); m_edx = (res >> 32) & 0xFFFFFFFF;
            m_eflags = (m_eflags & ~(FLAG_CF|FLAG_OF)) | ((res != (int64_t)(int32_t)(res&0xFFFFFFFF)) ? FLAG_CF|FLAG_OF : 0);
            break;
        }
        case 0xB6: case 0xB7: {
            uint8_t modrm = modrm_byte();
            int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7, seg = (m_seg_prefix>=0)?m_seg_prefix:0;
            if (op2 == 0xB6) { uint8_t v = (mod==3)?_rm8(rm):read8(modrm_addr_ea(seg, mod, rm)); if (m_op_size == 16) _wm16(reg, v); else _wm32(reg, v); }
            else { uint16_t v = (mod==3)?_rm16(rm):read16(modrm_addr_ea(seg, mod, rm)); if (m_op_size == 16) _wm16(reg, v); else _wm32(reg, v); }
            break;
        }
        case 0xBE: case 0xBF: {
            uint8_t modrm = modrm_byte();
            int mod = (modrm >> 6) & 3, reg = (modrm >> 3) & 7, rm = modrm & 7, seg = (m_seg_prefix>=0)?m_seg_prefix:0;
            if (op2 == 0xBE) { int8_t v = (mod==3)?(int8_t)_rm8(rm):(int8_t)read8(modrm_addr_ea(seg, mod, rm)); if (m_op_size == 16) _wm16(reg, (int16_t)v); else _wm32(reg, (int32_t)v); }
            else { int16_t v = (mod==3)?(int16_t)_rm16(rm):(int16_t)read16(modrm_addr_ea(seg, mod, rm)); if (m_op_size == 16) _wm16(reg, v); else _wm32(reg, (int32_t)v); }
            break;
        }
        case 0xC7: case 0xC1: break; // CMPXCHG8B/etc (skip)
        case 0xCB: break; // BSWAP
        default: break;
        }
        break;
    }
    default:
        break;
    }

    return cycles;
}
