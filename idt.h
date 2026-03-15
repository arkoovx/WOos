#ifndef WOOS_IDT_H
#define WOOS_IDT_H

#include "kernel.h"

void idt_init(void);
void idt_enable_interrupts(void);
uint8_t idt_is_ready(void);

void idt_set_irq_handler(uint8_t irq, void (*handler)(void));
void idt_dispatch_irq(uint8_t vector);

#endif
