#ifndef WOOS_IDT_H
#define WOOS_IDT_H

#include "kernel.h"

typedef struct registers {
    // Pushed by PUSH_GPRS (from lowest address to highest)
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;

    // Pushed by stub
    uint64_t vector;
    uint64_t error_code;

    // Pushed by CPU
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) registers_t;

void idt_init(void);
uint8_t idt_is_ready(void);
void idt_enable_interrupts(void);
uint32_t idt_keyboard_irq_count(void);
uint32_t idt_mouse_irq_count(void);
void idt_handle_exception(registers_t* regs);

#endif

