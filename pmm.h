#ifndef WOOS_PMM_H
#define WOOS_PMM_H

#include "kernel.h"

void pmm_init(const video_info_t* info);
void* pmm_alloc_page(void);
void pmm_free_page(void* page);

uint8_t pmm_is_ready(void);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
uint64_t pmm_reserved_pages(void);

#endif
