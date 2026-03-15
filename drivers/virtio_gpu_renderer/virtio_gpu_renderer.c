#include "virtio_gpu_renderer.h"

#include "../../pci.h"

#define VIRTIO_VENDOR_ID 0x1AF4u
#define VIRTIO_GPU_DEVICE_ID_MODERN 0x1050u
#define VIRTIO_GPU_DEVICE_ID_LEGACY 0x1012u

static virtio_gpu_renderer_status_t g_renderer = {
    0u, 0u, 0u, 0u,
    0u, 0u, 0u,
    0u, 0u,
    0ull, 0ull
};

static uint64_t pci_bar_to_mmio_base(uint32_t bar) {
    // I/O BAR (bit0=1) не подходит для framebuffer; нужен memory BAR.
    if ((bar & 0x1u) != 0u) {
        return 0ull;
    }

    return (uint64_t)(bar & ~0xFu);
}

static void apply_device_info(const pci_device_info_t* dev) {
    g_renderer.detected = 1u;
    g_renderer.pci_bus = dev->bus;
    g_renderer.pci_slot = dev->slot;
    g_renderer.pci_func = dev->func;
    g_renderer.bar0 = dev->bar0;
    g_renderer.bar1 = dev->bar1;
}

void virtio_gpu_renderer_init(video_info_t* info) {
    pci_device_info_t dev;
    g_renderer.fallback_framebuffer = info->framebuffer;
    g_renderer.active_framebuffer = info->framebuffer;

    if (pci_find_device_by_id(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID_MODERN, &dev)) {
        apply_device_info(&dev);
        g_renderer.modern_device = 1u;
    } else if (pci_find_device_by_id(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID_LEGACY, &dev)) {
        apply_device_info(&dev);
        g_renderer.legacy_device = 1u;
    } else if (pci_find_display_controller(VIRTIO_VENDOR_ID, &dev)) {
        // Совместимость с конфигурациями, где virtio-vga приходит как display class.
        apply_device_info(&dev);
    }

    if (!g_renderer.detected) {
        return;
    }

    // Приоритет BAR1: в типовой конфигурации virtio-vga это VRAM aperture.
    // Если BAR1 невалиден, пробуем BAR0, но сохраняем fallback на исходный framebuffer.
    uint64_t virtio_fb = pci_bar_to_mmio_base(g_renderer.bar1);
    if (virtio_fb == 0ull) {
        virtio_fb = pci_bar_to_mmio_base(g_renderer.bar0);
    }

    if (virtio_fb == 0ull) {
        return;
    }

    info->framebuffer = virtio_fb;
    if (info->pitch == 0u) {
        info->pitch = (uint16_t)(info->width * 4u);
    }

    g_renderer.active = 1u;
    g_renderer.active_framebuffer = virtio_fb;
}

void virtio_gpu_renderer_present_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    // Текущий фундамент: dirty-rect + double-buffer pipeline уже выполняет копирование
    // в активный framebuffer внутри fb_present_rect().
    // Здесь оставлен hook под будущий virtqueue RESOURCE_FLUSH/TRANSFER_TO_HOST_2D.
    (void)info;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

uint8_t virtio_gpu_renderer_is_active(void) {
    return g_renderer.active;
}

const virtio_gpu_renderer_status_t* virtio_gpu_renderer_status(void) {
    return &g_renderer;
}
