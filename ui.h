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

#endif
