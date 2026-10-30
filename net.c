#include "net.h"
#include "drivers/virtio_net.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/raw.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/icmp.h"
#include "lwip/inet_chksum.h"
#include "net_socket.h"
#include "serial.h"
#include "timer.h"
#include "kernel.h"
#include "kheap.h"
#include "vfs.h"
#include "bearssl.h"

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

    ip_addr_t dns_server2;
    IP4_ADDR(&dns_server2, 8, 8, 8, 8);
    dns_setserver(1, &dns_server2);
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
    volatile uint8_t resolved;
    volatile uint8_t failed;
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
    if (ipaddr_aton(hostname, &addr)) {
        ipaddr_ntoa_r(&addr, out_ip, (int)max_len);
        return 0;
    }

    dns_query_ctx_t ctx = {0, 0, {0}};
    err_t err = dns_gethostbyname(hostname, &addr, dns_callback, &ctx);
    if (err == ERR_OK) {
        ipaddr_ntoa_r(&addr, out_ip, (int)max_len);
        return 0;
    }
    if (err != ERR_INPROGRESS) {
        serial_printf("[DNS] gethostbyname('%s') returned error: %d\n", hostname, (int)err);
        return -1;
    }

    extern void thread_yield(void);
    uint64_t start_tsc = rdtsc();
    uint64_t timeout_tsc = 3500ULL * g_tsc_per_ms;

    while (!ctx.resolved && !ctx.failed) {
        net_poll();
        if ((rdtsc() - start_tsc) > timeout_tsc) {
            serial_printf("[DNS] Resolution for '%s' timed out (3.5s)\n", hostname);
            return -1; // timeout
        }
        thread_yield();
    }

    if (ctx.resolved) {
        ipaddr_ntoa_r(&ctx.addr, out_ip, (int)max_len);
        return 0;
    }
    serial_printf("[DNS] Resolution for '%s' failed (host not found)\n", hostname);
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

static void pad_spaces(char* dst, uint32_t target_len, uint32_t max_len) {
    uint32_t len = 0;
    while (dst[len] != '\0' && len < max_len) len++;
    while (len < target_len && len + 1 < max_len) {
        dst[len++] = ' ';
    }
    dst[len] = '\0';
}

static void hex_cat(char* dst, uint8_t byte, uint32_t max_len) {
    const char hex_chars[] = "0123456789abcdef";
    char buf[3];
    buf[0] = hex_chars[(byte >> 4) & 0x0F];
    buf[1] = hex_chars[byte & 0x0F];
    buf[2] = '\0';
    str_cat(dst, buf, max_len);
}

void net_get_arp(char* out_buf, uint32_t max_len) {
    if (!out_buf || max_len < 64) return;
    out_buf[0] = '\0';

    str_cat(out_buf, "Address                  HWtype  HWaddress           Flags Mask            Iface\n", max_len);

    int count = 0;
    for (size_t i = 0; i < 16; i++) {
        ip4_addr_t *ip = NULL;
        struct netif *nif = NULL;
        struct eth_addr *eth = NULL;
        if (etharp_get_entry(i, &ip, &nif, &eth)) {
            char ip_str[24];
            ip4addr_ntoa_r(ip, ip_str, sizeof(ip_str));

            uint32_t before = 0;
            while (out_buf[before] != '\0') before++;
            str_cat(out_buf, ip_str, max_len);
            pad_spaces(out_buf, before + 25, max_len);

            str_cat(out_buf, "ether   ", max_len);

            before = 0;
            while (out_buf[before] != '\0') before++;
            for (int b = 0; b < 6; b++) {
                if (b > 0) str_cat(out_buf, ":", max_len);
                hex_cat(out_buf, eth->addr[b], max_len);
            }
            pad_spaces(out_buf, before + 20, max_len);

            str_cat(out_buf, "C                     eth0\n", max_len);
            count++;
        }
    }
    if (count == 0) {
        str_cat(out_buf, "(ARP cache is empty - run 'ping 10.0.2.2' to populate)\n", max_len);
    }
}

