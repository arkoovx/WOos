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

extern void idt_load(const idtr_t* idtr);
extern void idt_stub_ignore(void);

static idt_entry_t g_idt[256];
static uint8_t g_idt_ready = 0;

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

    idtr_t idtr;
    idtr.limit = (uint16_t)(sizeof(g_idt) - 1u);
    idtr.base = (uint64_t)&g_idt[0];

    idt_load(&idtr);
    g_idt_ready = 1u;
}

uint8_t idt_is_ready(void) {
    return g_idt_ready;
}
