#include "fb.h"
#include "drivers/virtio_gpu_renderer/virtio_gpu_renderer.h"

#define FONT_W 8
#define FONT_H 8

#ifndef WOOS_ENABLE_DBL_BUFFER
#define WOOS_ENABLE_DBL_BUFFER 0
#endif

#define FB_BACKBUFFER_CAPACITY (8u * 1024u * 1024u)

static const uint8_t glyph_space[FONT_H] = {0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t glyph_exclam[FONT_H] = {0x18, 0x18, 0x18, 0x18, 0x18, 0, 0x18, 0};
static const uint8_t glyph_quote[FONT_H] = {0x36, 0x36, 0x24, 0, 0, 0, 0, 0};
static const uint8_t glyph_hash[FONT_H] = {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0};
static const uint8_t glyph_dollar[FONT_H] = {0x18, 0x3E, 0x58, 0x3C, 0x1A, 0x7C, 0x18, 0};
static const uint8_t glyph_percent[FONT_H] = {0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0, 0};
static const uint8_t glyph_amp[FONT_H] = {0x30, 0x48, 0x30, 0x4A, 0x44, 0x3A, 0, 0};
static const uint8_t glyph_apostrophe[FONT_H] = {0x18, 0x18, 0x10, 0, 0, 0, 0, 0};
static const uint8_t glyph_lparen[FONT_H] = {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0};
static const uint8_t glyph_rparen[FONT_H] = {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0};
static const uint8_t glyph_asterisk[FONT_H] = {0, 0x66, 0x3C, 0x7E, 0x3C, 0x66, 0, 0};
static const uint8_t glyph_plus[FONT_H] = {0, 0x18, 0x18, 0x7E, 0x18, 0x18, 0, 0};
static const uint8_t glyph_comma[FONT_H] = {0, 0, 0, 0, 0, 0x18, 0x18, 0x10};
static const uint8_t glyph_minus[FONT_H] = {0, 0, 0, 0x7E, 0, 0, 0, 0};
static const uint8_t glyph_dot[FONT_H] = {0, 0, 0, 0, 0, 0, 0x18, 0x18};
static const uint8_t glyph_slash[FONT_H] = {0x02, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0};
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
static const uint8_t glyph_colon[FONT_H] = {0, 0x18, 0x18, 0, 0, 0x18, 0x18, 0};
static const uint8_t glyph_semicolon[FONT_H] = {0, 0x18, 0x18, 0, 0, 0x18, 0x18, 0x10};
static const uint8_t glyph_less[FONT_H] = {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0};
static const uint8_t glyph_equal[FONT_H] = {0, 0x7E, 0, 0x7E, 0, 0, 0, 0};
static const uint8_t glyph_greater[FONT_H] = {0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0};
static const uint8_t glyph_question[FONT_H] = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0, 0x18, 0};
static const uint8_t glyph_at[FONT_H] = {0x3C, 0x42, 0x5A, 0x5E, 0x5C, 0x40, 0x3C, 0};
static const uint8_t glyph_A[FONT_H] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0};
static const uint8_t glyph_B[FONT_H] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0};
static const uint8_t glyph_C[FONT_H] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0};
static const uint8_t glyph_D[FONT_H] = {0x78, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0x78, 0};
static const uint8_t glyph_E[FONT_H] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E, 0};
static const uint8_t glyph_F[FONT_H] = {0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0};
static const uint8_t glyph_G[FONT_H] = {0x3C, 0x66, 0x60, 0x6E, 0x66, 0x66, 0x3C, 0};
static const uint8_t glyph_H[FONT_H] = {0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0};
static const uint8_t glyph_I[FONT_H] = {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0};
static const uint8_t glyph_J[FONT_H] = {0x1E, 0x0C, 0x0C, 0x0C, 0x6C, 0x6C, 0x38, 0};
static const uint8_t glyph_K[FONT_H] = {0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0};
static const uint8_t glyph_L[FONT_H] = {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0};
static const uint8_t glyph_M[FONT_H] = {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0};
static const uint8_t glyph_N[FONT_H] = {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0};
static const uint8_t glyph_O[FONT_H] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0};
static const uint8_t glyph_P[FONT_H] = {0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0};
static const uint8_t glyph_Q[FONT_H] = {0x3C, 0x66, 0x66, 0x66, 0x6E, 0x3C, 0x0E, 0};
static const uint8_t glyph_R[FONT_H] = {0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66, 0};
static const uint8_t glyph_S[FONT_H] = {0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0};
static const uint8_t glyph_T[FONT_H] = {0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x3C, 0};
static const uint8_t glyph_U[FONT_H] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0};
static const uint8_t glyph_V[FONT_H] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0};
static const uint8_t glyph_W[FONT_H] = {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0};
static const uint8_t glyph_X[FONT_H] = {0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0};
static const uint8_t glyph_Y[FONT_H] = {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0};
static const uint8_t glyph_Z[FONT_H] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E, 0};
static const uint8_t glyph_lbracket[FONT_H] = {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0};
static const uint8_t glyph_backslash[FONT_H] = {0x40, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0};
static const uint8_t glyph_rbracket[FONT_H] = {0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78, 0};
static const uint8_t glyph_caret[FONT_H] = {0x18, 0x3C, 0x66, 0x42, 0, 0, 0, 0};
static const uint8_t glyph_underscore[FONT_H] = {0, 0, 0, 0, 0, 0, 0x7E, 0};
static const uint8_t glyph_backtick[FONT_H] = {0x30, 0x18, 0x0C, 0, 0, 0, 0, 0};
static const uint8_t glyph_a[FONT_H] = {0, 0, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0};
static const uint8_t glyph_b[FONT_H] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0};
static const uint8_t glyph_c[FONT_H] = {0, 0, 0x3C, 0x66, 0x60, 0x66, 0x3C, 0};
static const uint8_t glyph_d[FONT_H] = {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0};
static const uint8_t glyph_e[FONT_H] = {0, 0, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0};
static const uint8_t glyph_f[FONT_H] = {0x1C, 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0};
static const uint8_t glyph_g[FONT_H] = {0, 0, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x7C};
static const uint8_t glyph_h[FONT_H] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0};
static const uint8_t glyph_i[FONT_H] = {0x18, 0, 0x38, 0x18, 0x18, 0x18, 0x3C, 0};
static const uint8_t glyph_j[FONT_H] = {0x0C, 0, 0x1C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38};
static const uint8_t glyph_k[FONT_H] = {0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0};
static const uint8_t glyph_l[FONT_H] = {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0};
static const uint8_t glyph_m[FONT_H] = {0, 0, 0x6C, 0x7E, 0x7E, 0x6B, 0x63, 0};
static const uint8_t glyph_n[FONT_H] = {0, 0, 0x7C, 0x66, 0x66, 0x66, 0x66, 0};
static const uint8_t glyph_o[FONT_H] = {0, 0, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0};
static const uint8_t glyph_p[FONT_H] = {0, 0, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60};
static const uint8_t glyph_q[FONT_H] = {0, 0, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x06};
static const uint8_t glyph_r[FONT_H] = {0, 0, 0x6C, 0x76, 0x60, 0x60, 0x60, 0};
static const uint8_t glyph_s[FONT_H] = {0, 0, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0};
static const uint8_t glyph_t[FONT_H] = {0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x1C, 0};
static const uint8_t glyph_u[FONT_H] = {0, 0, 0x66, 0x66, 0x66, 0x66, 0x3E, 0};
static const uint8_t glyph_v[FONT_H] = {0, 0, 0x66, 0x66, 0x66, 0x3C, 0x18, 0};
static const uint8_t glyph_w[FONT_H] = {0, 0, 0x63, 0x63, 0x6B, 0x7F, 0x36, 0};
static const uint8_t glyph_x[FONT_H] = {0, 0, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0};
static const uint8_t glyph_y[FONT_H] = {0, 0, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x7C};
static const uint8_t glyph_z[FONT_H] = {0, 0, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0};
static const uint8_t glyph_lbrace[FONT_H] = {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0};
static const uint8_t glyph_pipe[FONT_H] = {0x18, 0x18, 0x18, 0, 0x18, 0x18, 0x18, 0};
static const uint8_t glyph_rbrace[FONT_H] = {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0};
static const uint8_t glyph_tilde[FONT_H] = {0x32, 0x4C, 0, 0, 0, 0, 0, 0};

