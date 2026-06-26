#include "virtio_net.h"
#include "../pci.h"
#include "../pmm.h"
#include "../kheap.h"
#include "../fb.h"
#include "../serial.h"

// Включаем заголовки lwip. 
// ВАЖНО: lwip должен быть в инклуд-пути компилятора.
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "netif/ethernet.h"

#define VIRTIO_VENDOR_ID 0x1AF4u
#define VIRTIO_NET_DEVICE_ID_MODERN 0x1041u
#define VIRTIO_NET_DEVICE_ID_LEGACY 0x1000u

#define VIRTIO_STATUS_ACKNOWLEDGE 0x01u
#define VIRTIO_STATUS_DRIVER 0x02u
#define VIRTIO_STATUS_DRIVER_OK 0x04u
#define VIRTIO_STATUS_FEATURES_OK 0x08u
#define VIRTIO_STATUS_FAILED 0x80u

#define VIRTIO_F_VERSION_1 32u
#define VIRTIO_NET_F_MAC 5u

#define VIRTIO_PCI_CAP_COMMON_CFG 1u
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2u
#define VIRTIO_PCI_CAP_ISR_CFG 3u
#define VIRTIO_PCI_CAP_DEVICE_CFG 4u

#define VIRTQ_DESC_F_NEXT 1u
#define VIRTQ_DESC_F_WRITE 2u

#define VIRTIO_NET_QUEUE_RX 0u
#define VIRTIO_NET_QUEUE_TX 1u
#define VIRTIO_NET_QUEUE_SIZE 128u

typedef struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_NET_QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed)) virtq_avail_t;

typedef struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRTIO_NET_QUEUE_SIZE];
    uint16_t avail_event;
} __attribute__((packed)) virtq_used_t;

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

typedef struct virtio_net_config {
    uint8_t mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
} __attribute__((packed)) virtio_net_config_t;

typedef struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    uint16_t num_buffers;
} __attribute__((packed)) virtio_net_hdr_t;

typedef struct virtqueue {
    virtq_desc_t* desc;
    virtq_avail_t* avail;
    virtq_used_t* used;
    uint16_t size;
    uint16_t last_used_idx;
    uint16_t free_head;
    uint16_t num_free;
    uint16_t notify_off;
} virtqueue_t;

static virtio_net_status_t g_net_status = {0};
static volatile virtio_pci_common_cfg_t* g_common_cfg = 0;
static volatile uint8_t* g_notify_base = 0;
static uint32_t g_notify_off_multiplier = 0;
static volatile virtio_net_config_t* g_net_cfg = 0;

static virtqueue_t g_rx_vq;
static virtqueue_t g_tx_vq;

// Буферы для пакетов.
static uint8_t* g_tx_buffers = 0;

struct netif g_virtio_netif;

// Вспомогательные функции PCI (аналогично GPU)
static inline void io_outl(uint16_t port, uint32_t value) {
    __asm__ __volatile__("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t io_inl(uint16_t port) {
    uint32_t value;
    __asm__ __volatile__("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t pci_config_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) | ((uint32_t)func << 8) | (uint32_t)(offset & 0xFCu);
    io_outl(0xCF8u, address);
    return io_inl(0xCFCu);
}

static uint8_t pci_config_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_config_read_dword(bus, slot, func, (uint8_t)(offset & 0xFCu));
    return (uint8_t)((value >> ((offset & 0x3u) * 8u)) & 0xFFu);
}

static uint16_t pci_config_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_config_read_dword(bus, slot, func, (uint8_t)(offset & 0xFCu));
    return (uint16_t)((value >> ((offset & 0x2u) * 8u)) & 0xFFFFu);
}

