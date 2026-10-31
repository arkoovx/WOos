#include "fb.h"
#include "drivers/virtio_gpu_renderer/virtio_gpu_renderer.h"
#include "serial.h"
#include "input.h"
#include "sched.h"

#define FONT_W 8
#define FONT_H 8

#ifndef WOOS_ENABLE_DBL_BUFFER
#define WOOS_ENABLE_DBL_BUFFER 0
#endif

#define FB_BACKBUFFER_CAPACITY (8u * 1024u * 1024u)

#include "font_cp866.h"

static inline uint32_t utf8_decode_char(const char** pstr) {
    const uint8_t* s = (const uint8_t*)(*pstr);
    if (!*s) return 0;
    uint32_t cp = *s++;
    if (cp < 0x80) {
        *pstr = (const char*)s;
        return cp;
    }
    if ((cp & 0xE0) == 0xC0) {
        if (*s) {
            cp = ((cp & 0x1F) << 6) | (*s++ & 0x3F);
        }
    } else if ((cp & 0xF0) == 0xE0) {
        if (*s) {
            uint32_t b2 = *s++;
            if (*s) {
                uint32_t b3 = *s++;
                cp = ((cp & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            }
        }
    } else if ((cp & 0xF8) == 0xF0) {
        if (*s) {
            uint32_t b2 = *s++;
            if (*s) {
                uint32_t b3 = *s++;
                if (*s) {
                    uint32_t b4 = *s++;
                    cp = ((cp & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
                }
            }
        }
    }
    *pstr = (const char*)s;
    return cp;
}

static inline uint16_t clamp_u16(uint16_t value, uint16_t max) {
    return (value > max) ? max : value;
}

static inline uint16_t rect_end(uint16_t start, uint16_t length) {
    return (uint16_t)(start + length);
}

#if WOOS_ENABLE_DBL_BUFFER
static uint8_t* const g_backbuffer = (uint8_t*)(uint64_t)0x01000000ull;
static uint8_t g_backbuffer_enabled = 0;

static inline void mem_copy(uint8_t* dst, const uint8_t* src, uint32_t len) {
    // rep movsq — копирует 8 байт за такт, намного быстрее чем __builtin_memcpy
    // для крупных записей в VRAM (Write-Combining memory).
    uint64_t qwords = len >> 3;         // len / 8
    uint32_t tail   = len & 7u;         // остаток байт

    __asm__ __volatile__ (
        "rep movsq\n\t"
        : "+D"(dst), "+S"(src), "+c"(qwords)
        :
        : "memory"
    );

    // Хвост (0–7 байт) — простой байтовый цикл
    while (tail--) {
        *dst++ = *src++;
    }
}

static inline uint8_t* fb_target_base(video_info_t* info) {
    if (g_backbuffer_enabled) {
        return g_backbuffer;
    }

    return (uint8_t*)(uint64_t)info->framebuffer;
}
#else
static inline uint8_t* fb_target_base(video_info_t* info) {
    return (uint8_t*)(uint64_t)info->framebuffer;
}
#endif

static uint16_t g_clip_x = 0;
static uint16_t g_clip_y = 0;
static uint16_t g_clip_w = 0xFFFF;
static uint16_t g_clip_h = 0xFFFF;

void fb_set_clip(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    g_clip_x = x;
    g_clip_y = y;
    g_clip_w = w;
    g_clip_h = h;
}

void fb_clear_clip(void) {
    g_clip_x = 0;
    g_clip_y = 0;
    g_clip_w = 0xFFFF;
    g_clip_h = 0xFFFF;
}

void fb_init(video_info_t* info) {
#if WOOS_ENABLE_DBL_BUFFER
    uint32_t required = (uint32_t)info->pitch * (uint32_t)info->height;
    g_backbuffer_enabled = (required <= FB_BACKBUFFER_CAPACITY) ? 1u : 0u;

    if (g_backbuffer_enabled) {
        uint8_t* front = (uint8_t*)(uint64_t)info->framebuffer;
        for (uint32_t i = 0; i < required; i++) {
            g_backbuffer[i] = front[i];
        }
    }
#else
    (void)info;
#endif
}

void fb_enable_write_combining(video_info_t* info) {
    uint64_t old_pat = rdmsr(0x277);
    uint64_t pat = old_pat;
    pat &= ~(0xFFULL << 24);
    pat |= (0x01ULL << 24);
    wrmsr(0x277, pat);
    uint64_t new_pat = rdmsr(0x277);
    serial_printf("[PAT] MSR 0x277 changed from %p to %p (expected WC at PAT3)\n", (void*)old_pat, (void*)new_pat);

    uint64_t fb_start = info->framebuffer;
    uint64_t fb_size = (uint64_t)info->pitch * info->height;
    uint64_t fb_end = fb_start + fb_size;

    uint64_t* pml4 = (uint64_t*)read_cr3();
    uint32_t updated_pages = 0;

    for (uint64_t addr = fb_start; addr < fb_end; addr += 0x200000) {
        uint64_t pml4_idx = (addr >> 39) & 0x1FF;
        uint64_t pml4_val = pml4[pml4_idx];
        if (!(pml4_val & 1)) continue;

        uint64_t* pdpt = (uint64_t*)(pml4_val & ~0xFFFULL);
        uint64_t pdpt_idx = (addr >> 30) & 0x1FF;
        uint64_t pdpt_val = pdpt[pdpt_idx];
        if (!(pdpt_val & 1)) continue;

        uint64_t* pd = (uint64_t*)(pdpt_val & ~0xFFFULL);
        uint64_t pd_idx = (addr >> 21) & 0x1FF;
        uint64_t pd_val = pd[pd_idx];
        if (!(pd_val & 1)) continue;

        if (pd_val & (1ULL << 7)) {
            uint64_t old_val = pd_val;
            pd[pd_idx] = (pd_val & ~(1ULL << 12)) | (1ULL << 3) | (1ULL << 4);
            serial_printf("[PAT] PDE for %p (idx %u) changed from %p to %p\n", (void*)addr, (uint32_t)pd_idx, (void*)old_val, (void*)pd[pd_idx]);
            updated_pages++;
        }
    }

    serial_printf("[PAT] Total PDE entries updated to Write-Combining: %u\n", updated_pages);
    write_cr3(read_cr3());
}

static inline uint8_t bytes_per_pixel(const video_info_t* info) {
    uint8_t bytes = (uint8_t)(info->bpp / 8u);
    return (bytes == 0u) ? 4u : bytes;
}

static inline uint8_t use_gpu_draw_commands(void) {
    return 0;
}

uint32_t fb_readpixel(video_info_t* info, uint16_t x, uint16_t y) {
    if (use_gpu_draw_commands()) {
        return virtio_gpu_renderer_readpixel(info, x, y);
    }

    if (x >= info->width || y >= info->height) {
        return 0;
    }

    if (x < g_clip_x || y < g_clip_y || x >= g_clip_x + g_clip_w || y >= g_clip_y + g_clip_h) {
        return 0;
    }

    uint8_t* base = fb_target_base(info);
    uint8_t* px = base + ((uint64_t)y * info->pitch) + ((uint64_t)x * bytes_per_pixel(info));

    switch (bytes_per_pixel(info)) {
        case 2: {
            uint16_t packed = *(uint16_t*)px;
            uint8_t r = (uint8_t)(((packed >> 11) & 0x1Fu) << 3);
            uint8_t g = (uint8_t)(((packed >> 5) & 0x3Fu) << 2);
            uint8_t b = (uint8_t)((packed & 0x1Fu) << 3);
            return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        case 3:
            return ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | px[0];
        default:
            return *(uint32_t*)px & 0x00FFFFFFu;
    }
}

void fb_writepixel(video_info_t* info, uint16_t x, uint16_t y, uint32_t color) {
    if (use_gpu_draw_commands()) {
        virtio_gpu_renderer_writepixel(info, x, y, color);
        return;
    }

    if (x >= info->width || y >= info->height) {
        return;
    }

    if (x < g_clip_x || y < g_clip_y || x >= g_clip_x + g_clip_w || y >= g_clip_y + g_clip_h) {
        return;
    }

    uint8_t* base = fb_target_base(info);
    uint8_t* px = base + ((uint64_t)y * info->pitch) + ((uint64_t)x * bytes_per_pixel(info));

    switch (bytes_per_pixel(info)) {
        case 2: {
            uint8_t r = (uint8_t)((color >> 16) & 0xFFu);
            uint8_t g = (uint8_t)((color >> 8) & 0xFFu);
            uint8_t b = (uint8_t)(color & 0xFFu);
            uint16_t packed = (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
            *(uint16_t*)px = packed;
            break;
        }
        case 3:
            px[0] = (uint8_t)(color & 0xFFu);
            px[1] = (uint8_t)((color >> 8) & 0xFFu);
            px[2] = (uint8_t)((color >> 16) & 0xFFu);
            break;
        default:
            *(uint32_t*)px = color & 0x00FFFFFFu;
            break;
    }
}

void fb_fill(video_info_t* info, uint32_t color) {
    if (use_gpu_draw_commands()) {
        virtio_gpu_renderer_fill(info, color);
        return;
    }

    for (uint16_t y = 0; y < info->height; y++) {
        for (uint16_t x = 0; x < info->width; x++) {
            fb_writepixel(info, x, y, color);
        }
    }
}

static inline uint16_t max_u16(uint16_t a, uint16_t b) {
    return (a > b) ? a : b;
}

static inline uint16_t min_u16(uint16_t a, uint16_t b) {
    return (a < b) ? a : b;
}

void fb_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    if (use_gpu_draw_commands()) {
        virtio_gpu_renderer_rect(info, x, y, w, h, color);
        return;
    }

    if (w == 0 || h == 0) {
        return;
    }

    // 1. Вычисляем пересечение с областью клиппинга
    uint16_t cx = max_u16(x, g_clip_x);
    uint16_t cy = max_u16(y, g_clip_y);
    
    uint16_t x_end = (uint16_t)(x + w);
    uint16_t y_end = (uint16_t)(y + h);
    uint16_t clip_x_end = (uint16_t)(g_clip_x + g_clip_w);
    uint16_t clip_y_end = (uint16_t)(g_clip_y + g_clip_h);

    uint16_t cx2 = min_u16(x_end, clip_x_end);
    uint16_t cy2 = min_u16(y_end, clip_y_end);

    // 2. Ограничиваем границами экрана
    cx = min_u16(cx, info->width);
    cy = min_u16(cy, info->height);
    cx2 = min_u16(cx2, info->width);
    cy2 = min_u16(cy2, info->height);

    if (cx2 <= cx || cy2 <= cy) {
        return; // Полностью отсечено
    }

    uint16_t cw = (uint16_t)(cx2 - cx);
    uint8_t bpp = bytes_per_pixel(info);
    uint8_t* base = fb_target_base(info);

    if (bpp == 4) {
        for (uint16_t py = cy; py < cy2; py++) {
            uint32_t* dst = (uint32_t*)(base + ((uint64_t)py * info->pitch) + ((uint64_t)cx * 4));
            for (uint16_t px = 0; px < cw; px++) {
                dst[px] = color & 0x00FFFFFFu;
            }
        }
    } else if (bpp == 3) {
        uint8_t r = (uint8_t)((color >> 16) & 0xFFu);
        uint8_t g = (uint8_t)((color >> 8) & 0xFFu);
        uint8_t b = (uint8_t)(color & 0xFFu);
        for (uint16_t py = cy; py < cy2; py++) {
            uint8_t* dst = base + ((uint64_t)py * info->pitch) + ((uint64_t)cx * 3);
            for (uint16_t px = 0; px < cw; px++) {
                dst[0] = b;
                dst[1] = g;
                dst[2] = r;
                dst += 3;
            }
        }
    } else if (bpp == 2) {
        uint8_t r = (uint8_t)((color >> 16) & 0xFFu);
        uint8_t g = (uint8_t)((color >> 8) & 0xFFu);
        uint8_t b = (uint8_t)(color & 0xFFu);
        uint16_t packed = (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
        for (uint16_t py = cy; py < cy2; py++) {
            uint16_t* dst = (uint16_t*)(base + ((uint64_t)py * info->pitch) + ((uint64_t)cx * 2));
            for (uint16_t px = 0; px < cw; px++) {
                dst[px] = packed;
            }
        }
    }
}

void fb_frame(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t t, uint32_t color) {
    if (w == 0 || h == 0 || t == 0) {
        return;
    }

    if (t > w) t = w;
    if (t > h) t = h;

    fb_rect(info, x, y, w, t, color);
    fb_rect(info, x, (uint16_t)(y + h - t), w, t, color);
    fb_rect(info, x, y, t, h, color);
    fb_rect(info, (uint16_t)(x + w - t), y, t, h, color);
}

void fb_draw_glyph(video_info_t* info, uint16_t x, uint16_t y, const uint8_t* glyph, uint32_t color, uint32_t bg_color) {
    if (!glyph) return;

    if (use_gpu_draw_commands()) {
        virtio_gpu_renderer_draw_glyph(info, x, y, glyph, FONT_W, FONT_H, color, bg_color);
        return;
    }

    // Раннее отсечение всего символа, если он целиком за пределами клиппинга
    if (x + FONT_W <= g_clip_x || y + FONT_H <= g_clip_y || x >= g_clip_x + g_clip_w || y >= g_clip_y + g_clip_h) {
        return;
    }
    // Раннее отсечение границами экрана
    if (x >= info->width || y >= info->height) {
        return;
    }

    uint8_t bpp = bytes_per_pixel(info);
    uint8_t* base = fb_target_base(info);

    for (uint16_t gy = 0; gy < FONT_H; gy++) {
        uint16_t py = (uint16_t)(y + gy);
        if (py < g_clip_y || py >= g_clip_y + g_clip_h || py >= info->height) {
            continue;
        }

        uint8_t row = glyph[gy];
        uint8_t* row_base = base + ((uint64_t)py * info->pitch);

        for (uint16_t gx = 0; gx < FONT_W; gx++) {
            uint16_t px = (uint16_t)(x + gx);
            if (px < g_clip_x || px >= g_clip_x + g_clip_w || px >= info->width) {
                continue;
            }

            uint8_t bit = (uint8_t)(0x80u >> gx);
            uint32_t px_color = (row & bit) ? color : bg_color;

            uint8_t* target_pixel = row_base + ((uint64_t)px * bpp);
            if (bpp == 4) {
                *(uint32_t*)target_pixel = px_color & 0x00FFFFFFu;
            } else if (bpp == 3) {
                target_pixel[0] = (uint8_t)(px_color & 0xFFu);
                target_pixel[1] = (uint8_t)((px_color >> 8) & 0xFFu);
                target_pixel[2] = (uint8_t)((px_color >> 16) & 0xFFu);
            } else if (bpp == 2) {
                uint8_t r = (uint8_t)((px_color >> 16) & 0xFFu);
                uint8_t g = (uint8_t)((px_color >> 8) & 0xFFu);
                uint8_t b = (uint8_t)(px_color & 0xFFu);
                *(uint16_t*)target_pixel = (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
            }
        }
    }
}

void fb_draw_char(video_info_t* info, uint16_t x, uint16_t y, char c, uint32_t color, uint32_t bg_color) {
    const uint8_t* glyph = glyph_for_unicode((uint8_t)c);
    fb_draw_glyph(info, x, y, glyph, color, bg_color);
}

void fb_draw_text(video_info_t* info, uint16_t x, uint16_t y, const char* text, uint32_t color, uint32_t bg_color) {
    if (!text) return;
    uint16_t cursor_x = x;

    while (*text != '\0') {
        uint32_t cp = utf8_decode_char(&text);
        const uint8_t* glyph = glyph_for_unicode(cp);
        fb_draw_glyph(info, cursor_x, y, glyph, color, bg_color);
        cursor_x = (uint16_t)(cursor_x + FONT_W);
    }
}

void fb_present_rect_internal(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
#if WOOS_ENABLE_DBL_BUFFER
    if (virtio_gpu_renderer_is_active()) {
        if (!g_backbuffer_enabled || w == 0 || h == 0) {
            virtio_gpu_renderer_present_rect(info, x, y, w, h);
            return;
        }

        uint16_t x_end = clamp_u16(rect_end(x, w), info->width);
        uint16_t y_end = clamp_u16(rect_end(y, h), info->height);
        uint16_t copy_width = (uint16_t)(x_end - x);
        if (copy_width > 0 && y_end > y) {
            uint8_t* src_base = g_backbuffer;
            uint8_t* dst_base = (uint8_t*)(uint64_t)0x01800000ull; // VIRTIO_GPU_DRAW_SURFACE_BASE
            uint32_t src_pitch = info->pitch;
            uint32_t dst_pitch = info->width * 4;
            uint8_t src_bpp = bytes_per_pixel(info);

            if (src_bpp == 3) {
                for (uint16_t py = y; py < y_end; py++) {
                    uint8_t* src = src_base + (uint64_t)py * src_pitch + (uint64_t)x * 3;
                    uint32_t* dst = (uint32_t*)(dst_base + (uint64_t)py * dst_pitch + (uint64_t)x * 4);
                    for (uint16_t px = 0; px < copy_width; px++) {
                        uint8_t b = src[0];
                        uint8_t g = src[1];
                        uint8_t r = src[2];
                        dst[px] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                        src += 3;
                    }
                }
            } else if (src_bpp == 4) {
                for (uint16_t py = y; py < y_end; py++) {
                    uint8_t* src = src_base + (uint64_t)py * src_pitch + (uint64_t)x * 4;
                    uint8_t* dst = dst_base + (uint64_t)py * dst_pitch + (uint64_t)x * 4;
                    mem_copy(dst, src, copy_width * 4);
                }
            } else if (src_bpp == 2) {
                for (uint16_t py = y; py < y_end; py++) {
                    uint16_t* src = (uint16_t*)(src_base + (uint64_t)py * src_pitch + (uint64_t)x * 2);
                    uint32_t* dst = (uint32_t*)(dst_base + (uint64_t)py * dst_pitch + (uint64_t)x * 4);
                    for (uint16_t px = 0; px < copy_width; px++) {
                        uint16_t packed = src[px];
                        uint8_t r = (uint8_t)(((packed >> 11) & 0x1Fu) << 3);
                        uint8_t g = (uint8_t)(((packed >> 5) & 0x3Fu) << 2);
                        uint8_t b = (uint8_t)((packed & 0x1Fu) << 3);
                        dst[px] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                    }
                }
            }
        }
        virtio_gpu_renderer_present_rect(info, x, y, w, h);
        return;
    }

    if (use_gpu_draw_commands() || !g_backbuffer_enabled || w == 0 || h == 0) {
        virtio_gpu_renderer_present_rect(info, x, y, w, h);
        return;
    }

    uint16_t x_end = clamp_u16(rect_end(x, w), info->width);
    uint16_t y_end = clamp_u16(rect_end(y, h), info->height);
    uint16_t copy_width = (uint16_t)(x_end - x);
    uint32_t row_len = (uint32_t)copy_width * (uint32_t)bytes_per_pixel(info);
    uint8_t* front = (uint8_t*)(uint64_t)info->framebuffer;
    uint32_t pitch  = (uint32_t)info->pitch;
    uint32_t x_off  = (uint32_t)x * (uint32_t)bytes_per_pixel(info);

    for (uint16_t py = y; py < y_end; py++) {
        uint8_t* src = g_backbuffer + (uint64_t)py * pitch + x_off;
        uint8_t* dst = front        + (uint64_t)py * pitch + x_off;
        mem_copy(dst, src, row_len);
    }

    virtio_gpu_renderer_present_rect(info, x, y, w, h);
#else
    virtio_gpu_renderer_present_rect(info, x, y, w, h);
#endif
}

#include "kheap.h"

static woos_window_t g_windows[MAX_WINDOWS];
static uint32_t g_window_count = 0;
static uint32_t g_next_window_id = 1;

extern uint32_t sched_current_thread_id(void);
extern void* memset(void* s, int c, size_t n);
extern void* memcpy(void* dest, const void* src, size_t n);
extern int strcmp(const char* s1, const char* s2);

int32_t woos_graphics_create_window_ex(uint32_t w, uint32_t h, const char* title, uint32_t flags) {
    uint32_t* buf = (uint32_t*)kheap_alloc(w * h * 4);
    if (!buf) {
        serial_printf("[WinMgr] Error: failed to allocate window buffer (%dx%d)\n", w, h);
        return -1;
    }
    memset(buf, 0, w * h * 4);

    uint64_t irq_flags = irq_save();
    if (g_window_count >= MAX_WINDOWS) {
        irq_restore(irq_flags);
        kheap_free(buf);
        serial_printf("[WinMgr] Error: MAX_WINDOWS reached!\n");
        return -1;
    }
    
    uint32_t id = g_next_window_id++;
    uint32_t slot = g_window_count;

    // Clear focus from all existing windows
    for (uint32_t i = 0; i < g_window_count; i++) {
        g_windows[i].flags &= ~WOOS_WIN_FLAG_FOCUSED;
    }

    g_windows[slot].id = id;
    g_windows[slot].thread_id = sched_current_thread_id();
    g_windows[slot].x = 80 + (id * 35) % 400;
    g_windows[slot].y = 70 + (id * 25) % 250;
    g_windows[slot].w = w;
    g_windows[slot].h = h;
    g_windows[slot].flags = flags | WOOS_WIN_FLAG_FOCUSED;
    g_windows[slot].saved_x = g_windows[slot].x;
    g_windows[slot].saved_y = g_windows[slot].y;
    g_windows[slot].saved_w = w;
    g_windows[slot].saved_h = h;
    g_windows[slot].active = 1;
    g_windows[slot].buffer = buf;

    memset(g_windows[slot].title, 0, sizeof(g_windows[slot].title));
    if (title && title[0] != '\0') {
        uint32_t tlen = 0;
        while (tlen < (sizeof(g_windows[slot].title) - 1) && title[tlen] != '\0') {
            g_windows[slot].title[tlen] = title[tlen];
            tlen++;
        }
        g_windows[slot].title[tlen] = '\0';
    } else {
        memcpy(g_windows[slot].title, "Application", 12);
    }

    g_window_count++;
    irq_restore(irq_flags);

    serial_printf("[WinMgr] Created window %d ('%s', %dx%d, flags=0x%x) for thread %d\n", 
                  id, g_windows[slot].title, w, h, g_windows[slot].flags, g_windows[slot].thread_id);

    input_event_t ev = { .type = INPUT_EVENT_WINDOW_CHANGE, .x = 0, .y = 0, .buttons = 0 };
    input_push(&ev);
    extern void sched_unblock_all(void);
    sched_unblock_all();

    return (int32_t)id;
}

int32_t woos_graphics_create_window(uint32_t w, uint32_t h) {
    return woos_graphics_create_window_ex(w, h, "Window", 0);
}

int32_t woos_graphics_destroy_window(uint32_t window_id) {
    uint32_t* buf_to_free = 0;
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            serial_printf("[WinMgr] Destroying window %d ('%s')\n", window_id, g_windows[i].title);
            buf_to_free = g_windows[i].buffer;
            for (uint32_t j = i; j + 1 < g_window_count; j++) {
                g_windows[j] = g_windows[j + 1];
            }
            memset(&g_windows[g_window_count - 1], 0, sizeof(woos_window_t));
            g_window_count--;

            if (g_window_count > 0) {
                g_windows[g_window_count - 1].flags |= WOOS_WIN_FLAG_FOCUSED;
            }
            irq_restore(flags);

            if (buf_to_free) {
                kheap_free(buf_to_free);
            }

            input_event_t ev = { .type = INPUT_EVENT_WINDOW_CHANGE, .x = 0, .y = 0, .buttons = 0 };
            input_push(&ev);
            extern void sched_unblock_all(void);
            sched_unblock_all();
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

int32_t woos_graphics_destroy_thread_windows(uint32_t thread_id) {
    int32_t destroyed = 0;
    while (1) {
        uint32_t target_id = 0;
        uint64_t flags = irq_save();
        for (int32_t i = (int32_t)g_window_count - 1; i >= 0; i--) {
            if (g_windows[i].thread_id == thread_id) {
                target_id = g_windows[i].id;
                break;
            }
        }
        irq_restore(flags);
        if (target_id == 0) break;
        woos_graphics_destroy_window(target_id);
        destroyed++;
    }
    return destroyed;
}

int32_t woos_graphics_bring_to_front(uint32_t window_id) {
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].flags &= ~WOOS_WIN_FLAG_MINIMIZED;
            for (uint32_t j = 0; j < g_window_count; j++) {
                g_windows[j].flags &= ~WOOS_WIN_FLAG_FOCUSED;
            }
            if (i < g_window_count - 1) {
                woos_window_t tmp = g_windows[i];
                for (uint32_t j = i; j + 1 < g_window_count; j++) {
                    g_windows[j] = g_windows[j + 1];
                }
                g_windows[g_window_count - 1] = tmp;
            }
            g_windows[g_window_count - 1].flags |= WOOS_WIN_FLAG_FOCUSED;
            irq_restore(flags);

            input_event_t ev = { .type = INPUT_EVENT_WINDOW_CHANGE, .x = 0, .y = 0, .buttons = 0 };
            input_push(&ev);
            extern void sched_unblock_all(void);
            sched_unblock_all();
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

int32_t woos_graphics_bring_thread_to_front(uint32_t thread_id) {
    uint32_t target_id = 0;
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].thread_id == thread_id) {
            target_id = g_windows[i].id;
            break;
        }
    }
    irq_restore(flags);
    if (target_id != 0) {
        return woos_graphics_bring_to_front(target_id);
    }
    return -1;
}

int32_t woos_graphics_set_window_title(uint32_t window_id, const char* title) {
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            memset(g_windows[i].title, 0, sizeof(g_windows[i].title));
            if (title) {
                uint32_t tlen = 0;
                while (tlen < (sizeof(g_windows[i].title) - 1) && title[tlen] != '\0') {
                    g_windows[i].title[tlen] = title[tlen];
                    tlen++;
                }
                g_windows[i].title[tlen] = '\0';
            }
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

int32_t woos_graphics_set_window_flags(uint32_t window_id, uint32_t flags) {
    uint64_t irq_flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].flags = flags;
            irq_restore(irq_flags);
            return 0;
        }
    }
    irq_restore(irq_flags);
    return -1;
}

int32_t woos_graphics_toggle_minimize(uint32_t window_id) {
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            if (g_windows[i].flags & WOOS_WIN_FLAG_MINIMIZED) {
                irq_restore(flags);
                return woos_graphics_bring_to_front(window_id);
            } else {
                g_windows[i].flags |= WOOS_WIN_FLAG_MINIMIZED;
                g_windows[i].flags &= ~WOOS_WIN_FLAG_FOCUSED;
                for (int32_t j = (int32_t)g_window_count - 1; j >= 0; j--) {
                    if (!(g_windows[j].flags & WOOS_WIN_FLAG_MINIMIZED)) {
                        g_windows[j].flags |= WOOS_WIN_FLAG_FOCUSED;
                        break;
                    }
                }
                irq_restore(flags);
                input_event_t ev = { .type = INPUT_EVENT_WINDOW_CHANGE, .x = 0, .y = 0, .buttons = 0 };
                input_push(&ev);
                extern void sched_unblock_all(void);
                sched_unblock_all();
                return 0;
            }
        }
    }
    irq_restore(flags);
    return -1;
}

int32_t woos_graphics_toggle_maximize(uint32_t window_id) {
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            if (strcmp(g_windows[i].title, "Calculator") == 0) {
                irq_restore(flags);
                return 0;
            }
            uint32_t old_w = g_windows[i].w;
            uint32_t old_h = g_windows[i].h;
            uint32_t target_w = 0;
            uint32_t target_h = 0;
            int is_max = 0;
            if (g_windows[i].flags & WOOS_WIN_FLAG_MAXIMIZED) {
                g_windows[i].x = g_windows[i].saved_x;
                g_windows[i].y = g_windows[i].saved_y;
                g_windows[i].w = g_windows[i].saved_w;
                g_windows[i].h = g_windows[i].saved_h;
                g_windows[i].flags &= ~WOOS_WIN_FLAG_MAXIMIZED;
                target_w = g_windows[i].saved_w;
                target_h = g_windows[i].saved_h;
                is_max = 0;
            } else {
                g_windows[i].saved_x = g_windows[i].x;
                g_windows[i].saved_y = g_windows[i].y;
                g_windows[i].saved_w = g_windows[i].w;
                g_windows[i].saved_h = g_windows[i].h;
                video_info_t* info = get_video_info();
                g_windows[i].x = 0;
                g_windows[i].y = 22;
                if (info && info->width > 0 && info->height > 58) {
                    g_windows[i].w = info->width;
                    g_windows[i].h = info->height - 58;
                }
                g_windows[i].flags |= WOOS_WIN_FLAG_MAXIMIZED;
                target_w = g_windows[i].w;
                target_h = g_windows[i].h;
                is_max = 1;
            }

            if (target_w > 0 && target_h > 0) {
                uint32_t old_size = old_w * old_h * 4;
                uint32_t new_size = target_w * target_h * 4;
                uint32_t* new_buf = (uint32_t*)kheap_realloc(g_windows[i].buffer, (uint64_t)new_size);
                if (new_buf) {
                    if (new_size > old_size) {
                        memset((uint8_t*)new_buf + old_size, 0, new_size - old_size);
                    }
                    g_windows[i].buffer = new_buf;
                }
            }

            uint32_t tid = g_windows[i].thread_id;
            irq_restore(flags);
            woos_graphics_bring_to_front(window_id);

            if (tid > 0) {
                woos_ipc_send(tid, MSG_RESIZE, target_w, target_h, (uint32_t)is_max, 0, 0);
            }

            input_event_t ev = { .type = INPUT_EVENT_WINDOW_CHANGE, .x = 0, .y = 0, .buttons = 0 };
            input_push(&ev);
            extern void sched_unblock_all(void);
            sched_unblock_all();

            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

int32_t woos_graphics_set_window_pos(uint32_t window_id, int32_t x, int32_t y) {
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].x = x;
            g_windows[i].y = y;
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

uint32_t woos_graphics_get_window_count(void) {
    uint64_t flags = irq_save();
    uint32_t count = g_window_count;
    irq_restore(flags);
    return count;
}

int32_t woos_graphics_get_window_info(uint32_t index, woos_window_t* info) {
    uint64_t flags = irq_save();
    if (index < g_window_count && info) {
        memcpy(info, &g_windows[index], sizeof(woos_window_t));
        irq_restore(flags);
        return 0;
    }
    irq_restore(flags);
    return -1;
}

int32_t woos_graphics_get_focused_tid(void) {
    uint64_t flags = irq_save();
    for (int32_t i = (int32_t)g_window_count - 1; i >= 0; i--) {
        if ((g_windows[i].flags & WOOS_WIN_FLAG_FOCUSED) && !(g_windows[i].flags & WOOS_WIN_FLAG_MINIMIZED)) {
            int32_t tid = (int32_t)g_windows[i].thread_id;
            irq_restore(flags);
            return tid;
        }
    }
    irq_restore(flags);
    return -1;
}

void woos_graphics_draw_text_to_buffer(uint32_t* dst_buf, uint32_t dst_w, uint32_t dst_h, int32_t x, int32_t y, const char* text, uint32_t color) {
    if (!dst_buf || !text) return;
    int32_t cur_x = x;
    while (*text) {
        uint32_t cp = utf8_decode_char(&text);
        const uint8_t* glyph = glyph_for_unicode(cp);
        for (int32_t gy = 0; gy < FONT_H; gy++) {
            int32_t py = y + gy;
            if (py < 0 || py >= (int32_t)dst_h) continue;
            uint8_t row = glyph[gy];
            for (int32_t gx = 0; gx < FONT_W; gx++) {
                int32_t px = cur_x + gx;
                if (px < 0 || px >= (int32_t)dst_w) continue;
                if (row & (0x80 >> gx)) {
                    dst_buf[py * dst_w + px] = color;
                }
            }
        }
        cur_x += FONT_W;
    }
}

void fb_draw_cursor_crosshair(uint8_t* dst, uint32_t width, uint32_t height, uint32_t pitch, int32_t cx, int32_t cy, uint32_t color) {
    if (!dst) return;

    // Horizontal bar (w=11, h=2): cx - 5 to cx + 5, cy - 1 to cy
    int32_t hx_start = cx - 5;
    int32_t hx_end = cx + 6;
    int32_t hy_start = cy - 1;
    int32_t hy_end = cy + 1;

    for (int32_t y = hy_start; y <= hy_end; y++) {
        if (y >= 0 && y < (int32_t)height) {
            uint8_t* row = dst + (uint64_t)y * pitch;
            for (int32_t x = hx_start; x < hx_end; x++) {
                if (x >= 0 && x < (int32_t)width) {
                    *(uint32_t*)(row + (uint64_t)x * 4) = color;
                }
            }
        }
    }

    // Vertical bar (w=2, h=11): cx - 1 to cx, cy - 5 to cy + 5
    int32_t vx_start = cx - 1;
    int32_t vx_end = cx + 1;
    int32_t vy_start = cy - 5;
    int32_t vy_end = cy + 6;

    for (int32_t y = vy_start; y < vy_end; y++) {
        if (y >= 0 && y < (int32_t)height) {
            uint8_t* row = dst + (uint64_t)y * pitch;
            for (int32_t x = vx_start; x <= vx_end; x++) {
                if (x >= 0 && x < (int32_t)width) {
                    *(uint32_t*)(row + (uint64_t)x * 4) = color;
                }
            }
        }
    }
}

int32_t woos_graphics_blit_window_internal(uint32_t window_id, const uint32_t* wasm_buf, uint32_t w, uint32_t h) {
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            // Reallocate buffer if application is blitting at a different size (and not maximized)
            if (!(g_windows[i].flags & WOOS_WIN_FLAG_MAXIMIZED)) {
                if (w != g_windows[i].w || h != g_windows[i].h) {
                    uint32_t* new_buf = (uint32_t*)kheap_realloc(g_windows[i].buffer, (uint64_t)w * h * 4);
                    if (new_buf) {
                        g_windows[i].buffer = new_buf;
                        g_windows[i].w = w;
                        g_windows[i].h = h;
                    }
                }
            }

            uint32_t buf_w = g_windows[i].w;
            uint32_t buf_h = g_windows[i].h;

            if (w == buf_w && h == buf_h) {
                // Direct fast 1:1 copy
                for (uint32_t r = 0; r < buf_h; r++) {
                    const uint32_t* src_row = wasm_buf + r * w;
                    uint32_t* dst_row = g_windows[i].buffer + r * buf_w;
                    memcpy(dst_row, src_row, buf_w * 4);
                }
            } else if (g_windows[i].flags & WOOS_WIN_FLAG_MAXIMIZED) {
                // Application blitted at fixed dimensions; scale to fill full maximized window!
                for (uint32_t dy = 0; dy < buf_h; dy++) {
                    uint32_t sy = (dy * h) / buf_h;
                    const uint32_t* src_row = wasm_buf + sy * w;
                    uint32_t* dst_row = g_windows[i].buffer + dy * buf_w;
                    for (uint32_t dx = 0; dx < buf_w; dx++) {
                        uint32_t sx = (dx * w) / buf_w;
                        dst_row[dx] = src_row[sx];
                    }
                }
            } else {
                uint32_t copy_w_sub = (w < buf_w) ? w : buf_w;
                uint32_t copy_h_sub = (h < buf_h) ? h : buf_h;
                for (uint32_t r = 0; r < copy_h_sub; r++) {
                    const uint32_t* src_row = wasm_buf + r * w;
                    uint32_t* dst_row = g_windows[i].buffer + r * buf_w;
                    memcpy(dst_row, src_row, copy_w_sub * 4);
                }
            }

            uint32_t copy_w = buf_w;
            uint32_t copy_h = buf_h;
            
            // Present window region if not minimized
            if (!(g_windows[i].flags & WOOS_WIN_FLAG_MINIMIZED)) {
                video_info_t* info = get_video_info();
                if (info) {
                    extern uint8_t* wasi_get_compositor_buf(void);
                    uint8_t* comp_buf = wasi_get_compositor_buf();
                    int32_t win_x = g_windows[i].x;
                    int32_t win_y = g_windows[i].y;
                    uint32_t pitch = info->pitch;

                    // 1. Keep compositor's WASM backbuffer in sync with new window pixels
                    if (comp_buf) {
                        for (uint32_t r = 0; r < copy_h; r++) {
                            int32_t py = win_y + (int32_t)r;
                            if (py >= 0 && py < (int32_t)info->height) {
                                int32_t px = win_x;
                                int32_t pw = copy_w;
                                if (px < 0) {
                                    pw += px;
                                    px = 0;
                                }
                                if (px + pw > (int32_t)info->width) {
                                    pw = (int32_t)info->width - px;
                                }
                                if (pw > 0) {
                                    uint8_t* dst = comp_buf + (uint64_t)py * pitch + (uint64_t)px * 4;
                                    const uint8_t* src = (const uint8_t*)(g_windows[i].buffer + r * buf_w + (px - win_x));
                                    memcpy(dst, src, pw * 4);
                                }
                            }
                        }
                    }

                    // 2. Draw window to screen backbuffer
                    woos_graphics_draw_window_to_screen(window_id);

                    // 3. Draw mouse cursor if inside window area
                    extern uint16_t mouse_get_x(void);
                    extern uint16_t mouse_get_y(void);
                    int32_t mx = (int32_t)mouse_get_x();
                    int32_t my = (int32_t)mouse_get_y();
                    if (mx >= win_x - 6 && mx < win_x + (int32_t)g_windows[i].w + 6 &&
                        my >= win_y - 6 && my < win_y + (int32_t)g_windows[i].h + 6) {
                        fb_draw_cursor_crosshair(g_backbuffer, info->width, info->height, info->pitch, mx, my, 0xE74C3Cu);
                    }

                    // 4. Present window rectangle
                    fb_present_rect(info, g_windows[i].x, g_windows[i].y, g_windows[i].w, g_windows[i].h);
                }
            }

            irq_restore(flags);

            extern void sched_unblock_all(void);
            sched_unblock_all();
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

int32_t woos_graphics_draw_window_to_screen_internal(uint32_t window_id) {
    video_info_t* info = get_video_info();
    if (!info) return -1;
    
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            if (g_windows[i].flags & WOOS_WIN_FLAG_MINIMIZED) {
                irq_restore(flags);
                return 0;
            }
            int32_t win_x = g_windows[i].x;
            int32_t win_y = g_windows[i].y;
            uint32_t win_w = g_windows[i].w;
            uint32_t win_h = g_windows[i].h;
            
            int32_t start_y = (win_y < 0) ? -win_y : 0;
            int32_t end_y = (win_y + (int32_t)win_h > (int32_t)info->height) ? (int32_t)info->height - win_y : (int32_t)win_h;
            int32_t start_x = (win_x < 0) ? -win_x : 0;
            int32_t end_x = (win_x + (int32_t)win_w > (int32_t)info->width) ? (int32_t)info->width - win_x : (int32_t)win_w;
            
            if (start_x >= end_x || start_y >= end_y || !g_windows[i].buffer) {
                irq_restore(flags);
                return 0;
            }
            
            uint32_t copy_width = (uint32_t)(end_x - start_x);
            uint8_t* screen_base = g_backbuffer;
            uint32_t screen_pitch = info->pitch;
            uint8_t screen_bpp = bytes_per_pixel(info);
            
            for (int32_t cy = start_y; cy < end_y; cy++) {
                uint32_t* src_row = g_windows[i].buffer + cy * win_w + start_x;
                uint8_t* dst_row = screen_base + (uint64_t)(win_y + cy) * screen_pitch + (uint64_t)(win_x + start_x) * screen_bpp;
                
                if (screen_bpp == 4) {
                    memcpy(dst_row, src_row, copy_width * 4);
                } else if (screen_bpp == 3) {
                    uint8_t* dst = dst_row;
                    for (uint32_t cx = 0; cx < copy_width; cx++) {
                        uint32_t color = src_row[cx];
                        dst[0] = (uint8_t)(color & 0xFF);
                        dst[1] = (uint8_t)((color >> 8) & 0xFF);
                        dst[2] = (uint8_t)((color >> 16) & 0xFF);
                        dst += 3;
                    }
                } else if (screen_bpp == 2) {
                    uint16_t* dst = (uint16_t*)dst_row;
                    for (uint32_t cx = 0; cx < copy_width; cx++) {
                        uint32_t color = src_row[cx];
                        uint8_t r = (uint8_t)((color >> 16) & 0xFF);
                        uint8_t g = (uint8_t)((color >> 8) & 0xFF);
                        uint8_t b = (uint8_t)(color & 0xFF);
                        dst[cx] = (uint16_t)(((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (uint16_t)(b >> 3));
                    }
                }
            }
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

woos_perf_t g_perf_stats = {0};

void fb_present_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint64_t start = rdtsc();
    fb_present_rect_internal(info, x, y, w, h);
    g_perf_stats.present_count++;
    g_perf_stats.present_cycles += (rdtsc() - start);
}

int32_t woos_graphics_blit_window(uint32_t window_id, const uint32_t* wasm_buf, uint32_t w, uint32_t h) {
    uint64_t start = rdtsc();
    int32_t res = woos_graphics_blit_window_internal(window_id, wasm_buf, w, h);
    g_perf_stats.blit_count++;
    g_perf_stats.blit_cycles += (rdtsc() - start);
    return res;
}

int32_t woos_graphics_draw_window_to_screen(uint32_t window_id) {
    uint64_t start = rdtsc();
    int32_t res = woos_graphics_draw_window_to_screen_internal(window_id);
    g_perf_stats.draw_win_count++;
    g_perf_stats.draw_win_cycles += (rdtsc() - start);
    return res;
}

int32_t woos_graphics_draw_window_to_buffer(uint32_t window_id, uint32_t* dst_buf, uint32_t dst_w, uint32_t dst_h) {
    uint64_t flags = irq_save();
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            if (g_windows[i].flags & WOOS_WIN_FLAG_MINIMIZED) {
                irq_restore(flags);
                return 0;
            }
            int32_t win_x = g_windows[i].x;
            int32_t win_y = g_windows[i].y;
            uint32_t win_w = g_windows[i].w;
            uint32_t win_h = g_windows[i].h;
            
            int32_t start_y = (win_y < 0) ? -win_y : 0;
            int32_t end_y = (win_y + (int32_t)win_h > (int32_t)dst_h) ? (int32_t)dst_h - win_y : (int32_t)win_h;
            int32_t start_x = (win_x < 0) ? -win_x : 0;
            int32_t end_x = (win_x + (int32_t)win_w > (int32_t)dst_w) ? (int32_t)dst_w - win_x : (int32_t)win_w;
            
            if (start_x < end_x && start_y < end_y && g_windows[i].buffer) {
                uint32_t copy_width = (uint32_t)(end_x - start_x);
                for (int32_t cy = start_y; cy < end_y; cy++) {
                    uint32_t* src_row = g_windows[i].buffer + cy * win_w + start_x;
                    uint32_t* dst_row = dst_buf + (win_y + cy) * dst_w + (win_x + start_x);
                    memcpy(dst_row, src_row, copy_width * 4);
                }
            }
            irq_restore(flags);
            return 0;
        }
    }
    irq_restore(flags);
    return -1;
}

