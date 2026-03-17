#include "virtio_gpu_renderer.h"

#include "../../pci.h"

#define VIRTIO_VENDOR_ID 0x1AF4u
#define VIRTIO_GPU_DEVICE_ID_MODERN 0x1050u
#define VIRTIO_GPU_DEVICE_ID_LEGACY 0x1012u

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT 0xCFCu

#define PCI_CAP_ID_VENDOR_SPECIFIC 0x09u

#define VIRTIO_PCI_CAP_COMMON_CFG 1u
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2u
#define VIRTIO_PCI_CAP_ISR_CFG 3u
#define VIRTIO_PCI_CAP_DEVICE_CFG 4u

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01u
#define VIRTIO_STATUS_DRIVER 0x02u
#define VIRTIO_STATUS_DRIVER_OK 0x04u
#define VIRTIO_STATUS_FEATURES_OK 0x08u
#define VIRTIO_STATUS_FAILED 0x80u

#define VIRTQ_DESC_F_NEXT 1u
#define VIRTQ_DESC_F_WRITE 2u

#define VIRTIO_GPU_CONTROL_QUEUE 0u
#define VIRTIO_GPU_QUEUE_SIZE 8u
#define VIRTIO_GPU_RESOURCE_ID 1u

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100u
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101u
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103u
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104u
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105u
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106u

#define VIRTIO_GPU_RESP_OK_NODATA 0x1100u
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101u

#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM 2u

// VIRTIO_GPU_F_VIRGL: позволяет хосту принимать virgl-совместимый командный поток.
#define VIRTIO_GPU_F_VIRGL (1u << 0)

#define VIRTIO_GPU_DRAW_SURFACE_BASE 0x01800000ull
#define VIRTIO_GPU_DRAW_SURFACE_CAPACITY (8u * 1024u * 1024u)

#ifndef WOOS_ENABLE_VIRTIO_GPU
#define WOOS_ENABLE_VIRTIO_GPU 1
#endif

typedef struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_GPU_QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed)) virtq_avail_t;

typedef struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_GPU_QUEUE_SIZE];
    uint16_t avail_event;
} __attribute__((packed)) virtq_used_t;

typedef struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_ctrl_hdr_t;

typedef struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_rect_t;

typedef struct virtio_gpu_resource_create_2d {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_resource_create_2d_t;

typedef struct virtio_gpu_resource_attach_backing {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed)) virtio_gpu_resource_attach_backing_t;

typedef struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_mem_entry_t;

typedef struct virtio_gpu_set_scanout {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t rect;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed)) virtio_gpu_set_scanout_t;

