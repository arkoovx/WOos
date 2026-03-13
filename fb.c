#include "fb.h"

static inline uint16_t clamp_u16(uint16_t value, uint16_t max) {
    return (value > max) ? max : value;
}

void fb_fill(video_info_t* info, uint32_t color) {
    uint8_t* base = (uint8_t*)(uint64_t)info->framebuffer;

    for (uint16_t y = 0; y < info->height; y++) {
        uint32_t* row = (uint32_t*)(base + ((uint64_t)y * info->pitch));
        for (uint16_t x = 0; x < info->width; x++) {
            row[x] = color;
        }
    }
}

void fb_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    uint16_t x_end = clamp_u16((uint16_t)(x + w), info->width);
    uint16_t y_end = clamp_u16((uint16_t)(y + h), info->height);
    uint8_t* base = (uint8_t*)(uint64_t)info->framebuffer;

    for (uint16_t py = y; py < y_end; py++) {
        uint32_t* row = (uint32_t*)(base + ((uint64_t)py * info->pitch));
        for (uint16_t px = x; px < x_end; px++) {
            row[px] = color;
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
