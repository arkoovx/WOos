#include "net.h"
#include "drivers/virtio_net.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
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

    ip_addr_t dns_server;
    IP4_ADDR(&dns_server, 10, 0, 2, 3);
    dns_setserver(0, &dns_server);
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

typedef struct {
    uint8_t resolved;
    uint8_t failed;
    ip_addr_t addr;
} dns_query_ctx_t;

static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    (void)name;
    dns_query_ctx_t* ctx = (dns_query_ctx_t*)callback_arg;
    if (ipaddr) {
        ctx->addr = *ipaddr;
        ctx->resolved = 1;
    } else {
        ctx->failed = 1;
    }
}

int32_t net_resolve_host(const char* hostname, char* out_ip, uint32_t max_len) {
    if (!hostname || !out_ip || max_len < 16) return -1;

    ip_addr_t addr;
    if (ip4addr_aton(hostname, &addr)) {
        ip4addr_ntoa_r(&addr, out_ip, (int)max_len);
        return 0;
    }

    dns_query_ctx_t ctx = {0, 0, {0}};
    err_t err = dns_gethostbyname(hostname, &addr, dns_callback, &ctx);
    if (err == ERR_OK) {
        ip4addr_ntoa_r(&addr, out_ip, (int)max_len);
        return 0;
    }
    if (err != ERR_INPROGRESS) {
        return -1;
    }

    extern void thread_yield(void);
    uint64_t start_tsc = rdtsc();
    uint64_t timeout_tsc = 3000ULL * g_tsc_per_ms;

    while (!ctx.resolved && !ctx.failed) {
        net_poll();
        if ((rdtsc() - start_tsc) > timeout_tsc) {
            return -1; // timeout
        }
        thread_yield();
    }

    if (ctx.resolved) {
        ip4addr_ntoa_r(&ctx.addr, out_ip, (int)max_len);
        return 0;
    }
    return -1;
}

static void str_cat(char* dst, const char* src, uint32_t max_len) {
    uint32_t d_len = 0;
    while (dst[d_len] != '\0' && d_len < max_len) d_len++;
    while (*src != '\0' && d_len + 1 < max_len) {
        dst[d_len++] = *src++;
    }
    dst[d_len] = '\0';
}

static void uint_cat(char* dst, uint32_t val, uint32_t max_len) {
    char buf[16];
    int idx = 0;
    if (val == 0) {
        buf[idx++] = '0';
    } else {
        char temp[16];
        int t_idx = 0;
        while (val > 0) {
            temp[t_idx++] = '0' + (val % 10);
            val /= 10;
        }
        while (t_idx > 0) {
            buf[idx++] = temp[--t_idx];
        }
    }
    buf[idx] = '\0';
    str_cat(dst, buf, max_len);
}

void net_get_config(char* out_buf, uint32_t max_len) {
    if (!out_buf || max_len == 0) return;
    out_buf[0] = '\0';

    extern struct netif g_virtio_netif;
    const net_status_t* st = net_get_status();

    char ip_str[20] = "0.0.0.0";
    char mask_str[20] = "0.0.0.0";
    char gw_str[20] = "0.0.0.0";
    char dns_str[20] = "10.0.2.3";

    if (st->active) {
        ip4addr_ntoa_r(netif_ip4_addr(&g_virtio_netif), ip_str, sizeof(ip_str));
        ip4addr_ntoa_r(netif_ip4_netmask(&g_virtio_netif), mask_str, sizeof(mask_str));
        ip4addr_ntoa_r(netif_ip4_gw(&g_virtio_netif), gw_str, sizeof(gw_str));
    }
    const ip_addr_t* dns = dns_getserver(0);
    if (dns) {
        ip4addr_ntoa_r(dns, dns_str, sizeof(dns_str));
    }

    str_cat(out_buf, "eth0: flags=4163<UP,BROADCAST,RUNNING> mtu 1500\n", max_len);
    str_cat(out_buf, "      inet ", max_len);
    str_cat(out_buf, ip_str, max_len);
    str_cat(out_buf, " netmask ", max_len);
    str_cat(out_buf, mask_str, max_len);
    str_cat(out_buf, " gateway ", max_len);
    str_cat(out_buf, gw_str, max_len);
    str_cat(out_buf, "\n      dns ", max_len);
    str_cat(out_buf, dns_str, max_len);
    str_cat(out_buf, "\n      RX packets ", max_len);
    uint_cat(out_buf, st->rx_packets, max_len);
    str_cat(out_buf, "  TX packets ", max_len);
    uint_cat(out_buf, st->tx_packets, max_len);
    str_cat(out_buf, "\n", max_len);
}

// Заглушки для ACD (Address Conflict Detection), если lwip их требует
void acd_netif_ip_addr_changed(void* a, void* b, void* c) { (void)a; (void)b; (void)c; }
void acd_network_changed_link_down(void* a) { (void)a; }
void acd_start(void* a, void* b) { (void)a; (void)b; }
void acd_add(void* a, void* b, void* c) { (void)a; (void)b; (void)c; }
void acd_remove(void* a) { (void)a; }
void acd_arp_reply(void* a, void* b) { (void)a; (void)b; }
void acd_tmr(void) {}
