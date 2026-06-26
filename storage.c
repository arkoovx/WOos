#include "storage.h"

#define ATA_PRIMARY_IO_BASE      0x1F0u
#define ATA_PRIMARY_CTRL_BASE    0x3F6u

#define ATA_REG_DATA             (ATA_PRIMARY_IO_BASE + 0u)
#define ATA_REG_SECTOR_COUNT     (ATA_PRIMARY_IO_BASE + 2u)
#define ATA_REG_LBA_LOW          (ATA_PRIMARY_IO_BASE + 3u)
#define ATA_REG_LBA_MID          (ATA_PRIMARY_IO_BASE + 4u)
#define ATA_REG_LBA_HIGH         (ATA_PRIMARY_IO_BASE + 5u)
#define ATA_REG_DRIVE_HEAD       (ATA_PRIMARY_IO_BASE + 6u)
#define ATA_REG_STATUS           (ATA_PRIMARY_IO_BASE + 7u)
#define ATA_REG_COMMAND          (ATA_PRIMARY_IO_BASE + 7u)
#define ATA_REG_ALT_STATUS       (ATA_PRIMARY_CTRL_BASE + 0u)
#define ATA_REG_DEVICE_CONTROL   (ATA_PRIMARY_CTRL_BASE + 0u)

#define ATA_CMD_READ_SECTORS     0x20u
#define ATA_DH_LBA_MASTER        0xE0u
#define ATA_CTRL_NIEN            0x02u

#define ATA_STATUS_ERR           0x01u
#define ATA_STATUS_DRQ           0x08u
#define ATA_STATUS_DF            0x20u
#define ATA_STATUS_DRDY          0x40u
#define ATA_STATUS_BSY           0x80u

#define STORAGE_TEST_LBA         0u
#define STORAGE_TEST_SECTORS     1u
#define STORAGE_BOOT_SIG_OFFSET  510u

static uint8_t g_storage_ready = 0u;
static uint8_t g_last_read_ok = 0u;
static uint8_t g_boot_signature_valid = 0u;
static uint32_t g_last_lba = 0u;
static uint8_t g_boot_sector[STORAGE_SECTOR_SIZE];

static inline void io_wait(void) {
    __asm__ __volatile__("outb %%al, $0x80" : : "a"(0u));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ __volatile__("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t ata_wait_not_busy(void) {
    for (uint32_t spin = 0; spin < 100000u; spin++) {
        if ((inb(ATA_REG_ALT_STATUS) & ATA_STATUS_BSY) == 0u) {
            return 1u;
        }
    }

    return 0u;
}

static uint8_t ata_wait_drq(void) {
    for (uint32_t spin = 0; spin < 100000u; spin++) {
        uint8_t status = inb(ATA_REG_STATUS);
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) {
            return 0u;
        }
        if ((status & ATA_STATUS_BSY) == 0u && (status & ATA_STATUS_DRQ) != 0u) {
            return 1u;
        }
    }

    return 0u;
}

uint8_t storage_read_sectors(uint32_t lba, uint8_t sector_count, void* buffer) {
    uint16_t* words = (uint16_t*)buffer;

    if (sector_count == 0u || buffer == 0 || lba > 0x0FFFFFFFu) {
        g_last_read_ok = 0u;
        return 0u;
    }

    if (!ata_wait_not_busy()) {
        g_last_read_ok = 0u;
        return 0u;
    }

    outb(ATA_REG_DEVICE_CONTROL, ATA_CTRL_NIEN);
    outb(ATA_REG_DRIVE_HEAD, (uint8_t)(ATA_DH_LBA_MASTER | ((lba >> 24) & 0x0Fu)));
    io_wait();
    outb(ATA_REG_SECTOR_COUNT, sector_count);
    outb(ATA_REG_LBA_LOW, (uint8_t)(lba & 0xFFu));
    outb(ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFFu));
    outb(ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFFu));
    outb(ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    for (uint8_t sector = 0; sector < sector_count; sector++) {
        if (!ata_wait_drq()) {
            g_last_read_ok = 0u;
            g_last_lba = (uint32_t)(lba + sector);
            return 0u;
        }

        for (uint16_t word = 0; word < (STORAGE_SECTOR_SIZE / 2u); word++) {
            words[(sector * (STORAGE_SECTOR_SIZE / 2u)) + word] = inw(ATA_REG_DATA);
        }
    }

    g_last_lba = (uint32_t)(lba + sector_count - 1u);
    g_last_read_ok = 1u;
    return 1u;
}

void storage_init(void) {
    g_storage_ready = 0u;
    g_last_read_ok = 0u;
    g_boot_signature_valid = 0u;
    g_last_lba = 0u;

    outb(ATA_REG_DEVICE_CONTROL, ATA_CTRL_NIEN);
    outb(ATA_REG_DRIVE_HEAD, ATA_DH_LBA_MASTER);
    io_wait();

    uint8_t status = inb(ATA_REG_STATUS);
    if (status == 0x00u || status == 0xFFu) {
        return;
    }

    if ((status & (ATA_STATUS_BSY | ATA_STATUS_DRDY)) == ATA_STATUS_BSY && !ata_wait_not_busy()) {
        return;
    }

    if (!storage_read_sectors(STORAGE_TEST_LBA, STORAGE_TEST_SECTORS, g_boot_sector)) {
        return;
    }

    g_boot_signature_valid = (uint8_t)(g_boot_sector[STORAGE_BOOT_SIG_OFFSET] == 0x55u
        && g_boot_sector[STORAGE_BOOT_SIG_OFFSET + 1u] == 0xAAu);
    g_storage_ready = 1u;
}

uint8_t storage_is_ready(void) {
    return g_storage_ready;
}

uint8_t storage_last_read_ok(void) {
    return g_last_read_ok;
}

uint32_t storage_last_lba(void) {
    return g_last_lba;
}

uint8_t storage_boot_signature_valid(void) {
    return g_boot_signature_valid;
}
