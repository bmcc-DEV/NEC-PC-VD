#pragma once
#include <cstdint>
#include <functional>
#include <array>

class PciDevice {
public:
    virtual ~PciDevice() = default;
    virtual uint32_t config_read(int reg, uint32_t mask) = 0;
    virtual void config_write(int reg, uint32_t data, uint32_t mask) = 0;
    virtual uint32_t mmio_read(uint32_t offset) = 0;
    virtual void mmio_write(uint32_t offset, uint32_t data, uint32_t mask) = 0;
    virtual const char* name() = 0;

    uint16_t vendor_id = 0;
    uint16_t device_id = 0;
    uint16_t command = 0;
    uint16_t status = 0;
    uint8_t revision = 0;
    uint8_t class_code[3] = {};
    uint32_t bar[6] = {};
    uint8_t irq_line = 0;
};

class PciBus {
public:
    PciBus();

    void register_device(int bus, int dev, int func, PciDevice* device);
    PciDevice* find_device(int bus, int dev, int func);

    uint32_t config_read(int bus, int dev, int func, int reg, uint32_t mask);
    void config_write(int bus, int dev, int func, int reg, uint32_t data, uint32_t mask);

    uint32_t mmio_read(uint32_t addr);
    void mmio_write(uint32_t addr, uint32_t data, uint32_t mask);

private:
    struct Slot {
        PciDevice* device = nullptr;
        bool occupied = false;
    };
    std::array<Slot, 32> m_slots;
};
