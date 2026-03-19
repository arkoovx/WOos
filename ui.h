#ifndef WOOS_UI_H
#define WOOS_UI_H

#include "kernel.h"

typedef struct ui_dirty_rect {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} ui_dirty_rect_t;

void ui_render_desktop(video_info_t* info);
void ui_mark_dirty(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void ui_render_dirty(video_info_t* info);
uint16_t ui_last_dirty_count(void);
void ui_set_cursor(video_info_t* info, uint16_t x, uint16_t y, uint8_t buttons);
void ui_handle_mouse_move(video_info_t* info, uint16_t x, uint16_t y, uint8_t buttons);
void ui_handle_mouse_button(video_info_t* info, uint8_t buttons);
void ui_set_kernel_health(video_info_t* info, uint8_t idt_ready, uint32_t heartbeat);
void ui_set_irq_stats(video_info_t* info, uint32_t keyboard_irq, uint32_t mouse_irq);
void ui_set_memory_stats(video_info_t* info, uint8_t pmm_ready, uint64_t total_pages, uint64_t free_pages);
void ui_set_storage_stats(video_info_t* info, uint8_t storage_ready, uint8_t last_read_ok, uint32_t last_lba, uint8_t boot_signature_valid);
void ui_set_runtime_stats(
    video_info_t* info,
    uint16_t dirty_count,
    uint64_t heap_used,
    uint64_t heap_free,
    uint8_t virtio_detected,
    uint8_t virtio_active
);

#endif
