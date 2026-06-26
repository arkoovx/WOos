#ifndef WOOS_STORAGE_H
#define WOOS_STORAGE_H

#include "kernel.h"

#define STORAGE_SECTOR_SIZE 512u

void storage_init(void);
uint8_t storage_is_ready(void);
uint8_t storage_last_read_ok(void);
uint32_t storage_last_lba(void);
uint8_t storage_boot_signature_valid(void);
uint8_t storage_read_sectors(uint32_t lba, uint8_t sector_count, void* buffer);

#endif
