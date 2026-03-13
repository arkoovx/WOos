#ifndef WOOS_KERNEL_H
#define WOOS_KERNEL_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef struct video_info {
    uint64_t framebuffer;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  reserved;
} video_info_t;

#endif