typedef struct virtio_gpu_transfer_to_host_2d {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t rect;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_transfer_to_host_2d_t;

typedef struct virtio_gpu_resource_flush {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t rect;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_resource_flush_t;

typedef struct virtio_gpu_queue_buffers {
    virtq_desc_t desc[VIRTIO_GPU_QUEUE_SIZE];
    virtq_avail_t avail;
    virtq_used_t used;
} __attribute__((packed, aligned(16))) virtio_gpu_queue_buffers_t;

typedef struct virtio_gpu_req_attach_backing {
    virtio_gpu_resource_attach_backing_t req;
    virtio_gpu_mem_entry_t entry;
} __attribute__((packed)) virtio_gpu_req_attach_backing_t;

typedef struct virtio_gpu_req_set_scanout {
    virtio_gpu_set_scanout_t req;
} __attribute__((packed)) virtio_gpu_req_set_scanout_t;

typedef struct virtio_gpu_req_create_2d {
    virtio_gpu_resource_create_2d_t req;
} __attribute__((packed)) virtio_gpu_req_create_2d_t;

typedef struct virtio_gpu_req_get_display_info {
    virtio_gpu_ctrl_hdr_t req;
} __attribute__((packed)) virtio_gpu_req_get_display_info_t;

typedef struct virtio_gpu_req_transfer {
    virtio_gpu_transfer_to_host_2d_t req;
} __attribute__((packed)) virtio_gpu_req_transfer_t;

typedef struct virtio_gpu_req_flush {
    virtio_gpu_resource_flush_t req;
} __attribute__((packed)) virtio_gpu_req_flush_t;

typedef struct virtio_gpu_resp_header {
    virtio_gpu_ctrl_hdr_t resp;
} __attribute__((packed)) virtio_gpu_resp_header_t;

typedef struct virtio_gpu_config {
    volatile uint32_t events_read;
    volatile uint32_t events_clear;
    volatile uint32_t num_scanouts;
    volatile uint32_t num_capsets;
} __attribute__((packed)) virtio_gpu_config_t;

typedef struct virtio_pci_common_cfg {
    volatile uint32_t device_feature_select;
    volatile uint32_t device_feature;
    volatile uint32_t driver_feature_select;
    volatile uint32_t driver_feature;
    volatile uint16_t msix_config;
    volatile uint16_t num_queues;
    volatile uint8_t device_status;
    volatile uint8_t config_generation;

    volatile uint16_t queue_select;
    volatile uint16_t queue_size;
    volatile uint16_t queue_msix_vector;
    volatile uint16_t queue_enable;
    volatile uint16_t queue_notify_off;
    volatile uint64_t queue_desc;
    volatile uint64_t queue_avail;
    volatile uint64_t queue_used;
} __attribute__((packed)) virtio_pci_common_cfg_t;

typedef struct virtio_gpu_transport {
    volatile virtio_pci_common_cfg_t* common;
    volatile uint8_t* notify_base;
    volatile uint8_t* isr;
    volatile virtio_gpu_config_t* device_cfg;
    uint32_t notify_off_multiplier;
    uint16_t notify_off;
    uint16_t last_used_idx;
    uint8_t ready;
} virtio_gpu_transport_t;

static virtio_gpu_renderer_status_t g_renderer = {
    0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u,
    0u, 0u,
    0ull, 0ull
};

static virtio_gpu_transport_t g_transport = {0};
static virtio_gpu_queue_buffers_t g_queue;
static uint8_t* const g_draw_surface = (uint8_t*)(uint64_t)VIRTIO_GPU_DRAW_SURFACE_BASE;
static uint8_t g_draw_surface_enabled = 0u;

static virtio_gpu_req_get_display_info_t g_req_display_info;
static virtio_gpu_resp_header_t g_resp_display_info;
static virtio_gpu_req_create_2d_t g_req_create;
static virtio_gpu_resp_header_t g_resp_create;
static virtio_gpu_req_attach_backing_t g_req_attach;
static virtio_gpu_resp_header_t g_resp_attach;
static virtio_gpu_req_set_scanout_t g_req_scanout;
static virtio_gpu_resp_header_t g_resp_scanout;
static virtio_gpu_req_transfer_t g_req_transfer;
static virtio_gpu_resp_header_t g_resp_transfer;
static virtio_gpu_req_flush_t g_req_flush;
static virtio_gpu_resp_header_t g_resp_flush;

static inline void io_outl(uint16_t port, uint32_t value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t io_inl(uint16_t port) {
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void io_fence(void) {
    __asm__ __volatile__("" ::: "memory");
}

static inline uint8_t bytes_per_pixel(const video_info_t* info) {
    uint8_t bytes = (uint8_t)(info->bpp / 8u);
    return (bytes == 0u) ? 4u : bytes;
}

static uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000u
        | ((uint32_t)bus << 16)
        | ((uint32_t)slot << 11)
        | ((uint32_t)func << 8)
        | (uint32_t)(offset & 0xFCu);

    io_outl(PCI_CONFIG_ADDRESS_PORT, address);
    return io_inl(PCI_CONFIG_DATA_PORT);
}

static uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_config_read_dword(bus, slot, func, (uint8_t)(offset & 0xFCu));
    uint8_t shift = (uint8_t)((offset & 0x2u) * 8u);
    return (uint16_t)((value >> shift) & 0xFFFFu);
}

static uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_config_read_dword(bus, slot, func, (uint8_t)(offset & 0xFCu));
    uint8_t shift = (uint8_t)((offset & 0x3u) * 8u);
    return (uint8_t)((value >> shift) & 0xFFu);
}

static uint8_t pci_get_mmio_bar_base(uint8_t bar_index, uint64_t* out_base) {
    uint32_t bar = 0u;
    uint32_t bar_next = 0u;

    if (bar_index == 0u) {
        bar = g_renderer.bar0;
        bar_next = g_renderer.bar1;
    } else if (bar_index == 1u) {
        bar = g_renderer.bar1;
    } else {
        return 0u;
    }

    // Используем только MMIO BAR. I/O BAR для virtio-pci modern cfg не подходит.
    if ((bar & 0x1u) != 0u) {
        return 0u;
    }

    uint8_t bar_type = (uint8_t)((bar >> 1u) & 0x3u);
    uint64_t base = (uint64_t)(bar & ~0xFu);

    if (bar_type == 0x2u) {
        // 64-битный BAR: старшая половина приходит из следующего регистра BAR.
        base |= ((uint64_t)bar_next << 32u);
    } else if (bar_type != 0x0u) {
        return 0u;
    }

    *out_base = base;
    return 1u;
}

static void apply_device_info(const pci_device_info_t* dev) {
    g_renderer.detected = 1u;
    g_renderer.pci_bus = dev->bus;
    g_renderer.pci_slot = dev->slot;
    g_renderer.pci_func = dev->func;
    g_renderer.bar0 = dev->bar0;
    g_renderer.bar1 = dev->bar1;
}

static uint8_t virtio_gpu_locate_capabilities(void) {
    uint8_t bus = g_renderer.pci_bus;
    uint8_t slot = g_renderer.pci_slot;
    uint8_t func = g_renderer.pci_func;

    uint16_t status = pci_config_read_word(bus, slot, func, 0x06u);
    if ((status & 0x10u) == 0u) {
        return 0u;
    }

    uint8_t cap_ptr = (uint8_t)(pci_config_read_byte(bus, slot, func, 0x34u) & 0xFCu);

    while (cap_ptr >= 0x40u && cap_ptr != 0u) {
        uint8_t cap_id = pci_config_read_byte(bus, slot, func, cap_ptr);
        uint8_t next = (uint8_t)(pci_config_read_byte(bus, slot, func, (uint8_t)(cap_ptr + 1u)) & 0xFCu);

        if (cap_id == PCI_CAP_ID_VENDOR_SPECIFIC) {
            uint8_t cfg_type = pci_config_read_byte(bus, slot, func, (uint8_t)(cap_ptr + 3u));
            uint8_t bar = pci_config_read_byte(bus, slot, func, (uint8_t)(cap_ptr + 4u));
            uint32_t offset = pci_config_read_dword(bus, slot, func, (uint8_t)(cap_ptr + 8u));

            uint64_t bar_base = 0ull;
            if (pci_get_mmio_bar_base(bar, &bar_base)) {
                uint64_t region_addr = bar_base + (uint64_t)offset;

                // Stage2 сейчас identity-map'ит только нижние 4 ГиБ.
                // Если MMIO-регион virtio лежит выше, доступ к нему вызовет #PF.
                // В таком случае безопасно уходим в software-framebuffer fallback.
                if (region_addr <= 0xFFFFFFFFull) {
                    volatile uint8_t* region = (volatile uint8_t*)region_addr;

                    if (cfg_type == VIRTIO_PCI_CAP_COMMON_CFG) {
                        g_transport.common = (volatile virtio_pci_common_cfg_t*)region;
                    } else if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                        g_transport.notify_base = region;
                        g_transport.notify_off_multiplier = pci_config_read_dword(bus, slot, func, (uint8_t)(cap_ptr + 16u));
                    } else if (cfg_type == VIRTIO_PCI_CAP_ISR_CFG) {
                        g_transport.isr = region;
                    } else if (cfg_type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                        g_transport.device_cfg = (volatile virtio_gpu_config_t*)region;
                    }
                }
            }
        }

        cap_ptr = next;
    }

    return (g_transport.common != 0 && g_transport.notify_base != 0 && g_transport.device_cfg != 0) ? 1u : 0u;
}

