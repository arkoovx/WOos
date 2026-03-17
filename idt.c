#include "idt.h"

#define PIC1_COMMAND 0x20u
#define PIC1_DATA    0x21u
#define PIC2_COMMAND 0xA0u
#define PIC2_DATA    0xA1u
#define PIC_EOI      0x20u

#define PIC_ICW1_ICW4 0x01u
#define PIC_ICW1_INIT 0x10u
#define PIC_ICW4_8086 0x01u

#define IRQ_VECTOR_BASE_MASTER 32u
#define IRQ_VECTOR_BASE_SLAVE  40u
#define IRQ_KEYBOARD_VECTOR    (IRQ_VECTOR_BASE_MASTER + 1u)
#define IRQ_MOUSE_VECTOR       (IRQ_VECTOR_BASE_SLAVE + 4u)

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

extern void idt_load(const idtr_t* idtr);
extern void idt_stub_ignore(void);
extern void idt_stub_irq1(void);
extern void idt_stub_irq12(void);

static idt_entry_t g_idt[256];
static uint8_t g_idt_ready = 0;
static uint32_t g_keyboard_irq_count = 0u;
static uint32_t g_mouse_irq_count = 0u;

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

static void pic_remap(void) {
    uint8_t mask_master = inb(PIC1_DATA);
    uint8_t mask_slave = inb(PIC2_DATA);

    outb(PIC1_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_wait();

    outb(PIC1_DATA, IRQ_VECTOR_BASE_MASTER);
    io_wait();
    outb(PIC2_DATA, IRQ_VECTOR_BASE_SLAVE);
    io_wait();

    outb(PIC1_DATA, 4u);
    io_wait();
    outb(PIC2_DATA, 2u);
    io_wait();

    outb(PIC1_DATA, PIC_ICW4_8086);
    io_wait();
    outb(PIC2_DATA, PIC_ICW4_8086);
    io_wait();

    outb(PIC1_DATA, mask_master);
    outb(PIC2_DATA, mask_slave);
}

static void pic_set_irq_mask(uint8_t irq, uint8_t masked) {
    uint16_t port = (irq < 8u) ? PIC1_DATA : PIC2_DATA;
    uint8_t bit = (uint8_t)(irq % 8u);
    uint8_t current = inb(port);

    if (masked) {
        current = (uint8_t)(current | (uint8_t)(1u << bit));
    } else {
        current = (uint8_t)(current & (uint8_t)~(1u << bit));
    }

    outb(port, current);
}

static void pic_send_eoi(uint8_t vector) {
    if (vector >= IRQ_VECTOR_BASE_SLAVE && vector < (IRQ_VECTOR_BASE_SLAVE + 8u)) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
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

void idt_init(void) {
    for (uint16_t i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, idt_stub_ignore);
    }

    idt_set_gate(IRQ_KEYBOARD_VECTOR, idt_stub_irq1);
    idt_set_gate(IRQ_MOUSE_VECTOR, idt_stub_irq12);

    pic_remap();

    // Маскируем всё, кроме keyboard IRQ1 и cascade IRQ2 на master,
    // а на slave оставляем только mouse IRQ12.
    outb(PIC1_DATA, 0xFFu);
    outb(PIC2_DATA, 0xFFu);
    pic_set_irq_mask(1u, 0u);
    pic_set_irq_mask(2u, 0u);
    pic_set_irq_mask(12u, 0u);

    idtr_t idtr;
    idtr.limit = (uint16_t)(sizeof(g_idt) - 1u);
    idtr.base = (uint64_t)&g_idt[0];

    idt_load(&idtr);
    g_idt_ready = 1u;
}

void idt_enable_interrupts(void) {
    __asm__ __volatile__("sti");
}

void idt_handle_irq(uint32_t vector) {
    if (vector == IRQ_KEYBOARD_VECTOR) {
        (void)inb(0x60u);
        g_keyboard_irq_count++;
    } else if (vector == IRQ_MOUSE_VECTOR) {
        g_mouse_irq_count++;
    }

    pic_send_eoi((uint8_t)vector);
}

uint8_t idt_is_ready(void) {
    return g_idt_ready;
}

uint32_t idt_keyboard_irq_count(void) {
    return g_keyboard_irq_count;
}

uint32_t idt_mouse_irq_count(void) {
    return g_mouse_irq_count;
}
