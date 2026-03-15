#ifndef WOOS_PMM_H
#define WOOS_PMM_H

#include "kernel.h"

// Размер страницы фиксирован для x86_64 long mode.
#define PMM_PAGE_SIZE 4096u

// Инициализация простого stack-based PMM.
// На текущем этапе используется внутренний предвыделенный пул,
// чтобы не зависеть от карты памяти загрузчика.
void pmm_init(void);

// Возвращает адрес страницы из пула или 0, если свободных страниц нет.
uint64_t pmm_alloc_page(void);

// Возвращает страницу обратно в пул.
// Невалидные адреса и дубликаты безопасно игнорируются.
void pmm_free_page(uint64_t page_addr);

uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);
uint8_t pmm_is_ready(void);

#endif