int32_t net_nslookup(const char* host, char* out_buf, uint32_t max_len) {
    if (!host || !out_buf || max_len < 64) return -1;
    out_buf[0] = '\0';

    char dns_str[32] = "10.0.2.3";
    const ip_addr_t* dns = dns_getserver(0);
    if (dns) {
        ip4addr_ntoa_r(dns, dns_str, sizeof(dns_str));
    }

    str_cat(out_buf, "Server:    ", max_len);
    str_cat(out_buf, dns_str, max_len);
    str_cat(out_buf, "\nAddress:   ", max_len);
    str_cat(out_buf, dns_str, max_len);
    str_cat(out_buf, "#53\n\n", max_len);

    char ip_str[32];
    uint64_t t_start = rdtsc();
    int32_t res = net_resolve_host(host, ip_str, sizeof(ip_str));
    uint64_t t_diff = rdtsc() - t_start;
    uint32_t query_ms = (uint32_t)(t_diff / g_tsc_per_ms);
    if (query_ms == 0) query_ms = 1;

    if (res == 0) {
        str_cat(out_buf, "Non-authoritative answer:\n", max_len);
        str_cat(out_buf, "Name:      ", max_len);
        str_cat(out_buf, host, max_len);
        str_cat(out_buf, "\nAddress:   ", max_len);
        str_cat(out_buf, ip_str, max_len);
        str_cat(out_buf, "\nQuery time: ", max_len);
        uint_cat(out_buf, query_ms, max_len);
        str_cat(out_buf, " ms\n", max_len);
        return 0;
    } else {
        str_cat(out_buf, "** server can't find ", max_len);
        str_cat(out_buf, host, max_len);
        str_cat(out_buf, ": NXDOMAIN\n", max_len);
        return -1;
    }
}

typedef struct {
    uint16_t id;
    uint16_t seq;
    uint8_t received;
    uint8_t ttl;
} ping_state_t;

static volatile ping_state_t g_ping_state;

static u8_t ping_raw_recv(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
    (void)arg; (void)pcb; (void)addr;
    if (!p) return 0;

    if (p->tot_len >= (IP_HLEN + sizeof(struct icmp_echo_hdr))) {
        struct ip_hdr* iphdr = (struct ip_hdr*)p->payload;
        u8_t ip_hl = (u8_t)(IPH_HL(iphdr) * 4);
        if (p->tot_len >= (ip_hl + sizeof(struct icmp_echo_hdr))) {
            struct icmp_echo_hdr* iecho = (struct icmp_echo_hdr*)((u8_t*)p->payload + ip_hl);
            if (ICMPH_TYPE(iecho) == ICMP_ER &&
                iecho->id == g_ping_state.id &&
                iecho->seqno == lwip_htons(g_ping_state.seq)) {
                g_ping_state.ttl = IPH_TTL(iphdr);
                g_ping_state.received = 1;
                pbuf_free(p);
                return 1;
            }
        }
    }
    return 0;
}

