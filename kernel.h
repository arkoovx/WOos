#ifndef WOOS_KERNEL_H
#define WOOS_KERNEL_H

#include <stdint.h>

#define BOOT_INFO_MAGIC_EXPECTED 0x31424957u
#define BOOT_INFO_VERSION_V1     0x0001u
#define BOOT_INFO_VERSION_V2     0x0002u
#define BOOT_INFO_E820_MAX_ENTRIES 32u
#define BOOT_INFO_E820_TYPE_USABLE 1u

typedef struct boot_memory_region {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
} __attribute__((packed)) boot_memory_region_t;

typedef struct video_info {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint64_t framebuffer;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  reserved;
    uint16_t memory_region_count;
    uint16_t memory_region_capacity;
    boot_memory_region_t memory_regions[BOOT_INFO_E820_MAX_ENTRIES];
} __attribute__((packed)) video_info_t;

extern uint64_t g_tsc_per_ms;

static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ __volatile__("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ __volatile__("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ __volatile__("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

static inline uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(val));
    return val;
}

static inline void write_cr3(uint64_t val) {
    __asm__ __volatile__("mov %0, %%cr3" : : "r"(val));
}

video_info_t* get_video_info(void);

#endif
