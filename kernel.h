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

static inline uint64_t irq_save(void) {
    uint64_t rflags;
    __asm__ __volatile__("pushf; pop %0; cli" : "=r"(rflags) : : "memory");
    return rflags;
}

static inline void irq_restore(uint64_t rflags) {
    __asm__ __volatile__("push %0; popf" : : "r"(rflags) : "memory", "cc");
}

video_info_t* get_video_info(void);

// IPC Message types
#define MSG_CLICK        1
#define MSG_RIGHT_CLICK  2
#define MSG_CLOSE        3
#define MSG_KEY_DOWN     4
#define MSG_RESIZE       5
#define MSG_OPEN_FILE    10

#define MAX_WINDOWS 8

// Window Flags
#define WOOS_WIN_FLAG_MINIMIZED    (1 << 0) // Window minimized to taskbar
#define WOOS_WIN_FLAG_MAXIMIZED    (1 << 1) // Window maximized
#define WOOS_WIN_FLAG_CUSTOM_DECOR (1 << 2) // App draws custom window frame / no system frame
#define WOOS_WIN_FLAG_FOCUSED      (1 << 3) // Window is active / focused
#define WOOS_WIN_FLAG_RESIZABLE    (1 << 4) // Window supports maximize / resize

#define WOOS_WIN_TITLE_LEN 32

typedef struct {
    uint32_t id;                        // offset 0
    uint32_t thread_id;                 // offset 4
    int32_t x;                          // offset 8
    int32_t y;                          // offset 12
    uint32_t w;                         // offset 16
    uint32_t h;                         // offset 20
    uint32_t flags;                     // offset 24
    int32_t saved_x;                    // offset 28
    int32_t saved_y;                    // offset 32
    uint32_t saved_w;                   // offset 36
    uint32_t saved_h;                   // offset 40
    char title[WOOS_WIN_TITLE_LEN];     // offset 44 (44..75)
    uint32_t active;                    // offset 76
    uint32_t* buffer;                   // offset 80
} __attribute__((packed)) woos_window_t;

int32_t woos_graphics_create_window(uint32_t w, uint32_t h);
int32_t woos_graphics_create_window_ex(uint32_t w, uint32_t h, const char* title, uint32_t flags);
int32_t woos_graphics_destroy_window(uint32_t window_id);
int32_t woos_graphics_destroy_thread_windows(uint32_t thread_id);
int32_t woos_graphics_bring_to_front(uint32_t window_id);
int32_t woos_graphics_bring_thread_to_front(uint32_t thread_id);
int32_t woos_graphics_set_window_title(uint32_t window_id, const char* title);
int32_t woos_graphics_set_window_flags(uint32_t window_id, uint32_t flags);
int32_t woos_graphics_toggle_minimize(uint32_t window_id);
int32_t woos_graphics_toggle_maximize(uint32_t window_id);

int32_t woos_graphics_blit_window(uint32_t window_id, const uint32_t* wasm_buf, uint32_t w, uint32_t h);
int32_t woos_graphics_set_window_pos(uint32_t window_id, int32_t x, int32_t y);
uint32_t woos_graphics_get_window_count(void);
int32_t woos_graphics_get_window_info(uint32_t index, woos_window_t* info);
int32_t woos_graphics_get_focused_tid(void);
int32_t woos_graphics_draw_window_to_screen(uint32_t window_id);
int32_t woos_graphics_draw_window_to_buffer(uint32_t window_id, uint32_t* dst_buf, uint32_t dst_w, uint32_t dst_h);
void woos_graphics_draw_text_to_buffer(uint32_t* dst_buf, uint32_t dst_w, uint32_t dst_h, int32_t x, int32_t y, const char* text, uint32_t color);


typedef struct {
    uint32_t present_count;
    uint64_t present_cycles;
    uint32_t blit_count;
    uint64_t blit_cycles;
    uint32_t draw_win_count;
    uint64_t draw_win_cycles;
    uint32_t mouse_recv;
    uint32_t mouse_drop_align;
    uint32_t mouse_drop_overflow;
    uint32_t mouse_push;
} woos_perf_t;

extern woos_perf_t g_perf_stats;

#endif
