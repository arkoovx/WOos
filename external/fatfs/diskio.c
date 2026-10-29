#include "ff.h"
#include "diskio.h"
#include "storage.h"

#define DEV_DISK 0

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != DEV_DISK) {
        return STA_NOINIT;
    }
    if (storage_is_ready()) {
        return 0; // OK
    }
    return STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != DEV_DISK) {
        return STA_NOINIT;
    }
    if (storage_is_ready()) {
        return 0;
    }
    storage_init();
    if (storage_is_ready()) {
        return 0;
    }
    return STA_NOINIT;
}

extern uint32_t g_fat_start_lba;

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != DEV_DISK || count == 0) {
        return RES_PARERR;
    }
    BYTE* ptr = buff;
    LBA_t cur = sector;
    UINT rem = count;
    while (rem > 0) {
        UINT chunk = rem > 128 ? 128 : rem;
        if (!storage_read_sectors(g_fat_start_lba + (uint32_t)cur, (uint8_t)chunk, ptr)) {
            return RES_ERROR;
        }
        cur += chunk;
        ptr += chunk * 512;
        rem -= chunk;
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != DEV_DISK || count == 0) {
        return RES_PARERR;
    }
    const BYTE* ptr = buff;
    LBA_t cur = sector;
    UINT rem = count;
    while (rem > 0) {
        UINT chunk = rem > 128 ? 128 : rem;
        if (!storage_write_sectors(g_fat_start_lba + (uint32_t)cur, (uint8_t)chunk, ptr)) {
            return RES_ERROR;
        }
        cur += chunk;
        ptr += chunk * 512;
        rem -= chunk;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != DEV_DISK) {
        return RES_PARERR;
    }

    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            if (!buff) return RES_PARERR;
            *(LBA_t*)buff = 131072; // 64 MB volume
            return RES_OK;

        case GET_SECTOR_SIZE:
            if (!buff) return RES_PARERR;
            *(WORD*)buff = 512;
            return RES_OK;

        case GET_BLOCK_SIZE:
            if (!buff) return RES_PARERR;
            *(DWORD*)buff = 1; // Erase block size = 1 sector
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

// Заглушка для получения времени (FatFs требует её для таймстампов файлов)
DWORD get_fattime(void) {
    // Возвращаем фиксированное время: 2026-06-29 00:00:00
    return ((DWORD)(2026 - 1980) << 25) | ((DWORD)6 << 21) | ((DWORD)29 << 16) |
           ((DWORD)0 << 11) | ((DWORD)0 << 5) | ((DWORD)0 >> 1);
}
