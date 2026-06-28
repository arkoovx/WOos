#include "vmm.h"
#include "pmm.h"
#include "serial.h"

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

static pml4_t* g_kernel_pml4 = 0;

static inline uint64_t get_cr3(void) {
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void vmm_init(void) {
    g_kernel_pml4 = (pml4_t*)get_cr3();
    serial_printf("[VMM] Initialized. Kernel PML4: %p\n", g_kernel_pml4);
}

pml4_t* vmm_create_address_space(void) {
    // 1. Выделяем страницу под PML4
    pml4_t* new_pml4 = (pml4_t*)pmm_alloc_page();
    if (!new_pml4) return 0;
    
    // Заполняем нулями
    for (int i = 0; i < 512; i++) {
        new_pml4[i] = 0;
    }

    // 2. Копируем все записи из текущего PML4 ядра
    // Это сохранит все ядерные маппинги (включая фреймбуфер и PCI)
    for (int i = 0; i < 512; i++) {
        new_pml4[i] = g_kernel_pml4[i];
    }

    // 3. Создаем приватную PDPT для первой записи (чтобы изолировать пользовательскую память выше 4 ГБ)
    pml4_t* new_pdpt = (pml4_t*)pmm_alloc_page();
    if (!new_pdpt) {
        pmm_free_page(new_pml4);
        return 0;
    }
    
    for (int i = 0; i < 512; i++) {
        new_pdpt[i] = 0;
    }

    // Копируем ядерные 0-4 ГБ маппинги из текущей PDPT в новую
    pml4_t* old_pdpt = (pml4_t*)(g_kernel_pml4[0] & ~0xFFFull);
    for (int i = 0; i < 4; i++) {
        new_pdpt[i] = old_pdpt[i];
    }

    // Перезаписываем первую запись PML4, чтобы она указывала на нашу новую приватную PDPT
    new_pml4[0] = (uint64_t)new_pdpt | VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER;

    return new_pml4;
}

void vmm_destroy_address_space(pml4_t* pml4) {
    if (!pml4 || pml4 == g_kernel_pml4) return;

    // Освобождаем приватную PDPT
    if (pml4[0] & VMM_PAGE_PRESENT) {
        pml4_t* pdpt = (pml4_t*)(pml4[0] & ~0xFFFull);
        
        // Освобождаем только те каталоги страниц, которые лежат выше 4 ГБ (индексы >= 4)
        for (int i = 4; i < 512; i++) {
            if (pdpt[i] & VMM_PAGE_PRESENT) {
                pml4_t* pd = (pml4_t*)(pdpt[i] & ~0xFFFull);
                for (int j = 0; j < 512; j++) {
                    if (pd[j] & VMM_PAGE_PRESENT) {
                        pml4_t* pt = (pml4_t*)(pd[j] & ~0xFFFull);
                        pmm_free_page(pt);
                    }
                }
                pmm_free_page(pd);
            }
        }
        pmm_free_page(pdpt);
    }
    
    // Освобождаем саму PML4 таблицу
    pmm_free_page(pml4);
}

uint8_t vmm_map_page(pml4_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4) pml4 = g_kernel_pml4;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    // 1. Уровень PML4
    if (!(pml4[pml4_idx] & VMM_PAGE_PRESENT)) {
        void* new_table = pmm_alloc_page();
        if (!new_table) return 0;
        for (int i = 0; i < 512; i++) ((uint64_t*)new_table)[i] = 0;
        pml4[pml4_idx] = (uint64_t)new_table | VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | (flags & VMM_PAGE_USER);
    } else {
        if (flags & VMM_PAGE_USER) {
            pml4[pml4_idx] |= VMM_PAGE_USER;
        }
    }
    pml4_t* pdpt = (pml4_t*)(pml4[pml4_idx] & ~0xFFFull);

    // 2. Уровень PDPT
    if (!(pdpt[pdpt_idx] & VMM_PAGE_PRESENT)) {
        void* new_table = pmm_alloc_page();
        if (!new_table) return 0;
        for (int i = 0; i < 512; i++) ((uint64_t*)new_table)[i] = 0;
        pdpt[pdpt_idx] = (uint64_t)new_table | VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | (flags & VMM_PAGE_USER);
    } else {
        if (flags & VMM_PAGE_USER) {
            pdpt[pdpt_idx] |= VMM_PAGE_USER;
        }
    }
    pml4_t* pd = (pml4_t*)(pdpt[pdpt_idx] & ~0xFFFull);

    // 3. Уровень PD
    if (!(pd[pd_idx] & VMM_PAGE_PRESENT)) {
        void* new_table = pmm_alloc_page();
        if (!new_table) return 0;
        for (int i = 0; i < 512; i++) ((uint64_t*)new_table)[i] = 0;
        pd[pd_idx] = (uint64_t)new_table | VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | (flags & VMM_PAGE_USER);
    } else {
        if (flags & VMM_PAGE_USER) {
            pd[pd_idx] |= VMM_PAGE_USER;
        }
    }
    pml4_t* pt = (pml4_t*)(pd[pd_idx] & ~0xFFFull);

    // 4. Уровень Page Table
    pt[pt_idx] = (phys & ~0xFFFull) | flags | VMM_PAGE_PRESENT;

    // Сбрасываем запись TLB для этого адреса
    __asm__ __volatile__("invlpg (%0)" : : "r"(virt) : "memory");

    return 1;
}

uint8_t vmm_unmap_page(pml4_t* pml4, uint64_t virt) {
    if (!pml4) pml4 = g_kernel_pml4;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_PAGE_PRESENT)) return 0;
    pml4_t* pdpt = (pml4_t*)(pml4[pml4_idx] & ~0xFFFull);

    if (!(pdpt[pdpt_idx] & VMM_PAGE_PRESENT)) return 0;
    pml4_t* pd = (pml4_t*)(pdpt[pdpt_idx] & ~0xFFFull);

    if (!(pd[pd_idx] & VMM_PAGE_PRESENT)) return 0;
    pml4_t* pt = (pml4_t*)(pd[pd_idx] & ~0xFFFull);

    if (!(pt[pt_idx] & VMM_PAGE_PRESENT)) return 0;
    pt[pt_idx] = 0;

    __asm__ __volatile__("invlpg (%0)" : : "r"(virt) : "memory");
    return 1;
}

void vmm_switch(pml4_t* pml4) {
    if (!pml4) pml4 = g_kernel_pml4;
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(pml4) : "memory");
}

uint64_t vmm_get_phys(pml4_t* pml4, uint64_t virt) {
    if (!pml4) pml4 = g_kernel_pml4;

    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & VMM_PAGE_PRESENT)) return 0;
    pml4_t* pdpt = (pml4_t*)(pml4[pml4_idx] & ~0xFFFull);

    if (!(pdpt[pdpt_idx] & VMM_PAGE_PRESENT)) return 0;
    pml4_t* pd = (pml4_t*)(pdpt[pdpt_idx] & ~0xFFFull);

    if (!(pd[pd_idx] & VMM_PAGE_PRESENT)) return 0;
    pml4_t* pt = (pml4_t*)(pd[pd_idx] & ~0xFFFull);

    if (!(pt[pt_idx] & VMM_PAGE_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFFull) | (virt & 0xFFFull);
}
