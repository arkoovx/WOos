#include "net.h"
#include "drivers/virtio_net.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "timer.h"
#include "kernel.h"

static net_status_t g_net_status = {0u, 0u, 0u, 0u, 0u, 0u};
static uint64_t g_last_net_poll_tsc = 0;

u32_t sys_now(void) {
    extern uint32_t timer_frequency_hz(void);
    uint32_t hz = timer_frequency_hz();
    if (hz == 0u) {
        hz = 20u;
    }
    return (timer_ticks() * 1000u) / hz;
}

void net_init(void) {
    lwip_init();
    virtio_net_init();
}

void net_poll(void) {
    // Опрашиваем сеть не чаще раза в 5 мс, чтобы избежать огромного оверхеда
    // от вызова sys_check_timeouts() на каждой итерации бесконечного цикла kmain.
    uint64_t current_tsc = rdtsc();
    if (g_last_net_poll_tsc != 0 && (current_tsc - g_last_net_poll_tsc) < (5ULL * g_tsc_per_ms)) {
        return;
    }
    g_last_net_poll_tsc = current_tsc;

    virtio_net_poll();
    
    // В NO_SYS режиме нужно вручную опрашивать таймеры lwip
    sys_check_timeouts();

    // Обновляем статус для UI
    const virtio_net_status_t* vnet = virtio_net_get_status();
    g_net_status.ready = vnet->detected;
    g_net_status.active = vnet->active;
    g_net_status.rx_packets = vnet->rx_count;
    g_net_status.tx_packets = vnet->tx_count;
    
    extern struct netif g_virtio_netif;
    if (g_net_status.active) {
        g_net_status.link_up = netif_is_link_up(&g_virtio_netif);
        g_net_status.ip_addr = ip4_addr_get_u32(netif_ip4_addr(&g_virtio_netif));
    }
}

const net_status_t* net_get_status(void) {
    return &g_net_status;
}

// Заглушки для ACD (Address Conflict Detection), если lwip их требует
void acd_netif_ip_addr_changed(void* a, void* b, void* c) { (void)a; (void)b; (void)c; }
void acd_network_changed_link_down(void* a) { (void)a; }
void acd_start(void* a, void* b) { (void)a; (void)b; }
void acd_add(void* a, void* b, void* c) { (void)a; (void)b; (void)c; }
void acd_remove(void* a) { (void)a; }
void acd_arp_reply(void* a, void* b) { (void)a; (void)b; }
void acd_tmr(void) {}