int32_t net_ping(const char* host, uint32_t count, char* out_buf, uint32_t max_len) {
    if (!host || !out_buf || max_len < 128) return -1;
    out_buf[0] = '\0';
    if (count == 0) count = 4;
    if (count > 10) count = 10;

    ip_addr_t target_ip;
    char ip_str[32];
    if (ipaddr_aton(host, &target_ip)) {
        ipaddr_ntoa_r(&target_ip, ip_str, sizeof(ip_str));
    } else {
        if (net_resolve_host(host, ip_str, sizeof(ip_str)) != 0) {
            str_cat(out_buf, "ping: unknown host ", max_len);
            str_cat(out_buf, host, max_len);
            str_cat(out_buf, "\n", max_len);
            return -1;
        }
        ipaddr_aton(ip_str, &target_ip);
    }

    str_cat(out_buf, "PING ", max_len);
    str_cat(out_buf, host, max_len);
    str_cat(out_buf, " (", max_len);
    str_cat(out_buf, ip_str, max_len);
    str_cat(out_buf, "): 56 data bytes\n", max_len);

    struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb) {
        str_cat(out_buf, "ping: raw socket error\n", max_len);
        return -1;
    }

    g_ping_state.id = 0xAFAF;
    raw_recv(pcb, ping_raw_recv, NULL);
    raw_bind(pcb, IP_ADDR_ANY);

    uint32_t pkts_tx = 0;
    uint32_t pkts_rx = 0;
    uint32_t min_rtt = 0xFFFFFFFF;
    uint32_t max_rtt = 0;
    uint32_t sum_rtt = 0;

    extern void thread_yield(void);

    for (uint32_t seq = 1; seq <= count; seq++) {
        g_ping_state.seq = (uint16_t)seq;
        g_ping_state.received = 0;

        u16_t ping_size = (u16_t)(sizeof(struct icmp_echo_hdr) + 32);
        struct pbuf* p = pbuf_alloc(PBUF_IP, ping_size, PBUF_RAM);
        if (!p) break;

        struct icmp_echo_hdr* iecho = (struct icmp_echo_hdr*)p->payload;
        ICMPH_TYPE_SET(iecho, ICMP_ECHO);
        ICMPH_CODE_SET(iecho, 0);
        iecho->chksum = 0;
        iecho->id = g_ping_state.id;
        iecho->seqno = lwip_htons(g_ping_state.seq);
        for (int i = 0; i < 32; i++) {
            ((char*)iecho)[sizeof(struct icmp_echo_hdr) + i] = (char)('a' + (i % 26));
        }
        iecho->chksum = inet_chksum(iecho, ping_size);

        uint64_t t_send = rdtsc();
        pkts_tx++;
        (void)raw_sendto(pcb, p, &target_ip);
        pbuf_free(p);

        uint64_t timeout_tsc = 1000ULL * g_tsc_per_ms;
        while (!g_ping_state.received) {
            net_poll();
            if ((rdtsc() - t_send) > timeout_tsc) {
                break;
            }
            thread_yield();
        }

        if (g_ping_state.received) {
            uint64_t t_diff = rdtsc() - t_send;
            uint32_t rtt = (uint32_t)(t_diff / g_tsc_per_ms);
            if (rtt == 0) rtt = 1;
            pkts_rx++;
            if (rtt < min_rtt) min_rtt = rtt;
            if (rtt > max_rtt) max_rtt = rtt;
            sum_rtt += rtt;

            str_cat(out_buf, "64 bytes from ", max_len);
            str_cat(out_buf, ip_str, max_len);
            str_cat(out_buf, ": icmp_seq=", max_len);
            uint_cat(out_buf, seq, max_len);
            str_cat(out_buf, " ttl=", max_len);
            uint_cat(out_buf, g_ping_state.ttl, max_len);
            str_cat(out_buf, " time=", max_len);
            uint_cat(out_buf, rtt, max_len);
            str_cat(out_buf, " ms\n", max_len);
        } else {
            str_cat(out_buf, "Request timeout for icmp_seq ", max_len);
            uint_cat(out_buf, seq, max_len);
            str_cat(out_buf, "\n", max_len);
        }

        if (seq < count) {
            uint64_t pause_start = rdtsc();
            while ((rdtsc() - pause_start) < (250ULL * g_tsc_per_ms)) {
                net_poll();
                thread_yield();
            }
        }
    }

    raw_remove(pcb);

    str_cat(out_buf, "--- ", max_len);
    str_cat(out_buf, host, max_len);
    str_cat(out_buf, " ping statistics ---\n", max_len);
    uint_cat(out_buf, pkts_tx, max_len);
    str_cat(out_buf, " packets transmitted, ", max_len);
    uint_cat(out_buf, pkts_rx, max_len);
    str_cat(out_buf, " received, ", max_len);
    uint32_t loss = pkts_tx > 0 ? ((pkts_tx - pkts_rx) * 100 / pkts_tx) : 0;
    uint_cat(out_buf, loss, max_len);
    str_cat(out_buf, "% packet loss\n", max_len);

    if (pkts_rx > 0) {
        uint32_t avg_rtt = sum_rtt / pkts_rx;
        str_cat(out_buf, "round-trip min/avg/max = ", max_len);
        uint_cat(out_buf, min_rtt, max_len);
        str_cat(out_buf, "/", max_len);
        uint_cat(out_buf, avg_rtt, max_len);
        str_cat(out_buf, "/", max_len);
        uint_cat(out_buf, max_rtt, max_len);
        str_cat(out_buf, " ms\n", max_len);
    }

    return (pkts_rx > 0) ? 0 : -1;
}

// --- BearSSL TLS 1.2 HTTPS Support ---

typedef struct {
    const br_x509_class *vtable;
    br_x509_decoder_context ctx;
    int first;
} x509_insecure_context;

static void insecure_start_chain(const br_x509_class **ctx, const char *server_name) {
    (void)server_name;
    x509_insecure_context *xc = (x509_insecure_context *)ctx;
    xc->first = 1;
}

static void insecure_start_cert(const br_x509_class **ctx, uint32_t length) {
    (void)length;
    x509_insecure_context *xc = (x509_insecure_context *)ctx;
    if (xc->first) {
        br_x509_decoder_init(&xc->ctx, 0, 0);
    }
}

static void insecure_append(const br_x509_class **ctx, const unsigned char *buf, size_t len) {
    x509_insecure_context *xc = (x509_insecure_context *)ctx;
    if (xc->first) {
        br_x509_decoder_push(&xc->ctx, buf, len);
    }
}

static void insecure_end_cert(const br_x509_class **ctx) {
    x509_insecure_context *xc = (x509_insecure_context *)ctx;
    xc->first = 0;
}

