#include "vfs.h"
#include "storage.h"
#include "ff.h"
#include "serial.h"
#include "net_socket.h"

#define VFS_MAX_HANDLES 8u
#define VFS_BOOT_SECTORS 1u

typedef struct vfs_handle {
    uint8_t in_use;
    uint8_t type;       // VFS_HANDLE_FILE, VFS_HANDLE_DIR, VFS_HANDLE_SOCKET
    FIL fil;
    DIR dir;
    int32_t socket_id;
} vfs_handle_t;

// Глобальная переменная для LBA начала раздела FAT12 (используется в diskio.c)
uint32_t g_fat_start_lba = VFS_BOOT_SECTORS;

static uint8_t g_vfs_ready = 0u;
static FATFS g_fatfs;
static vfs_handle_t g_handles[VFS_MAX_HANDLES];

extern uint8_t __kernel_end;
extern void* memset(void* s, int c, size_t n);
extern void* memcpy(void* dest, const void* src, size_t n);

// Вспомогательная функция для копирования строки с безопасным нуль-терминатором
static void copy_string(char* dst, const char* src, uint32_t capacity) {
    if (!dst || !src || capacity == 0) return;
    uint32_t i = 0;
    for (; i + 1 < capacity && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

void vfs_init(void) {
    g_vfs_ready = 0u;
    g_fat_start_lba = VFS_BOOT_SECTORS;

    for (uint8_t i = 0u; i < VFS_MAX_HANDLES; i++) {
        g_handles[i].in_use = 0u;
    }

    if (!storage_is_ready()) {
        serial_printf("[VFS] Error: storage is not ready!\n");
        return;
    }

    // Сканируем диск в поисках сигнатуры загрузочного сектора FAT12 (0xEB, 0x3C, 0x90 и 0x55, 0xAA)
    // Начинаем с LBA 400 (около 200 КБ от начала диска), чтобы гарантированно пропустить начало ядра.
    uint8_t sector_buf[STORAGE_SECTOR_SIZE];
    uint8_t found = 0;
    for (uint32_t lba = 400; lba < 2880; lba++) {
        if (storage_read_sectors(lba, 1, sector_buf)) {
            if (sector_buf[0] == 0xEB && sector_buf[1] == 0x3C && sector_buf[2] == 0x90 &&
                sector_buf[510] == 0x55 && sector_buf[511] == 0xAA) {
                g_fat_start_lba = lba;
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        serial_printf("[VFS] Error: FAT12 signature not found on disk!\n");
        return;
    }

    serial_printf("[VFS] FAT12 start LBA set to %u\n", g_fat_start_lba);

    // Монтируем файловую систему тома FAT
    FRESULT res = f_mount(&g_fatfs, "", 1);
    if (res != FR_OK) {
        serial_printf("[VFS] Error: failed to mount FAT volume (res=%d)\n", (int)res);
        return;
    }

    g_vfs_ready = 1u;
    serial_printf("[VFS] FAT12 volume mounted successfully.\n");
}

uint8_t vfs_is_ready(void) {
    return g_vfs_ready;
}

static int32_t allocate_handle(void) {
    for (uint8_t i = 0u; i < VFS_MAX_HANDLES; i++) {
        if (!g_handles[i].in_use) {
            g_handles[i].in_use = 1u;
            g_handles[i].type = VFS_HANDLE_FILE;
            g_handles[i].socket_id = -1;
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t vfs_create_socket_handle(int32_t socket_id) {
    int32_t h = allocate_handle();
    if (h < 0) return -1;
    g_handles[h].type = VFS_HANDLE_SOCKET;
    g_handles[h].socket_id = socket_id;
    return h;
}

int32_t vfs_get_socket_id(int32_t handle) {
    if (handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) return -1;
    if (!g_handles[handle].in_use || g_handles[handle].type != VFS_HANDLE_SOCKET) return -1;
    return g_handles[handle].socket_id;
}

int32_t vfs_open(const char* path, uint8_t mode) {
    if (!g_vfs_ready || !path) {
        return -1;
    }

    int32_t h = allocate_handle();
    if (h < 0) return -1;

    // 1. Проверяем, не открываем ли мы корневой каталог "/" или любую папку
    // Для простоты, если путь оканчивается на "/" или является "/" — открываем как каталог.
    uint32_t path_len = 0;
    while (path[path_len] != '\0') path_len++;

    if (path_len == 1 && path[0] == '/') {
        FRESULT res = f_opendir(&g_handles[h].dir, path);
        if (res == FR_OK) {
            g_handles[h].type = VFS_HANDLE_DIR;
            return h;
        }
        g_handles[h].in_use = 0u;
        return -1;
    }

    // 2. Транслируем флаги VFS в флаги FatFs
    BYTE fat_mode = 0;
    if (mode & VFS_MODE_READ)  fat_mode |= FA_READ;
    if (mode & VFS_MODE_WRITE) fat_mode |= FA_WRITE;

    if (mode & VFS_MODE_CREATE) {
        if (mode & VFS_MODE_TRUNC) {
            fat_mode |= FA_CREATE_ALWAYS;
        } else {
            fat_mode |= FA_OPEN_ALWAYS;
        }
    } else {
        if (mode & VFS_MODE_TRUNC) {
            fat_mode |= FA_CREATE_ALWAYS;
        } else {
            fat_mode |= FA_OPEN_EXISTING;
        }
    }

    FRESULT res = f_open(&g_handles[h].fil, path, fat_mode);
    if (res == FR_OK) {
        g_handles[h].type = VFS_HANDLE_FILE;
        return h;
    }

    // Если это была попытка открыть директорию через f_open, FatFs вернет FR_NO_FILE или FR_DENIED.
    // Попробуем открыть как директорию.
    res = f_opendir(&g_handles[h].dir, path);
    if (res == FR_OK) {
        g_handles[h].type = VFS_HANDLE_DIR;
        return h;
    }

    g_handles[h].in_use = 0u;
    return -1;
}

uint32_t vfs_read(int32_t handle, void* buffer, uint32_t bytes) {
    if (!g_vfs_ready || !buffer || bytes == 0 || handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return 0u;
    }

    vfs_handle_t* h = &g_handles[handle];
    if (!h->in_use) return 0u;

    if (h->type == VFS_HANDLE_SOCKET) {
        int32_t res = net_socket_recv(h->socket_id, buffer, bytes);
        return (res < 0) ? 0u : (uint32_t)res;
    }

    if (h->type != VFS_HANDLE_FILE) return 0u;

    UINT br = 0;
    FRESULT res = f_read(&h->fil, buffer, (UINT)bytes, &br);
    if (res == FR_OK) {
        return (uint32_t)br;
    }
    return 0u;
}

uint32_t vfs_write(int32_t handle, const void* buffer, uint32_t bytes) {
    if (!g_vfs_ready || !buffer || bytes == 0 || handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return 0u;
    }

    vfs_handle_t* h = &g_handles[handle];
    if (!h->in_use) return 0u;

    if (h->type == VFS_HANDLE_SOCKET) {
        int32_t res = net_socket_send(h->socket_id, buffer, bytes);
        return (res < 0) ? 0u : (uint32_t)res;
    }

    if (h->type != VFS_HANDLE_FILE) return 0u;

    UINT bw = 0;
    FRESULT res = f_write(&h->fil, buffer, (UINT)bytes, &bw);
    if (res == FR_OK) {
        return (uint32_t)bw;
    }
    return 0u;
}

int32_t vfs_seek(int32_t handle, uint32_t offset) {
    if (!g_vfs_ready || handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return -1;
    }

    vfs_handle_t* h = &g_handles[handle];
    if (!h->in_use || h->type != VFS_HANDLE_FILE) return -1;

    FRESULT res = f_lseek(&h->fil, (FSIZE_t)offset);
    if (res == FR_OK) {
        return 0;
    }
    return -1;
}

uint32_t vfs_tell(int32_t handle) {
    if (!g_vfs_ready || handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return 0;
    }

    vfs_handle_t* h = &g_handles[handle];
    if (!h->in_use || h->type != VFS_HANDLE_FILE) return 0;

    return (uint32_t)f_tell(&h->fil);
}

uint32_t vfs_size(int32_t handle) {
    if (!g_vfs_ready || handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return 0;
    }

    vfs_handle_t* h = &g_handles[handle];
    if (!h->in_use || h->type != VFS_HANDLE_FILE) return 0;

    return (uint32_t)f_size(&h->fil);
}

int32_t vfs_readdir(int32_t handle, vfs_dirent_t* out_entry) {
    if (!g_vfs_ready || !out_entry || handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return -1;
    }

    vfs_handle_t* h = &g_handles[handle];
    if (!h->in_use || h->type != VFS_HANDLE_DIR) return -1;

    FILINFO fno;
    FRESULT res = f_readdir(&h->dir, &fno);
    if (res != FR_OK || fno.fname[0] == '\0') {
        return 0; // Конец каталога
    }

    copy_string(out_entry->name, fno.fname, sizeof(out_entry->name));
    out_entry->is_dir = (fno.fattrib & AM_DIR) ? 1u : 0u;
    out_entry->size = (uint32_t)fno.fsize;
    return 1;
}

void vfs_close(int32_t handle) {
    if (handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return;
    }

    vfs_handle_t* h = &g_handles[handle];
    if (!h->in_use) return;

    if (h->type == VFS_HANDLE_SOCKET) {
        net_socket_close(h->socket_id);
    } else if (h->type == VFS_HANDLE_DIR) {
        f_closedir(&h->dir);
    } else {
        f_close(&h->fil);
    }

    h->in_use = 0u;
    h->type = VFS_HANDLE_FILE;
    h->socket_id = -1;
}
