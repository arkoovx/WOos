#include "pmm.h"
#include "serial.h"

#define PMM_PAGE_SIZE 4096ull
#define PMM_LOW_MEMORY_CUTOFF 0x00100000ull

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

typedef struct pmm_state {
    uint64_t last_search_idx;
    uint64_t total_pages;
    uint64_t free_pages;
    uint8_t ready;
} pmm_state_t;

static pmm_state_t g_pmm = {0ull, 0ull, 0ull, 0u};
static uint8_t g_pmm_bitmap[PMM_BITMAP_SIZE];

static inline void bitmap_set(uint64_t page_idx) {
    g_pmm_bitmap[page_idx / 8u] |= (uint8_t)(1u << (page_idx % 8u));
}

static inline void bitmap_clear(uint64_t page_idx) {
    g_pmm_bitmap[page_idx / 8u] &= (uint8_t)~(1u << (page_idx % 8u));
}

static inline uint8_t bitmap_test(uint64_t page_idx) {
    return (g_pmm_bitmap[page_idx / 8u] & (uint8_t)(1u << (page_idx % 8u))) != 0u;
}

static uint64_t align_up_u64(uint64_t value, uint64_t align) {
    return (value + (align - 1ull)) & ~(align - 1ull);
}

static uint64_t align_down_u64(uint64_t value, uint64_t align) {
    return value & ~(align - 1ull);
}

static uint8_t range_overlaps(uint64_t start_a, uint64_t end_a, uint64_t start_b, uint64_t end_b) {
    return !(end_a <= start_b || end_b <= start_a);
}

static uint8_t page_is_reserved(uint64_t page_addr) {
    uint64_t page_end = page_addr + PMM_PAGE_SIZE;
    uint64_t kernel_start = align_down_u64((uint64_t)__kernel_start, PMM_PAGE_SIZE);
    uint64_t kernel_end = align_up_u64((uint64_t)__kernel_end, PMM_PAGE_SIZE);

    if (page_addr < PMM_LOW_MEMORY_CUTOFF) {
        return 1u;
    }

    if (range_overlaps(page_addr, page_end, kernel_start, kernel_end)) {
        return 1u;
    }

    return 0u;
}

void pmm_init(const video_info_t* info) {
    g_pmm.last_search_idx = 0;
    g_pmm.total_pages = 0;
    g_pmm.free_pages = 0;
    g_pmm.ready = 0;

    // Помечаем всю память как занятую (1)
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        g_pmm_bitmap[i] = 0xFFu;
    }

    serial_printf("[PMM] pmm_init start. info=%p\n", info);
    if (info) {
        serial_printf("[PMM] info->magic=%x, info->version=%u, count=%u, capacity=%u\n", 
                      (uint32_t)info->magic, (uint32_t)info->version, (uint32_t)info->memory_region_count, (uint32_t)info->memory_region_capacity);
    }

    if (info == 0 || info->version < BOOT_INFO_VERSION_V2 || info->memory_region_count == 0) {
        serial_printf("[PMM] early return from pmm_init!\n");
        return;
    }

    uint16_t count = info->memory_region_count;
    if (count > info->memory_region_capacity) {
        count = info->memory_region_capacity;
    }

    for (uint16_t i = 0; i < count; i++) {
        const boot_memory_region_t* region = &info->memory_regions[i];
        serial_printf("[PMM] Region %d: base=0x%x", (int)i, (uint32_t)(region->base >> 32));
        serial_printf("%x, ", (uint32_t)region->base);
        serial_printf("len=0x%x", (uint32_t)(region->length >> 32));
        serial_printf("%x, ", (uint32_t)region->length);
        serial_printf("type=%u\n", (uint32_t)region->type);

        if (region->type != BOOT_INFO_E820_TYPE_USABLE || region->length < PMM_PAGE_SIZE) {
            continue;
        }

        uint64_t region_start = align_up_u64(region->base, PMM_PAGE_SIZE);
        uint64_t region_end = align_down_u64(region->base + region->length, PMM_PAGE_SIZE);
        if (region_end <= region_start) {
            continue;
        }

        for (uint64_t page = region_start; page < region_end; page += PMM_PAGE_SIZE) {
            uint64_t page_idx = page / PMM_PAGE_SIZE;
            if (page_idx >= PMM_MAX_PAGES) {
                break; // Выходим за пределы поддерживаемой памяти (4 ГБ)
            }
            g_pmm.total_pages++;

            if (!page_is_reserved(page)) {
                bitmap_clear(page_idx); // Освобождаем страницу
                g_pmm.free_pages++;
            }
        }
    }

    g_pmm.ready = (uint8_t)(g_pmm.free_pages != 0);
    serial_printf("[PMM] pmm_init done. ready=%d, free_pages=%u, total_pages=%u\n",
                  (int)g_pmm.ready, (uint32_t)g_pmm.free_pages, (uint32_t)g_pmm.total_pages);
}