static unsigned insecure_end_chain(const br_x509_class **ctx) {
    x509_insecure_context *xc = (x509_insecure_context *)ctx;
    const br_x509_pkey *pk = br_x509_decoder_get_pkey(&xc->ctx);
    if (pk != NULL) {
        return 0; // Public key successfully decoded; allow connection
    }
    return (unsigned)br_x509_decoder_last_error(&xc->ctx);
}

static const br_x509_pkey *insecure_get_pkey(const br_x509_class *const *ctx, unsigned *usages) {
    x509_insecure_context *xc = (x509_insecure_context *)ctx;
    if (usages != NULL) {
        *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    }
    return br_x509_decoder_get_pkey(&xc->ctx);
}

static const br_x509_class g_insecure_x509_vtable = {
    sizeof(x509_insecure_context),
    insecure_start_chain,
    insecure_start_cert,
    insecure_append,
    insecure_end_cert,
    insecure_end_chain,
    insecure_get_pkey
};

static int tls_sock_read(void *ctx, unsigned char *buf, size_t len) {
    int32_t sock = (int32_t)(intptr_t)ctx;
    uint64_t t_start = rdtsc();
    extern void thread_yield(void);
    while (1) {
        int32_t r = net_socket_recv(sock, buf, (uint32_t)len);
        if (r > 0) return r;
        if (r == 0) return -1; // Connection closed / EOF
        net_poll();
        if ((rdtsc() - t_start) > (4000ULL * g_tsc_per_ms)) {
            return -1; // 4s Timeout
        }
        thread_yield();
    }
}

static int tls_sock_write(void *ctx, const unsigned char *buf, size_t len) {
    int32_t sock = (int32_t)(intptr_t)ctx;
    uint32_t sent = 0;
    uint64_t t_start = rdtsc();
    extern void thread_yield(void);
    while (sent < len) {
        int32_t s = net_socket_send(sock, buf + sent, (uint32_t)(len - sent));
        if (s > 0) {
            sent += (uint32_t)s;
            t_start = rdtsc();
        } else {
            net_poll();
            if ((rdtsc() - t_start) > (4000ULL * g_tsc_per_ms)) {
                return -1; // 4s Timeout
            }
            thread_yield();
        }
    }
    return (int)sent;
}

typedef struct {
    br_ssl_client_context cc;
    br_x509_minimal_context xc_dummy;
    x509_insecure_context xc_insecure;
    br_sslio_context sctx;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
} tls_session_t;

static char* find_substr(char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (*needle == '\0') return haystack;
    for (; *haystack != '\0'; haystack++) {
        if (*haystack == *needle) {
            char* h = haystack;
            const char* n = needle;
            while (*h != '\0' && *n != '\0' && *h == *n) {
                h++;
                n++;
            }
            if (*n == '\0') return haystack;
        }
    }
    return NULL;
}

static const char* find_case_header(const char* buf, const char* name) {
    if (!buf || !name) return NULL;
    int nlen = 0;
    while (name[nlen] != '\0') nlen++;
    const char* p = buf;
    while (*p != '\0') {
        int match = 1;
        for (int i = 0; i < nlen; i++) {
            char c1 = p[i];
            char c2 = name[i];
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            if (c1 != c2) { match = 0; break; }
        }
        if (match) return p + nlen;
        while (*p != '\0' && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return NULL;
}

static int parse_url(const char* url, int* is_https, char* host, uint32_t host_sz, uint16_t* port, const char** path, int* had_scheme) {
    if (!url || !host || host_sz < 4) return -1;
    const char* p = url;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return -1;

    *is_https = 1;
    *had_scheme = 0;

    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == 's' && p[5] == ':' && p[6] == '/' && p[7] == '/') {
        *is_https = 1;
        *had_scheme = 1;
        p += 8;
    } else if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == ':' && p[5] == '/' && p[6] == '/') {
        *is_https = 0;
        *had_scheme = 1;
        p += 7;
    } else if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == 's' && (p[5] == ' ' || p[5] == ':')) {
        *is_https = 1;
        *had_scheme = 1;
        p += 5;
        while (*p == ' ' || *p == '/' || *p == ':') p++;
    } else if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && (p[4] == ' ' || p[4] == ':')) {
        *is_https = 0;
        *had_scheme = 1;
        p += 4;
        while (*p == ' ' || *p == '/' || *p == ':') p++;
    }

    uint32_t h_idx = 0;
    while (*p != '\0' && *p != ':' && *p != '/' && *p != ' ' && h_idx + 1 < host_sz) {
        host[h_idx++] = *p++;
    }
    host[h_idx] = '\0';
    if (h_idx == 0) return -1;

    *port = *is_https ? 443 : 80;
    if (*p == ':') {
        p++;
        uint32_t p_val = 0;
        while (*p >= '0' && *p <= '9') {
            p_val = p_val * 10 + (*p - '0');
            p++;
        }
        if (p_val > 0 && p_val <= 65535) {
            *port = (uint16_t)p_val;
        }
    }
    while (*p == ' ') p++;
    *path = (*p == '/') ? p : "/";
    return 0;
}

