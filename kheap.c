#include "kheap.h"

#define KHEAP_ARENA_SIZE (64u * 1024u)
#define KHEAP_ALIGN      16u

typedef struct kheap_block {
    uint64_t size;
    struct kheap_block* next;
    uint8_t used;
    // Делаем заголовок кратным 16 байтам, чтобы payload каждого блока
    // начинался с адреса, совместимого с KHEAP_ALIGN.
    uint8_t _pad[15];
} kheap_block_t;

static uint8_t g_kheap_arena[KHEAP_ARENA_SIZE] __attribute__((aligned(KHEAP_ALIGN)));
static kheap_block_t* g_kheap_head = 0;
static uint64_t g_kheap_used = 0;

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + (align - 1u)) & ~(align - 1u);
}

static uint8_t* block_payload(kheap_block_t* block) {
    return (uint8_t*)((uint8_t*)block + sizeof(kheap_block_t));
}

static uint64_t block_payload_size(const kheap_block_t* block) {
    if (block->size <= sizeof(kheap_block_t)) {
        return 0;
    }

    return block->size - (uint64_t)sizeof(kheap_block_t);
}

void kheap_init(void) {
    g_kheap_head = (kheap_block_t*)g_kheap_arena;
    g_kheap_head->size = KHEAP_ARENA_SIZE;
    g_kheap_head->next = 0;
    g_kheap_head->used = 0;
    g_kheap_used = 0;
}

void* kheap_alloc(uint64_t size) {
    if (size == 0 || g_kheap_head == 0) {
        return 0;
    }

    uint64_t need = align_up(size, KHEAP_ALIGN);
    uint64_t min_split_size = (uint64_t)sizeof(kheap_block_t) + KHEAP_ALIGN;

    for (kheap_block_t* block = g_kheap_head; block != 0; block = block->next) {
        if (block->used) {
            continue;
        }

        uint64_t payload = block_payload_size(block);
        if (payload < need) {
            continue;
        }

        uint64_t remain_payload = payload - need;
        if (remain_payload >= min_split_size) {
            uint8_t* split_addr = block_payload(block) + need;
            kheap_block_t* split = (kheap_block_t*)split_addr;

            // remain_payload уже включает место под заголовок split-блока,
            // поэтому добавлять sizeof(kheap_block_t) повторно нельзя.
            split->size = remain_payload;
            split->next = block->next;
            split->used = 0;

            block->size = (uint64_t)sizeof(kheap_block_t) + need;
            block->next = split;
        }

        block->used = 1;
        g_kheap_used += block_payload_size(block);
        return block_payload(block);
    }

    return 0;
}

static void coalesce_free_blocks(void) {
    for (kheap_block_t* block = g_kheap_head; block != 0 && block->next != 0;) {
        kheap_block_t* next = block->next;
        uint8_t* expected_next = ((uint8_t*)block) + block->size;

        if (!block->used && !next->used && (uint8_t*)next == expected_next) {
            block->size += next->size;
            block->next = next->next;
            continue;
        }

        block = block->next;
    }
}

void kheap_free(void* ptr) {
    if (ptr == 0 || g_kheap_head == 0) {
        return;
    }

    for (kheap_block_t* block = g_kheap_head; block != 0; block = block->next) {
        if (block_payload(block) != (uint8_t*)ptr) {
            continue;
        }

        if (!block->used) {
            return;
        }

        block->used = 0;
        g_kheap_used -= block_payload_size(block);
        coalesce_free_blocks();
        return;
    }
}

uint64_t kheap_total_bytes(void) {
    return KHEAP_ARENA_SIZE - (uint64_t)sizeof(kheap_block_t);
}

uint64_t kheap_used_bytes(void) {
    return g_kheap_used;
}

uint64_t kheap_free_bytes(void) {
    return kheap_total_bytes() - g_kheap_used;
}
