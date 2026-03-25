#include "vfs.h"

#include "storage.h"

#define VFS_MAX_NODES 16u
#define VFS_MAX_HANDLES 8u
#define VFS_ROOT_NODE 0u
#define VFS_SECTOR_BUFFER_SIZE STORAGE_SECTOR_SIZE
#define VFS_WOFS_MAGIC_U32 0x53464F57u /* "WOFS" little-endian */
#define VFS_WOFS_VERSION 1u
#define VFS_WOFS_MAX_ENTRY_COUNT 1024u
#define VFS_KERNEL_LOAD_ADDR 0x8000u
#define VFS_BOOT_SECTORS 1u

#ifndef WOOS_ENABLE_WOFS
#define WOOS_ENABLE_WOFS 1
#endif

typedef struct vfs_wofs_superblock {
    uint8_t magic[4];
    uint16_t version;
    uint16_t entry_count;
    uint32_t dir_lba;
} vfs_wofs_superblock_t;

typedef struct vfs_wofs_dirent {
    char name[24];
    uint32_t first_lba;
    uint32_t size_bytes;
} vfs_wofs_dirent_t;

typedef enum vfs_node_type {
    VFS_NODE_DIR = 1,
    VFS_NODE_FILE = 2,
} vfs_node_type_t;

typedef struct vfs_node {
    char name[24];
    uint8_t type;
    uint8_t parent;
    uint32_t first_lba;
    uint32_t size_bytes;
    vfs_acl_t acl;
} vfs_node_t;

typedef struct vfs_handle {
    uint8_t in_use;
    uint8_t node_index;
    uint32_t cursor;
    uint16_t dir_scan_index;
} vfs_handle_t;

static uint8_t g_vfs_ready = 0u;
static uint8_t g_wofs_mount_attempted = 0u;
static vfs_node_t g_nodes[VFS_MAX_NODES];
static uint8_t g_node_count = 0u;
static vfs_handle_t g_handles[VFS_MAX_HANDLES];
static uint8_t g_sector_buffer[VFS_SECTOR_BUFFER_SIZE];
static uint32_t g_wofs_start_lba = VFS_BOOT_SECTORS;
static uint16_t g_current_uid = 0u;
static uint16_t g_current_gid = 0u;

extern uint8_t __kernel_end;

static uint8_t str_equals(const char* a, const char* b) {
    if (a == 0 || b == 0) {
        return 0u;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0u;
        }
        a++;
        b++;
    }

    return (uint8_t)(*a == '\0' && *b == '\0');
}

static void copy_name(char* dst, const char* src, uint32_t capacity) {
    if (dst == 0 || src == 0 || capacity == 0u) {
        return;
    }

    uint32_t i = 0u;
    for (; i + 1u < capacity && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }

    dst[i] = '\0';
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return a < b ? a : b;
}

static uint32_t read_magic_u32(const uint8_t magic[4]) {
    return (uint32_t)magic[0] | ((uint32_t)magic[1] << 8u) | ((uint32_t)magic[2] << 16u) | ((uint32_t)magic[3] << 24u);
}

