#ifndef WOOS_SCHED_H
#define WOOS_SCHED_H

#include <stdint.h>

#define MAX_THREADS 16

typedef struct context {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) context_t;

typedef enum thread_state {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_TERMINATED
} thread_state_t;

typedef struct thread {
    uint64_t rsp;              // Указатель на вершину стека (сохранённый контекст)
    uint64_t id;               // Уникальный ID потока
    thread_state_t state;      // Состояние потока
    void* stack_limit;         // Нижняя граница выделенной памяти стека
    uint64_t cr3;              // Физический адрес PML4 (0 для ядра)
    uint64_t kernel_stack_top; // Вершина стека ядра для переключения TSS
} thread_t;

void sched_init(void);
void thread_create(void (*entry_point)(void));
void process_create(void);
uint64_t schedule_interrupt(uint64_t current_rsp);
void thread_yield(void);
void thread_exit(int status);

typedef struct {
    uint32_t sender_id;
    uint32_t dest_id;
    uint32_t type;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint32_t arg4;
    uint32_t arg5;
} __attribute__((packed)) woos_ipc_msg_t;

uint32_t sched_current_thread_id(void);
int32_t woos_ipc_send(uint32_t dest_id, uint32_t type, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);
int32_t woos_ipc_recv(woos_ipc_msg_t* msg);
int32_t woos_system_spawn(const char* path);

void sched_block_current(void);
void sched_unblock_all(void);
uint64_t schedule_irq(uint64_t current_rsp, uint64_t vector);

#endif