static void virtio_gpu_set_failed(void) {
    if (g_transport.common != 0) {
        g_transport.common->device_status |= VIRTIO_STATUS_FAILED;
    }

    g_transport.ready = 0u;
    g_renderer.active = 0u;
}

static uint8_t virtio_gpu_setup_queue(void) {
    volatile virtio_pci_common_cfg_t* common = g_transport.common;

    common->queue_select = VIRTIO_GPU_CONTROL_QUEUE;

    if (common->queue_size == 0u || common->queue_size < VIRTIO_GPU_QUEUE_SIZE) {
        return 0u;
    }

    common->queue_size = VIRTIO_GPU_QUEUE_SIZE;

    g_transport.notify_off = common->queue_notify_off;

    common->queue_desc = (uint64_t)&g_queue.desc[0];
    common->queue_avail = (uint64_t)&g_queue.avail;
    common->queue_used = (uint64_t)&g_queue.used;

    common->queue_enable = 1u;

    g_queue.avail.flags = 0u;
    g_queue.avail.idx = 0u;
    g_queue.used.flags = 0u;
    g_queue.used.idx = 0u;

    g_transport.last_used_idx = 0u;

    return 1u;
}

static void virtio_gpu_notify_queue(void) {
    uint64_t offset = (uint64_t)g_transport.notify_off * (uint64_t)g_transport.notify_off_multiplier;
    volatile uint16_t* notify_addr = (volatile uint16_t*)(g_transport.notify_base + offset);
    *notify_addr = VIRTIO_GPU_CONTROL_QUEUE;
}