static void copy_dirent_name(char* dst, const char src[24], uint32_t capacity) {
    if (dst == 0 || src == 0 || capacity == 0u) {
        return;
    }

    uint32_t i = 0u;
    const uint32_t src_capacity = 24u;
    for (; i + 1u < capacity && i < src_capacity && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int32_t allocate_handle(uint8_t node_index) {
    for (uint8_t i = 0u; i < VFS_MAX_HANDLES; i++) {
        if (!g_handles[i].in_use) {
            g_handles[i].in_use = 1u;
            g_handles[i].node_index = node_index;
            g_handles[i].cursor = 0u;
            g_handles[i].dir_scan_index = 0u;
            return (int32_t)i;
        }
    }

    return -1;
}

static uint8_t acl_allows(const vfs_node_t* node, uint8_t requested_perm) {
    if (node == 0 || requested_perm == 0u) {
        return 0u;
    }

    // uid=0 рассматривается как kernel/superuser и пока обходит ACL-проверки.
    // Это даёт минимально совместимый scaffold до появления userspace и login-контекста.
    if (g_current_uid == 0u) {
        return 1u;
    }

    uint8_t granted = node->acl.other_perms;
    if (g_current_uid == node->acl.owner_uid) {
        granted = node->acl.owner_perms;
    } else if (g_current_gid == node->acl.owner_gid) {
        granted = node->acl.group_perms;
    }

    return (uint8_t)((granted & requested_perm) == requested_perm);
}

static void set_default_acl(vfs_node_t* node, uint8_t owner_perms, uint8_t group_perms, uint8_t other_perms) {
    if (node == 0) {
        return;
    }

    node->acl.owner_uid = 0u;
    node->acl.owner_gid = 0u;
    node->acl.owner_perms = owner_perms;
    node->acl.group_perms = group_perms;
    node->acl.other_perms = other_perms;
}

static void try_mount_wofs(void) {
#if !WOOS_ENABLE_WOFS
    return;
#endif

    if (!g_vfs_ready || g_wofs_mount_attempted) {
        return;
    }
    g_wofs_mount_attempted = 1u;

    if (!storage_read_sectors(g_wofs_start_lba, 1u, g_sector_buffer)) {
        return;
    }

    const vfs_wofs_superblock_t* sb = (const vfs_wofs_superblock_t*)g_sector_buffer;
    if (read_magic_u32(sb->magic) != VFS_WOFS_MAGIC_U32 || sb->version != VFS_WOFS_VERSION || sb->entry_count == 0u
        || sb->entry_count > (VFS_MAX_NODES - 1u) || sb->entry_count > VFS_WOFS_MAX_ENTRY_COUNT) {
        return;
    }

    if (!storage_read_sectors(g_wofs_start_lba + sb->dir_lba, 1u, g_sector_buffer)) {
        return;
    }

    const vfs_wofs_dirent_t* entries = (const vfs_wofs_dirent_t*)g_sector_buffer;
    for (uint16_t i = 0u; i < sb->entry_count; i++) {
        const vfs_wofs_dirent_t* entry = &entries[i];
        if (entry->name[0] == '\0' || entry->size_bytes == 0u || g_node_count >= VFS_MAX_NODES) {
            continue;
        }

        vfs_node_t* node = &g_nodes[g_node_count];
        copy_dirent_name(node->name, entry->name, sizeof(node->name));
        node->type = VFS_NODE_FILE;
        node->parent = VFS_ROOT_NODE;
        node->first_lba = entry->first_lba;
        node->size_bytes = entry->size_bytes;
        set_default_acl(node, VFS_PERM_READ, VFS_PERM_READ, VFS_PERM_READ);
        g_node_count++;
    }
}

void vfs_init(void) {
    g_vfs_ready = 0u;
    g_wofs_mount_attempted = 0u;
    g_node_count = 0u;
    g_wofs_start_lba = VFS_BOOT_SECTORS;
    g_current_uid = 0u;
    g_current_gid = 0u;

    for (uint8_t i = 0u; i < VFS_MAX_HANDLES; i++) {
        g_handles[i].in_use = 0u;
    }

    // Корневой каталог существует всегда, даже если FS-образ невалиден.
    copy_name(g_nodes[0].name, "/", sizeof(g_nodes[0].name));
    g_nodes[0].type = VFS_NODE_DIR;
    g_nodes[0].parent = VFS_ROOT_NODE;
    g_nodes[0].first_lba = 0u;
    g_nodes[0].size_bytes = 0u;
    set_default_acl(&g_nodes[0], VFS_PERM_LIST, VFS_PERM_LIST, VFS_PERM_LIST);
    g_node_count = 1u;

    if (!storage_is_ready()) {
        return;
    }

    uint64_t kernel_end = (uint64_t)&__kernel_end;
    if (kernel_end > VFS_KERNEL_LOAD_ADDR) {
        uint32_t kernel_size = (uint32_t)(kernel_end - VFS_KERNEL_LOAD_ADDR);
        uint32_t kernel_sectors = (kernel_size + STORAGE_SECTOR_SIZE - 1u) / STORAGE_SECTOR_SIZE;
        g_wofs_start_lba = VFS_BOOT_SECTORS + kernel_sectors;
    }

#if !WOOS_ENABLE_WOFS
    // Безопасный дефолтный профиль: фиксированный read-only файл из boot-sector.
    // WOFS можно включить отдельно через сборочный флаг WOFS=1 после диагностики.
    copy_name(g_nodes[1].name, "bootsect.bin", sizeof(g_nodes[1].name));
    g_nodes[1].type = VFS_NODE_FILE;
    g_nodes[1].parent = VFS_ROOT_NODE;
    g_nodes[1].first_lba = 0u;
    g_nodes[1].size_bytes = STORAGE_SECTOR_SIZE;
    set_default_acl(&g_nodes[1], VFS_PERM_READ, VFS_PERM_READ, VFS_PERM_READ);

    g_node_count = 2u;
    g_vfs_ready = 1u;
    return;
#endif
    // В WOFS-режиме монтирование делаем лениво при первом файловом обращении,
    // чтобы не добавлять агрессивные disk-read операции в ранний init-path.
    g_vfs_ready = 1u;
}

uint8_t vfs_is_ready(void) {
    return g_vfs_ready;
}

void vfs_set_identity(uint16_t uid, uint16_t gid) {
    g_current_uid = uid;
    g_current_gid = gid;
}

int32_t vfs_open(const char* path) {
    if (!g_vfs_ready || path == 0) {
        return -1;
    }

    if (str_equals(path, "/")) {
        if (!acl_allows(&g_nodes[VFS_ROOT_NODE], VFS_PERM_LIST)) {
            return -1;
        }
        return allocate_handle(VFS_ROOT_NODE);
    }

    if (path[0] == '/' && path[1] != '\0') {
        path++;
    }

    // Ленивый mount WOFS нужен только для доступа к файловым нодам.
    // Операции с корнем не должны автоматически дёргать disk-read.
    try_mount_wofs();

    for (uint8_t node = 0u; node < g_node_count; node++) {
        if (g_nodes[node].parent == VFS_ROOT_NODE && str_equals(g_nodes[node].name, path)) {
            if (!acl_allows(&g_nodes[node], VFS_PERM_READ)) {
                return -1;
            }
            return allocate_handle(node);
        }
    }

    return -1;
}

uint32_t vfs_read(int32_t handle, void* buffer, uint32_t bytes) {
    if (!g_vfs_ready || buffer == 0 || bytes == 0u || handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return 0u;
    }

    vfs_handle_t* h = &g_handles[handle];
    if (!h->in_use) {
        return 0u;
    }

    vfs_node_t* node = &g_nodes[h->node_index];
    if (node->type != VFS_NODE_FILE || h->cursor >= node->size_bytes || !acl_allows(node, VFS_PERM_READ)) {
        return 0u;
    }

    uint32_t remaining_file = node->size_bytes - h->cursor;
    uint32_t to_read = min_u32(remaining_file, bytes);
    uint8_t* dst = (uint8_t*)buffer;
    uint32_t copied = 0u;

    while (copied < to_read) {
        uint32_t absolute_offset = h->cursor + copied;
        uint32_t sector_offset = absolute_offset % STORAGE_SECTOR_SIZE;
        uint32_t lba = g_wofs_start_lba + node->first_lba + (absolute_offset / STORAGE_SECTOR_SIZE);

        if (!storage_read_sectors(lba, 1u, g_sector_buffer)) {
            break;
        }

        uint32_t chunk = min_u32(STORAGE_SECTOR_SIZE - sector_offset, to_read - copied);
        for (uint32_t i = 0u; i < chunk; i++) {
            dst[copied + i] = g_sector_buffer[sector_offset + i];
        }

        copied += chunk;
    }

    h->cursor += copied;
    return copied;
}

int32_t vfs_readdir(int32_t handle, vfs_dirent_t* out_entry) {
    if (!g_vfs_ready || out_entry == 0 || handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return -1;
    }

    vfs_handle_t* h = &g_handles[handle];
    if (!h->in_use) {
        return -1;
    }

    vfs_node_t* node = &g_nodes[h->node_index];
    if (node->type != VFS_NODE_DIR || !acl_allows(node, VFS_PERM_LIST)) {
        return -1;
    }

    for (; h->dir_scan_index < g_node_count; h->dir_scan_index++) {
        const vfs_node_t* child = &g_nodes[h->dir_scan_index];
        if (child->parent != h->node_index || h->dir_scan_index == h->node_index) {
            continue;
        }

        copy_name(out_entry->name, child->name, sizeof(out_entry->name));
        out_entry->is_dir = (uint8_t)(child->type == VFS_NODE_DIR);
        out_entry->size = child->size_bytes;
        h->dir_scan_index++;
        return 1;
    }

    return 0;
}

void vfs_close(int32_t handle) {
    if (handle < 0 || handle >= (int32_t)VFS_MAX_HANDLES) {
        return;
    }

    g_handles[handle].in_use = 0u;
    g_handles[handle].node_index = 0u;
    g_handles[handle].cursor = 0u;
    g_handles[handle].dir_scan_index = 0u;
}
