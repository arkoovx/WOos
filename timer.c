#include "timer.h"

#define PIT_CHANNEL0_DATA        0x40u
#define PIT_COMMAND              0x43u
#define PIT_BASE_FREQUENCY_HZ    1193182u
#define PIT_MODE_RATE_GENERATOR  0x34u
#define PIT_LATCH_CHANNEL0       0x00u
#define PIT_MAX_RELOAD           0x10000u

static uint32_t g_tick_hz = 20u;
static uint16_t g_pit_reload = 0u;
static uint16_t g_last_counter = 0u;
static uint32_t g_subtick_accumulator = 0u;
static uint32_t g_ticks = 0u;
static uint8_t g_pit_ready = 0u;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static uint32_t timer_read_counter0(void) {
    outb(PIT_COMMAND, PIT_LATCH_CHANNEL0);

    uint8_t low = inb(PIT_CHANNEL0_DATA);
    uint8_t high = inb(PIT_CHANNEL0_DATA);
    uint16_t counter = (uint16_t)(((uint16_t)high << 8) | low);

    // Для PIT значение 0 в latched count означает полный период 65536 тиков.
    return (counter == 0u) ? PIT_MAX_RELOAD : (uint32_t)counter;
}

void timer_init(uint32_t tick_hz) {
    if (tick_hz == 0u) {
        tick_hz = 20u;
    }

    uint32_t reload = PIT_BASE_FREQUENCY_HZ / tick_hz;
    if (reload == 0u) {
        reload = 1u;
    }
    if (reload > PIT_MAX_RELOAD) {
        reload = PIT_MAX_RELOAD;
    }

    g_tick_hz = tick_hz;
    g_pit_reload = (reload == PIT_MAX_RELOAD) ? 0u : (uint16_t)reload;
    g_subtick_accumulator = 0u;
    g_ticks = 0u;

    outb(PIT_COMMAND, PIT_MODE_RATE_GENERATOR);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(g_pit_reload & 0xFFu));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((g_pit_reload >> 8) & 0xFFu));

    g_last_counter = (uint16_t)timer_read_counter0();
    g_pit_ready = 1u;
}

uint8_t timer_poll_tick(void) {
    if (!g_pit_ready) {
        return 0u;
    }

    uint32_t current_counter = timer_read_counter0();
    uint32_t previous_counter = (g_last_counter == 0u) ? PIT_MAX_RELOAD : (uint32_t)g_last_counter;
    uint32_t reload_value = (g_pit_reload == 0u) ? PIT_MAX_RELOAD : (uint32_t)g_pit_reload;
    uint32_t elapsed_counts;

    if (previous_counter >= current_counter) {
        elapsed_counts = previous_counter - current_counter;
    } else {
        // Счётчик PIT убывает и затем перезагружается. Если текущее значение стало
        // больше предыдущего, значит между двумя опросами произошёл wrap на reload.
        elapsed_counts = previous_counter + (reload_value - current_counter);
    }

    g_last_counter = (current_counter == PIT_MAX_RELOAD) ? 0u : (uint16_t)current_counter;
    g_subtick_accumulator += elapsed_counts;

    if (g_subtick_accumulator < reload_value) {
        return 0u;
    }

    g_subtick_accumulator -= reload_value;
    g_ticks++;
    return 1u;
}

uint32_t timer_ticks(void) {
    return g_ticks;
}

uint32_t timer_frequency_hz(void) {
    return g_tick_hz;
}
