#ifndef WOOS_KHEAP_H
#define WOOS_KHEAP_H

#include "kernel.h"

// Инициализирует базовый heap аллокатор ядра поверх PMM-страниц.
// В случае нехватки памяти аллокатор переходит в not-ready состояние.
void kheap_init(void);

// Выделение/освобождение памяти для внутренних структур ядра.
void* kmalloc(uint64_t size);
void kfree(void* ptr);

// Диагностика состояния heap.
uint8_t kheap_is_ready(void);
uint64_t kheap_total_bytes(void);
uint64_t kheap_free_bytes(void);
uint64_t kheap_largest_free_block(void);

#endif
