#ifndef WOOS_VFS_H
#define WOOS_VFS_H

#include "kernel.h"

typedef struct vfs_dirent {
    char name[24];
    uint8_t is_dir;
    uint32_t size;
} vfs_dirent_t;

typedef struct vfs_acl {
    uint16_t owner_uid;
    uint16_t owner_gid;
    uint8_t owner_perms;
    uint8_t group_perms;
    uint8_t other_perms;
} vfs_acl_t;

typedef enum vfs_perm {
    VFS_PERM_READ = 1u << 0,
    VFS_PERM_WRITE = 1u << 1,
    VFS_PERM_EXEC = 1u << 2,
    VFS_PERM_LIST = 1u << 3,
} vfs_perm_t;

void vfs_init(void);
uint8_t vfs_is_ready(void);
void vfs_set_identity(uint16_t uid, uint16_t gid);
int32_t vfs_open(const char* path);
uint32_t vfs_read(int32_t handle, void* buffer, uint32_t bytes);
int32_t vfs_readdir(int32_t handle, vfs_dirent_t* out_entry);
void vfs_close(int32_t handle);

#endif