static int32_t net_http_fetch_single(const char* url, char* out_buf, uint32_t max_len, char* next_url, uint32_t next_sz) {
    if (!url || !out_buf || max_len < 64) return -1;
    out_buf[0] = '\0';
    if (next_url && next_sz > 0) next_url[0] = '\0';

    int is_https = 1;
    int had_scheme = 0;
    char host_buf[64];
    uint16_t port = 443;
    const char* path = "/";

    if (parse_url(url, &is_https, host_buf, sizeof(host_buf), &port, &path, &had_scheme) != 0) {
        str_cat(out_buf, "fetch: invalid URL or host\n", max_len);
        return -1;
    }

    serial_printf("[HTTP] Fetch '%s' -> host='%s' port=%u https=%d path='%s'\n", url, host_buf, (uint32_t)port, is_https, path);

    char ip_str[32];
    if (net_resolve_host(host_buf, ip_str, sizeof(ip_str)) != 0) {
        str_cat(out_buf, "fetch: cannot resolve host '", max_len);
        str_cat(out_buf, host_buf, max_len);
        str_cat(out_buf, "'\n", max_len);
        return -1;
    }

    int32_t sock = net_socket_create(SOCKET_TYPE_TCP);
    if (sock < 0) {
        str_cat(out_buf, "fetch: socket allocation failed\n", max_len);
        return -1;
    }

    if (net_socket_connect(sock, ip_str, port) != 0) {
        net_socket_close(sock);
        // If default HTTPS failed, try HTTP port 80
        if (!had_scheme && is_https) {
            serial_printf("[HTTP] HTTPS connect failed, retrying plain HTTP:80...\n");
            is_https = 0;
            port = 80;
            sock = net_socket_create(SOCKET_TYPE_TCP);
            if (sock < 0 || net_socket_connect(sock, ip_str, port) != 0) {
                if (sock >= 0) net_socket_close(sock);
                str_cat(out_buf, "fetch: connect call failed\n", max_len);
                return -1;
            }
        } else {
            str_cat(out_buf, "fetch: connect call failed\n", max_len);
            return -1;
        }
    }

    extern void thread_yield(void);

    uint64_t t_conn = rdtsc();
    while (net_socket_get_state(sock) == 5 /* CONNECTING */) {
        net_poll();
        if ((rdtsc() - t_conn) > (3500ULL * g_tsc_per_ms)) {
            break;
        }
        thread_yield();
    }

    if (net_socket_get_state(sock) != 3 /* CONNECTED */) {
        net_socket_close(sock);
        if (!had_scheme && is_https) {
            serial_printf("[HTTP] HTTPS connect timed out, retrying plain HTTP:80...\n");
            is_https = 0;
            port = 80;
            sock = net_socket_create(SOCKET_TYPE_TCP);
            if (sock >= 0 && net_socket_connect(sock, ip_str, port) == 0) {
                t_conn = rdtsc();
                while (net_socket_get_state(sock) == 5) {
                    net_poll();
                    if ((rdtsc() - t_conn) > (3500ULL * g_tsc_per_ms)) break;
                    thread_yield();
                }
            }
        }
    }

    if (net_socket_get_state(sock) != 3 /* CONNECTED */) {
        if (sock >= 0) net_socket_close(sock);
        str_cat(out_buf, "fetch: connection failed to ", max_len);
        str_cat(out_buf, host_buf, max_len);
        str_cat(out_buf, "\n", max_len);
        return -1;
    }

    char req[512];
    req[0] = '\0';
    str_cat(req, "GET ", sizeof(req));
    str_cat(req, path, sizeof(req));
    str_cat(req, " HTTP/1.1\r\nHost: ", sizeof(req));
    str_cat(req, host_buf, sizeof(req));
    str_cat(req, "\r\nUser-Agent: Mozilla/5.0 (compatible; WoOS/1.55; +http://woos.org)\r\nAccept: text/html,*/*\r\nConnection: close\r\n\r\n", sizeof(req));

    uint32_t req_len = 0;
    while (req[req_len] != '\0') req_len++;

    uint32_t total_rx = 0;

    if (is_https) {
        tls_session_t *tls = (tls_session_t *)kheap_alloc(sizeof(tls_session_t));
        if (!tls) {
            net_socket_close(sock);
            str_cat(out_buf, "fetch: out of memory for TLS session\n", max_len);
            return -1;
        }

        br_ssl_client_init_full(&tls->cc, &tls->xc_dummy, NULL, 0);
        tls->xc_insecure.vtable = &g_insecure_x509_vtable;
        br_ssl_engine_set_x509(&tls->cc.eng, &tls->xc_insecure.vtable);
        br_ssl_engine_set_buffer(&tls->cc.eng, tls->iobuf, sizeof(tls->iobuf), 1);

        uint64_t entropy[4];
        entropy[0] = rdtsc();
        entropy[1] = (uint64_t)timer_ticks();
        entropy[2] = (uint64_t)(uintptr_t)sock;
        entropy[3] = (uint64_t)(uintptr_t)tls;
        br_ssl_engine_inject_entropy(&tls->cc.eng, entropy, sizeof(entropy));

        if (!br_ssl_client_reset(&tls->cc, host_buf, 0)) {
            kheap_free(tls);
            net_socket_close(sock);
            str_cat(out_buf, "fetch: TLS reset failed\n", max_len);
            return -1;
        }

        br_sslio_init(&tls->sctx, &tls->cc.eng, tls_sock_read, (void*)(intptr_t)sock, tls_sock_write, (void*)(intptr_t)sock);

        if (br_sslio_write_all(&tls->sctx, req, req_len) != 0 || br_sslio_flush(&tls->sctx) != 0) {
            kheap_free(tls);
            net_socket_close(sock);
            str_cat(out_buf, "fetch: TLS request write failed\n", max_len);
            return -1;
        }

        while (total_rx + 1 < max_len) {
            int r = br_sslio_read(&tls->sctx, out_buf + total_rx, max_len - 1 - total_rx);
            if (r > 0) {
                total_rx += (uint32_t)r;
            } else {
                break;
            }
        }
        out_buf[total_rx] = '\0';
        br_ssl_engine_close(&tls->cc.eng);
        kheap_free(tls);
        net_socket_close(sock);
    } else {
        // Plain HTTP
        uint32_t sent = 0;
        uint64_t t_send = rdtsc();
        while (sent < req_len) {
            int32_t s = net_socket_send(sock, req + sent, req_len - sent);
            if (s > 0) {
                sent += (uint32_t)s;
                t_send = rdtsc();
            } else {
                net_poll();
                if ((rdtsc() - t_send) > (3000ULL * g_tsc_per_ms)) {
                    break;
                }
                thread_yield();
            }
        }

        uint64_t t_last_rx = rdtsc();
        while (total_rx + 1 < max_len) {
            int32_t r = net_socket_recv(sock, out_buf + total_rx, max_len - 1 - total_rx);
            if (r > 0) {
                total_rx += (uint32_t)r;
                t_last_rx = rdtsc();
            } else if (r == 0) {
                break;
            } else {
                net_poll();
                if ((rdtsc() - t_last_rx) > (3000ULL * g_tsc_per_ms)) {
                    break;
                }
                thread_yield();
            }
        }
        out_buf[total_rx] = '\0';
        net_socket_close(sock);
    }

    if (total_rx == 0) {
        str_cat(out_buf, "fetch: connected, no data returned\n", max_len);
        return 0;
    }

    serial_printf("[HTTP] Received %u raw bytes from '%s'\n", total_rx, host_buf);

    // Check for HTTP 3xx Redirect (301, 302, 303, 307, 308)
    if (total_rx >= 12 && (out_buf[0] == 'H' && out_buf[1] == 'T' && out_buf[2] == 'T' && out_buf[3] == 'P') &&
        (out_buf[9] == '3')) {
        const char* loc = find_case_header(out_buf, "location:");
        if (loc && next_url && next_sz > 0) {
            while (*loc == ' ' || *loc == '\t') loc++;
            uint32_t n_idx = 0;
            // Handle relative redirect (e.g. Location: /index.html)
            if (*loc == '/') {
                if (is_https) str_cat(next_url, "https://", next_sz);
                else str_cat(next_url, "http://", next_sz);
                str_cat(next_url, host_buf, next_sz);
                while (next_url[n_idx] != '\0') n_idx++;
            }
            while (*loc != '\0' && *loc != '\r' && *loc != '\n' && n_idx + 1 < next_sz) {
                next_url[n_idx++] = *loc++;
            }
            next_url[n_idx] = '\0';
            serial_printf("[HTTP] 3xx Redirect to '%s'\n", next_url);
            return 0; // Trigger redirect follow
        }
    }

    // Strip HTTP response headers so browser gets clean HTML
    char* body = find_substr(out_buf, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        body = find_substr(out_buf, "\n\n");
        if (body) body += 2;
    }

    if (body) {
        extern void* memmove(void* dest, const void* src, size_t n);
        uint32_t body_len = 0;
        while (body[body_len] != '\0') body_len++;
        memmove(out_buf, body, body_len + 1);
        total_rx = body_len;
    }

    return (int32_t)total_rx;
}

