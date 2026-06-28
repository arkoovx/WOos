#ifndef WOOS_VMM_H
#define WOOS_VMM_H

#include "kernel.h"

#define VMM_PAGE_PRESENT   (1ull << 0)
#define VMM_PAGE_WRITABLE  (1ull << 1)
#define VMM_PAGE_USER      (1ull << 2)
#define VMM_PAGE_HUGE      (1ull << 7)
#define VMM_PAGE_NX        (1ull << 63)

#define PAGE_SIZE 4096ull

typedef uint64_t pml4_t;

void vmm_init(void);
pml4_t* vmm_create_address_space(void);
void vmm_destroy_address_space(pml4_t* pml4);
uint8_t vmm_map_page(pml4_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags);
uint8_t vmm_unmap_page(pml4_t* pml4, uint64_t virt);
void vmm_switch(pml4_t* pml4);

// Вспомогательная функция для получения физического адреса по виртуальному
uint64_t vmm_get_phys(pml4_t* pml4, uint64_t virt);

#endif
