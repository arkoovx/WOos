/*
 * NetSurf Box Redraw Implementation for WoOS
 * Copyright 2004-2008 James Bursa <bursa@users.sourceforge.net>
 * Ported to WoOS WebAssembly architecture
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "netsurf/types.h"
#include "netsurf/plotters.h"
#include "netsurf/plot_style.h"
#include "render/box.h"

void redraw_box_tree(struct box *b, int parent_x, int parent_y, int scroll_y, int viewport_h, const struct redraw_context *ctx) {
    if (!b || !ctx || !ctx->plot) return;

    int abs_x = parent_x + b->x;
    int abs_y = parent_y + b->y - scroll_y;

    // Viewport culling
    if (abs_y + b->height >= 0 && abs_y <= viewport_h) {
        // Draw background if present
        if (b->background_colour != 0 && b->background_colour != NS_TRANSPARENT) {
            plot_style_t bg_style;
            bg_style.fill_type = PLOT_OP_TYPE_SOLID;
            bg_style.stroke_type = PLOT_OP_TYPE_NONE;
            bg_style.fill_colour = b->background_colour;

            struct rect r;
            r.x0 = abs_x;
            r.y0 = abs_y;
            r.x1 = abs_x + b->width;
            r.y1 = abs_y + b->height;
            ctx->plot->rectangle(ctx, &bg_style, &r);
        }

        // Draw borders if present
        if (b->border[TOP].width > 0 || b->border[LEFT].width > 0) {
            plot_style_t border_style;
            border_style.fill_type = PLOT_OP_TYPE_NONE;
            border_style.stroke_type = PLOT_OP_TYPE_SOLID;
            border_style.stroke_colour = b->border[TOP].c ? b->border[TOP].c : 0xCCCCCC;
            border_style.stroke_width = plot_style_int_to_fixed(1);

            struct rect r;
            r.x0 = abs_x;
            r.y0 = abs_y;
            r.x1 = abs_x + b->width;
            r.y1 = abs_y + b->height;
            ctx->plot->rectangle(ctx, &border_style, &r);
        }

        // Draw text
        if (b->text && b->length > 0) {
            struct plot_font_style fstyle;
            fstyle.family = PLOT_FONT_FAMILY_SANS_SERIF;
            fstyle.size = 14 * PLOT_STYLE_SCALE;
            fstyle.weight = 400;
            fstyle.flags = 0;
            fstyle.foreground = b->colour ? b->colour : 0x222222;
            fstyle.background = NS_TRANSPARENT;

            ctx->plot->text(ctx, &fstyle, abs_x, abs_y, b->text, b->length);
        }

        // Draw bitmap / image if attached
        if (b->type == BOX_INLINE_BLOCK && b->image_data && b->width > 0 && b->height > 0) {
            ctx->plot->bitmap(ctx, (struct bitmap*)b->image_data, abs_x, abs_y, b->width, b->height, 0, 0);
        }
    }

    // Traverse children
    for (struct box *c = b->children; c != NULL; c = c->next) {
        redraw_box_tree(c, parent_x + b->x, parent_y + b->y, scroll_y, viewport_h, ctx);
    }
}
