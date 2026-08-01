#include "emulator.h"
#include <cstdio>
#include <cstring>
#include <chrono>

Emulator::Emulator()
    : m_cpu(m_mem)
    , m_mediagx(m_mem, m_pci)
    , m_voodoo() {
}

Emulator::~Emulator() {
    delete[] m_ram;
}

bool Emulator::init(const char* rom_path) {
    if (!m_window.init(640, 480, "NEC PC-VD Emulator")) {
        fprintf(stderr, "Failed to init SDL window\n");
        return false;
    }

    m_ram = new uint8_t[m_ram_size];
    memset(m_ram, 0, m_ram_size);

    m_mediagx.set_ram(m_ram, m_ram_size);
    m_voodoo.set_memory(&m_mem);
    m_voodoo.reset();
    m_pci.register_device(0, 1, 0, &m_voodoo);
    setup_memory_map();
    setup_io_handlers();

    m_cpu.reset();

    if (rom_path) {
        // Load ROM at high address and copy to BIOS shadow
        m_mem.load_rom(rom_path, 0xFFFC0000);

        // Also load at 0x000C0000 for real-mode compatibility
        FILE* f = fopen(rom_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 0x40000) sz = 0x40000;
            fread(m_ram + 0xC0000, 1, sz, f);
            // Also copy to 0xF0000 (standard BIOS area)
            if (sz > 0x10000) {
                memcpy(m_ram + 0xF0000, m_ram + 0xC0000 + 0x30000, 0x10000);
            }
            fclose(f);
            printf("BIOS shadowed to 0xC0000 (%ld bytes)\n", sz);
        }

        // Debug: check ROM content at reset vector
        printf("Reset vector byte at 0xFFFFFFF0: 0x%02X\n", m_mem.read8(0xFFFFFFF0));
        printf("Code byte at 0xFC000: 0x%02X\n", m_mem.read8(0xFC000));
    }

    return true;
}

void Emulator::setup_memory_map() {
    // Main RAM (0x00000000 - 0x00FFFFFF)
    m_mem.add_region(0x00000000, 0x0009FFFF, m_ram, false);
    m_mem.add_region(0x000A0000, 0x000AFFFF, m_ram + 0xA0000, false);
    m_mem.add_region(0x000B0000, 0x000B7FFF, m_ram + 0xB0000, false);
    m_mem.add_region(0x00100000, 0x01FFFFFF, m_ram + 0x100000, false);

    // MediaGX BIU registers at 0x40008000
    m_mem.add_read_handler(0x40008000, 0x400080FF, [this](uint32_t addr) -> uint32_t {
        return m_mediagx.biu_read((addr - 0x40008000) / 4);
    });
    m_mem.add_write_handler(0x40008000, 0x400080FF, [this](uint32_t addr, uint32_t data, uint32_t mask) {
        m_mediagx.biu_write((addr - 0x40008000) / 4, data, mask);
    });

    // MediaGX Display Controller at 0x40008300
    m_mem.add_read_handler(0x40008300, 0x400083FF, [this](uint32_t addr) -> uint32_t {
        return m_mediagx.display().read((addr - 0x40008300) / 4);
    });
    m_mem.add_write_handler(0x40008300, 0x400083FF, [this](uint32_t addr, uint32_t data, uint32_t mask) {
        m_mediagx.display().write((addr - 0x40008300) / 4, data, mask);
    });

    // MediaGX Memory Controller at 0x40008400
    m_mem.add_read_handler(0x40008400, 0x400084FF, [this](uint32_t addr) -> uint32_t {
        return m_mediagx.memory().read((addr - 0x40008400) / 4);
    });
    m_mem.add_write_handler(0x40008400, 0x400084FF, [this](uint32_t addr, uint32_t data, uint32_t mask) {
        m_mediagx.memory().write((addr - 0x40008400) / 4, data, mask);
    });

    // 2D framebuffer at 0x40800000 (4MB)
    m_mem.add_region(0x40800000, 0x40BFFFFF, m_ram + 0x800000, false);

    // Voodoo 2 MMIO at 0x42000000 (4MB aperture)
    m_mem.add_read_handler(0x42000000, 0x43FFFFFF, [this](uint32_t addr) -> uint32_t {
        return m_voodoo.read(addr - 0x42000000);
    });
    m_mem.add_write_handler(0x42000000, 0x43FFFFFF, [this](uint32_t addr, uint32_t data, uint32_t mask) {
        m_voodoo.write(addr - 0x42000000, data, mask);
    });

    // BIOS shadow at 0xC0000 (real-mode accessible)
    m_mem.add_region(0x000C0000, 0x000FFFFF, m_ram + 0xC0000, false);
}

void Emulator::setup_io_handlers() {
    // Cyrix config registers
    m_cpu.set_io_read_handler(0x22, [this](uint16_t) { return m_mediagx.io_read(0x22); });
    m_cpu.set_io_read_handler(0x23, [this](uint16_t) { return m_mediagx.io_read(0x23); });
    m_cpu.set_io_write_handler(0x22, [this](uint16_t, uint8_t d) { m_mediagx.io_write(0x22, d); });
    m_cpu.set_io_write_handler(0x23, [this](uint16_t, uint8_t d) { m_mediagx.io_write(0x23, d); });

    // I/O delay port
    for (int p = 0xE8; p <= 0xEB; p++) {
        m_cpu.set_io_read_handler(p, [](uint16_t) { return 0; });
        m_cpu.set_io_write_handler(p, [](uint16_t, uint8_t) {});
    }

    // Parallel port (controles)
    m_cpu.set_io_read_handler(0x378, [](uint16_t) { return 0xFF; });
    m_cpu.set_io_read_handler(0x379, [](uint16_t) { return 0xFF; });
    m_cpu.set_io_read_handler(0x37A, [](uint16_t) { return 0xFF; });
    m_cpu.set_io_write_handler(0x378, [](uint16_t, uint8_t) {});
    m_cpu.set_io_write_handler(0x37A, [](uint16_t, uint8_t) {});

    // Audio
    for (int p = 0x400; p <= 0x4FF; p++) {
        m_cpu.set_io_read_handler(p, [](uint16_t) { return 0; });
        m_cpu.set_io_write_handler(p, [](uint16_t, uint8_t) {});
    }
}

void Emulator::step_frame() {
    m_cpu.reset_spinning();
    m_cpu.execute(500000);

    // Render 2D background (MediaGX framebuffer)
    m_mediagx.update_display(m_framebuffer, 640, 480);

    // Overlay 3D (Voodoo Rush) onto framebuffer
    // Using a separate buffer for 3D, then compositing
    static uint32_t voodoo_buf[640 * 480];
    if (m_voodoo.update(voodoo_buf, 640, 480)) {
        // Composite 3D over 2D (simple over, alpha checker)
        for (int i = 0; i < 640 * 480; i++) {
            uint32_t v = voodoo_buf[i];
            if ((v & 0xFF000000) != 0) // non-transparent pixel from 3D
                m_framebuffer[i] = v;
        }
    }

    m_window.render_framebuffer(m_framebuffer, 640, 480);
    m_window.present();
}

void Emulator::run() {
    auto last_time = std::chrono::steady_clock::now();
    int frame_count = 0;

    while (m_window.poll_events()) {
        step_frame();
        frame_count++;

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time).count();
        if (elapsed >= 1000) {
            char title[64];
            snprintf(title, sizeof(title), "NEC PC-VD Emulator - %d FPS", frame_count);
            SDL_SetWindowTitle(m_window.window(), title);
            frame_count = 0;
            last_time = now;
        }
    }
}

void Emulator::shutdown() {
    m_window.close();
}
