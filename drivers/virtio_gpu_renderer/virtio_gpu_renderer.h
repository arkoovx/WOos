#ifndef WOOS_VIRTIO_GPU_RENDERER_H
#define WOOS_VIRTIO_GPU_RENDERER_H

#include "../../kernel.h"

typedef struct virtio_gpu_renderer_status {
    uint8_t detected;
    uint8_t active;
    uint8_t modern_device;
    uint8_t legacy_device;
    uint8_t virgl_enabled;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    uint32_t pci_bars[6];
    uint64_t fallback_framebuffer;
    uint64_t active_framebuffer;
} virtio_gpu_renderer_status_t;

void virtio_gpu_renderer_init(video_info_t* info);
void virtio_gpu_renderer_present_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
uint8_t virtio_gpu_renderer_is_active(void);
uint32_t virtio_gpu_renderer_readpixel(video_info_t* info, uint16_t x, uint16_t y);
void virtio_gpu_renderer_writepixel(video_info_t* info, uint16_t x, uint16_t y, uint32_t color);
void virtio_gpu_renderer_fill(video_info_t* info, uint32_t color);
void virtio_gpu_renderer_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
void virtio_gpu_renderer_draw_glyph(
    video_info_t* info,
    uint16_t x,
    uint16_t y,
    const uint8_t* glyph,
    uint8_t glyph_w,
    uint8_t glyph_h,
    uint32_t color,
    uint32_t bg_color
);
const virtio_gpu_renderer_status_t* virtio_gpu_renderer_status(void);

#endif
