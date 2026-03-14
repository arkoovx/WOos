#ifndef WOOS_IDT_H
#define WOOS_IDT_H

#include "kernel.h"

void idt_init(void);
uint8_t idt_is_ready(void);

#endif