static uint8_t virtio_gpu_submit_request(void* req, uint32_t req_len, void* resp, uint32_t resp_len) {
    if (!g_transport.ready) {
        return 0u;
    }

    g_queue.desc[0].addr = (uint64_t)req;
    g_queue.desc[0].len = req_len;
    g_queue.desc[0].flags = VIRTQ_DESC_F_NEXT;
    g_queue.desc[0].next = 1u;

    g_queue.desc[1].addr = (uint64_t)resp;
    g_queue.desc[1].len = resp_len;
    g_queue.desc[1].flags = VIRTQ_DESC_F_WRITE;
    g_queue.desc[1].next = 0u;

    uint16_t avail_idx = g_queue.avail.idx;
    g_queue.avail.ring[avail_idx % VIRTIO_GPU_QUEUE_SIZE] = 0u;

    io_fence();
    g_queue.avail.idx = (uint16_t)(avail_idx + 1u);
    io_fence();

    virtio_gpu_notify_queue();

    uint32_t spin = 0u;
    while (g_queue.used.idx == g_transport.last_used_idx) {
        spin++;
        if (spin > 10000000u) {
            return 0u;
        }
        __asm__ __volatile__("pause");
    }

    g_transport.last_used_idx = g_queue.used.idx;

    virtio_gpu_ctrl_hdr_t* hdr = (virtio_gpu_ctrl_hdr_t*)resp;
    if (hdr->type != VIRTIO_GPU_RESP_OK_NODATA && hdr->type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
        return 0u;
    }

    return 1u;
}

