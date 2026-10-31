/*
 * NetSurf Box Tree Implementation for WoOS
 * Copyright 2005 James Bursa <bursa@users.sourceforge.net>
 * Copyright 2003 Phil Mellor <monkeyson@users.sourceforge.net>
 * Ported to WoOS WebAssembly architecture
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "netsurf/types.h"
#include "netsurf/plot_style.h"
#include "render/box.h"

struct box *box_create(box_type type) {
    struct box *b = (struct box*)calloc(1, sizeof(struct box));
    if (!b) return NULL;

    b->type = type;
    b->width = UNKNOWN_WIDTH;
    b->max_width = UNKNOWN_MAX_WIDTH;
    return b;
}

void box_add_child(struct box *parent, struct box *child) {
    if (!parent || !child) return;

    if (parent->children != NULL) {
        parent->last->next = child;
        child->prev = parent->last;
    } else {
        parent->children = child;
        child->prev = NULL;
    }

    parent->last = child;
    child->parent = parent;
}

void box_insert_sibling(struct box *box, struct box *new_box) {
    if (!box || !new_box) return;

    new_box->parent = box->parent;
    new_box->prev = box;
    new_box->next = box->next;
    box->next = new_box;
    if (new_box->next) {
        new_box->next->prev = new_box;
    } else if (new_box->parent) {
        new_box->parent->last = new_box;
    }
}

void box_free_tree(struct box *b) {
    if (!b) return;

    struct box *child = b->children;
    while (child) {
        struct box *next = child->next;
        box_free_tree(child);
        child = next;
    }

    if (b->text) {
        free(b->text);
        b->text = NULL;
    }

    free(b);
}
