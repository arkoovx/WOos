#ifndef WOOS_VFS_H
#define WOOS_VFS_H

#include "kernel.h"

typedef struct vfs_dirent {
    char name[24];
    uint8_t is_dir;
    uint32_t size;
} vfs_dirent_t;

void vfs_init(void);
uint8_t vfs_is_ready(void);
int32_t vfs_open(const char* path);
uint32_t vfs_read(int32_t handle, void* buffer, uint32_t bytes);
int32_t vfs_readdir(int32_t handle, vfs_dirent_t* out_entry);
void vfs_close(int32_t handle);

#endif
