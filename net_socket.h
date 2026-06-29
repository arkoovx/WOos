#ifndef WOOS_NET_SOCKET_H
#define WOOS_NET_SOCKET_H

#include "kernel.h"

#define SOCKET_TYPE_TCP 1
#define SOCKET_TYPE_UDP 2

int32_t net_socket_create(uint8_t type);
int32_t net_socket_bind(int32_t sock, uint16_t port);
int32_t net_socket_listen(int32_t sock);
int32_t net_socket_accept(int32_t sock);
int32_t net_socket_connect(int32_t sock, const char* remote_ip, uint16_t remote_port);
int32_t net_socket_send(int32_t sock, const void* data, uint32_t len);
int32_t net_socket_recv(int32_t sock, void* buf, uint32_t len);
void net_socket_close(int32_t sock);

#endif
