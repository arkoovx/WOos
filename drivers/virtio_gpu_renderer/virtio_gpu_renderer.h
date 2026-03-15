#ifndef WOOS_VIRTIO_GPU_RENDERER_H
#define WOOS_VIRTIO_GPU_RENDERER_H

#include "../../kernel.h"

typedef struct virtio_gpu_renderer_status {
    uint8_t detected;
    uint8_t active;
    uint8_t modern_device;
    uint8_t legacy_device;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    uint32_t bar0;
    uint32_t bar1;
    uint64_t fallback_framebuffer;
    uint64_t active_framebuffer;
} virtio_gpu_renderer_status_t;

void virtio_gpu_renderer_init(video_info_t* info);
void virtio_gpu_renderer_present_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
uint8_t virtio_gpu_renderer_is_active(void);
const virtio_gpu_renderer_status_t* virtio_gpu_renderer_status(void);

#endif
