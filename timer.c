#include "timer.h"

static uint32_t g_loops_per_tick = 1u;
static uint32_t g_loop_acc = 0u;
static uint32_t g_ticks = 0u;

void timer_init(uint32_t loops_per_tick) {
    g_loops_per_tick = (loops_per_tick == 0u) ? 1u : loops_per_tick;
    g_loop_acc = 0u;
    g_ticks = 0u;
}

uint8_t timer_poll_tick(void) {
    g_loop_acc++;

    if (g_loop_acc < g_loops_per_tick) {
        return 0u;
    }

    g_loop_acc = 0u;
    g_ticks++;
    return 1u;
}

uint32_t timer_ticks(void) {
    return g_ticks;
}
