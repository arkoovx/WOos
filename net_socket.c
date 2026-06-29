#include "net_socket.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "kheap.h"
#include "net.h"
#include "serial.h"

#define NET_MAX_SOCKETS 8
#define NET_SOCKET_BUF_SIZE 2048

typedef struct net_socket {
    uint8_t in_use;
    uint8_t type;       // SOCKET_TYPE_TCP or SOCKET_TYPE_UDP
    uint8_t state;      // 0: CLOSED, 1: BOUND, 2: LISTENING, 3: CONNECTED, 4: ERROR, 5: CONNECTING
    
    struct tcp_pcb* tcp;
    struct udp_pcb* udp;
    
    // Очередь входящих соединений для listening сокета
    int32_t accept_queue[4];
    uint8_t accept_head;
    uint8_t accept_tail;
    uint8_t accept_count;
    
    // Кольцевой буфер приема
    uint8_t* rx_buf;
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;
} net_socket_t;

static net_socket_t g_sockets[NET_MAX_SOCKETS];

extern void* memset(void* s, int c, size_t n);
extern void* memcpy(void* dest, const void* src, size_t n);

static int32_t allocate_socket_slot(void) {
    for (int32_t i = 0; i < NET_MAX_SOCKETS; i++) {
        if (!g_sockets[i].in_use) {
            memset(&g_sockets[i], 0, sizeof(net_socket_t));
            g_sockets[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

int32_t net_socket_create(uint8_t type) {
    int32_t sock = allocate_socket_slot();
    if (sock < 0) {
        serial_printf("[Socket] Error: socket pool is full!\n");
        return -1;
    }

    net_socket_t* s = &g_sockets[sock];
    s->type = type;
    s->state = 0; // CLOSED

    s->rx_buf = (uint8_t*)kheap_alloc(NET_SOCKET_BUF_SIZE);
    if (!s->rx_buf) {
        s->in_use = 0;
        serial_printf("[Socket] Error: failed to allocate rx buffer!\n");
        return -1;
    }
    s->rx_head = s->rx_tail = s->rx_count = 0;

    if (type == SOCKET_TYPE_TCP) {
        s->tcp = tcp_new();
        if (!s->tcp) {
            kheap_free(s->rx_buf);
            s->in_use = 0;
            serial_printf("[Socket] Error: failed to create tcp pcb!\n");
            return -1;
        }
    } else {
        s->udp = udp_new();
        if (!s->udp) {
            kheap_free(s->rx_buf);
            s->in_use = 0;
            serial_printf("[Socket] Error: failed to create udp pcb!\n");
            return -1;
        }
    }

    return sock;
}

int32_t net_socket_bind(int32_t sock, uint16_t port) {
    if (sock < 0 || sock >= NET_MAX_SOCKETS || !g_sockets[sock].in_use) {
        return -1;
    }

    net_socket_t* s = &g_sockets[sock];
    if (s->type == SOCKET_TYPE_TCP) {
        if (tcp_bind(s->tcp, IP4_ADDR_ANY, port) != ERR_OK) {
            return -1;
        }
    } else {
        if (udp_bind(s->udp, IP4_ADDR_ANY, port) != ERR_OK) {
            return -1;
        }
    }

    s->state = 1; // BOUND
    return 0;
}

static err_t tcp_recv_callback(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);
static void tcp_err_callback(void* arg, err_t err);

static err_t tcp_accept_callback(void* arg, struct tcp_pcb* newpcb, err_t err) {
    int32_t parent_sock = (int32_t)(uintptr_t)arg;
    (void)err;

    if (parent_sock < 0 || parent_sock >= NET_MAX_SOCKETS || !g_sockets[parent_sock].in_use) {
        return ERR_VAL;
    }

    net_socket_t* parent = &g_sockets[parent_sock];
    if (parent->accept_count >= 4) {
        // Очередь переполнена — отклоняем подключение
        return ERR_MEM;
    }

    // Немедленно выделяем слот под новое клиентское соединение
    int32_t client_sock = allocate_socket_slot();
    if (client_sock < 0) {
        return ERR_MEM;
    }

    net_socket_t* client = &g_sockets[client_sock];
    client->type = SOCKET_TYPE_TCP;
    client->state = 3; // CONNECTED
    client->tcp = newpcb;

    client->rx_buf = (uint8_t*)kheap_alloc(NET_SOCKET_BUF_SIZE);
    if (!client->rx_buf) {
        client->in_use = 0;
        return ERR_MEM;
    }
    client->rx_head = client->rx_tail = client->rx_count = 0;

    // Настраиваем колбэки для клиента
    tcp_arg(newpcb, (void*)(uintptr_t)client_sock);
    tcp_recv(newpcb, tcp_recv_callback);
    tcp_err(newpcb, tcp_err_callback);

    // Добавляем клиента в очередь родительского сокета
    parent->accept_queue[parent->accept_tail] = client_sock;
    parent->accept_tail = (parent->accept_tail + 1) % 4;
    parent->accept_count++;

    return ERR_OK;
}

int32_t net_socket_listen(int32_t sock) {
    if (sock < 0 || sock >= NET_MAX_SOCKETS || !g_sockets[sock].in_use) {
        return -1;
    }

    net_socket_t* s = &g_sockets[sock];
    if (s->type != SOCKET_TYPE_TCP || s->state != 1) {
        return -1;
    }

    struct tcp_pcb* listening_pcb = tcp_listen(s->tcp);
    if (!listening_pcb) {
        return -1;
    }

    s->tcp = listening_pcb;
    s->state = 2; // LISTENING
    s->accept_head = s->accept_tail = s->accept_count = 0;

    tcp_arg(s->tcp, (void*)(uintptr_t)sock);
    tcp_accept(s->tcp, tcp_accept_callback);

    return 0;
}

int32_t net_socket_accept(int32_t sock) {
    if (sock < 0 || sock >= NET_MAX_SOCKETS || !g_sockets[sock].in_use) {
        return -1;
    }

    net_socket_t* s = &g_sockets[sock];
    if (s->type != SOCKET_TYPE_TCP || s->state != 2) {
        return -1;
    }

    // Прокачиваем сетевые пакеты
    net_poll();

    if (s->accept_count > 0) {
        int32_t client_sock = s->accept_queue[s->accept_head];
        s->accept_head = (s->accept_head + 1) % 4;
        s->accept_count--;
        return client_sock;
    }

    return -1; // EAGAIN / EWOULDBLOCK
}

static err_t tcp_connect_callback(void* arg, struct tcp_pcb* tpcb, err_t err) {
    int32_t sock = (int32_t)(uintptr_t)arg;
    if (sock < 0 || sock >= NET_MAX_SOCKETS || !g_sockets[sock].in_use) {
        return ERR_VAL;
    }

    net_socket_t* s = &g_sockets[sock];
    if (err == ERR_OK) {
        s->state = 3; // CONNECTED
        tcp_recv(tpcb, tcp_recv_callback);
        tcp_err(tpcb, tcp_err_callback);
    } else {
        s->state = 4; // ERROR
    }

    return ERR_OK;
}

int32_t net_socket_connect(int32_t sock, const char* remote_ip, uint16_t remote_port) {
    if (sock < 0 || sock >= NET_MAX_SOCKETS || !g_sockets[sock].in_use || !remote_ip) {
        return -1;
    }

    net_socket_t* s = &g_sockets[sock];
    if (s->type != SOCKET_TYPE_TCP || s->state != 0) {
        return -1;
    }

    ip_addr_t remote_addr;
    if (!ipaddr_aton(remote_ip, &remote_addr)) {
        return -1;
    }

    s->state = 5; // CONNECTING
    tcp_arg(s->tcp, (void*)(uintptr_t)sock);
    err_t err = tcp_connect(s->tcp, &remote_addr, remote_port, tcp_connect_callback);
    if (err == ERR_OK) {
        // Прокачиваем пакеты для отправки SYN
        net_poll();
        return 0;
    }

    s->state = 4; // ERROR
    return -1;
}

int32_t net_socket_send(int32_t sock, const void* data, uint32_t len) {
    if (sock < 0 || sock >= NET_MAX_SOCKETS || !g_sockets[sock].in_use || !data || len == 0) {
        return -1;
    }

    net_socket_t* s = &g_sockets[sock];
    if (s->type != SOCKET_TYPE_TCP || s->state != 3 || !s->tcp) {
        return -1;
    }

    uint16_t send_buf_size = tcp_sndbuf(s->tcp);
    if (send_buf_size == 0) {
        net_poll();
        return -1; // EAGAIN / EWOULDBLOCK
    }

    uint32_t to_send = len;
    if (to_send > send_buf_size) {
        to_send = send_buf_size;
    }

    err_t err = tcp_write(s->tcp, data, (uint16_t)to_send, TCP_WRITE_FLAG_COPY);
    if (err == ERR_OK) {
        tcp_output(s->tcp); // Выталкиваем данные
        net_poll();
        return (int32_t)to_send;
    }

    return -1;
}

int32_t net_socket_recv(int32_t sock, void* buf, uint32_t len) {
    if (sock < 0 || sock >= NET_MAX_SOCKETS || !g_sockets[sock].in_use || !buf || len == 0) {
        return -1;
    }

    net_socket_t* s = &g_sockets[sock];
    
    // Прокачиваем входящие пакеты
    net_poll();

    if (s->rx_count == 0) {
        if (s->state == 4 || !s->tcp) {
            return 0; // EOF / Connection closed
        }
        return -1; // EAGAIN / EWOULDBLOCK
    }

    uint32_t to_copy = len;
    if (to_copy > s->rx_count) {
        to_copy = s->rx_count;
    }

    uint8_t* dest = (uint8_t*)buf;
    for (uint32_t i = 0; i < to_copy; i++) {
        dest[i] = s->rx_buf[s->rx_head];
        s->rx_head = (s->rx_head + 1) % NET_SOCKET_BUF_SIZE;
        s->rx_count--;
    }

    return (int32_t)to_copy;
}

void net_socket_close(int32_t sock) {
    if (sock < 0 || sock >= NET_MAX_SOCKETS || !g_sockets[sock].in_use) {
        return;
    }

    net_socket_t* s = &g_sockets[sock];
    s->in_use = 0;

    if (s->type == SOCKET_TYPE_TCP && s->state == 2) {
        // Закрываем все висящие в очереди accept клиентские сокеты
        while (s->accept_count > 0) {
            int32_t client_sock = s->accept_queue[s->accept_head];
            s->accept_head = (s->accept_head + 1) % 4;
            s->accept_count--;
            net_socket_close(client_sock);
        }
    }

    if (s->tcp) {
        tcp_arg(s->tcp, NULL);
        tcp_recv(s->tcp, NULL);
        tcp_err(s->tcp, NULL);
        
        tcp_close(s->tcp);
        s->tcp = NULL;
    }

    if (s->udp) {
        udp_remove(s->udp);
        s->udp = NULL;
    }

    if (s->rx_buf) {
        kheap_free(s->rx_buf);
        s->rx_buf = NULL;
    }
}

static err_t tcp_recv_callback(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    int32_t sock = (int32_t)(uintptr_t)arg;
    (void)err;

    if (sock < 0 || sock >= NET_MAX_SOCKETS || !g_sockets[sock].in_use) {
        return ERR_VAL;
    }

    net_socket_t* s = &g_sockets[sock];
    if (p == NULL) {
        // Удаленный хост закрыл соединение
        s->state = 4; // ERROR / CLOSED
        return ERR_OK;
    }

    struct pbuf* q = p;
    uint32_t copied = 0;
    while (q != NULL) {
        const uint8_t* data = (const uint8_t*)q->payload;
        for (uint16_t i = 0; i < q->len; i++) {
            if (s->rx_count < NET_SOCKET_BUF_SIZE) {
                s->rx_buf[s->rx_tail] = data[i];
                s->rx_tail = (s->rx_tail + 1) % NET_SOCKET_BUF_SIZE;
                s->rx_count++;
                copied++;
            }
        }
        q = q->next;
    }

    tcp_recved(pcb, copied);
    pbuf_free(p);
    return ERR_OK;
}

static void tcp_err_callback(void* arg, err_t err) {
    int32_t sock = (int32_t)(uintptr_t)arg;
    (void)err;

    if (sock >= 0 && sock < NET_MAX_SOCKETS) {
        g_sockets[sock].state = 4; // ERROR
        g_sockets[sock].tcp = NULL; // Освобожден lwIP
    }
}