static const uint8_t* glyph_for_char(char c) {
    switch (c) {
        case ' ': return glyph_space;
        case '!': return glyph_exclam;
        case '"': return glyph_quote;
        case '#': return glyph_hash;
        case '$': return glyph_dollar;
        case '%': return glyph_percent;
        case '&': return glyph_amp;
        case '\'': return glyph_apostrophe;
        case '(': return glyph_lparen;
        case ')': return glyph_rparen;
        case '*': return glyph_asterisk;
        case '+': return glyph_plus;
        case ',': return glyph_comma;
        case '-': return glyph_minus;
        case '.': return glyph_dot;
        case '/': return glyph_slash;
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
        case ':': return glyph_colon;
        case ';': return glyph_semicolon;
        case '<': return glyph_less;
        case '=': return glyph_equal;
        case '>': return glyph_greater;
        case '?': return glyph_question;
        case '@': return glyph_at;
        case 'A': return glyph_A;
        case 'B': return glyph_B;
        case 'C': return glyph_C;
        case 'D': return glyph_D;
        case 'E': return glyph_E;
        case 'F': return glyph_F;
        case 'G': return glyph_G;
        case 'H': return glyph_H;
        case 'I': return glyph_I;
        case 'J': return glyph_J;
        case 'K': return glyph_K;
        case 'L': return glyph_L;
        case 'M': return glyph_M;
        case 'N': return glyph_N;
        case 'O': return glyph_O;
        case 'P': return glyph_P;
        case 'Q': return glyph_Q;
        case 'R': return glyph_R;
        case 'S': return glyph_S;
        case 'T': return glyph_T;
        case 'U': return glyph_U;
        case 'V': return glyph_V;
        case 'W': return glyph_W;
        case 'X': return glyph_X;
        case 'Y': return glyph_Y;
        case 'Z': return glyph_Z;
        case '[': return glyph_lbracket;
        case '\\': return glyph_backslash;
        case ']': return glyph_rbracket;
        case '^': return glyph_caret;
        case '_': return glyph_underscore;
        case '`': return glyph_backtick;
        case 'a': return glyph_a;
        case 'b': return glyph_b;
        case 'c': return glyph_c;
        case 'd': return glyph_d;
        case 'e': return glyph_e;
        case 'f': return glyph_f;
        case 'g': return glyph_g;
        case 'h': return glyph_h;
        case 'i': return glyph_i;
        case 'j': return glyph_j;
        case 'k': return glyph_k;
        case 'l': return glyph_l;
        case 'm': return glyph_m;
        case 'n': return glyph_n;
        case 'o': return glyph_o;
        case 'p': return glyph_p;
        case 'q': return glyph_q;
        case 'r': return glyph_r;
        case 's': return glyph_s;
        case 't': return glyph_t;
        case 'u': return glyph_u;
        case 'v': return glyph_v;
        case 'w': return glyph_w;
        case 'x': return glyph_x;
        case 'y': return glyph_y;
        case 'z': return glyph_z;
        case '{': return glyph_lbrace;
        case '|': return glyph_pipe;
        case '}': return glyph_rbrace;
        case '~': return glyph_tilde;
        default: return glyph_question;
    }
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

static inline void mem_copy(uint8_t* dst, const uint8_t* src, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        dst[i] = src[i];
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

static inline uint8_t bytes_per_pixel(const video_info_t* info) {
    uint8_t bytes = (uint8_t)(info->bpp / 8u);
    return (bytes == 0u) ? 4u : bytes;
}

static inline uint8_t use_gpu_draw_commands(void) {
    return virtio_gpu_renderer_is_active();
}

uint32_t fb_readpixel(video_info_t* info, uint16_t x, uint16_t y) {
    if (use_gpu_draw_commands()) {
        return virtio_gpu_renderer_readpixel(info, x, y);
    }

    if (x >= info->width || y >= info->height) {
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

void fb_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color) {
    if (use_gpu_draw_commands()) {
        virtio_gpu_renderer_rect(info, x, y, w, h, color);
        return;
    }

    uint16_t x_end = clamp_u16((uint16_t)(x + w), info->width);
    uint16_t y_end = clamp_u16((uint16_t)(y + h), info->height);

    for (uint16_t py = y; py < y_end; py++) {
        for (uint16_t px = x; px < x_end; px++) {
            fb_writepixel(info, px, py, color);
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

    if (use_gpu_draw_commands()) {
        virtio_gpu_renderer_draw_glyph(info, x, y, glyph, FONT_W, FONT_H, color, bg_color);
        return;
    }

    for (uint16_t gy = 0; gy < FONT_H; gy++) {
        uint8_t row = glyph[gy];
        for (uint16_t gx = 0; gx < FONT_W; gx++) {
            uint8_t bit = (uint8_t)(0x80u >> gx);
            uint32_t px_color = (row & bit) ? color : bg_color;
            fb_writepixel(info, (uint16_t)(x + gx), (uint16_t)(y + gy), px_color);
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

void fb_present_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
#if WOOS_ENABLE_DBL_BUFFER
    if (use_gpu_draw_commands() || !g_backbuffer_enabled || w == 0 || h == 0) {
        virtio_gpu_renderer_present_rect(info, x, y, w, h);
        return;
    }

    uint16_t x_end = clamp_u16(rect_end(x, w), info->width);
    uint16_t y_end = clamp_u16(rect_end(y, h), info->height);
    uint16_t copy_width = (uint16_t)(x_end - x);
    uint16_t row_len = (uint16_t)(copy_width * bytes_per_pixel(info));
    uint8_t* front = (uint8_t*)(uint64_t)info->framebuffer;

    for (uint16_t py = y; py < y_end; py++) {
        uint8_t* src = g_backbuffer + ((uint64_t)py * info->pitch) + ((uint64_t)x * bytes_per_pixel(info));
        uint8_t* dst = front + ((uint64_t)py * info->pitch) + ((uint64_t)x * bytes_per_pixel(info));
        mem_copy(dst, src, row_len);
    }

    virtio_gpu_renderer_present_rect(info, x, y, w, h);
#else
    virtio_gpu_renderer_present_rect(info, x, y, w, h);
#endif
}
