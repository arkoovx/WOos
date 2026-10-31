#ifndef WOOS_INPUT_H
#define WOOS_INPUT_H

#include "kernel.h"

typedef enum input_event_type {
    INPUT_EVENT_MOUSE_MOVE = 0,
    INPUT_EVENT_MOUSE_BUTTON = 1,
    INPUT_EVENT_MOUSE_RELEASE = 2,
    INPUT_EVENT_TIMER_TICK = 3,
    INPUT_EVENT_KEY_DOWN = 4,
    INPUT_EVENT_KEY_UP = 5,
    INPUT_EVENT_WINDOW_CHANGE = 6,
    INPUT_EVENT_MOUSE_WHEEL = 7,
} input_event_type_t;

typedef struct input_event {
    input_event_type_t type;
    uint16_t x;
    uint16_t y;
    uint8_t buttons;
} input_event_t;

void input_init(void);
uint8_t input_push(const input_event_t* event);
uint8_t input_pop(input_event_t* out_event);
uint16_t input_dropped_events(void);
uint8_t input_uses_heap_queue(void);

#endif