void* pmm_alloc_page(void) {
    if (!g_pmm.ready || g_pmm.free_pages == 0) {
        return 0;
    }

    // Ищем свободную страницу с прошлого индекса
    for (uint64_t i = g_pmm.last_search_idx; i < PMM_MAX_PAGES; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            g_pmm.free_pages--;
            g_pmm.last_search_idx = i;
            return (void*)(i * PMM_PAGE_SIZE);
        }
    }

    // Если не нашли, ищем с начала
    for (uint64_t i = 0; i < g_pmm.last_search_idx; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            g_pmm.free_pages--;
            g_pmm.last_search_idx = i;
            return (void*)(i * PMM_PAGE_SIZE);
        }
    }

    return 0;
}

void pmm_free_page(void* page) {
    uint64_t page_addr = (uint64_t)page;
    if (!g_pmm.ready || page == 0) {
        return;
    }

    if ((page_addr & (PMM_PAGE_SIZE - 1ull)) != 0ull || page_is_reserved(page_addr)) {
        return;
    }

    uint64_t page_idx = page_addr / PMM_PAGE_SIZE;
    if (page_idx >= PMM_MAX_PAGES) {
        return;
    }

    if (bitmap_test(page_idx)) {
        bitmap_clear(page_idx);
        g_pmm.free_pages++;
        if (page_idx < g_pmm.last_search_idx) {
            g_pmm.last_search_idx = page_idx;
        }
    }
}

void* pmm_alloc_pages_multi(uint32_t count) {
    if (!g_pmm.ready || g_pmm.free_pages < count || count == 0) {
        return 0;
    }

    uint32_t found_count = 0;
    uint64_t start_idx = 0;

    for (uint64_t i = 0; i < PMM_MAX_PAGES; i++) {
        if (!bitmap_test(i)) {
            if (found_count == 0) {
                start_idx = i;
            }
            found_count++;
            if (found_count == count) {
                for (uint64_t j = start_idx; j <= i; j++) {
                    bitmap_set(j);
                }
                g_pmm.free_pages -= count;
                return (void*)(start_idx * PMM_PAGE_SIZE);
            }
        } else {
            found_count = 0;
        }
    }

    return 0;
}

void pmm_free_pages_multi(void* pages, uint32_t count) {
    uint64_t page_addr = (uint64_t)pages;
    if (!g_pmm.ready || pages == 0 || count == 0) {
        return;
    }
    uint64_t page_idx = page_addr / PMM_PAGE_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t cur_idx = page_idx + i;
        if (cur_idx < PMM_MAX_PAGES && bitmap_test(cur_idx)) {
            bitmap_clear(cur_idx);
            g_pmm.free_pages++;
        }
    }
}

uint8_t pmm_is_ready(void) {
    return g_pmm.ready;
}

uint64_t pmm_total_pages(void) {
    return g_pmm.total_pages;
}

uint64_t pmm_free_pages(void) {
    return g_pmm.free_pages;
}

uint64_t pmm_reserved_pages(void) {
    return g_pmm.total_pages - g_pmm.free_pages;
}
