#include "input.h"
#include "kheap.h"

#define INPUT_QUEUE_CAPACITY 256u

typedef struct input_queue {
    input_event_t* buffer;
    uint16_t capacity;
    uint16_t head;
    uint16_t tail;
    uint16_t size;
    uint16_t dropped;
    uint8_t uses_heap;
} input_queue_t;

static input_queue_t g_queue = {0, INPUT_QUEUE_CAPACITY, 0, 0, 0, 0, 0};
static input_event_t g_static_buffer[INPUT_QUEUE_CAPACITY];

void input_init(void) {
    g_queue.buffer = (input_event_t*)kheap_alloc(sizeof(g_static_buffer));
    g_queue.capacity = INPUT_QUEUE_CAPACITY;
    g_queue.head = 0;
    g_queue.tail = 0;
    g_queue.size = 0;
    g_queue.dropped = 0;
    g_queue.uses_heap = 1;

    // Если allocator ещё не готов или heap исчерпан,
    // работаем в безопасном fallback-режиме со статическим буфером.
    if (g_queue.buffer == 0) {
        g_queue.buffer = g_static_buffer;
        g_queue.uses_heap = 0;
    }
}

static inline uint64_t save_interrupts(void) {
    uint64_t rflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(rflags));
    return rflags;
}

static inline void restore_interrupts(uint64_t rflags) {
    __asm__ __volatile__("push %0; popf" : : "r"(rflags));
}

uint8_t input_push(const input_event_t* event) {
    uint64_t flags = save_interrupts();

    if (g_queue.size >= g_queue.capacity) {
        g_queue.dropped++;
        restore_interrupts(flags);
        return 0;
    }

    g_queue.buffer[g_queue.tail] = *event;
    g_queue.tail = (uint16_t)((g_queue.tail + 1u) % g_queue.capacity);
    g_queue.size++;

    extern void sched_unblock_all(void);
    sched_unblock_all();

    restore_interrupts(flags);
    return 1;
}

uint8_t input_pop(input_event_t* out_event) {
    uint64_t flags = save_interrupts();

    if (g_queue.size == 0) {
        restore_interrupts(flags);
        return 0;
    }

    *out_event = g_queue.buffer[g_queue.head];
    g_queue.head = (uint16_t)((g_queue.head + 1u) % g_queue.capacity);
    g_queue.size--;

    restore_interrupts(flags);
    return 1;
}

uint16_t input_dropped_events(void) {
    return g_queue.dropped;
}

uint8_t input_uses_heap_queue(void) {
    return g_queue.uses_heap;
}

uint32_t input_queue_size(void) {
    return g_queue.size;
}

