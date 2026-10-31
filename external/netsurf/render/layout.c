/*
 * NetSurf Layout Engine Implementation for WoOS
 * Copyright 2005 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
 * Ported to WoOS WebAssembly architecture
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "netsurf/types.h"
#include "netsurf/layout.h"
#include "netsurf/plot_style.h"
#include "render/box.h"

#define CHAR_WIDTH 8
#define LINE_HEIGHT 18

static void layout_block(struct box *b, int available_width, int *cur_y) {
    if (!b) return;

    b->x = b->margin[LEFT] + b->border[LEFT].width + b->padding[LEFT];
    b->y = *cur_y + b->margin[TOP];

    if (b->width == UNKNOWN_WIDTH || b->width <= 0) {
        int avail = available_width - (b->margin[LEFT] + b->border[LEFT].width + b->padding[LEFT] +
                                      b->padding[RIGHT] + b->border[RIGHT].width + b->margin[RIGHT]);
        b->width = avail > 0 ? avail : 0;
    }

    int child_y = 0;
    int cur_line_x = 0;
    int line_h = LINE_HEIGHT;

    for (struct box *c = b->children; c != NULL; c = c->next) {
        if (c->type == BOX_BLOCK || c->type == BOX_TABLE || c->type == BOX_TABLE_ROW) {
            // New line before block
            if (cur_line_x > 0) {
                child_y += line_h;
                cur_line_x = 0;
            }
            int sub_y = child_y;
            layout_block(c, b->width, &sub_y);
            child_y = sub_y;
        } else if (c->type == BOX_BR) {
            child_y += line_h;
            cur_line_x = 0;
        } else if (c->type == BOX_TEXT || c->type == BOX_INLINE || c->type == BOX_INLINE_BLOCK) {
            int text_w = 0;
            if (c->text) {
                text_w = (int)c->length * CHAR_WIDTH;
            } else if (c->width > 0 && c->width != UNKNOWN_WIDTH) {
                text_w = c->width;
            }

            // Word wrap if beyond available width
            if (cur_line_x + text_w > b->width && cur_line_x > 0) {
                child_y += line_h;
                cur_line_x = 0;
            }

            c->x = cur_line_x;
            c->y = child_y;
            c->width = text_w;
            c->height = c->height > 0 ? c->height : line_h;

            cur_line_x += text_w;
            if (c->height > line_h) line_h = c->height;
        }
    }

    if (cur_line_x > 0) {
        child_y += line_h;
    }

    if (b->height <= 0) {
        b->height = child_y + b->padding[TOP] + b->padding[BOTTOM] + b->border[TOP].width + b->border[BOTTOM].width;
    }

    *cur_y = b->y + b->height + b->margin[BOTTOM];
}

int layout_document_tree(struct box *root, int viewport_width) {
    if (!root) return 0;

    int cur_y = 0;
    root->x = 0;
    root->y = 0;
    root->width = viewport_width;

    layout_block(root, viewport_width, &cur_y);
    return cur_y;
}
