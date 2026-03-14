#ifndef WOOS_TIMER_H
#define WOOS_TIMER_H

#include "kernel.h"

void timer_init(uint32_t loops_per_tick);
uint8_t timer_poll_tick(void);
uint32_t timer_ticks(void);

#endif
