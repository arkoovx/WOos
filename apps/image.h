#ifndef WOOS_IMAGE_H
#define WOOS_IMAGE_H

#include "woos.h"
#include "woos_mem.h"

#define STBI_NO_STDIO
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM

#define STBI_MALLOC(sz)        woos_malloc(sz)
#define STBI_FREE(p)           woos_free(p)
#define STBI_REALLOC(p, sz)    woos_realloc(p, sz)
#define STBI_MEMCPY(dst,src,n) woos_memcpy(dst,src,n)
#define STBI_MEMSET(dst,v,n)   woos_memset(dst,v,n)
#define STBI_ASSERT(x)         ((void)0)

#define STBI_WRITE_NO_STDIO
#define STBIW_MALLOC(sz)        woos_malloc(sz)
#define STBIW_FREE(p)           woos_free(p)
#define STBIW_REALLOC(p, sz)    woos_realloc(p, sz)
#define STBIW_MEMCPY(dst,src,n) woos_memcpy(dst,src,n)
#define STBIW_MEMSET(dst,v,n)   woos_memset(dst,v,n)
#define STBIW_ASSERT(x)         ((void)0)

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#ifndef STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include "../external/stb/stb_image.h"
#include "../external/stb/stb_image_write.h"

typedef struct {
    int width;
    int height;
    int channels;
    uint32_t* pixels; // Format: 0xAARRGGBB
} woos_image_t;

// Auto-scaler: High quality Bilinear & Nearest-Neighbor scaling
static inline void image_scale_nearest(const uint32_t* src, int sw, int sh, uint32_t* dst, int dw, int dh) {
    if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    for (int dy = 0; dy < dh; dy++) {
        int sy = (dy * sh) / dh;
        if (sy >= sh) sy = sh - 1;
        const uint32_t* src_row = src + sy * sw;
        uint32_t* dst_row = dst + dy * dw;
        for (int dx = 0; dx < dw; dx++) {
            int sx = (dx * sw) / dw;
            if (sx >= sw) sx = sw - 1;
            dst_row[dx] = src_row[sx];
        }
    }
}

