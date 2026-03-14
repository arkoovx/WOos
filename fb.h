#ifndef WOOS_FB_H
#define WOOS_FB_H

#include "kernel.h"

void fb_init(video_info_t* info);
uint32_t fb_readpixel(video_info_t* info, uint16_t x, uint16_t y);
void fb_writepixel(video_info_t* info, uint16_t x, uint16_t y, uint32_t color);
void fb_fill(video_info_t* info, uint32_t color);
void fb_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color);
void fb_frame(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t t, uint32_t color);
void fb_draw_char(video_info_t* info, uint16_t x, uint16_t y, char c, uint32_t color, uint32_t bg_color);
void fb_draw_text(video_info_t* info, uint16_t x, uint16_t y, const char* text, uint32_t color, uint32_t bg_color);
void fb_present_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h);

#endif