static uint8_t virtio_gpu_initialize_pipe(video_info_t* info) {
    g_req_display_info.req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    g_req_display_info.req.flags = 0u;
    g_req_display_info.req.fence_id = 0u;
    g_req_display_info.req.ctx_id = 0u;
    g_req_display_info.req.padding = 0u;

    if (!virtio_gpu_submit_request(&g_req_display_info, sizeof(g_req_display_info), &g_resp_display_info, sizeof(g_resp_display_info))) {
        return 0u;
    }

    g_req_create.req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    g_req_create.req.hdr.flags = 0u;
    g_req_create.req.hdr.fence_id = 0u;
    g_req_create.req.hdr.ctx_id = 0u;
    g_req_create.req.hdr.padding = 0u;
    g_req_create.req.resource_id = VIRTIO_GPU_RESOURCE_ID;
    g_req_create.req.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    g_req_create.req.width = info->width;
    g_req_create.req.height = info->height;

    if (!virtio_gpu_submit_request(&g_req_create, sizeof(g_req_create), &g_resp_create, sizeof(g_resp_create))) {
        return 0u;
    }

    g_req_attach.req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    g_req_attach.req.hdr.flags = 0u;
    g_req_attach.req.hdr.fence_id = 0u;
    g_req_attach.req.hdr.ctx_id = 0u;
    g_req_attach.req.hdr.padding = 0u;
    g_req_attach.req.resource_id = VIRTIO_GPU_RESOURCE_ID;
    g_req_attach.req.nr_entries = 1u;
    g_req_attach.entry.addr = g_renderer.active_framebuffer;
    g_req_attach.entry.length = (uint32_t)info->pitch * (uint32_t)info->height;
    g_req_attach.entry.padding = 0u;

    if (!virtio_gpu_submit_request(&g_req_attach, sizeof(g_req_attach), &g_resp_attach, sizeof(g_resp_attach))) {
        return 0u;
    }

    g_req_scanout.req.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    g_req_scanout.req.hdr.flags = 0u;
    g_req_scanout.req.hdr.fence_id = 0u;
    g_req_scanout.req.hdr.ctx_id = 0u;
    g_req_scanout.req.hdr.padding = 0u;
    g_req_scanout.req.rect.x = 0u;
    g_req_scanout.req.rect.y = 0u;
    g_req_scanout.req.rect.width = info->width;
    g_req_scanout.req.rect.height = info->height;
    g_req_scanout.req.scanout_id = 0u;
    g_req_scanout.req.resource_id = VIRTIO_GPU_RESOURCE_ID;

    if (!virtio_gpu_submit_request(&g_req_scanout, sizeof(g_req_scanout), &g_resp_scanout, sizeof(g_resp_scanout))) {
        return 0u;
    }

    return 1u;
}

static uint16_t clip_end(uint16_t start, uint16_t len, uint16_t limit) {
    uint16_t end = (uint16_t)(start + len);
    return (end > limit) ? limit : end;
}

static inline uint8_t* renderer_base(video_info_t* info) {
    if (g_renderer.active && g_draw_surface_enabled) {
        (void)info;
        return g_draw_surface;
    }

    return (uint8_t*)(uint64_t)info->framebuffer;
}

