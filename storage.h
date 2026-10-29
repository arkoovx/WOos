#ifndef WOOS_STORAGE_H
#define WOOS_STORAGE_H

#include "kernel.h"

#define STORAGE_SECTOR_SIZE 512u

typedef enum {
    STORAGE_TYPE_NONE = 0,
    STORAGE_TYPE_AHCI = 1,
    STORAGE_TYPE_ATA_PIO = 2
} storage_type_t;

void storage_init(void);
uint8_t storage_is_ready(void);
uint8_t storage_last_read_ok(void);
uint32_t storage_last_lba(void);
uint8_t storage_boot_signature_valid(void);
storage_type_t storage_get_type(void);
uint8_t storage_read_sectors(uint32_t lba, uint8_t sector_count, void* buffer);
uint8_t storage_write_sectors(uint32_t lba, uint8_t sector_count, const void* buffer);

#endif
