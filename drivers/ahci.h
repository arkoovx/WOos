#ifndef WOOS_DRIVERS_AHCI_H
#define WOOS_DRIVERS_AHCI_H

#include "kernel.h"

uint8_t ahci_init(void);
uint8_t ahci_is_active(void);
uint8_t ahci_read_sectors(uint32_t lba, uint8_t sector_count, void* buffer);
uint8_t ahci_write_sectors(uint32_t lba, uint8_t sector_count, const void* buffer);

#endif
