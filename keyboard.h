#ifndef WOOS_KEYBOARD_H
#define WOOS_KEYBOARD_H

#include "kernel.h"

void keyboard_init(void);
void keyboard_handle_irq(void);
uint8_t keyboard_last_scancode(void);

#endif
