#ifndef WOOS_LWIPOPTS_H
#define WOOS_LWIPOPTS_H

#define NO_SYS                          1
#define LWIP_SOCKET                     0
#define LWIP_NETCONN                    0
#define SYS_LIGHTWEIGHT_PROT            0

#define MEM_ALIGNMENT                   16
#define MEMP_NUM_PBUF                   32
#define MEMP_NUM_UDP_PCB                4
#define MEMP_NUM_TCP_PCB                4
#define MEMP_NUM_TCP_PCB_LISTEN         4
#define MEMP_NUM_TCP_SEG                16
#define MEMP_NUM_REASSDATA              4
#define MEMP_NUM_FRAG_PBUF              16
#define MEMP_NUM_ARP_QUEUE              16

#define PBUF_POOL_SIZE                  32
#define PBUF_POOL_BUFSIZE               1536

#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1

#define LWIP_IP_REASSEMBLY              1
#define LWIP_IP_FRAG                    1

#define LWIP_DHCP                       1
#define LWIP_UDP                        1
#define LWIP_TCP                        1

#define LWIP_ACD                        0
#define LWIP_DHCP_DOES_ACD_CHECK        0

#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)

#define LWIP_STATS                      0
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_NETIF_STATUS_CALLBACK      1

#define LWIP_DEBUG                      0

#endif
