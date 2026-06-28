#include "tss.h"
#include "serial.h"

extern uint64_t gdt64[];

static tss_t g_tss;

// Простая обертка для memset из lib.c
extern void* memset(void* s, int c, size_t n);

void tss_init(void* kernel_stack_top) {
    // 1. Очищаем структуру TSS
    memset(&g_tss, 0, sizeof(tss_t));
    g_tss.rsp0 = (uint64_t)kernel_stack_top;
    g_tss.iopb = sizeof(tss_t);

    // 2. Настраиваем дескриптор TSS в GDT (индексы 6 и 7)
    uint64_t tss_base = (uint64_t)&g_tss;
    uint32_t tss_limit = sizeof(tss_t) - 1;

    uint64_t entry_low = 0;
    entry_low |= (tss_limit & 0xFFFF);
    entry_low |= ((tss_base & 0xFFFFFF) << 16);
    entry_low |= (0x89ULL << 40); // Present, Ring 0, Type 9 (64-bit Available TSS)
    entry_low |= (((tss_limit >> 16) & 0xF) << 48);
    entry_low |= (((tss_base >> 24) & 0xFF) << 56);

    uint64_t entry_high = (tss_base >> 32);

    gdt64[6] = entry_low;
    gdt64[7] = entry_high;

    // 3. Загружаем TSS с помощью инструкции ltr
    __asm__ __volatile__("ltr %%ax" : : "a"((uint16_t)0x30));
    
    serial_printf("[TSS] TSS loaded. Base: %p, Limit: %u, RSP0: %p\n", 
                  (void*)tss_base, tss_limit, (void*)g_tss.rsp0);
}

void tss_set_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}
