#include "ui.h"
#include "fb.h"

#define COLOR_BG_DARK   0x0E1116u
#define COLOR_BG_LIGHT  0x141A22u
#define COLOR_PANEL     0x1F2630u
#define COLOR_ACCENT    0x58A6FFu
#define COLOR_WINDOW    0xE6EDF3u
#define COLOR_BORDER    0x2B3442u

static void draw_background(video_info_t* info) {
    fb_fill(info, COLOR_BG_DARK);

    for (uint16_t y = 0; y < info->height; y += 8) {
        if ((y / 8) & 1u) {
            fb_rect(info, 0, y, info->width, 4, COLOR_BG_LIGHT);
        }
    }
}

void ui_render_desktop(video_info_t* info) {
    draw_background(info);

    fb_rect(info, 0, 0, info->width, 34, COLOR_PANEL);
    fb_rect(info, 12, 8, 120, 18, COLOR_ACCENT);

    uint16_t win_w = (uint16_t)(info->width / 3);
    uint16_t win_h = (uint16_t)(info->height / 3);
    uint16_t win_x = (uint16_t)(info->width - win_w - 24);
    uint16_t win_y = 56;

    fb_rect(info, win_x, win_y, win_w, win_h, COLOR_WINDOW);
    fb_frame(info, win_x, win_y, win_w, win_h, 2, COLOR_BORDER);
    fb_rect(info, win_x, win_y, win_w, 22, COLOR_ACCENT);
}
