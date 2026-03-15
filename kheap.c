#include "kheap.h"
#include "pmm.h"

#define KHEAP_ALIGNMENT 16ull
#define KHEAP_BOOTSTRAP_PAGES 64u

typedef struct kheap_block {
    uint64_t size;
    struct kheap_block* next;
    struct kheap_block* prev;
    uint8_t free;
} kheap_block_t;

static uint8_t* g_heap_base = (uint8_t*)0;
static uint64_t g_heap_size = 0;
static kheap_block_t* g_head = (kheap_block_t*)0;
static uint8_t g_ready = 0;

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

static uint8_t pointer_in_heap(const void* ptr) {
    if (!g_ready || ptr == (void*)0) {
        return 0u;
    }

    uint64_t addr = (uint64_t)ptr;
    uint64_t begin = (uint64_t)g_heap_base;
    uint64_t end = begin + g_heap_size;
    return (addr >= begin) && (addr < end);
}

static void split_block(kheap_block_t* block, uint64_t wanted_size) {
    uint64_t header_size = (uint64_t)sizeof(kheap_block_t);

    if (block->size <= wanted_size + header_size + KHEAP_ALIGNMENT) {
        return;
    }

    uint8_t* block_start = (uint8_t*)block;
    kheap_block_t* tail = (kheap_block_t*)(block_start + header_size + wanted_size);

    tail->size = block->size - wanted_size - header_size;
    tail->next = block->next;
    tail->prev = block;
    tail->free = 1u;

    if (tail->next != (kheap_block_t*)0) {
        tail->next->prev = tail;
    }

    block->size = wanted_size;
    block->next = tail;
}

static void merge_with_next(kheap_block_t* block) {
    if (block == (kheap_block_t*)0 || block->next == (kheap_block_t*)0) {
        return;
    }

    kheap_block_t* next = block->next;
    if (!block->free || !next->free) {
        return;
    }

    block->size += (uint64_t)sizeof(kheap_block_t) + next->size;
    block->next = next->next;

    if (block->next != (kheap_block_t*)0) {
        block->next->prev = block;
    }
}

void kheap_init(void) {
    g_heap_base = (uint8_t*)0;
    g_heap_size = 0;
    g_head = (kheap_block_t*)0;
    g_ready = 0u;

    if (!pmm_is_ready()) {
        return;
    }

    uint8_t* first_page = (uint8_t*)(uint64_t)pmm_alloc_page();
    if (first_page == (uint8_t*)0) {
        return;
    }

    uint32_t acquired_pages = 1u;

    // Для baseline heap ожидаем, что страницы PMM выдаются подряд.
    // Это верно для текущего pool allocator и даёт простой непрерывный arena.
    while (acquired_pages < KHEAP_BOOTSTRAP_PAGES) {
        uint8_t* page = (uint8_t*)(uint64_t)pmm_alloc_page();

        if (page == (uint8_t*)0) {
            break;
        }

        uint8_t* expected = first_page + ((uint64_t)acquired_pages * (uint64_t)PMM_PAGE_SIZE);
        if (page != expected) {
            pmm_free_page((uint64_t)page);
            break;
        }

        acquired_pages++;
    }

    g_heap_base = first_page;
    g_heap_size = (uint64_t)acquired_pages * (uint64_t)PMM_PAGE_SIZE;
    g_head = (kheap_block_t*)g_heap_base;

    if (g_heap_size <= sizeof(kheap_block_t)) {
        return;
    }

    g_head->size = g_heap_size - (uint64_t)sizeof(kheap_block_t);
    g_head->next = (kheap_block_t*)0;
    g_head->prev = (kheap_block_t*)0;
    g_head->free = 1u;
    g_ready = 1u;
}

void* kmalloc(uint64_t size) {
    if (!g_ready || size == 0ull) {
        return (void*)0;
    }

    uint64_t wanted = align_up(size, KHEAP_ALIGNMENT);

    for (kheap_block_t* it = g_head; it != (kheap_block_t*)0; it = it->next) {
        if (!it->free || it->size < wanted) {
            continue;
        }

        split_block(it, wanted);
        it->free = 0u;
        return (void*)((uint8_t*)it + sizeof(kheap_block_t));
    }

    return (void*)0;
}

void kfree(void* ptr) {
    if (!pointer_in_heap(ptr)) {
        return;
    }

    kheap_block_t* block = (kheap_block_t*)((uint8_t*)ptr - sizeof(kheap_block_t));
    block->free = 1u;

    merge_with_next(block);

    if (block->prev != (kheap_block_t*)0 && block->prev->free) {
        merge_with_next(block->prev);
    }
}

uint8_t kheap_is_ready(void) {
    return g_ready;
}

uint64_t kheap_total_bytes(void) {
    if (!g_ready) {
        return 0ull;
    }

    return g_heap_size;
}

uint64_t kheap_free_bytes(void) {
    if (!g_ready) {
        return 0ull;
    }

    uint64_t free_total = 0ull;
    for (kheap_block_t* it = g_head; it != (kheap_block_t*)0; it = it->next) {
        if (it->free) {
            free_total += it->size;
        }
    }

    return free_total;
}

uint64_t kheap_largest_free_block(void) {
    if (!g_ready) {
        return 0ull;
    }

    uint64_t largest = 0ull;
    for (kheap_block_t* it = g_head; it != (kheap_block_t*)0; it = it->next) {
        if (it->free && it->size > largest) {
            largest = it->size;
        }
    }

    return largest;
}