int32_t net_http_fetch(const char* url, char* out_buf, uint32_t max_len) {
    if (!url || !out_buf || max_len < 64) return -1;
    char current_url[256];
    uint32_t u_idx = 0;
    while (url[u_idx] != '\0' && u_idx + 1 < sizeof(current_url)) {
        current_url[u_idx] = url[u_idx];
        u_idx++;
    }
    current_url[u_idx] = '\0';

    int redirects = 0;
    while (redirects < 4) {
        char next_url[256] = "";
        int32_t res = net_http_fetch_single(current_url, out_buf, max_len, next_url, sizeof(next_url));
        if (res > 0) {
            return res; // Webpage successfully fetched!
        }
        if (res == 0 && next_url[0] != '\0') {
            redirects++;
            serial_printf("[HTTP] Redirect #%d: '%s' -> '%s'\n", redirects, current_url, next_url);
            u_idx = 0;
            while (next_url[u_idx] != '\0' && u_idx + 1 < sizeof(current_url)) {
                current_url[u_idx] = next_url[u_idx];
                u_idx++;
            }
            current_url[u_idx] = '\0';
            continue;
        }
        return -1; // Failure
    }

    str_cat(out_buf, "fetch: exceeded redirect limit (>3)\n", max_len);
    return -1;
}

