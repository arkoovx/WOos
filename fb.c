#include "fb.h"

#define FONT_W 8
#define FONT_H 8

static const uint8_t glyph_space[FONT_H] = {0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t glyph_dot[FONT_H]   = {0, 0, 0, 0, 0, 0, 0x18, 0x18};
static const uint8_t glyph_colon[FONT_H] = {0, 0x18, 0x18, 0, 0, 0x18, 0x18, 0};

static const uint8_t glyph_0[FONT_H] = {0x3C, 0x66, 0x76, 0x7E, 0x6E, 0x66, 0x3C, 0};
static const uint8_t glyph_1[FONT_H] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C, 0};
static const uint8_t glyph_2[FONT_H] = {0x3C, 0x66, 0x06, 0x1C, 0x30, 0x66, 0x7E, 0};
static const uint8_t glyph_3[FONT_H] = {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0};
static const uint8_t glyph_4[FONT_H] = {0x0C, 0x1C, 0x3C, 0x6C, 0x7E, 0x0C, 0x1E, 0};
static const uint8_t glyph_5[FONT_H] = {0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0};
static const uint8_t glyph_6[FONT_H] = {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x3C, 0};
static const uint8_t glyph_7[FONT_H] = {0x7E, 0x66, 0x06, 0x0C, 0x18, 0x18, 0x18, 0};
static const uint8_t glyph_8[FONT_H] = {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C, 0};
static const uint8_t glyph_9[FONT_H] = {0x3C, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x38, 0};

static const uint8_t glyph_A[FONT_H] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0};
static const uint8_t glyph_B[FONT_H] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0};
static const uint8_t glyph_D[FONT_H] = {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0};
static const uint8_t glyph_E[FONT_H] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0};
static const uint8_t glyph_H[FONT_H] = {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0};
static const uint8_t glyph_I[FONT_H] = {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0};
static const uint8_t glyph_L[FONT_H] = {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0};
static const uint8_t glyph_O[FONT_H] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0};
static const uint8_t glyph_R[FONT_H] = {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0};
static const uint8_t glyph_S[FONT_H] = {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0};
static const uint8_t glyph_T[FONT_H] = {0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x3C, 0};
static const uint8_t glyph_U[FONT_H] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0};
static const uint8_t glyph_V[FONT_H] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0};
static const uint8_t glyph_W[FONT_H] = {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0};
static const uint8_t glyph_Y[FONT_H] = {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0};

static const uint8_t* glyph_for_char(char c) {
    switch (c) {
        case ' ': return glyph_space;
        case '.': return glyph_dot;
        case ':': return glyph_colon;
        case '0': return glyph_0;
        case '1': return glyph_1;
        case '2': return glyph_2;
        case '3': return glyph_3;
        case '4': return glyph_4;
        case '5': return glyph_5;
        case '6': return glyph_6;
        case '7': return glyph_7;
        case '8': return glyph_8;
        case '9': return glyph_9;
        case 'A': return glyph_A;
        case 'B': return glyph_B;
        case 'D': return glyph_D;
        case 'E': return glyph_E;
        case 'H': return glyph_H;
        case 'I': return glyph_I;
        case 'L': return glyph_L;
        case 'O': return glyph_O;
        case 'R': return glyph_R;
        case 'S': return glyph_S;
        case 'T': return glyph_T;
        case 'U': return glyph_U;
        case 'V': return glyph_V;
        case 'W': return glyph_W;
        case 'Y': return glyph_Y;
        default: return glyph_space;
    }
}

static inline uint16_t clamp_u16(uint16_t value, uint16_t max) {
    return (value > max) ? max : value;
}

static inline void fb_putpixel(video_info_t* info, uint16_t x, uint16_t y, uint32_t color) {
    if (x >= info->width || y >= info->height) {
        return;
    }

    uint8_t* base = (uint8_t*)(uint64_t)info->framebuffer;
    uint32_t* row = (uint32_t*)(base + ((uint64_t)y * info->pitch));
    row[x] = color;
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

void fb_draw_char(video_info_t* info, uint16_t x, uint16_t y, char c, uint32_t color, uint32_t bg_color) {
    const uint8_t* glyph = glyph_for_char(c);

    for (uint16_t gy = 0; gy < FONT_H; gy++) {
        uint8_t row = glyph[gy];
        for (uint16_t gx = 0; gx < FONT_W; gx++) {
            uint8_t bit = (uint8_t)(0x80u >> gx);
            uint32_t px_color = (row & bit) ? color : bg_color;
            fb_putpixel(info, (uint16_t)(x + gx), (uint16_t)(y + gy), px_color);
        }
    }
}

void fb_draw_text(video_info_t* info, uint16_t x, uint16_t y, const char* text, uint32_t color, uint32_t bg_color) {
    uint16_t cursor_x = x;

    while (*text != '\0') {
        fb_draw_char(info, cursor_x, y, *text, color, bg_color);
        cursor_x = (uint16_t)(cursor_x + FONT_W);
        text++;
    }
}
