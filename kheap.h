#ifndef WOOS_KHEAP_H
#define WOOS_KHEAP_H

#include "kernel.h"

void kheap_init(void);
void* kheap_alloc(uint64_t size);
void kheap_free(void* ptr);

uint64_t kheap_total_bytes(void);
uint64_t kheap_used_bytes(void);
uint64_t kheap_free_bytes(void);

#endif