static uint8_t virtio_net_setup_vq(uint16_t index, virtqueue_t* vq) {
    g_common_cfg->queue_select = index;
    uint16_t size = g_common_cfg->queue_size;
    serial_printf("[Virtio-Net] setup_vq index=%d, queue_size=%d\n", index, size);
    if (size == 0 || size > VIRTIO_NET_QUEUE_SIZE) {
        serial_printf("[Virtio-Net] Warning: queue_size %d invalid, using %d\n", size, VIRTIO_NET_QUEUE_SIZE);
        size = VIRTIO_NET_QUEUE_SIZE;
        g_common_cfg->queue_size = size;
    }

    void* pages = pmm_alloc_page();
    serial_printf("[Virtio-Net] pmm_alloc_page returned %p\n", pages);
    if (!pages) {
        serial_printf("[Virtio-Net] Error: failed to allocate page for vq!\n");
        return 0;
    }

    vq->desc = (virtq_desc_t*)pages;
    vq->avail = (virtq_avail_t*)((uint8_t*)pages + size * sizeof(virtq_desc_t));
    vq->used = (virtq_used_t*)((uint8_t*)vq->avail + sizeof(virtq_avail_t) + size * sizeof(uint16_t));
    vq->size = size;
    vq->notify_off = g_common_cfg->queue_notify_off;

    g_common_cfg->queue_desc = (uint64_t)vq->desc;
    g_common_cfg->queue_avail = (uint64_t)vq->avail;
    g_common_cfg->queue_used = (uint64_t)vq->used;

    serial_printf("[Virtio-Net] queue_desc set to %p (readback: %p)\n", vq->desc, (void*)g_common_cfg->queue_desc);
    serial_printf("[Virtio-Net] queue_avail set to %p (readback: %p)\n", vq->avail, (void*)g_common_cfg->queue_avail);
    serial_printf("[Virtio-Net] queue_used set to %p (readback: %p)\n", vq->used, (void*)g_common_cfg->queue_used);

    g_common_cfg->queue_enable = 1;
    serial_printf("[Virtio-Net] queue_enable readback: %d\n", g_common_cfg->queue_enable);

    return 1;
}

static void virtio_net_notify(virtqueue_t* vq, uint16_t index) {
    volatile uint16_t* notify_addr = (volatile uint16_t*)(g_notify_base + vq->notify_off * g_notify_off_multiplier);
    *notify_addr = index;
}

// lwIP netif callbacks
static err_t virtio_net_output(struct netif *netif, struct pbuf *p) {
    (void)netif;
    if (!g_net_status.active) return ERR_IF;

    // Подготовка заголовка и данных
    virtio_net_hdr_t* hdr = (virtio_net_hdr_t*)g_tx_buffers;
    for(int i=0; i<(int)sizeof(virtio_net_hdr_t); i++) ((uint8_t*)hdr)[i] = 0;
    
    uint16_t len = pbuf_copy_partial(p, g_tx_buffers + sizeof(virtio_net_hdr_t), 1514, 0);

    g_tx_vq.desc[0].addr = (uint64_t)g_tx_buffers;
    g_tx_vq.desc[0].len = sizeof(virtio_net_hdr_t) + len;
    g_tx_vq.desc[0].flags = 0;

    uint16_t avail_idx = g_tx_vq.avail->idx;
    g_tx_vq.avail->ring[avail_idx % VIRTIO_NET_QUEUE_SIZE] = 0;
    __asm__ __volatile__("" ::: "memory");
    g_tx_vq.avail->idx = avail_idx + 1;
    __asm__ __volatile__("" ::: "memory");

    virtio_net_notify(&g_tx_vq, VIRTIO_NET_QUEUE_TX);

    // Ждем завершения передачи (синхронно для простоты)
    while(g_tx_vq.used->idx == g_tx_vq.last_used_idx) {
        __asm__ __volatile__("pause");
    }
    g_tx_vq.last_used_idx = g_tx_vq.used->idx;
    g_net_status.tx_count++;

    return ERR_OK;
}

