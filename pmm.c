#include "pmm.h"

#define PMM_POOL_PAGES 1024u
#define PMM_POOL_SIZE_BYTES ((uint64_t)PMM_POOL_PAGES * (uint64_t)PMM_PAGE_SIZE)

// Предвыделенный физический пул (baseline до подключения memory map загрузчика).
static uint8_t pmm_pool[PMM_POOL_SIZE_BYTES] __attribute__((aligned(PMM_PAGE_SIZE)));

// Stack free-list: храним индексы страниц в пуле.
static uint32_t pmm_free_stack[PMM_POOL_PAGES];
static uint32_t pmm_stack_top = 0u;
static uint8_t pmm_ready = 0u;

void pmm_init(void) {
    pmm_stack_top = 0u;

    // Заполняем стек в обратном порядке, чтобы выдача шла от начала пула.
    for (uint32_t i = PMM_POOL_PAGES; i > 0u; i--) {
        pmm_free_stack[pmm_stack_top++] = i - 1u;
    }

    pmm_ready = 1u;
}

uint64_t pmm_alloc_page(void) {
    if (!pmm_ready || pmm_stack_top == 0u) {
        return 0u;
    }

    uint32_t page_index = pmm_free_stack[--pmm_stack_top];
    return (uint64_t)((uint8_t*)pmm_pool + ((uint64_t)page_index * (uint64_t)PMM_PAGE_SIZE));
}

static uint8_t pmm_is_pool_page(uint64_t page_addr) {
    uint64_t pool_begin = (uint64_t)(uint8_t*)pmm_pool;
    uint64_t pool_end = pool_begin + PMM_POOL_SIZE_BYTES;

    if (page_addr < pool_begin || page_addr >= pool_end) {
        return 0u;
    }

    return ((page_addr - pool_begin) % PMM_PAGE_SIZE) == 0u;
}

void pmm_free_page(uint64_t page_addr) {
    if (!pmm_ready || pmm_stack_top >= PMM_POOL_PAGES || !pmm_is_pool_page(page_addr)) {
        return;
    }

    // Простейшая защита от двойного освобождения через линейную проверку стека.
    for (uint32_t i = 0u; i < pmm_stack_top; i++) {
        uint64_t stacked_page = (uint64_t)((uint8_t*)pmm_pool + ((uint64_t)pmm_free_stack[i] * (uint64_t)PMM_PAGE_SIZE));
        if (stacked_page == page_addr) {
            return;
        }
    }

    uint32_t page_index = (uint32_t)((page_addr - (uint64_t)(uint8_t*)pmm_pool) / PMM_PAGE_SIZE);
    pmm_free_stack[pmm_stack_top++] = page_index;
}

uint32_t pmm_total_pages(void) {
    return PMM_POOL_PAGES;
}

uint32_t pmm_free_pages(void) {
    return pmm_stack_top;
}

uint8_t pmm_is_ready(void) {
    return pmm_ready;
}
