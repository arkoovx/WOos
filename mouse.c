#include "mouse.h"

#include "input.h"

#define PS2_DATA_PORT 0x60u
#define PS2_STATUS_PORT 0x64u
#define PS2_COMMAND_PORT 0x64u

#define PS2_STATUS_OUTPUT_FULL 0x01u
#define PS2_STATUS_INPUT_FULL  0x02u
#define PS2_STATUS_AUX_DATA    0x20u

#define MOUSE_PACKET_SIZE 3u
#define MOUSE_POLL_MAX_BYTES 64u

typedef struct mouse_state {
    uint16_t x;
    uint16_t y;
    uint8_t buttons;
    uint8_t ready;
    uint8_t packet_index;
    uint8_t packet[MOUSE_PACKET_SIZE];
} mouse_state_t;

static mouse_state_t g_mouse = {0u, 0u, 0u, 0u, 0u, {0u, 0u, 0u}};

static inline void io_wait(void) {
    __asm__ __volatile__("outb %%al, $0x80" : : "a"(0u));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static uint8_t ps2_wait_input_clear(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL) == 0u) {
            return 1u;
        }
        io_wait();
    }

    return 0u;
}

static uint8_t ps2_wait_output_full(void) {
    for (uint32_t i = 0; i < 100000u; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return 1u;
        }
        io_wait();
    }

    return 0u;
}

static void ps2_flush_output(void) {
    for (uint16_t i = 0; i < 32u; i++) {
        uint8_t status = inb(PS2_STATUS_PORT);
        if ((status & PS2_STATUS_OUTPUT_FULL) == 0u) {
            return;
        }
        (void)inb(PS2_DATA_PORT);
    }
}

static uint8_t ps2_write_command(uint8_t command) {
    if (!ps2_wait_input_clear()) {
        return 0u;
    }

    outb(PS2_COMMAND_PORT, command);
    return 1u;
}

static uint8_t ps2_write_data(uint8_t value) {
    if (!ps2_wait_input_clear()) {
        return 0u;
    }

    outb(PS2_DATA_PORT, value);
    return 1u;
}

static uint8_t ps2_read_data(uint8_t* out_value) {
    if (!ps2_wait_output_full()) {
        return 0u;
    }

    *out_value = inb(PS2_DATA_PORT);
    return 1u;
}

// Отправка команды устройству мыши через порт 0xD4 с проверкой ACK (0xFA).
static uint8_t mouse_write(uint8_t value) {
    uint8_t ack = 0u;

    if (!ps2_write_command(0xD4u)) {
        return 0u;
    }

    if (!ps2_write_data(value)) {
        return 0u;
    }

    if (!ps2_read_data(&ack)) {
        return 0u;
    }

    return (uint8_t)(ack == 0xFAu);
}

void mouse_init(uint16_t start_x, uint16_t start_y) {
    g_mouse.x = start_x;
    g_mouse.y = start_y;
    g_mouse.buttons = 0u;
    g_mouse.packet_index = 0u;
    g_mouse.ready = 0u;

    ps2_flush_output();

    if (!ps2_write_command(0xA8u)) {
        return;
    }

    if (!ps2_write_command(0x20u)) {
        return;
    }

    uint8_t config = 0u;
    if (!ps2_read_data(&config)) {
        return;
    }

    config = (uint8_t)((config | 0x02u) & (uint8_t)~0x20u);
    if (!ps2_write_command(0x60u)) {
        return;
    }

    if (!ps2_write_data(config)) {
        return;
    }

    if (!mouse_write(0xF6u)) {
        return;
    }

    if (!mouse_write(0xF4u)) {
        return;
    }

    g_mouse.ready = 1u;
}

uint8_t mouse_is_ready(void) {
    return g_mouse.ready;
}

// Разбор стандартного 3-байтового PS/2 пакета и публикация нормализованных input-событий.
static void handle_mouse_packet(const uint8_t packet[MOUSE_PACKET_SIZE]) {
    if ((packet[0] & 0x08u) == 0u) {
        return;
    }

    if (packet[0] & 0xC0u) {
        return;
    }

    int16_t dx = (int16_t)(int8_t)packet[1];
    int16_t dy = (int16_t)(int8_t)packet[2];

    if (dx != 0 || dy != 0) {
        int32_t next_x = (int32_t)g_mouse.x + dx;
        int32_t next_y = (int32_t)g_mouse.y - dy;

        if (next_x < 0) {
            next_x = 0;
        }
        if (next_y < 0) {
            next_y = 0;
        }

        g_mouse.x = (uint16_t)next_x;
        g_mouse.y = (uint16_t)next_y;

        input_event_t move_event = {INPUT_EVENT_MOUSE_MOVE, g_mouse.x, g_mouse.y, g_mouse.buttons};
        input_push(&move_event);
    }

    uint8_t next_buttons = (uint8_t)(packet[0] & 0x07u);
    if (next_buttons != g_mouse.buttons) {
        g_mouse.buttons = next_buttons;
        input_event_t button_event = {INPUT_EVENT_MOUSE_BUTTON, g_mouse.x, g_mouse.y, g_mouse.buttons};
        input_push(&button_event);
    }
}

// Polling-путь: вычитываем доступные байты из 8042 и собираем полные пакеты мыши.
void mouse_poll(void) {
    if (!g_mouse.ready) {
        return;
    }

    // Вычитываем больше байтов за одну итерацию цикла ядра, чтобы не терять
    // PS/2-пакеты при быстрых движениях мыши на стороне хоста/QEMU.
    for (uint8_t i = 0; i < MOUSE_POLL_MAX_BYTES; i++) {
        uint8_t status = inb(PS2_STATUS_PORT);
        if ((status & PS2_STATUS_OUTPUT_FULL) == 0u) {
            return;
        }

        uint8_t data = inb(PS2_DATA_PORT);

        if ((status & PS2_STATUS_AUX_DATA) == 0u) {
            continue;
        }

        if (g_mouse.packet_index == 0u && (data & 0x08u) == 0u) {
            continue;
        }

        g_mouse.packet[g_mouse.packet_index++] = data;

        if (g_mouse.packet_index == MOUSE_PACKET_SIZE) {
            handle_mouse_packet(g_mouse.packet);
            g_mouse.packet_index = 0u;
        }
    }
}