void virtio_net_poll(void) {
    if (!g_net_status.active) return;

    while (g_rx_vq.used->idx != g_rx_vq.last_used_idx) {
        uint16_t used_idx = g_rx_vq.last_used_idx % VIRTIO_NET_QUEUE_SIZE;
        uint32_t id = g_rx_vq.used->ring[used_idx].id;
        uint32_t len = g_rx_vq.used->ring[used_idx].len;

        // len включает заголовок virtio_net_hdr
        if (len > sizeof(virtio_net_hdr_t)) {
            uint32_t packet_len = len - sizeof(virtio_net_hdr_t);
            struct pbuf* p = pbuf_alloc(PBUF_RAW, (uint16_t)packet_len, PBUF_POOL);
            if (p) {
                pbuf_take(p, (uint8_t*)g_rx_vq.desc[id].addr + sizeof(virtio_net_hdr_t), (uint16_t)packet_len);
                if (g_virtio_netif.input(p, &g_virtio_netif) != ERR_OK) {
                    pbuf_free(p);
                }
                g_net_status.rx_count++;
            }
        }

        // Возвращаем дескриптор в очередь
        uint16_t avail_idx = g_rx_vq.avail->idx;
        g_rx_vq.avail->ring[avail_idx % VIRTIO_NET_QUEUE_SIZE] = (uint16_t)id;
        __asm__ __volatile__("" ::: "memory");
        g_rx_vq.avail->idx = avail_idx + 1;
        
        g_rx_vq.last_used_idx++;
    }
    
    virtio_net_notify(&g_rx_vq, VIRTIO_NET_QUEUE_RX);
}

const virtio_net_status_t* virtio_net_get_status(void) {
    return &g_net_status;
}

static err_t virtio_net_netif_init(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->output = etharp_output;
    netif->linkoutput = virtio_net_output;
    netif->hwaddr_len = 6;
    for(int i=0; i<6; i++) netif->hwaddr[i] = g_net_status.mac[i];
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    return ERR_OK;
}

