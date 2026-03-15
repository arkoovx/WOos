#include "keyboard.h"

#include "input.h"

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_AUX_DATA    0x20u

static uint8_t g_last_scancode = 0u;

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void keyboard_init(void) {
    g_last_scancode = 0u;
}

void keyboard_handle_irq(void) {
    uint8_t status = inb(PS2_STATUS_PORT);
    if ((status & PS2_STATUS_OUTPUT_FULL) == 0u) {
        return;
    }

    // Клавиатура обслуживает только non-AUX данные, чтобы не перехватывать байты мыши.
    if (status & PS2_STATUS_AUX_DATA) {
        return;
    }

    uint8_t scancode = inb(PS2_DATA_PORT);
    g_last_scancode = scancode;

    input_event_t key_event = {INPUT_EVENT_KEY_PRESS, 0u, 0u, 0u, scancode};
    input_push(&key_event);
}

uint8_t keyboard_last_scancode(void) {
    return g_last_scancode;
}
