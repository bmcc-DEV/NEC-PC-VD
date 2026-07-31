#include "pci_bus.h"
#include <cstdio>

PciBus::PciBus() = default;

void PciBus::register_device(int bus, int dev, int func, PciDevice* device) {
    if (bus == 0 && dev < 32) {
        m_slots[dev].device = device;
        m_slots[dev].occupied = true;
        printf("PCI: Registered %s at bus 0 dev %d func %d\n", device->name(), dev, func);
    }
}

PciDevice* PciBus::find_device(int bus, int dev, int func) {
    if (bus == 0 && dev < 32 && m_slots[dev].occupied)
        return m_slots[dev].device;
    return nullptr;
}

uint32_t PciBus::config_read(int bus, int dev, int func, int reg, uint32_t mask) {
    auto* d = find_device(bus, dev, func);
    if (!d) return 0xFFFFFFFF;
    return d->config_read(reg, mask);
}

void PciBus::config_write(int bus, int dev, int func, int reg, uint32_t data, uint32_t mask) {
    auto* d = find_device(bus, dev, func);
    if (d) d->config_write(reg, data, mask);
}

uint32_t PciBus::mmio_read(uint32_t addr) {
    for (int i = 0; i < 32; i++) {
        auto* d = m_slots[i].device;
        if (!d) continue;
        for (int b = 0; b < 6; b++) {
            if (d->bar[b] && addr >= d->bar[b]) {
                uint32_t offset = addr - d->bar[b];
                return d->mmio_read(offset);
            }
        }
    }
    return 0xFFFFFFFF;
}

void PciBus::mmio_write(uint32_t addr, uint32_t data, uint32_t mask) {
    for (int i = 0; i < 32; i++) {
        auto* d = m_slots[i].device;
        if (!d) continue;
        for (int b = 0; b < 6; b++) {
            if (d->bar[b] && addr >= d->bar[b]) {
                uint32_t offset = addr - d->bar[b];
                d->mmio_write(offset, data, mask);
                return;
            }
        }
    }
}
