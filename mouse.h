#ifndef WOOS_MOUSE_H
#define WOOS_MOUSE_H

#include "kernel.h"

void mouse_init(uint16_t start_x, uint16_t start_y);
void mouse_poll(void);
void mouse_handle_irq(void);
uint8_t mouse_is_ready(void);

#endif