uint32_t virtio_gpu_renderer_readpixel(video_info_t* info, uint16_t x, uint16_t y) {
    if (x >= info->width || y >= info->height) {
        return 0u;
    }

    uint8_t bpp = bytes_per_pixel(info);
    uint8_t* px = renderer_base(info) + ((uint64_t)y * info->pitch) + ((uint64_t)x * bpp);

    if (bpp == 2u) {
        uint16_t packed = *(uint16_t*)px;
        uint8_t r = (uint8_t)(((packed >> 11) & 0x1Fu) << 3);
        uint8_t g = (uint8_t)(((packed >> 5) & 0x3Fu) << 2);
        uint8_t b = (uint8_t)((packed & 0x1Fu) << 3);
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    if (bpp == 3u) {
        return ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | px[0];
    }

    return *(uint32_t*)px & 0x00FFFFFFu;
}

void virtio_gpu_renderer_writepixel(video_info_t* info, uint16_t x, uint16_t y, uint32_t color) {
    if (x >= info->width || y >= info->height) {
        return;
    }

    uint8_t bpp = bytes_per_pixel(info);
    uint8_t* px = renderer_base(info) + ((uint64_t)y * info->pitch) + ((uint64_t)x * bpp);

    if (bpp == 2u) {
        uint8_t r = (uint8_t)((color >> 16) & 0xFFu);
        uint8_t g = (uint8_t)((color >> 8) & 0xFFu);
        uint8_t b = (uint8_t)(color & 0xFFu);
        *(uint16_t*)px = (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
        return;
    }

    if (bpp == 3u) {
        px[0] = (uint8_t)(color & 0xFFu);
        px[1] = (uint8_t)((color >> 8) & 0xFFu);
        px[2] = (uint8_t)((color >> 16) & 0xFFu);
        return;
    }

    *(uint32_t*)px = color & 0x00FFFFFFu;
}

void virtio_gpu_renderer_fill(video_info_t* info, uint32_t color) {
    virtio_gpu_renderer_rect(info, 0, 0, info->width, info->height, color);
}

void virtio_gpu_renderer_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    if (w == 0u || h == 0u || x >= info->width || y >= info->height) {
        return;
    }

    uint16_t x_end = clip_end(x, w, info->width);
    uint16_t y_end = clip_end(y, h, info->height);

    for (uint16_t py = y; py < y_end; py++) {
        for (uint16_t px = x; px < x_end; px++) {
            virtio_gpu_renderer_writepixel(info, px, py, color);
        }
    }
}

void virtio_gpu_renderer_draw_glyph(
    video_info_t* info,
    uint16_t x,
    uint16_t y,
    const uint8_t* glyph,
    uint8_t glyph_w,
    uint8_t glyph_h,
    uint32_t color,
    uint32_t bg_color
) {
    if (glyph == 0) {
        return;
    }

    for (uint8_t gy = 0; gy < glyph_h; gy++) {
        uint8_t row = glyph[gy];
        for (uint8_t gx = 0; gx < glyph_w; gx++) {
            uint8_t bit = (uint8_t)(0x80u >> gx);
            uint32_t px_color = (row & bit) ? color : bg_color;
            virtio_gpu_renderer_writepixel(info, (uint16_t)(x + gx), (uint16_t)(y + gy), px_color);
        }
    }
}

void virtio_gpu_renderer_init(video_info_t* info) {
    pci_device_info_t dev;
    g_renderer.fallback_framebuffer = info->framebuffer;
    g_renderer.active_framebuffer = info->framebuffer;

#if !WOOS_ENABLE_VIRTIO_GPU
    // Диагностически безопасный режим по умолчанию:
    // не трогаем virtio MMIO/queue path и работаем через
    // стабильный software framebuffer из stage2.
    (void)dev;
    return;
#endif

    if (pci_find_device_by_id(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID_MODERN, &dev)) {
        apply_device_info(&dev);
        g_renderer.modern_device = 1u;
    } else if (pci_find_device_by_id(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID_LEGACY, &dev)) {
        apply_device_info(&dev);
        g_renderer.legacy_device = 1u;
    } else if (pci_find_display_controller(VIRTIO_VENDOR_ID, &dev)) {
        apply_device_info(&dev);
    }

    if (!g_renderer.detected || !g_renderer.modern_device) {
        return;
    }

    uint32_t required = (uint32_t)info->pitch * (uint32_t)info->height;
    g_draw_surface_enabled = (required <= VIRTIO_GPU_DRAW_SURFACE_CAPACITY) ? 1u : 0u;
    if (g_draw_surface_enabled) {
        g_renderer.active_framebuffer = VIRTIO_GPU_DRAW_SURFACE_BASE;
    }

    if (!virtio_gpu_locate_capabilities()) {
        return;
    }

    g_transport.common->device_status = 0u;
    g_transport.common->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
    g_transport.common->device_status |= VIRTIO_STATUS_DRIVER;

    g_transport.common->device_feature_select = 0u;
    uint32_t dev_features = g_transport.common->device_feature;
    uint32_t driver_features = 0u;

    if ((dev_features & VIRTIO_GPU_F_VIRGL) != 0u) {
        driver_features |= VIRTIO_GPU_F_VIRGL;
        g_renderer.virgl_enabled = 1u;
    }

    g_transport.common->driver_feature_select = 0u;
    g_transport.common->driver_feature = driver_features;
    g_transport.common->driver_feature_select = 1u;
    g_transport.common->driver_feature = 0u;

    g_transport.common->device_status |= VIRTIO_STATUS_FEATURES_OK;
    if ((g_transport.common->device_status & VIRTIO_STATUS_FEATURES_OK) == 0u) {
        virtio_gpu_set_failed();
        return;
    }

    if (!virtio_gpu_setup_queue()) {
        virtio_gpu_set_failed();
        return;
    }

    g_transport.common->device_status |= VIRTIO_STATUS_DRIVER_OK;
    g_transport.ready = 1u;

    if (!virtio_gpu_initialize_pipe(info)) {
        virtio_gpu_set_failed();
        return;
    }

    g_renderer.active = 1u;
}

void virtio_gpu_renderer_present_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (!g_transport.ready || w == 0u || h == 0u) {
        return;
    }

    if (x >= info->width || y >= info->height) {
        return;
    }

    uint16_t end_x = clip_end(x, w, info->width);
    uint16_t end_y = clip_end(y, h, info->height);
    uint16_t clipped_w = (uint16_t)(end_x - x);
    uint16_t clipped_h = (uint16_t)(end_y - y);

    if (clipped_w == 0u || clipped_h == 0u) {
        return;
    }

    // Dirty rectangle отправляем в control virtqueue: transfer + flush.
    g_req_transfer.req.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    g_req_transfer.req.hdr.flags = 0u;
    g_req_transfer.req.hdr.fence_id = 0u;
    g_req_transfer.req.hdr.ctx_id = 0u;
    g_req_transfer.req.hdr.padding = 0u;
    g_req_transfer.req.rect.x = x;
    g_req_transfer.req.rect.y = y;
    g_req_transfer.req.rect.width = clipped_w;
    g_req_transfer.req.rect.height = clipped_h;
    g_req_transfer.req.offset = (uint64_t)y * (uint64_t)info->pitch + (uint64_t)x * (uint64_t)bytes_per_pixel(info);
    g_req_transfer.req.resource_id = VIRTIO_GPU_RESOURCE_ID;
    g_req_transfer.req.padding = 0u;

    if (!virtio_gpu_submit_request(&g_req_transfer, sizeof(g_req_transfer), &g_resp_transfer, sizeof(g_resp_transfer))) {
        virtio_gpu_set_failed();
        return;
    }

    g_req_flush.req.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    g_req_flush.req.hdr.flags = 0u;
    g_req_flush.req.hdr.fence_id = 0u;
    g_req_flush.req.hdr.ctx_id = 0u;
    g_req_flush.req.hdr.padding = 0u;
    g_req_flush.req.rect.x = x;
    g_req_flush.req.rect.y = y;
    g_req_flush.req.rect.width = clipped_w;
    g_req_flush.req.rect.height = clipped_h;
    g_req_flush.req.resource_id = VIRTIO_GPU_RESOURCE_ID;
    g_req_flush.req.padding = 0u;

    if (!virtio_gpu_submit_request(&g_req_flush, sizeof(g_req_flush), &g_resp_flush, sizeof(g_resp_flush))) {
        virtio_gpu_set_failed();
    }
}

uint8_t virtio_gpu_renderer_is_active(void) {
    return g_renderer.active;
}

const virtio_gpu_renderer_status_t* virtio_gpu_renderer_status(void) {
    return &g_renderer;
}
