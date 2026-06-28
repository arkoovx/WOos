#ifndef WOOS_SCHED_H
#define WOOS_SCHED_H

#include <stdint.h>

#define MAX_THREADS 16

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

#endif
