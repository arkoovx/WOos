#include "virtio_gpu.h"

#include "pci.h"

#define VIRTIO_VENDOR_ID 0x1AF4u
#define VIRTIO_GPU_DEVICE_ID_MODERN 0x1050u
#define VIRTIO_GPU_DEVICE_ID_LEGACY 0x1012u

static virtio_gpu_status_t g_virtio_gpu = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

static void apply_device_info(const pci_device_info_t* dev) {
    g_virtio_gpu.detected = 1u;
    g_virtio_gpu.pci_bus = dev->bus;
    g_virtio_gpu.pci_slot = dev->slot;
    g_virtio_gpu.pci_func = dev->func;
    g_virtio_gpu.bar0 = dev->bar0;
    g_virtio_gpu.bar1 = dev->bar1;
}

void virtio_gpu_init(video_info_t* info) {
    // Сейчас ядро использует уже подготовленный stage2 framebuffer.
    // Этот драйвер делает безопасный probing virtio-gpu по PCI,
    // чтобы режим `-vga virtio` определялся и не ломал текущий вывод.
    (void)info;

    pci_device_info_t dev;

    if (pci_find_device_by_id(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID_MODERN, &dev)) {
        apply_device_info(&dev);
        g_virtio_gpu.modern_device = 1u;
        return;
    }

    if (pci_find_device_by_id(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID_LEGACY, &dev)) {
        apply_device_info(&dev);
        g_virtio_gpu.legacy_device = 1u;
        return;
    }

    // Fallback: `-vga virtio` в части конфигураций отдаёт VGA-compatible
    // устройство через тот же vendor (class=display). Фиксируем его как найденное.
    if (pci_find_display_controller(VIRTIO_VENDOR_ID, &dev)) {
        apply_device_info(&dev);
    }
}

const virtio_gpu_status_t* virtio_gpu_status(void) {
    return &g_virtio_gpu;
}
