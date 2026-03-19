#ifndef WOOS_KERNEL_H
#define WOOS_KERNEL_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

#define BOOT_INFO_MAGIC_EXPECTED 0x31424957u
#define BOOT_INFO_VERSION_V1     0x0001u
#define BOOT_INFO_VERSION_V2     0x0002u
#define BOOT_INFO_E820_MAX_ENTRIES 32u
#define BOOT_INFO_E820_TYPE_USABLE 1u

typedef struct boot_memory_region {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
} boot_memory_region_t;

typedef struct video_info {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint64_t framebuffer;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  reserved;
    uint16_t memory_region_count;
    uint16_t memory_region_capacity;
    boot_memory_region_t memory_regions[BOOT_INFO_E820_MAX_ENTRIES];
} video_info_t;

#endif
