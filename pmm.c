#include "pmm.h"

#define PMM_PAGE_SIZE 4096ull
#define PMM_MAX_STACK_PAGES 16384u
#define PMM_LOW_MEMORY_CUTOFF 0x00100000ull

extern uint8_t __kernel_start[];
extern uint8_t __kernel_end[];

typedef struct pmm_state {
    uint64_t stack[PMM_MAX_STACK_PAGES];
    uint64_t top;
    uint64_t total_pages;
    uint64_t free_pages;
    uint8_t ready;
} pmm_state_t;

static pmm_state_t g_pmm = {{0}, 0ull, 0ull, 0ull, 0u};

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

static void pmm_push_page(uint64_t page_addr) {
    if (g_pmm.top >= PMM_MAX_STACK_PAGES) {
        return;
    }

    g_pmm.stack[g_pmm.top++] = page_addr;
    g_pmm.free_pages++;
}

void pmm_init(const video_info_t* info) {
    g_pmm.top = 0;
    g_pmm.total_pages = 0;
    g_pmm.free_pages = 0;
    g_pmm.ready = 0;

    if (info == 0 || info->version < BOOT_INFO_VERSION_V2 || info->memory_region_count == 0) {
        return;
    }

    uint16_t count = info->memory_region_count;
    if (count > info->memory_region_capacity) {
        count = info->memory_region_capacity;
    }

    for (uint16_t i = 0; i < count; i++) {
        const boot_memory_region_t* region = &info->memory_regions[i];
        if (region->type != BOOT_INFO_E820_TYPE_USABLE || region->length < PMM_PAGE_SIZE) {
            continue;
        }

        uint64_t region_start = align_up_u64(region->base, PMM_PAGE_SIZE);
        uint64_t region_end = align_down_u64(region->base + region->length, PMM_PAGE_SIZE);
        if (region_end <= region_start) {
            continue;
        }

        for (uint64_t page = region_start; page < region_end; page += PMM_PAGE_SIZE) {
            g_pmm.total_pages++;
            if (!page_is_reserved(page)) {
                pmm_push_page(page);
            }
        }
    }

    g_pmm.ready = (uint8_t)(g_pmm.free_pages != 0);
}

void* pmm_alloc_page(void) {
    if (!g_pmm.ready || g_pmm.top == 0) {
        return 0;
    }

    uint64_t page = g_pmm.stack[--g_pmm.top];
    g_pmm.free_pages--;
    return (void*)(uint64_t)page;
}

void pmm_free_page(void* page) {
    uint64_t page_addr = (uint64_t)page;
    if (!g_pmm.ready || page == 0) {
        return;
    }

    if ((page_addr & (PMM_PAGE_SIZE - 1ull)) != 0ull || page_is_reserved(page_addr)) {
        return;
    }

    pmm_push_page(page_addr);
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
