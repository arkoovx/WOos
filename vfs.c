#include "vfs.h"

#include "storage.h"

#define VFS_MAX_NODES 4u
#define VFS_MAX_HANDLES 8u
#define VFS_ROOT_NODE 0u
#define VFS_SECTOR_BUFFER_SIZE STORAGE_SECTOR_SIZE

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
} vfs_node_t;

typedef struct vfs_handle {
    uint8_t in_use;
    uint8_t node_index;
    uint32_t cursor;
    uint16_t dir_scan_index;
} vfs_handle_t;

static uint8_t g_vfs_ready = 0u;
static vfs_node_t g_nodes[VFS_MAX_NODES];
static uint8_t g_node_count = 0u;
static vfs_handle_t g_handles[VFS_MAX_HANDLES];
static uint8_t g_sector_buffer[VFS_SECTOR_BUFFER_SIZE];

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

void vfs_init(void) {
    g_vfs_ready = 0u;
    g_node_count = 0u;

    for (uint8_t i = 0u; i < VFS_MAX_HANDLES; i++) {
        g_handles[i].in_use = 0u;
    }

    // Базовый VFS-каталог фиксированный и read-only:
    // корень "/" и один тестовый файл boot-сектора, чтобы
    // унифицировать API open/read/close/readdir для следующих PR.
    copy_name(g_nodes[0].name, "/", sizeof(g_nodes[0].name));
    g_nodes[0].type = VFS_NODE_DIR;
    g_nodes[0].parent = VFS_ROOT_NODE;
    g_nodes[0].first_lba = 0u;
    g_nodes[0].size_bytes = 0u;

    copy_name(g_nodes[1].name, "bootsect.bin", sizeof(g_nodes[1].name));
    g_nodes[1].type = VFS_NODE_FILE;
    g_nodes[1].parent = VFS_ROOT_NODE;
    g_nodes[1].first_lba = 0u;
    g_nodes[1].size_bytes = STORAGE_SECTOR_SIZE;

    g_node_count = 2u;
    g_vfs_ready = storage_is_ready();
}

uint8_t vfs_is_ready(void) {
    return g_vfs_ready;
}

int32_t vfs_open(const char* path) {
    if (!g_vfs_ready || path == 0) {
        return -1;
    }

    if (str_equals(path, "/")) {
        return allocate_handle(VFS_ROOT_NODE);
    }

    if (path[0] == '/' && path[1] != '\0') {
        path++;
    }

    for (uint8_t node = 0u; node < g_node_count; node++) {
        if (g_nodes[node].parent == VFS_ROOT_NODE && str_equals(g_nodes[node].name, path)) {
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
    if (node->type != VFS_NODE_FILE || h->cursor >= node->size_bytes) {
        return 0u;
    }

    uint32_t remaining_file = node->size_bytes - h->cursor;
    uint32_t to_read = min_u32(remaining_file, bytes);
    uint8_t* dst = (uint8_t*)buffer;
    uint32_t copied = 0u;

    while (copied < to_read) {
        uint32_t absolute_offset = h->cursor + copied;
        uint32_t sector_offset = absolute_offset % STORAGE_SECTOR_SIZE;
        uint32_t lba = node->first_lba + (absolute_offset / STORAGE_SECTOR_SIZE);

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
    if (node->type != VFS_NODE_DIR) {
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
