#ifndef WOOS_VFS_H
#define WOOS_VFS_H

#include "kernel.h"

// Режимы открытия файлов VFS
#define VFS_MODE_READ      0x01
#define VFS_MODE_WRITE     0x02
#define VFS_MODE_CREATE    0x04
#define VFS_MODE_TRUNC     0x08

typedef struct vfs_dirent {
    char name[24];
    uint8_t is_dir;
    uint32_t size;
} vfs_dirent_t;

void vfs_init(void);
uint8_t vfs_is_ready(void);
int32_t vfs_open(const char* path, uint8_t mode);
uint32_t vfs_read(int32_t handle, void* buffer, uint32_t bytes);
uint32_t vfs_write(int32_t handle, const void* buffer, uint32_t bytes);
int32_t vfs_seek(int32_t handle, uint32_t offset);
uint32_t vfs_tell(int32_t handle);
uint32_t vfs_size(int32_t handle);
int32_t vfs_readdir(int32_t handle, vfs_dirent_t* out_entry);
void vfs_close(int32_t handle);

#endif
