#ifndef WOOS_MOUSE_H
#define WOOS_MOUSE_H

#include "kernel.h"

void mouse_init(uint16_t start_x, uint16_t start_y);
void mouse_poll(void);
uint8_t mouse_is_ready(void);
void mouse_handler(void);
uint16_t mouse_get_x(void);
uint16_t mouse_get_y(void);

#endif
