#ifndef WOOS_VIRTIO_NET_H
#define WOOS_VIRTIO_NET_H

#include "kernel.h"

typedef struct virtio_net_status {
    uint8_t detected;
    uint8_t active;
    uint8_t mac[6];
    uint32_t rx_count;
    uint32_t tx_count;
} virtio_net_status_t;

void virtio_net_init(void);
const virtio_net_status_t* virtio_net_get_status(void);
void virtio_net_poll(void);

#endif
