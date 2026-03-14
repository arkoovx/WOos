#include "input.h"

#define INPUT_QUEUE_CAPACITY 64u

static input_event_t g_queue[INPUT_QUEUE_CAPACITY];
static uint16_t g_head = 0;
static uint16_t g_tail = 0;
static uint16_t g_size = 0;
static uint16_t g_dropped = 0;

void input_init(void) {
    g_head = 0;
    g_tail = 0;
    g_size = 0;
    g_dropped = 0;
}

uint8_t input_push(const input_event_t* event) {
    if (g_size >= INPUT_QUEUE_CAPACITY) {
        g_dropped++;
        return 0;
    }

    g_queue[g_tail] = *event;
    g_tail = (uint16_t)((g_tail + 1u) % INPUT_QUEUE_CAPACITY);
    g_size++;
    return 1;
}

uint8_t input_pop(input_event_t* out_event) {
    if (g_size == 0) {
        return 0;
    }

    *out_event = g_queue[g_head];
    g_head = (uint16_t)((g_head + 1u) % INPUT_QUEUE_CAPACITY);
    g_size--;
    return 1;
}

uint16_t input_dropped_events(void) {
    return g_dropped;
}