// Потоковое скачивание файлов напрямую на диск в FAT32 (поддерживает HTTP и HTTPS)
int32_t net_http_download(const char* url, const char* dest_path) {
    if (!url || !dest_path) return -1;

    int is_https = 1;
    int had_scheme = 0;
    char host_buf[64];
    uint16_t port = 443;
    const char* path = "/";

    if (parse_url(url, &is_https, host_buf, sizeof(host_buf), &port, &path, &had_scheme) != 0) {
        return -1;
    }

    char ip_str[32];
    if (net_resolve_host(host_buf, ip_str, sizeof(ip_str)) != 0) {
        return -1;
    }

    int32_t sock = net_socket_create(SOCKET_TYPE_TCP);
    if (sock < 0) return -1;

    if (net_socket_connect(sock, ip_str, port) != 0) {
        net_socket_close(sock);
        return -1;
    }

    extern void thread_yield(void);

    uint64_t t_conn = rdtsc();
    while (net_socket_get_state(sock) == 5 /* CONNECTING */) {
        net_poll();
        if ((rdtsc() - t_conn) > (4000ULL * g_tsc_per_ms)) {
            break;
        }
        thread_yield();
    }

    if (net_socket_get_state(sock) != 3 /* CONNECTED */) {
        net_socket_close(sock);
        return -1;
    }

    char req[512];
    req[0] = '\0';
    str_cat(req, "GET ", sizeof(req));
    str_cat(req, path, sizeof(req));
    str_cat(req, " HTTP/1.1\r\nHost: ", sizeof(req));
    str_cat(req, host_buf, sizeof(req));
    str_cat(req, "\r\nUser-Agent: Mozilla/5.0 (compatible; WoOS/1.55; +http://woos.org)\r\nAccept: */*\r\nConnection: close\r\n\r\n", sizeof(req));

    uint32_t req_len = 0;
    while (req[req_len] != '\0') req_len++;

    tls_session_t *tls = NULL;
    if (is_https) {
        tls = (tls_session_t *)kheap_alloc(sizeof(tls_session_t));
        if (!tls) {
            net_socket_close(sock);
            return -1;
        }

        br_ssl_client_init_full(&tls->cc, &tls->xc_dummy, NULL, 0);
        tls->xc_insecure.vtable = &g_insecure_x509_vtable;
        br_ssl_engine_set_x509(&tls->cc.eng, &tls->xc_insecure.vtable);
        br_ssl_engine_set_buffer(&tls->cc.eng, tls->iobuf, sizeof(tls->iobuf), 1);

        uint64_t entropy[4];
        entropy[0] = rdtsc();
        entropy[1] = (uint64_t)timer_ticks();
        entropy[2] = (uint64_t)(uintptr_t)sock;
        entropy[3] = (uint64_t)(uintptr_t)tls;
        br_ssl_engine_inject_entropy(&tls->cc.eng, entropy, sizeof(entropy));

        if (!br_ssl_client_reset(&tls->cc, host_buf, 0)) {
            kheap_free(tls);
            net_socket_close(sock);
            return -1;
        }

        br_sslio_init(&tls->sctx, &tls->cc.eng, tls_sock_read, (void*)(intptr_t)sock, tls_sock_write, (void*)(intptr_t)sock);

        if (br_sslio_write_all(&tls->sctx, req, req_len) != 0 || br_sslio_flush(&tls->sctx) != 0) {
            kheap_free(tls);
            net_socket_close(sock);
            return -1;
        }
    } else {
        uint32_t sent = 0;
        uint64_t t_send = rdtsc();
        while (sent < req_len) {
            int32_t s = net_socket_send(sock, req + sent, req_len - sent);
            if (s > 0) {
                sent += (uint32_t)s;
                t_send = rdtsc();
            } else {
                net_poll();
                if ((rdtsc() - t_send) > (3000ULL * g_tsc_per_ms)) break;
                thread_yield();
            }
        }
    }

    // Считываем HTTP-заголовки до \r\n\r\n
    char hdr_buf[2048];
    uint32_t hdr_len = 0;
    int body_start_idx = -1;

    while (hdr_len + 1 < sizeof(hdr_buf)) {
        char ch;
        int r = 0;
        if (is_https) {
            r = br_sslio_read(&tls->sctx, &ch, 1);
        } else {
            uint64_t t_rx = rdtsc();
            while (1) {
                r = net_socket_recv(sock, &ch, 1);
                if (r > 0 || r == 0) break;
                net_poll();
                if ((rdtsc() - t_rx) > (4000ULL * g_tsc_per_ms)) { r = -1; break; }
                thread_yield();
            }
        }
        if (r <= 0) break;
        hdr_buf[hdr_len++] = ch;
        hdr_buf[hdr_len] = '\0';

        if (hdr_len >= 4 &&
            hdr_buf[hdr_len - 4] == '\r' && hdr_buf[hdr_len - 3] == '\n' &&
            hdr_buf[hdr_len - 2] == '\r' && hdr_buf[hdr_len - 1] == '\n') {
            body_start_idx = (int)hdr_len;
            break;
        }
    }

    if (body_start_idx < 0) {
        if (tls) { br_ssl_engine_close(&tls->cc.eng); kheap_free(tls); }
        net_socket_close(sock);
        return -1;
    }

    // Открываем целевой файл для записи
    int32_t fd = vfs_open(dest_path, VFS_MODE_WRITE | VFS_MODE_CREATE | VFS_MODE_TRUNC);
    if (fd < 0) {
        if (tls) { br_ssl_engine_close(&tls->cc.eng); kheap_free(tls); }
        net_socket_close(sock);
        return -1;
    }

    // Считываем тело ответа потоком прямо в файл
    uint8_t chunk[2048];
    uint32_t total_written = 0;

    while (1) {
        int r = 0;
        if (is_https) {
            r = br_sslio_read(&tls->sctx, chunk, sizeof(chunk));
        } else {
            uint64_t t_rx = rdtsc();
            while (1) {
                r = net_socket_recv(sock, chunk, sizeof(chunk));
                if (r > 0 || r == 0) break;
                net_poll();
                if ((rdtsc() - t_rx) > (4000ULL * g_tsc_per_ms)) { r = -1; break; }
                thread_yield();
            }
        }
        if (r <= 0) break;

        vfs_write(fd, chunk, (uint32_t)r);
        total_written += (uint32_t)r;
    }

    vfs_close(fd);
    if (tls) { br_ssl_engine_close(&tls->cc.eng); kheap_free(tls); }
    net_socket_close(sock);

    return (int32_t)total_written;
}

// Заглушки для ACD (Address Conflict Detection), если lwip их требует
void acd_netif_ip_addr_changed(void* a, void* b, void* c) { (void)a; (void)b; (void)c; }
void acd_network_changed_link_down(void* a) { (void)a; }
void acd_start(void* a, void* b) { (void)a; (void)b; }
void acd_add(void* a, void* b, void* c) { (void)a; (void)b; (void)c; }
void acd_remove(void* a) { (void)a; }
void acd_arp_reply(void* a, void* b) { (void)a; (void)b; }
void acd_tmr(void) {}
