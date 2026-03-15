#include "idt.h"

typedef struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct idtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

#define PIC1_CMD  0x20u
#define PIC1_DATA 0x21u
#define PIC2_CMD  0xA0u
#define PIC2_DATA 0xA1u
#define PIC_EOI   0x20u

#define PIC1_OFFSET 32u
#define PIC2_OFFSET 40u

extern void idt_load(const idtr_t* idtr);
extern void idt_stub_ignore(void);
extern void idt_irq1_stub(void);
extern void idt_irq12_stub(void);

static idt_entry_t g_idt[256];
static uint8_t g_idt_ready = 0;
static void (*g_irq_handlers[16])(void);

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ __volatile__("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline void io_wait(void) {
    __asm__ __volatile__("outb %%al, $0x80" : : "a"(0u));
}

static void idt_set_gate(uint8_t vector, void (*handler)(void)) {
    uint64_t addr = (uint64_t)handler;

    g_idt[vector].offset_low = (uint16_t)(addr & 0xFFFFu);
    g_idt[vector].selector = 0x08u;
    g_idt[vector].ist = 0;
    g_idt[vector].type_attr = 0x8Eu;
    g_idt[vector].offset_mid = (uint16_t)((addr >> 16) & 0xFFFFu);
    g_idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFFu);
    g_idt[vector].zero = 0;
}

static void pic_remap(void) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11u);
    io_wait();
    outb(PIC2_CMD, 0x11u);
    io_wait();

    outb(PIC1_DATA, PIC1_OFFSET);
    io_wait();
    outb(PIC2_DATA, PIC2_OFFSET);
    io_wait();

    outb(PIC1_DATA, 0x04u);
    io_wait();
    outb(PIC2_DATA, 0x02u);
    io_wait();

    outb(PIC1_DATA, 0x01u);
    io_wait();
    outb(PIC2_DATA, 0x01u);
    io_wait();

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

static void pic_set_irq_mask(uint8_t irq, uint8_t masked) {
    uint16_t port = (irq < 8u) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(irq & 7u);
    uint8_t value = inb(port);

    if (masked) {
        value = (uint8_t)(value | (1u << bit));
    } else {
        value = (uint8_t)(value & (uint8_t)~(1u << bit));
    }

    outb(port, value);
}

static void pic_send_eoi(uint8_t irq) {
    if (irq >= 8u) {
        outb(PIC2_CMD, PIC_EOI);
    }
    outb(PIC1_CMD, PIC_EOI);
}

void idt_set_irq_handler(uint8_t irq, void (*handler)(void)) {
    if (irq >= 16u) {
        return;
    }

    g_irq_handlers[irq] = handler;
}

void idt_dispatch_irq(uint8_t vector) {
    if (vector < PIC1_OFFSET || vector >= (uint8_t)(PIC1_OFFSET + 16u)) {
        return;
    }

    uint8_t irq = (uint8_t)(vector - PIC1_OFFSET);
    if (g_irq_handlers[irq] != 0) {
        g_irq_handlers[irq]();
    }

    pic_send_eoi(irq);
}

void idt_init(void) {
    for (uint16_t i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, idt_stub_ignore);
    }

    for (uint8_t i = 0; i < 16u; i++) {
        g_irq_handlers[i] = 0;
    }

    pic_remap();

    idt_set_gate((uint8_t)(PIC1_OFFSET + 1u), idt_irq1_stub);
    idt_set_gate((uint8_t)(PIC1_OFFSET + 12u), idt_irq12_stub);

    // Маскируем всё, кроме IRQ1 (keyboard), IRQ2 (cascade) и IRQ12 (mouse).
    for (uint8_t i = 0; i < 16u; i++) {
        pic_set_irq_mask(i, 1u);
    }
    pic_set_irq_mask(1u, 0u);
    pic_set_irq_mask(2u, 0u);
    pic_set_irq_mask(12u, 0u);

    idtr_t idtr;
    idtr.limit = (uint16_t)(sizeof(g_idt) - 1u);
    idtr.base = (uint64_t)&g_idt[0];

    idt_load(&idtr);
    __asm__ __volatile__("sti");
    g_idt_ready = 1u;
}

uint8_t idt_is_ready(void) {
    return g_idt_ready;
}