static inline void image_scale_bilinear(const uint32_t* src, int sw, int sh, uint32_t* dst, int dw, int dh) {
    if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    if (dw == sw && dh == sh) {
        woos_memcpy(dst, src, (size_t)sw * sh * sizeof(uint32_t));
        return;
    }
    // For large downscaling or tiny icons, nearest is fast and crisp
    if (dw < 32 || dh < 32 || sw / dw >= 3 || sh / dh >= 3) {
        image_scale_nearest(src, sw, sh, dst, dw, dh);
        return;
    }

    int x_ratio = ((sw - 1) << 16) / (dw > 1 ? dw - 1 : 1);
    int y_ratio = ((sh - 1) << 16) / (dh > 1 ? dh - 1 : 1);

    for (int y = 0; y < dh; y++) {
        int sy = (y * y_ratio) >> 16;
        int y_diff = (y * y_ratio) & 0xFFFF;
        int y_diff_inv = 65536 - y_diff;
        uint32_t* dst_row = dst + y * dw;

        for (int x = 0; x < dw; x++) {
            int sx = (x * x_ratio) >> 16;
            int x_diff = (x * x_ratio) & 0xFFFF;
            int x_diff_inv = 65536 - x_diff;

            uint32_t p00 = src[sy * sw + sx];
            uint32_t p01 = (sx + 1 < sw) ? src[sy * sw + sx + 1] : p00;
            uint32_t p10 = (sy + 1 < sh) ? src[(sy + 1) * sw + sx] : p00;
            uint32_t p11 = (sx + 1 < sw && sy + 1 < sh) ? src[(sy + 1) * sw + sx + 1] : p00;

            // Interpolate each channel
            uint32_t a = ((((p00 >> 24) * x_diff_inv + (p01 >> 24) * x_diff) >> 16) * y_diff_inv +
                          (((p10 >> 24) * x_diff_inv + (p11 >> 24) * x_diff) >> 16) * y_diff) >> 16;
            uint32_t r = (((((p00 >> 16) & 0xFF) * x_diff_inv + ((p01 >> 16) & 0xFF) * x_diff) >> 16) * y_diff_inv +
                          ((((p10 >> 16) & 0xFF) * x_diff_inv + ((p11 >> 16) & 0xFF) * x_diff) >> 16) * y_diff) >> 16;
            uint32_t g = (((((p00 >> 8) & 0xFF) * x_diff_inv + ((p01 >> 8) & 0xFF) * x_diff) >> 16) * y_diff_inv +
                          ((((p10 >> 8) & 0xFF) * x_diff_inv + ((p11 >> 8) & 0xFF) * x_diff) >> 16) * y_diff) >> 16;
            uint32_t b = ((((p00 & 0xFF) * x_diff_inv + (p01 & 0xFF) * x_diff) >> 16) * y_diff_inv +
                          (((p10 & 0xFF) * x_diff_inv + (p11 & 0xFF) * x_diff) >> 16) * y_diff) >> 16;

            dst_row[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

static inline void image_calc_fit(int sw, int sh, int max_w, int max_h, int* out_w, int* out_h) {
    if (sw <= 0 || sh <= 0 || max_w <= 0 || max_h <= 0) {
        *out_w = max_w;
        *out_h = max_h;
        return;
    }

    // Try fit by width
    int fit_w = max_w;
    int fit_h = (sh * max_w) / sw;

    if (fit_h > max_h) {
        fit_h = max_h;
        fit_w = (sw * max_h) / sh;
    }

    if (fit_w <= 0) fit_w = 1;
    if (fit_h <= 0) fit_h = 1;

    *out_w = fit_w;
    *out_h = fit_h;
}

// Alpha Blending
static inline void image_blit_alpha(uint32_t* dst, int dst_w, int dst_h, int dx, int dy,
                                   const uint32_t* src, int sw, int sh, int src_pitch) {
    if (!dst || !src) return;

    for (int y = 0; y < sh; y++) {
        int target_y = dy + y;
        if (target_y < 0 || target_y >= dst_h) continue;

        const uint32_t* s_line = src + y * src_pitch;
        uint32_t* d_line = dst + target_y * dst_w;

        for (int x = 0; x < sw; x++) {
            int target_x = dx + x;
            if (target_x < 0 || target_x >= dst_w) continue;

            uint32_t sp = s_line[x];
            uint32_t sa = (sp >> 24) & 0xFF;
            if (sa == 0) continue;
            if (sa == 255) {
                d_line[target_x] = sp;
                continue;
            }

            uint32_t dp = d_line[target_x];
            uint32_t sr = (sp >> 16) & 0xFF;
            uint32_t sg = (sp >> 8) & 0xFF;
            uint32_t sb = sp & 0xFF;

            uint32_t dr = (dp >> 16) & 0xFF;
            uint32_t dg = (dp >> 8) & 0xFF;
            uint32_t db = dp & 0xFF;

            uint32_t r = (sr * sa + dr * (255 - sa)) / 255;
            uint32_t g = (sg * sa + dg * (255 - sa)) / 255;
            uint32_t b = (sb * sa + db * (255 - sa)) / 255;

            d_line[target_x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

static inline void image_blit_alpha_clipped(uint32_t* dst, int dst_w, int dst_h, int dx, int dy,
                                           const uint32_t* src, int sw, int sh, int src_pitch,
                                           int clip_x, int clip_y, int clip_w, int clip_h) {
    if (!dst || !src) return;

    int min_y = clip_y > 0 ? clip_y : 0;
    int max_y = (clip_y + clip_h < dst_h) ? (clip_y + clip_h) : dst_h;
    int min_x = clip_x > 0 ? clip_x : 0;
    int max_x = (clip_x + clip_w < dst_w) ? (clip_x + clip_w) : dst_w;

    int start_y = (dy < min_y) ? (min_y - dy) : 0;
    int end_y   = (dy + sh > max_y) ? (max_y - dy) : sh;

    for (int y = start_y; y < end_y; y++) {
        int target_y = dy + y;
        const uint32_t* s_line = src + y * src_pitch;
        uint32_t* d_line = dst + target_y * dst_w;

        int start_x = (dx < min_x) ? (min_x - dx) : 0;
        int end_x   = (dx + sw > max_x) ? (max_x - dx) : sw;

        for (int x = start_x; x < end_x; x++) {
            int target_x = dx + x;

            uint32_t sp = s_line[x];
            uint32_t sa = (sp >> 24) & 0xFF;
            if (sa == 0) continue;
            if (sa == 255) {
                d_line[target_x] = sp;
                continue;
            }

            uint32_t dp = d_line[target_x];
            uint32_t sr = (sp >> 16) & 0xFF;
            uint32_t sg = (sp >> 8) & 0xFF;
            uint32_t sb = sp & 0xFF;

            uint32_t dr = (dp >> 16) & 0xFF;
            uint32_t dg = (dp >> 8) & 0xFF;
            uint32_t db = dp & 0xFF;

            uint32_t r = (sr * sa + dr * (255 - sa)) / 255;
            uint32_t g = (sg * sa + dg * (255 - sa)) / 255;
            uint32_t b = (sb * sa + db * (255 - sa)) / 255;

            d_line[target_x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

// Rotate 90 degrees clockwise
static inline uint32_t* image_rotate_90_cw(const uint32_t* src, int w, int h) {
    if (!src || w <= 0 || h <= 0) return 0;
    uint32_t* dst = (uint32_t*)woos_malloc((size_t)w * h * sizeof(uint32_t));
    if (!dst) return 0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int nx = h - 1 - y;
            int ny = x;
            dst[ny * h + nx] = src[y * w + x];
        }
    }
    return dst;
}

// Fast uncompressed BMP decoder
static inline woos_image_t* image_decode_bmp(const uint8_t* data, size_t len) {
    if (!data || len < 54) return 0;
    if (data[0] != 'B' || data[1] != 'M') return 0;

    uint32_t offset = *(const uint32_t*)&data[10];
    int32_t w = *(const int32_t*)&data[18];
    int32_t h = *(const int32_t*)&data[22];
    uint16_t bpp = *(const uint16_t*)&data[28];
    uint32_t comp = *(const uint32_t*)&data[30];

    if (comp != 0 && comp != 3) return 0; // Only uncompressed or bitfields
    if (bpp != 24 && bpp != 32) return 0;
    if (w <= 0 || w > 4096 || h == 0 || (h > 4096 || h < -4096)) return 0;

    int is_top_down = 0;
    int abs_h = h;
    if (abs_h < 0) {
        abs_h = -abs_h;
        is_top_down = 1;
    }

    uint32_t* pixels = (uint32_t*)woos_malloc((size_t)w * abs_h * sizeof(uint32_t));
    if (!pixels) return 0;

    int row_bytes = ((w * (bpp / 8) + 3) & ~3);
    const uint8_t* src_pixels = data + offset;

    for (int y = 0; y < abs_h; y++) {
        int src_y = is_top_down ? y : (abs_h - 1 - y);
        if (offset + (size_t)(src_y + 1) * row_bytes > len) break;
        const uint8_t* row = src_pixels + src_y * row_bytes;
        uint32_t* dst_row = pixels + y * w;

        if (bpp == 32) {
            for (int x = 0; x < w; x++) {
                uint8_t b = row[x * 4 + 0];
                uint8_t g = row[x * 4 + 1];
                uint8_t r = row[x * 4 + 2];
                uint8_t a = row[x * 4 + 3];
                // If all alpha is 0 in 32-bit BMP, treat as opaque
                dst_row[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        } else if (bpp == 24) {
            for (int x = 0; x < w; x++) {
                uint8_t b = row[x * 3 + 0];
                uint8_t g = row[x * 3 + 1];
                uint8_t r = row[x * 3 + 2];
                dst_row[x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }

    woos_image_t* img = (woos_image_t*)woos_malloc(sizeof(woos_image_t));
    if (!img) {
        woos_free(pixels);
        return 0;
    }
    img->width = w;
    img->height = abs_h;
    img->channels = (bpp == 32) ? 4 : 3;
    img->pixels = pixels;
    return img;
}

// Fast uncompressed 32-bit BMP encoder
static inline int image_encode_bmp(const uint32_t* pixels, int w, int h, uint8_t** out_data, size_t* out_len) {
    if (!pixels || w <= 0 || h <= 0 || !out_data || !out_len) return -1;

    size_t img_bytes = (size_t)w * h * 4;
    size_t total_size = 54 + img_bytes;

    uint8_t* buf = (uint8_t*)woos_malloc(total_size);
    if (!buf) return -1;

    // BitmapFileHeader
    buf[0] = 'B';
    buf[1] = 'M';
    *(uint32_t*)&buf[2] = (uint32_t)total_size;
    *(uint16_t*)&buf[6] = 0;
    *(uint16_t*)&buf[8] = 0;
    *(uint32_t*)&buf[10] = 54; // Pixel data offset

    // BitmapInfoHeader
    *(uint32_t*)&buf[14] = 40; // biSize
    *(int32_t*)&buf[18] = w;
    *(int32_t*)&buf[22] = -h;  // negative for top-down
    *(uint16_t*)&buf[26] = 1;  // biPlanes
    *(uint16_t*)&buf[28] = 32; // biBitCount
    *(uint32_t*)&buf[30] = 0;  // BI_RGB
    *(uint32_t*)&buf[34] = (uint32_t)img_bytes;
    *(int32_t*)&buf[38] = 2835;
    *(int32_t*)&buf[42] = 2835;
    *(uint32_t*)&buf[46] = 0;
    *(uint32_t*)&buf[50] = 0;

    // Copy top-down pixels (0xAARRGGBB -> BGRA in memory)
    uint32_t* dst_pix = (uint32_t*)&buf[54];
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint32_t p = pixels[i];
        uint8_t a = (p >> 24) & 0xFF;
        uint8_t r = (p >> 16) & 0xFF;
        uint8_t g = (p >> 8) & 0xFF;
        uint8_t b = p & 0xFF;
        dst_pix[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }

    *out_data = buf;
    *out_len = total_size;
    return 0;
}

// Universal Image Decoder (PNG, JPG, BMP)
static inline woos_image_t* image_load_from_memory(const uint8_t* data, size_t len) {
    if (!data || len < 4) return 0;

    // Fast path: BMP
    if (data[0] == 'B' && data[1] == 'M') {
        woos_image_t* bmp = image_decode_bmp(data, len);
        if (bmp) return bmp;
    }

    // STB Image: PNG, JPG, BMP, etc.
    int w = 0, h = 0, channels = 0;
    stbi_uc* decoded = stbi_load_from_memory(data, (int)len, &w, &h, &channels, 4);
    if (!decoded || w <= 0 || h <= 0) {
        return 0;
    }

    uint32_t* pixels = (uint32_t*)woos_malloc((size_t)w * h * sizeof(uint32_t));
    if (!pixels) {
        stbi_image_free(decoded);
        return 0;
    }

    // Convert RGBA from stb to 0xAARRGGBB
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint8_t r = decoded[i * 4 + 0];
        uint8_t g = decoded[i * 4 + 1];
        uint8_t b = decoded[i * 4 + 2];
        uint8_t a = decoded[i * 4 + 3];
        pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
    stbi_image_free(decoded);

    woos_image_t* img = (woos_image_t*)woos_malloc(sizeof(woos_image_t));
    if (!img) {
        woos_free(pixels);
        return 0;
    }
    img->width = w;
    img->height = h;
    img->channels = channels;
    img->pixels = pixels;
    return img;
}

static inline woos_image_t* image_load_file(const char* path) {
    if (!path || path[0] == '\0') return 0;

    int32_t fd = woos_vfs_open(path, VFS_MODE_READ);
    if (fd < 0) return 0;

    // Max 4 MB image file
    size_t max_size = 4 * 1024 * 1024;
    uint8_t* buf = (uint8_t*)woos_malloc(max_size);
    if (!buf) {
        woos_vfs_close(fd);
        return 0;
    }

    int32_t bytes_read = woos_vfs_read(fd, buf, (uint32_t)max_size);
    woos_vfs_close(fd);

    if (bytes_read <= 0) {
        woos_free(buf);
        return 0;
    }

    woos_image_t* img = image_load_from_memory(buf, (size_t)bytes_read);
    woos_free(buf);
    return img;
}

// Memory write context for stb_image_write
typedef struct {
    uint8_t* buffer;
    size_t size;
    size_t capacity;
} stbi_write_mem_t;

static inline void stbi_write_func_cb(void* context, void* data, int size) {
    stbi_write_mem_t* ctx = (stbi_write_mem_t*)context;
    if (size <= 0 || !data) return;
    if (ctx->size + (size_t)size > ctx->capacity) {
        size_t new_cap = ctx->capacity * 2 + (size_t)size + 4096;
        uint8_t* new_buf = (uint8_t*)woos_realloc(ctx->buffer, new_cap);
        if (!new_buf) return;
        ctx->buffer = new_buf;
        ctx->capacity = new_cap;
    }
    woos_memcpy(ctx->buffer + ctx->size, data, (size_t)size);
    ctx->size += (size_t)size;
}

// Universal Image Encoder & Saver
static inline int image_save_file(const char* path, const uint32_t* pixels, int w, int h, const char* format) {
    if (!path || !pixels || w <= 0 || h <= 0) return -1;

    // Format detection
    int is_png = 0, is_jpg = 0, is_bmp = 0;
    if (format) {
        if (strcmp(format, "png") == 0 || strcmp(format, "PNG") == 0) is_png = 1;
        else if (strcmp(format, "jpg") == 0 || strcmp(format, "JPG") == 0 ||
                 strcmp(format, "jpeg") == 0 || strcmp(format, "JPEG") == 0) is_jpg = 1;
        else is_bmp = 1;
    } else {
        int plen = (int)strlen(path);
        if (plen >= 4 && (strcmp(&path[plen - 4], ".png") == 0 || strcmp(&path[plen - 4], ".PNG") == 0)) is_png = 1;
        else if (plen >= 4 && (strcmp(&path[plen - 4], ".jpg") == 0 || strcmp(&path[plen - 4], ".JPG") == 0 ||
                               strcmp(&path[plen - 4], ".jpeg") == 0 || strcmp(&path[plen - 4], ".JPEG") == 0)) is_jpg = 1;
        else is_bmp = 1;
    }

    if (is_bmp) {
        uint8_t* bmp_data = 0;
        size_t bmp_len = 0;
        if (image_encode_bmp(pixels, w, h, &bmp_data, &bmp_len) == 0 && bmp_data) {
            int32_t fd = woos_vfs_open(path, VFS_MODE_WRITE | VFS_MODE_CREATE | VFS_MODE_TRUNC);
            if (fd >= 0) {
                woos_vfs_write(fd, bmp_data, (uint32_t)bmp_len);
                woos_vfs_close(fd);
                woos_free(bmp_data);
                return 0;
            }
            woos_free(bmp_data);
        }
        return -1;
    }

    // Convert pixels to RGBA buffer for STB Image Write
    uint8_t* rgba = (uint8_t*)woos_malloc((size_t)w * h * 4);
    if (!rgba) return -1;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint32_t p = pixels[i];
        rgba[i * 4 + 0] = (uint8_t)((p >> 16) & 0xFF); // R
        rgba[i * 4 + 1] = (uint8_t)((p >> 8) & 0xFF);  // G
        rgba[i * 4 + 2] = (uint8_t)(p & 0xFF);         // B
        rgba[i * 4 + 3] = (uint8_t)((p >> 24) & 0xFF); // A
    }

    stbi_write_mem_t ctx;
    ctx.capacity = (size_t)w * h + 8192;
    ctx.size = 0;
    ctx.buffer = (uint8_t*)woos_malloc(ctx.capacity);
    if (!ctx.buffer) {
        woos_free(rgba);
        return -1;
    }

    int res = 0;
    if (is_png || is_jpg) {
        res = stbi_write_png_to_func(stbi_write_func_cb, &ctx, w, h, 4, rgba, w * 4);
    }
    woos_free(rgba);

    if (res && ctx.size > 0 && ctx.buffer) {
        int32_t fd = woos_vfs_open(path, VFS_MODE_WRITE | VFS_MODE_CREATE | VFS_MODE_TRUNC);
        if (fd >= 0) {
            woos_vfs_write(fd, ctx.buffer, (uint32_t)ctx.size);
            woos_vfs_close(fd);
            woos_free(ctx.buffer);
            return 0;
        }
    }

    if (ctx.buffer) woos_free(ctx.buffer);
    return -1;
}

static inline void image_free(woos_image_t* img) {
    if (!img) return;
    if (img->pixels) woos_free(img->pixels);
    woos_free(img);
}

#endif
