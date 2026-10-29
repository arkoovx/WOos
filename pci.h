#ifndef WOOS_PCI_H
#define WOOS_PCI_H

#include "kernel.h"

typedef struct pci_device_info {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
    uint32_t bars[6];
} pci_device_info_t;

uint8_t pci_find_device_by_id(uint16_t vendor_id, uint16_t device_id, pci_device_info_t* out);
uint8_t pci_find_display_controller(uint16_t vendor_id, pci_device_info_t* out);
uint8_t pci_find_storage_controller(uint8_t subclass, uint8_t prog_if, pci_device_info_t* out);
uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);

#endif