void virtio_net_init(void) {
    pci_device_info_t dev;
    serial_printf("[Virtio-Net] Searching for Virtio-Net PCI device...\n");
    if (!pci_find_device_by_id(VIRTIO_VENDOR_ID, VIRTIO_NET_DEVICE_ID_MODERN, &dev)) {
        if (!pci_find_device_by_id(VIRTIO_VENDOR_ID, VIRTIO_NET_DEVICE_ID_LEGACY, &dev)) {
            serial_printf("[Virtio-Net] Virtio-Net PCI device not found!\n");
            return;
        }
    }

    serial_printf("[Virtio-Net] Found Virtio-Net PCI device at bus %u, slot %u, func %u.\n", dev.bus, dev.slot, dev.func);
    g_net_status.detected = 1;
    
    // Упрощенный поиск capabilities (как в GPU драйвере)
    uint8_t cap_ptr = pci_config_read_byte(dev.bus, dev.slot, dev.func, 0x34u);
    while (cap_ptr != 0) {
        uint8_t cap_id = pci_config_read_byte(dev.bus, dev.slot, dev.func, cap_ptr);
        if (cap_id == 0x09u) { // PCI_CAP_ID_VENDOR_SPECIFIC
            uint8_t type = pci_config_read_byte(dev.bus, dev.slot, dev.func, cap_ptr + 3);
            uint8_t bar_idx = pci_config_read_byte(dev.bus, dev.slot, dev.func, cap_ptr + 4);
            uint32_t offset = pci_config_read_dword(dev.bus, dev.slot, dev.func, cap_ptr + 8);
            
            uint64_t bar_base = dev.bars[bar_idx] & ~0xFu;
            uint64_t addr = bar_base + offset;

            if (type == VIRTIO_PCI_CAP_COMMON_CFG) {
                g_common_cfg = (virtio_pci_common_cfg_t*)addr;
                serial_printf("[Virtio-Net] Found VIRTIO_PCI_CAP_COMMON_CFG at %p\n", (void*)addr);
            }
            else if (type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                g_notify_base = (uint8_t*)addr;
                g_notify_off_multiplier = pci_config_read_dword(dev.bus, dev.slot, dev.func, cap_ptr + 16);
                serial_printf("[Virtio-Net] Found VIRTIO_PCI_CAP_NOTIFY_CFG at %p (multiplier: %u)\n", (void*)addr, g_notify_off_multiplier);
            }
            else if (type == VIRTIO_PCI_CAP_DEVICE_CFG) {
                g_net_cfg = (virtio_net_config_t*)addr;
                serial_printf("[Virtio-Net] Found VIRTIO_PCI_CAP_DEVICE_CFG at %p\n", (void*)addr);
            }
        }
        cap_ptr = pci_config_read_byte(dev.bus, dev.slot, dev.func, cap_ptr + 1);
    }

    if (!g_common_cfg || !g_net_cfg) {
        serial_printf("[Virtio-Net] Failed to parse capabilities (common_cfg=%p, net_cfg=%p)\n", g_common_cfg, g_net_cfg);
        return;
    }

    // Reset
    g_common_cfg->device_status = 0;
    g_common_cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;

    // Features
    g_common_cfg->device_feature_select = 0;
    uint32_t features = g_common_cfg->device_feature;
    features &= (1u << VIRTIO_NET_F_MAC);
    g_common_cfg->driver_feature_select = 0;
    g_common_cfg->driver_feature = features;

    g_common_cfg->device_feature_select = 1;
    features = g_common_cfg->device_feature;
    features &= (1u << (VIRTIO_F_VERSION_1 - 32));
    g_common_cfg->driver_feature_select = 1;
    g_common_cfg->driver_feature = features;

    g_common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    if (!(g_common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        serial_printf("[Virtio-Net] Failed to negotiate features!\n");
        return;
    }

    // Queues
    if (!virtio_net_setup_vq(VIRTIO_NET_QUEUE_RX, &g_rx_vq)) {
        serial_printf("[Virtio-Net] Failed to setup RX virtqueue!\n");
        return;
    }
    if (!virtio_net_setup_vq(VIRTIO_NET_QUEUE_TX, &g_tx_vq)) {
        serial_printf("[Virtio-Net] Failed to setup TX virtqueue!\n");
        return;
    }

    // TX: 1 буфер 2KB = 1 страница
    g_tx_buffers = (uint8_t*)pmm_alloc_page();
    if (!g_tx_buffers) {
        serial_printf("[Virtio-Net] Failed to allocate TX buffer!\n");
        return;
    }

    // Заполнение RX очереди
    // VIRTIO_NET_QUEUE_SIZE = 128. Каждая страница 4KB вмещает 2 буфера по 2KB.
    uint8_t* current_page = 0;
    for(uint16_t i=0; i<VIRTIO_NET_QUEUE_SIZE; i++) {
        if (i % 2 == 0) {
            current_page = (uint8_t*)pmm_alloc_page();
        }
        
        if (!current_page) {
            serial_printf("[Virtio-Net] Failed to allocate RX page buffer at index %u!\n", i);
            break;
        }

        g_rx_vq.desc[i].addr = (uint64_t)current_page + (i % 2 == 0 ? 0 : 2048);
        g_rx_vq.desc[i].len = 2048;
        g_rx_vq.desc[i].flags = VIRTQ_DESC_F_WRITE;
        g_rx_vq.desc[i].next = 0;
        g_rx_vq.avail->ring[i] = i;
    }
    g_rx_vq.avail->idx = VIRTIO_NET_QUEUE_SIZE;
    g_rx_vq.last_used_idx = 0;

    // MAC
    for(int i=0; i<6; i++) g_net_status.mac[i] = g_net_cfg->mac[i];
    serial_printf("[Virtio-Net] MAC Address: %x:%x:%x:%x:%x:%x\n",
                  g_net_status.mac[0], g_net_status.mac[1], g_net_status.mac[2],
                  g_net_status.mac[3], g_net_status.mac[4], g_net_status.mac[5]);

    g_common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
    g_net_status.active = 1;
    serial_printf("[Virtio-Net] Interface activated.\n");

    // Init lwIP netif
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 0, 0, 0, 0);
    IP4_ADDR(&netmask, 0, 0, 0, 0);
    IP4_ADDR(&gw, 0, 0, 0, 0);

    netif_add(&g_virtio_netif, &ipaddr, &netmask, &gw, NULL, virtio_net_netif_init, ethernet_input);
    netif_set_default(&g_virtio_netif);
    netif_set_up(&g_virtio_netif);
    netif_set_link_up(&g_virtio_netif);
    serial_printf("[Virtio-Net] lwIP netif added, DHCP client started.\n");
    dhcp_start(&g_virtio_netif);
}
