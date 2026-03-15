#include "pci.h"

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT    0xCFCu

static inline void outl(uint16_t port, uint32_t value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000u
        | ((uint32_t)bus << 16)
        | ((uint32_t)slot << 11)
        | ((uint32_t)func << 8)
        | (uint32_t)(offset & 0xFCu);

    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

static uint16_t pci_vendor_id(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint16_t)(pci_read_config_dword(bus, slot, func, 0x00u) & 0xFFFFu);
}

static void pci_fill_device_info(uint8_t bus, uint8_t slot, uint8_t func, pci_device_info_t* out) {
    uint32_t id = pci_read_config_dword(bus, slot, func, 0x00u);
    uint32_t class_reg = pci_read_config_dword(bus, slot, func, 0x08u);
    uint32_t header_reg = pci_read_config_dword(bus, slot, func, 0x0Cu);

    out->bus = bus;
    out->slot = slot;
    out->func = func;
    out->vendor_id = (uint16_t)(id & 0xFFFFu);
    out->device_id = (uint16_t)((id >> 16) & 0xFFFFu);
    out->prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);
    out->subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
    out->class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
    out->header_type = (uint8_t)((header_reg >> 16) & 0xFFu);
    out->bar0 = pci_read_config_dword(bus, slot, func, 0x10u);
    out->bar1 = pci_read_config_dword(bus, slot, func, 0x14u);
}

uint8_t pci_find_device_by_id(uint16_t vendor_id, uint16_t device_id, pci_device_info_t* out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendor = pci_vendor_id((uint8_t)bus, slot, func);
                if (vendor == 0xFFFFu) {
                    if (func == 0u) {
                        break;
                    }
                    continue;
                }

                uint32_t id = pci_read_config_dword((uint8_t)bus, slot, func, 0x00u);
                uint16_t device = (uint16_t)((id >> 16) & 0xFFFFu);

                if (vendor == vendor_id && device == device_id) {
                    pci_fill_device_info((uint8_t)bus, slot, func, out);
                    return 1u;
                }
            }
        }
    }

    return 0u;
}

uint8_t pci_find_display_controller(uint16_t vendor_id, pci_device_info_t* out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint16_t vendor = pci_vendor_id((uint8_t)bus, slot, func);
                if (vendor == 0xFFFFu) {
                    if (func == 0u) {
                        break;
                    }
                    continue;
                }

                uint32_t class_reg = pci_read_config_dword((uint8_t)bus, slot, func, 0x08u);
                uint8_t class_code = (uint8_t)((class_reg >> 24) & 0xFFu);
                uint8_t subclass = (uint8_t)((class_reg >> 16) & 0xFFu);

                if (vendor == vendor_id && class_code == 0x03u && subclass == 0x00u) {
                    pci_fill_device_info((uint8_t)bus, slot, func, out);
                    return 1u;
                }
            }
        }
    }

    return 0u;
}
