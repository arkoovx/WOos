#include "input.h"
#include "kheap.h"

#define INPUT_QUEUE_CAPACITY 64u

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

uint8_t input_push(const input_event_t* event) {
    if (g_queue.size >= g_queue.capacity) {
        g_queue.dropped++;
        return 0;
    }

    g_queue.buffer[g_queue.tail] = *event;
    g_queue.tail = (uint16_t)((g_queue.tail + 1u) % g_queue.capacity);
    g_queue.size++;
    return 1;
}

uint8_t input_pop(input_event_t* out_event) {
    if (g_queue.size == 0) {
        return 0;
    }

    *out_event = g_queue.buffer[g_queue.head];
    g_queue.head = (uint16_t)((g_queue.head + 1u) % g_queue.capacity);
    g_queue.size--;
    return 1;
}

uint16_t input_dropped_events(void) {
    return g_queue.dropped;
}

uint8_t input_uses_heap_queue(void) {
    return g_queue.uses_heap;
}
