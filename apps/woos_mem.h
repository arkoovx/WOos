#ifndef WOOS_MEM_H
#define WOOS_MEM_H

#include "woos.h"

#define WASM_HEAP_SIZE (8 * 1024 * 1024)

typedef struct wasm_block {
    uint32_t size;
    struct wasm_block* next;
    uint8_t used;
    uint8_t _pad[7];
} wasm_block_t;

static uint8_t g_wasm_heap[WASM_HEAP_SIZE];
static wasm_block_t* g_wasm_heap_head = 0;

static inline void wasm_heap_init_if_needed(void) {
    if (!g_wasm_heap_head) {
        g_wasm_heap_head = (wasm_block_t*)g_wasm_heap;
        g_wasm_heap_head->size = WASM_HEAP_SIZE;
        g_wasm_heap_head->next = 0;
        g_wasm_heap_head->used = 0;
    }
}

static inline void* woos_malloc(size_t size) {
    if (size == 0) return 0;
    wasm_heap_init_if_needed();

    // Align to 8 bytes
    uint32_t need = (uint32_t)((size + 7) & ~7);
    uint32_t min_split = (uint32_t)sizeof(wasm_block_t) + 8;

    for (wasm_block_t* cur = g_wasm_heap_head; cur != 0; cur = cur->next) {
        if (cur->used) continue;
        if (cur->size <= sizeof(wasm_block_t)) continue;

        uint32_t payload = cur->size - (uint32_t)sizeof(wasm_block_t);
        if (payload < need) continue;

        if (payload >= need + min_split) {
            wasm_block_t* next_block = (wasm_block_t*)((uint8_t*)cur + sizeof(wasm_block_t) + need);
            next_block->size = cur->size - sizeof(wasm_block_t) - need;
            next_block->used = 0;
            next_block->next = cur->next;

            cur->size = sizeof(wasm_block_t) + need;
            cur->next = next_block;
        }

        cur->used = 1;
        return (void*)((uint8_t*)cur + sizeof(wasm_block_t));
    }
    return 0;
}

static inline void woos_free(void* ptr) {
    if (!ptr) return;
    wasm_block_t* cur = (wasm_block_t*)((uint8_t*)ptr - sizeof(wasm_block_t));
    cur->used = 0;

    // Coalesce adjacent free blocks
    for (wasm_block_t* b = g_wasm_heap_head; b != 0; b = b->next) {
        while (b->next && !b->used && !b->next->used &&
               (uint8_t*)b + b->size == (uint8_t*)b->next) {
            b->size += b->next->size;
            b->next = b->next->next;
        }
    }
}

static inline void* woos_memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline void* woos_memset(void* dst, int val, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)val;
    return dst;
}

static inline void* woos_memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

static inline int woos_memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

static inline void* woos_realloc(void* ptr, size_t new_size) {
    if (!ptr) return woos_malloc(new_size);
    if (new_size == 0) {
        woos_free(ptr);
        return 0;
    }
    wasm_block_t* cur = (wasm_block_t*)((uint8_t*)ptr - sizeof(wasm_block_t));
    uint32_t old_payload = cur->size - (uint32_t)sizeof(wasm_block_t);
    if (old_payload >= new_size) return ptr;

    void* new_ptr = woos_malloc(new_size);
    if (new_ptr) {
        woos_memcpy(new_ptr, ptr, old_payload);
        woos_free(ptr);
    }
    return new_ptr;
}

#endif
