#ifndef WOOS_NET_H
#define WOOS_NET_H

#include "kernel.h"

// Статус сетевой подсистемы для UI
typedef struct net_status {
    uint8_t ready;
    uint8_t active;
    uint8_t link_up;
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t ip_addr;
} net_status_t;

void net_init(void);
void net_poll(void);
const net_status_t* net_get_status(void);
int32_t net_resolve_host(const char* hostname, char* out_ip, uint32_t max_len);
void net_get_config(char* out_buf, uint32_t max_len);

#endif
