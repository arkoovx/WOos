/*
 * NetSurf Framebuffer Implementation for WoOS
 * Copyright 2008 Vincent Sanders <vince@netsurf-browser.org>
 * Ported to WoOS WebAssembly architecture
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_event.h"
#include "nsfb.h"
typedef struct nsfb_bbox_s bbox_t;
struct fbtk_bitmap;
#include "netsurf/plotters.h"
#include "netsurf/plot_style.h"
#include "netsurf/types.h"
#include "framebuffer.h"
#include "fbtk.h"
#include "apps/woos.h"

nsfb_t *nsfb = NULL;

nsfb_t *framebuffer_set_surface(nsfb_t *new_nsfb) {
    nsfb_t *prev = nsfb;
    nsfb = new_nsfb;
    return prev;
}

static nserror framebuffer_plot_clip(const struct redraw_context *ctx, const struct rect *clip) {
    nsfb_bbox_t nsfb_clip;
    nsfb_clip.x0 = clip->x0;
    nsfb_clip.y0 = clip->y0;
    nsfb_clip.x1 = clip->x1;
    nsfb_clip.y1 = clip->y1;

    if (!nsfb_plot_set_clip(nsfb, &nsfb_clip)) {
        return NSERROR_INVALID;
    }
    return NSERROR_OK;
}

static nserror framebuffer_plot_arc(const struct redraw_context *ctx,
                                   const plot_style_t *style,
                                   int x, int y, int radius, int angle1, int angle2) {
    if (!nsfb_plot_arc(nsfb, x, y, radius, angle1, angle2, style->fill_colour)) {
        return NSERROR_INVALID;
    }
    return NSERROR_OK;
}

static nserror framebuffer_plot_disc(const struct redraw_context *ctx,
                                    const plot_style_t *style,
                                    int x, int y, int radius) {
    nsfb_bbox_t rect;
    rect.x0 = x - radius;
    rect.y0 = y - radius;
    rect.x1 = x + radius;
    rect.y1 = y + radius;
    if (!nsfb_plot_ellipse_fill(nsfb, &rect, style->fill_colour)) {
        return NSERROR_INVALID;
    }
    return NSERROR_OK;
}

static nserror framebuffer_plot_line(const struct redraw_context *ctx,
                                    const plot_style_t *style,
                                    const struct rect *line) {
    nsfb_bbox_t rect;
    nsfb_plot_pen_t pen;

    rect.x0 = line->x0;
    rect.y0 = line->y0;
    rect.x1 = line->x1;
    rect.y1 = line->y1;

    if (style->stroke_type != PLOT_OP_TYPE_NONE) {
        pen.stroke_type = NFSB_PLOT_OPTYPE_SOLID;
        pen.stroke_colour = style->stroke_colour;
        pen.stroke_width = plot_style_fixed_to_int(style->stroke_width);
        if (pen.stroke_width < 1) pen.stroke_width = 1;
        nsfb_plot_line(nsfb, &rect, &pen);
    }
    return NSERROR_OK;
}

static nserror framebuffer_plot_rectangle(const struct redraw_context *ctx,
                                         const plot_style_t *style,
                                         const struct rect *nsrect) {
    nsfb_bbox_t rect;
    rect.x0 = nsrect->x0;
    rect.y0 = nsrect->y0;
    rect.x1 = nsrect->x1;
    rect.y1 = nsrect->y1;

    if (style->fill_type != PLOT_OP_TYPE_NONE) {
        nsfb_plot_rectangle_fill(nsfb, &rect, style->fill_colour);
    }

    if (style->stroke_type != PLOT_OP_TYPE_NONE) {
        int width = plot_style_fixed_to_int(style->stroke_width);
        if (width < 1) width = 1;
        nsfb_plot_rectangle(nsfb, &rect, width, style->stroke_colour, false, false);
    }
    return NSERROR_OK;
}

static nserror framebuffer_plot_polygon(const struct redraw_context *ctx,
                                       const plot_style_t *style,
                                       const int *p,
                                       unsigned int n) {
    if (!nsfb_plot_polygon(nsfb, p, (int)n, style->fill_colour)) {
        return NSERROR_INVALID;
    }
    return NSERROR_OK;
}

static nserror framebuffer_plot_path(const struct redraw_context *ctx,
                                    const plot_style_t *pstyle,
                                    const float *p,
                                    unsigned int n,
                                    const float transform[6]) {
    return NSERROR_OK;
}

static nserror framebuffer_plot_bitmap(const struct redraw_context *ctx,
                                      struct bitmap *bm,
                                      int x, int y,
                                      int width, int height,
                                      colour bg,
                                      bitmap_flags_t flags) {
    if (!nsfb || !bm) return NSERROR_OK;

    uint32_t* src = (uint32_t*)bm;
    uint32_t* dst = (uint32_t*)nsfb->ptr;
    if (!dst || !src) return NSERROR_OK;

    int fb_w = nsfb->width;
    int fb_h = nsfb->height;

    for (int dy = 0; dy < height; dy++) {
        int py = y + dy;
        if (py < 0 || py >= fb_h) continue;
        for (int dx = 0; dx < width; dx++) {
            int px = x + dx;
            if (px < 0 || px >= fb_w) continue;
            uint32_t color = src[dy * width + dx];
            if ((color >> 24) != 0) {
                dst[py * fb_w + px] = color & 0xFFFFFF;
            }
        }
    }
    return NSERROR_OK;
}

static nserror framebuffer_plot_text(const struct redraw_context *ctx,
                                    const struct plot_font_style *fstyle,
                                    int x,
                                    int y,
                                    const char *text,
                                    size_t length) {
    if (!nsfb || !text || length == 0) return NSERROR_OK;

    char tmp[256];
    size_t copy_len = length < 255 ? length : 255;
    memcpy(tmp, text, copy_len);
    tmp[copy_len] = '\0';

    uint32_t color = fstyle ? (fstyle->foreground & 0xFFFFFF) : 0x000000;
    woos_draw_text(nsfb->ptr, nsfb->width, nsfb->height, x, y, tmp, color);
    return NSERROR_OK;
}

const struct plotter_table fb_plotters = {
    .clip = framebuffer_plot_clip,
    .arc = framebuffer_plot_arc,
    .disc = framebuffer_plot_disc,
    .line = framebuffer_plot_line,
    .rectangle = framebuffer_plot_rectangle,
    .polygon = framebuffer_plot_polygon,
    .path = framebuffer_plot_path,
    .bitmap = framebuffer_plot_bitmap,
    .text = framebuffer_plot_text,
    .option_knockout = true,
};

nsfb_t *framebuffer_initialise(const char *fename, int width, int height, int bpp) {
    nsfb_t *new_nsfb = nsfb_new(NSFB_SURFACE_RAM);
    if (!new_nsfb) return NULL;

    nsfb_set_geometry(new_nsfb, width, height, NSFB_FMT_XRGB8888);
    if (nsfb_init(new_nsfb) != 0) {
        nsfb_free(new_nsfb);
        return NULL;
    }
    nsfb = new_nsfb;
    return new_nsfb;
}

bool framebuffer_resize(nsfb_t *fb, int width, int height, int bpp) {
    if (!fb) return false;
    return (nsfb_set_geometry(fb, width, height, NSFB_FMT_XRGB8888) == 0);
}

void framebuffer_finalise(void) {
    if (nsfb) {
        nsfb_free(nsfb);
        nsfb = NULL;
    }
}
