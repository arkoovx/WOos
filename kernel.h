#ifndef WOOS_KERNEL_H
#define WOOS_KERNEL_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef signed short       int16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

#define BOOT_INFO_MAGIC_EXPECTED 0x31424957u
#define BOOT_INFO_VERSION_V1     0x0001u

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
} video_info_t;

#endif
