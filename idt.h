#ifndef WOOS_IDT_H
#define WOOS_IDT_H

#include "kernel.h"

void idt_init(void);
uint8_t idt_is_ready(void);
void idt_enable_interrupts(void);
uint32_t idt_keyboard_irq_count(void);
uint32_t idt_mouse_irq_count(void);

#endif
