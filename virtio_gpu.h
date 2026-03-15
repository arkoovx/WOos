#ifndef WOOS_VIRTIO_GPU_H
#define WOOS_VIRTIO_GPU_H

#include "kernel.h"

typedef struct virtio_gpu_status {
    uint8_t detected;
    uint8_t modern_device;
    uint8_t legacy_device;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    uint32_t bar0;
    uint32_t bar1;
} virtio_gpu_status_t;

void virtio_gpu_init(video_info_t* info);
const virtio_gpu_status_t* virtio_gpu_status(void);

#endif
