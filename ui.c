#include "ui.h"
#include "fb.h"

#define UI_DIRTY_CAPACITY 16u
#define UI_WM_WINDOW_COUNT 2u
#define UI_WM_MIN_WIDTH 220u
#define UI_WM_MIN_HEIGHT 120u
#define UI_WM_TITLE_HEIGHT 22u

static ui_dirty_rect_t g_dirty_queue[UI_DIRTY_CAPACITY];
static uint16_t g_dirty_count = 0;
static uint16_t g_last_dirty_count = 0;

#define CURSOR_W 10u
#define CURSOR_H 16u

typedef struct ui_palette {
    uint32_t bg_dark;
    uint32_t panel;
    uint32_t accent;
    uint32_t accent_hover;
    uint32_t accent_pressed;
    uint32_t window;
    uint32_t border;
    uint32_t text_light;
    uint32_t cursor;
} ui_palette_t;

typedef enum ui_theme_kind {
    UI_THEME_NIGHT = 0,
    UI_THEME_PAPER,
    UI_THEME_COUNT
} ui_theme_kind_t;

static const ui_palette_t g_palettes[UI_THEME_COUNT] = {
    [UI_THEME_NIGHT] = {
        .bg_dark = 0x0E1116u,
        .panel = 0x1F2630u,
        .accent = 0x58A6FFu,
        .accent_hover = 0x79C0FFu,
        .accent_pressed = 0x1F6FEBu,
        .window = 0xE6EDF3u,
        .border = 0x2B3442u,
        .text_light = 0xE6EDF3u,
        .cursor = 0xFFFFFFu
    },
    [UI_THEME_PAPER] = {
        .bg_dark = 0xF2EEE6u,
        .panel = 0xD8CBB7u,
        .accent = 0xB97A57u,
        .accent_hover = 0xC89173u,
        .accent_pressed = 0x8D5E45u,
        .window = 0xFFF9F0u,
        .border = 0x8B7C68u,
        .text_light = 0x2A2014u,
        .cursor = 0x2A2014u
    }
};

typedef struct ui_cursor_state {
    uint16_t x;
    uint16_t y;
    uint8_t buttons;
    uint8_t visible;
    uint32_t saved[CURSOR_W * CURSOR_H];
} ui_cursor_state_t;

static ui_cursor_state_t g_cursor = {0, 0, 0, 0, {0}};

typedef struct ui_interaction_state {
    uint8_t panel_hover;
    uint8_t panel_pressed;
} ui_interaction_state_t;

static ui_interaction_state_t g_ui_state = {0, 0};
static ui_layout_t g_layout = {0};

typedef struct ui_runtime_stats_state {
    uint8_t idt_ready;
    uint8_t virtio_detected;
    uint8_t virtio_active;
    uint32_t heartbeat;
    uint32_t keyboard_irq;
    uint32_t mouse_irq;
    uint16_t dirty_count;
    uint8_t pmm_ready;
    uint8_t storage_ready;
    uint8_t storage_last_read_ok;
    uint8_t boot_signature_valid;
    uint64_t heap_used;
    uint64_t heap_free;
    uint64_t pmm_total_pages;
    uint64_t pmm_free_pages;
    uint32_t storage_last_lba;
} ui_runtime_stats_state_t;

typedef struct ui_window {
    ui_dirty_rect_t rect;
    const char* title;
} ui_window_t;

typedef struct ui_window_manager_state {
    ui_window_t windows[UI_WM_WINDOW_COUNT];
    uint8_t order[UI_WM_WINDOW_COUNT];
    uint8_t focused_slot;
    uint8_t dragging;
    uint8_t drag_slot;
    uint16_t drag_offset_x;
    uint16_t drag_offset_y;
} ui_window_manager_state_t;

static ui_runtime_stats_state_t g_runtime_stats = {0};
static ui_theme_kind_t g_active_theme = UI_THEME_NIGHT;
static ui_window_manager_state_t g_wm = {0};

static const ui_palette_t* active_palette(void) {
    return &g_palettes[g_active_theme];
}

static const char* theme_name(void) {
    return (g_active_theme == UI_THEME_NIGHT) ? "NIGHT" : "PAPER";
}

static void cycle_theme(void) {
    g_active_theme = (ui_theme_kind_t)((g_active_theme + 1) % UI_THEME_COUNT);
}

static inline uint16_t clamp_u16(uint16_t value, uint16_t max) {
    return (value > max) ? max : value;
}

static inline uint16_t rect_end(uint16_t start, uint16_t size) {
    return (uint16_t)(start + size);
}

static uint8_t rect_intersects(const ui_dirty_rect_t* a, const ui_dirty_rect_t* b) {
    uint16_t a_end_x = rect_end(a->x, a->w);
    uint16_t a_end_y = rect_end(a->y, a->h);
    uint16_t b_end_x = rect_end(b->x, b->w);
    uint16_t b_end_y = rect_end(b->y, b->h);

    return !(a_end_x <= b->x || b_end_x <= a->x || a_end_y <= b->y || b_end_y <= a->y);
}

static ui_dirty_rect_t rect_union(const ui_dirty_rect_t* a, const ui_dirty_rect_t* b) {
    ui_dirty_rect_t out;
    uint16_t a_end_x = rect_end(a->x, a->w);
    uint16_t a_end_y = rect_end(a->y, a->h);
    uint16_t b_end_x = rect_end(b->x, b->w);
    uint16_t b_end_y = rect_end(b->y, b->h);

    out.x = (a->x < b->x) ? a->x : b->x;
    out.y = (a->y < b->y) ? a->y : b->y;
    out.w = (uint16_t)(((a_end_x > b_end_x) ? a_end_x : b_end_x) - out.x);
    out.h = (uint16_t)(((a_end_y > b_end_y) ? a_end_y : b_end_y) - out.y);
    return out;
}

static uint8_t wm_is_focused_slot(uint8_t slot) {
    return (g_wm.focused_slot == slot);
}

static void wm_raise_slot(uint8_t slot) {
    uint8_t index = 0;
    while (index < UI_WM_WINDOW_COUNT && g_wm.order[index] != slot) {
        index++;
    }

    if (index >= UI_WM_WINDOW_COUNT || index == (UI_WM_WINDOW_COUNT - 1u)) {
        g_wm.focused_slot = slot;
        return;
    }

    while (index + 1u < UI_WM_WINDOW_COUNT) {
        g_wm.order[index] = g_wm.order[index + 1u];
        index++;
    }

    g_wm.order[UI_WM_WINDOW_COUNT - 1u] = slot;
    g_wm.focused_slot = slot;
}

static uint8_t point_inside_rect(const ui_dirty_rect_t* rect, uint16_t x, uint16_t y) {
    uint16_t end_x = (uint16_t)(rect->x + rect->w);
    uint16_t end_y = (uint16_t)(rect->y + rect->h);
    return (x >= rect->x && x < end_x && y >= rect->y && y < end_y);
}

static uint8_t wm_pick_window_slot(uint16_t x, uint16_t y, uint8_t* out_slot) {
    for (int8_t i = (int8_t)(UI_WM_WINDOW_COUNT - 1u); i >= 0; i--) {
        uint8_t slot = g_wm.order[(uint8_t)i];
        if (point_inside_rect(&g_wm.windows[slot].rect, x, y)) {
            *out_slot = slot;
            return 1;
        }
    }

    return 0;
}

static uint8_t wm_pick_titlebar_slot(uint16_t x, uint16_t y, uint8_t* out_slot) {
    uint8_t slot = 0;
    if (!wm_pick_window_slot(x, y, &slot)) {
        return 0;
    }

    ui_dirty_rect_t title_rect = g_wm.windows[slot].rect;
    title_rect.h = UI_WM_TITLE_HEIGHT;

    if (!point_inside_rect(&title_rect, x, y)) {
        return 0;
    }

    *out_slot = slot;
    return 1;
}

void ui_layout_compute(video_info_t* info, ui_layout_t* layout) {
    layout->panel.x = 0;
    layout->panel.y = 0;
    layout->panel.w = info->width;
    layout->panel.h = 34;

    layout->panel_button.x = 12;
    layout->panel_button.y = 8;
    layout->panel_button.w = 136;
    layout->panel_button.h = 18;

    layout->content.x = 0;
    layout->content.y = layout->panel.h;
    layout->content.w = info->width;
    layout->content.h = (info->height > 48) ? (uint16_t)(info->height - 48 - layout->content.y) : 0;

    layout->status_window.w = (uint16_t)(info->width / 3);
    layout->status_window.h = (uint16_t)(info->height / 3);
    layout->status_window.x = (uint16_t)(info->width - layout->status_window.w - 24);
    layout->status_window.y = (uint16_t)(layout->panel.h + 22);

    layout->status_window_title.x = layout->status_window.x;
    layout->status_window_title.y = layout->status_window.y;
    layout->status_window_title.w = layout->status_window.w;
    layout->status_window_title.h = UI_WM_TITLE_HEIGHT;

    layout->footer.x = 0;
    layout->footer.y = (info->height > 48) ? (uint16_t)(info->height - 48) : 0;
    layout->footer.w = info->width;
    layout->footer.h = 48;

    layout->footer_status_line.x = 0;
    layout->footer_status_line.y = (info->height > 44) ? (uint16_t)(info->height - 44) : 0;
    layout->footer_status_line.w = info->width;
    layout->footer_status_line.h = 24;

    layout->footer_runtime_line.x = 0;
    layout->footer_runtime_line.y = (info->height > 24) ? (uint16_t)(info->height - 24) : 0;
    layout->footer_runtime_line.w = info->width;
    layout->footer_runtime_line.h = 24;
}

static void wm_sync_layout(video_info_t* info, ui_layout_t* layout) {
    uint16_t content_height = layout->content.h;
    uint16_t content_width = layout->content.w;

    uint16_t min_w = (content_width < UI_WM_MIN_WIDTH) ? content_width : UI_WM_MIN_WIDTH;
    uint16_t min_h = (content_height < UI_WM_MIN_HEIGHT) ? content_height : UI_WM_MIN_HEIGHT;

    uint16_t status_w = (layout->status_window.w < min_w) ? min_w : layout->status_window.w;
    uint16_t status_h = (layout->status_window.h < min_h) ? min_h : layout->status_window.h;

    uint16_t log_w = (uint16_t)(status_w + 54);
    if (log_w > content_width) {
        log_w = content_width;
    }

    uint16_t log_h = (uint16_t)(status_h - 34);
    if (log_h < min_h) {
        log_h = min_h;
    }

    g_wm.windows[0].title = "SYSTEM STATUS";
    g_wm.windows[0].rect.x = layout->status_window.x;
    g_wm.windows[0].rect.y = layout->status_window.y;
    g_wm.windows[0].rect.w = status_w;
    g_wm.windows[0].rect.h = status_h;

    g_wm.windows[1].title = "EVENT TRACE";
    g_wm.windows[1].rect.x = (uint16_t)(layout->content.x + 72);
    g_wm.windows[1].rect.y = (uint16_t)(layout->content.y + 84);
    g_wm.windows[1].rect.w = log_w;
    g_wm.windows[1].rect.h = log_h;

    for (uint8_t i = 0; i < UI_WM_WINDOW_COUNT; i++) {
        ui_dirty_rect_t* rect = &g_wm.windows[i].rect;

        if (rect->w > content_width) {
            rect->w = content_width;
        }
        if (rect->h > content_height) {
            rect->h = content_height;
        }

        uint16_t max_x = (uint16_t)(layout->content.x + content_width - rect->w);
        uint16_t max_y = (uint16_t)(layout->content.y + content_height - rect->h);

        if (rect->x < layout->content.x) {
            rect->x = layout->content.x;
        }
        if (rect->y < layout->content.y) {
            rect->y = layout->content.y;
        }

        rect->x = clamp_u16(rect->x, max_x);
        rect->y = clamp_u16(rect->y, max_y);
    }

    g_wm.order[0] = 1;
    g_wm.order[1] = 0;
    g_wm.focused_slot = 0;
    g_wm.dragging = 0;
    g_wm.drag_slot = 0;
    g_wm.drag_offset_x = 0;
    g_wm.drag_offset_y = 0;

    layout->status_window = g_wm.windows[g_wm.focused_slot].rect;
    layout->status_window_title = layout->status_window;
    layout->status_window_title.h = UI_WM_TITLE_HEIGHT;

    (void)info;
}

static void draw_background_band(video_info_t* info, const ui_dirty_rect_t* clip) {
    const ui_palette_t* palette = active_palette();
    // Однотонный фон: без полос по всей высоте экрана.
    fb_rect(info, clip->x, clip->y, clip->w, clip->h, palette->bg_dark);
}

static void draw_top_panel(video_info_t* info, const ui_dirty_rect_t* clip, const ui_layout_t* layout) {
    (void)clip;
    const ui_palette_t* palette = active_palette();
    uint32_t panel_button_color = palette->accent;

    if (g_ui_state.panel_pressed) {
        panel_button_color = palette->accent_pressed;
    } else if (g_ui_state.panel_hover) {
        panel_button_color = palette->accent_hover;
    }

    fb_rect(info, layout->panel.x, layout->panel.y, layout->panel.w, layout->panel.h, palette->panel);
    fb_rect(
        info,
        layout->panel_button.x,
        layout->panel_button.y,
        layout->panel_button.w,
        layout->panel_button.h,
        panel_button_color
    );
    fb_draw_text(info, 18, 13, "WOOS 1.25.3", palette->text_light, panel_button_color);
    fb_draw_text(info, (uint16_t)(info->width - 166), 13, "THEME:", palette->text_light, palette->panel);
    fb_draw_text(info, (uint16_t)(info->width - 108), 13, theme_name(), panel_button_color, palette->panel);
    fb_draw_text(info, (uint16_t)(info->width - 50), 13, "DEV", palette->text_light, palette->panel);
}

static void draw_window_contents(video_info_t* info, uint8_t slot, const ui_window_t* window) {
    const ui_palette_t* palette = active_palette();

    if (slot == 0) {
        const char* idt_line = g_runtime_stats.idt_ready ? "IDT: READY" : "IDT: BAD";
        const char* storage_line = g_runtime_stats.storage_ready ? "STORAGE: READY" : "STORAGE: WAIT";
        const char* irq_line = g_runtime_stats.mouse_irq > 0 ? "MOUSE IRQ: ACTIVE" : "MOUSE IRQ: POLL";

        fb_draw_text(info, (uint16_t)(window->rect.x + 8), (uint16_t)(window->rect.y + 34), idt_line, palette->border, palette->window);
        fb_draw_text(info, (uint16_t)(window->rect.x + 8), (uint16_t)(window->rect.y + 50), storage_line, palette->border, palette->window);
        fb_draw_text(info, (uint16_t)(window->rect.x + 8), (uint16_t)(window->rect.y + 66), irq_line, palette->border, palette->window);
    } else {
        const char* drag_line = g_wm.dragging ? "DRAG: ACTIVE" : "DRAG: READY";
        const char* focus_line = wm_is_focused_slot(slot) ? "FOCUS: THIS WINDOW" : "FOCUS: BACKGROUND";

        fb_draw_text(info, (uint16_t)(window->rect.x + 8), (uint16_t)(window->rect.y + 34), "MOUSE TOOLS:", palette->border, palette->window);
        fb_draw_text(info, (uint16_t)(window->rect.x + 8), (uint16_t)(window->rect.y + 50), "- TITLE CLICK = FOCUS", palette->border, palette->window);
        fb_draw_text(info, (uint16_t)(window->rect.x + 8), (uint16_t)(window->rect.y + 66), "- TITLE DRAG = MOVE", palette->border, palette->window);
        fb_draw_text(info, (uint16_t)(window->rect.x + 8), (uint16_t)(window->rect.y + 82), drag_line, palette->border, palette->window);
        fb_draw_text(info, (uint16_t)(window->rect.x + 8), (uint16_t)(window->rect.y + 98), focus_line, palette->border, palette->window);
    }
}

static void draw_window_manager(video_info_t* info, const ui_dirty_rect_t* clip) {
    (void)clip;
    const ui_palette_t* palette = active_palette();

    for (uint8_t i = 0; i < UI_WM_WINDOW_COUNT; i++) {
        uint8_t slot = g_wm.order[i];
        const ui_window_t* window = &g_wm.windows[slot];
        uint32_t title_color = wm_is_focused_slot(slot) ? palette->accent : palette->panel;
        uint32_t title_text_color = wm_is_focused_slot(slot) ? palette->text_light : palette->cursor;

        fb_rect(info, window->rect.x, window->rect.y, window->rect.w, window->rect.h, palette->window);
        fb_frame(info, window->rect.x, window->rect.y, window->rect.w, window->rect.h, 2, palette->border);
        fb_rect(info, window->rect.x, window->rect.y, window->rect.w, UI_WM_TITLE_HEIGHT, title_color);
        fb_draw_text(info, (uint16_t)(window->rect.x + 8), (uint16_t)(window->rect.y + 7), window->title, title_text_color, title_color);
        draw_window_contents(info, slot, window);
    }
}

static void write_decimal_padded(char* buffer, uint16_t last_index, uint64_t value, uint16_t digits) {
    for (uint16_t i = 0; i < digits; i++) {
        buffer[last_index - i] = (char)('0' + (value % 10u));
        value /= 10u;
    }
}

static void draw_footer(video_info_t* info, const ui_dirty_rect_t* clip, const ui_layout_t* layout) {
    (void)clip;
    const ui_palette_t* palette = active_palette();
    const char* status = "EVENTS: MOVE CURSOR OVER PANEL";

    if (g_wm.dragging) {
        status = "EVENTS: WINDOW DRAG ACTIVE";
    } else if (g_ui_state.panel_pressed) {
        status = "EVENTS: PANEL CLICK HANDLED";
    } else if (g_ui_state.panel_hover) {
        status = "EVENTS: PANEL HOVER ACTIVE";
    }

    char heartbeat_text[17] = "HEARTBEAT: 000000";
    write_decimal_padded(heartbeat_text, 15, g_runtime_stats.heartbeat, 6);

    char irq_text[19] = "IRQ K:0000 M:0000";
    write_decimal_padded(irq_text, 10, g_runtime_stats.keyboard_irq % 10000u, 4);
    write_decimal_padded(irq_text, 17, g_runtime_stats.mouse_irq % 10000u, 4);

    char dirty_text[10] = "DIRTY: 00";
    write_decimal_padded(dirty_text, 8, g_runtime_stats.dirty_count % 100u, 2);

    char heap_text[24] = "HEAP U:00000 F:00000";
    write_decimal_padded(heap_text, 12, g_runtime_stats.heap_used % 100000u, 5);
    write_decimal_padded(heap_text, 20, g_runtime_stats.heap_free % 100000u, 5);

    char pmm_text[24] = "PMM T:00000 F:00000";
    write_decimal_padded(pmm_text, 11, g_runtime_stats.pmm_total_pages % 100000u, 5);
    write_decimal_padded(pmm_text, 19, g_runtime_stats.pmm_free_pages % 100000u, 5);

    char storage_lba_text[19] = "DISK LBA: 000000";
    write_decimal_padded(storage_lba_text, 15, g_runtime_stats.storage_last_lba % 1000000u, 6);

    const char* video_text = "VIDEO: VBE";
    if (g_runtime_stats.virtio_active) {
        video_text = "VIDEO: VIRTIO";
    } else if (g_runtime_stats.virtio_detected) {
        // Устройство virtio-vga/virtio-gpu найдено,
        // но активным остаётся безопасный VBE fallback.
        video_text = "VIDEO: VBE (VIRTIO PCI)";
    }
    const char* pmm_status = g_runtime_stats.pmm_ready ? "PMM READY" : "PMM WAIT";
    const char* storage_status = g_runtime_stats.storage_ready
        ? (g_runtime_stats.boot_signature_valid ? "DISK READY SIG OK" : "DISK READY SIG BAD")
        : (g_runtime_stats.storage_last_read_ok ? "DISK WAIT" : "DISK READ FAIL");

    fb_draw_text(info, 16, (uint16_t)(layout->footer_runtime_line.y - 8), status, palette->text_light, palette->bg_dark);
    fb_draw_text(info, 16, (uint16_t)(layout->footer_runtime_line.y + 4), dirty_text, palette->text_light, palette->bg_dark);
    fb_draw_text(info, 104, (uint16_t)(layout->footer_runtime_line.y + 4), heap_text, palette->text_light, palette->bg_dark);
    fb_draw_text(info, 292, (uint16_t)(layout->footer_runtime_line.y + 4), pmm_text, palette->text_light, palette->bg_dark);
    fb_draw_text(info, (uint16_t)(info->width - 404), (uint16_t)(layout->footer_runtime_line.y - 8), video_text, palette->text_light, palette->bg_dark);
    fb_draw_text(info, (uint16_t)(info->width - 300), (uint16_t)(layout->footer_runtime_line.y - 8), pmm_status, palette->text_light, palette->bg_dark);
    fb_draw_text(info, (uint16_t)(info->width - 300), (uint16_t)(layout->footer_runtime_line.y + 4), irq_text, palette->text_light, palette->bg_dark);
    fb_draw_text(info, (uint16_t)(info->width - 142), (uint16_t)(layout->footer_runtime_line.y + 4), heartbeat_text, palette->text_light, palette->bg_dark);
    fb_draw_text(info, 16, (uint16_t)(layout->footer_status_line.y + 4), storage_status, palette->text_light, palette->bg_dark);
    fb_draw_text(info, 176, (uint16_t)(layout->footer_status_line.y + 4), storage_lba_text, palette->text_light, palette->bg_dark);
    fb_draw_text(info, (uint16_t)(info->width - 120), (uint16_t)(layout->footer_status_line.y + 4), theme_name(), palette->text_light, palette->bg_dark);
}

static void ui_draw_region(video_info_t* info, const ui_dirty_rect_t* clip) {
    draw_background_band(info, clip);
    draw_top_panel(info, clip, &g_layout);
    draw_window_manager(info, clip);
    draw_footer(info, clip, &g_layout);
}

static void cursor_restore_underlay(video_info_t* info) {
    if (!g_cursor.visible) {
        return;
    }

    for (uint16_t py = 0; py < CURSOR_H; py++) {
        uint16_t y = (uint16_t)(g_cursor.y + py);
        if (y >= info->height) {
            break;
        }

        for (uint16_t px = 0; px < CURSOR_W; px++) {
            uint16_t x = (uint16_t)(g_cursor.x + px);
            if (x >= info->width) {
                break;
            }

            fb_writepixel(info, x, y, g_cursor.saved[(py * CURSOR_W) + px]);
        }
    }

}

static void cursor_draw(video_info_t* info) {
    const ui_palette_t* palette = active_palette();
    for (uint16_t py = 0; py < CURSOR_H; py++) {
        uint16_t y = (uint16_t)(g_cursor.y + py);
        if (y >= info->height) {
            break;
        }

        for (uint16_t px = 0; px < CURSOR_W; px++) {
            uint16_t x = (uint16_t)(g_cursor.x + px);
            uint32_t* slot = &g_cursor.saved[(py * CURSOR_W) + px];

            if (x >= info->width) {
                *slot = 0;
                continue;
            }

            *slot = fb_readpixel(info, x, y);

            // Простая L-образная форма курсора без альфа-смешивания.
            uint8_t on = (px == 0) || (py == 0) || (px == py && px < 8);
            if (on) {
                fb_writepixel(info, x, y, palette->cursor);
            }
        }
    }

    g_cursor.visible = 1;
}

static void mark_cursor_delta_dirty(uint16_t old_x, uint16_t old_y, uint8_t had_visible) {
    if (!had_visible) {
        ui_mark_dirty(g_cursor.x, g_cursor.y, CURSOR_W, CURSOR_H);
        return;
    }

    ui_dirty_rect_t old_rect = {old_x, old_y, CURSOR_W, CURSOR_H};
    ui_dirty_rect_t new_rect = {g_cursor.x, g_cursor.y, CURSOR_W, CURSOR_H};
    ui_dirty_rect_t union_rect = rect_union(&old_rect, &new_rect);
    // Не рисуем курсор прямо в input-path: только помечаем dirty-область.
    // Реальная отрисовка идёт в ui_render_dirty(), чтобы избежать мерцания
    // из-за двойного restore/draw в пределах одного кадра.
    ui_mark_dirty(union_rect.x, union_rect.y, union_rect.w, union_rect.h);
}

void ui_set_cursor(video_info_t* info, uint16_t x, uint16_t y, uint8_t buttons) {
    uint16_t max_x = (info->width > CURSOR_W) ? (uint16_t)(info->width - CURSOR_W) : 0;
    uint16_t max_y = (info->height > CURSOR_H) ? (uint16_t)(info->height - CURSOR_H) : 0;

    if (x > max_x) {
        x = max_x;
    }

    if (y > max_y) {
        y = max_y;
    }

    uint16_t old_x = g_cursor.x;
    uint16_t old_y = g_cursor.y;
    uint8_t had_visible = g_cursor.visible;

    if (had_visible && g_cursor.x == x && g_cursor.y == y && g_cursor.buttons == buttons) {
        return;
    }

    g_cursor.x = x;
    g_cursor.y = y;
    g_cursor.buttons = buttons;
    g_cursor.visible = 1;
    mark_cursor_delta_dirty(old_x, old_y, had_visible);
}

static uint8_t dirty_intersects_rect(const ui_dirty_rect_t* rect) {
    for (uint16_t i = 0; i < g_dirty_count; i++) {
        if (rect_intersects(&g_dirty_queue[i], rect)) {
            return 1;
        }
    }

    return 0;
}

void ui_mark_dirty(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if (w == 0 || h == 0) {
        return;
    }

    ui_dirty_rect_t next = {x, y, w, h};

    for (uint16_t i = 0; i < g_dirty_count; i++) {
        if (rect_intersects(&g_dirty_queue[i], &next)) {
            g_dirty_queue[i] = rect_union(&g_dirty_queue[i], &next);
            return;
        }
    }

    if (g_dirty_count < UI_DIRTY_CAPACITY) {
        g_dirty_queue[g_dirty_count++] = next;
        return;
    }

    g_dirty_queue[0] = rect_union(&g_dirty_queue[0], &next);
}

void ui_render_dirty(video_info_t* info) {
    if (g_dirty_count == 0) {
        g_last_dirty_count = 0;
        return;
    }

    ui_dirty_rect_t present_queue[UI_DIRTY_CAPACITY];
    uint16_t present_count = 0;

    uint8_t should_refresh_cursor = 0;
    ui_dirty_rect_t cursor_rect = {g_cursor.x, g_cursor.y, CURSOR_W, CURSOR_H};
    if (g_cursor.visible) {
        // Трогаем курсор только когда dirty-область реально пересекается
        // с его прямоугольником: это убирает лишние restore/draw/present
        // и заметно снижает визуальное мерцание.
        should_refresh_cursor = dirty_intersects_rect(&cursor_rect);
        if (should_refresh_cursor) {
            cursor_restore_underlay(info);
        }
    }

    g_last_dirty_count = g_dirty_count;

    for (uint16_t i = 0; i < g_dirty_count; i++) {
        ui_dirty_rect_t clip = g_dirty_queue[i];

        clip.x = clamp_u16(clip.x, info->width);
        clip.y = clamp_u16(clip.y, info->height);

        uint16_t end_x = clamp_u16(rect_end(clip.x, clip.w), info->width);
        uint16_t end_y = clamp_u16(rect_end(clip.y, clip.h), info->height);

        clip.w = (uint16_t)(end_x - clip.x);
        clip.h = (uint16_t)(end_y - clip.y);

        if (clip.w == 0 || clip.h == 0) {
            continue;
        }

        ui_draw_region(info, &clip);
        if (present_count < UI_DIRTY_CAPACITY) {
            present_queue[present_count++] = clip;
        }
    }

    if (g_cursor.visible && should_refresh_cursor) {
        cursor_draw(info);
    }

    uint8_t cursor_presented = 0;
    for (uint16_t i = 0; i < present_count; i++) {
        fb_present_rect(info, present_queue[i].x, present_queue[i].y, present_queue[i].w, present_queue[i].h);
        if (g_cursor.visible && should_refresh_cursor && rect_intersects(&present_queue[i], &cursor_rect)) {
            cursor_presented = 1;
        }
    }

    g_dirty_count = 0;

    if (g_cursor.visible && should_refresh_cursor && !cursor_presented) {
        fb_present_rect(info, g_cursor.x, g_cursor.y, CURSOR_W, CURSOR_H);
    }
}

uint16_t ui_last_dirty_count(void) {
    return g_last_dirty_count;
}

void ui_render_desktop(video_info_t* info) {
    ui_layout_compute(info, &g_layout);
    wm_sync_layout(info, &g_layout);
    ui_mark_dirty(0, 0, info->width, info->height);
    ui_render_dirty(info);
    ui_set_cursor(info, (uint16_t)(info->width / 2), (uint16_t)(info->height / 2), 0);
    ui_render_dirty(info);
}

static uint8_t point_inside_panel_button(uint16_t x, uint16_t y) {
    uint16_t end_x = (uint16_t)(g_layout.panel_button.x + g_layout.panel_button.w);
    uint16_t end_y = (uint16_t)(g_layout.panel_button.y + g_layout.panel_button.h);
    return (x >= g_layout.panel_button.x && x < end_x && y >= g_layout.panel_button.y && y < end_y);
}

static void wm_drag_to(video_info_t* info, uint16_t cursor_x, uint16_t cursor_y) {
    if (!g_wm.dragging) {
        return;
    }

    ui_window_t* window = &g_wm.windows[g_wm.drag_slot];
    ui_dirty_rect_t old_rect = window->rect;

    uint16_t content_x = g_layout.content.x;
    uint16_t content_y = g_layout.content.y;
    uint16_t content_max_x = (uint16_t)(g_layout.content.x + g_layout.content.w - window->rect.w);
    uint16_t content_max_y = (uint16_t)(g_layout.content.y + g_layout.content.h - window->rect.h);

    uint16_t next_x = (cursor_x > g_wm.drag_offset_x) ? (uint16_t)(cursor_x - g_wm.drag_offset_x) : content_x;
    uint16_t next_y = (cursor_y > g_wm.drag_offset_y) ? (uint16_t)(cursor_y - g_wm.drag_offset_y) : content_y;

    if (next_x < content_x) {
        next_x = content_x;
    }
    if (next_y < content_y) {
        next_y = content_y;
    }

    next_x = clamp_u16(next_x, content_max_x);
    next_y = clamp_u16(next_y, content_max_y);

    if (window->rect.x == next_x && window->rect.y == next_y) {
        return;
    }

    window->rect.x = next_x;
    window->rect.y = next_y;

    ui_mark_dirty(old_rect.x, old_rect.y, old_rect.w, old_rect.h);
    ui_mark_dirty(window->rect.x, window->rect.y, window->rect.w, window->rect.h);
    ui_mark_dirty(g_layout.footer_runtime_line.x, g_layout.footer_runtime_line.y, g_layout.footer_runtime_line.w, g_layout.footer_runtime_line.h);

    (void)info;
}

void ui_handle_mouse_move(video_info_t* info, uint16_t x, uint16_t y, uint8_t buttons) {
    uint8_t next_hover = point_inside_panel_button(x, y);

    if (next_hover != g_ui_state.panel_hover) {
        g_ui_state.panel_hover = next_hover;
        ui_mark_dirty(g_layout.panel.x, g_layout.panel.y, g_layout.panel.w, g_layout.panel.h);
        ui_mark_dirty(
            g_layout.footer_runtime_line.x,
            g_layout.footer_runtime_line.y,
            g_layout.footer_runtime_line.w,
            g_layout.footer_runtime_line.h
        );
    }

    wm_drag_to(info, x, y);
    ui_set_cursor(info, x, y, buttons);
}

void ui_handle_mouse_button(video_info_t* info, uint8_t buttons) {
    uint8_t left_pressed = (uint8_t)(buttons & 0x1u);
    uint8_t next_pressed = (uint8_t)(left_pressed && g_ui_state.panel_hover);
    uint8_t was_pressed = g_ui_state.panel_pressed;
    uint8_t was_dragging = g_wm.dragging;

    if (!left_pressed) {
        g_wm.dragging = 0;
    }

    if (left_pressed && !g_wm.dragging) {
        uint8_t slot = 0;
        if (wm_pick_titlebar_slot(g_cursor.x, g_cursor.y, &slot)) {
            ui_dirty_rect_t old_rect = g_wm.windows[slot].rect;
            wm_raise_slot(slot);
            g_wm.dragging = 1;
            g_wm.drag_slot = slot;
            g_wm.drag_offset_x = (uint16_t)(g_cursor.x - g_wm.windows[slot].rect.x);
            g_wm.drag_offset_y = (uint16_t)(g_cursor.y - g_wm.windows[slot].rect.y);
            ui_mark_dirty(old_rect.x, old_rect.y, old_rect.w, old_rect.h);
            ui_mark_dirty(g_wm.windows[slot].rect.x, g_wm.windows[slot].rect.y, g_wm.windows[slot].rect.w, g_wm.windows[slot].rect.h);
            ui_mark_dirty(g_layout.footer_runtime_line.x, g_layout.footer_runtime_line.y, g_layout.footer_runtime_line.w, g_layout.footer_runtime_line.h);
        }
    }

    if (next_pressed != g_ui_state.panel_pressed) {
        g_ui_state.panel_pressed = next_pressed;
        if (next_pressed && !was_pressed) {
            // Сменяем палитру только по фронту клика, иначе удержание кнопки
            // приводило бы к непрерывному переключению тем в каждом кадре.
            cycle_theme();
            // Тема влияет на весь desktop (включая "обои"), поэтому локального
            // dirty-marking панели/footer недостаточно — нужен полный redraw.
            ui_mark_dirty(0, 0, info->width, info->height);
        }
        ui_mark_dirty(g_layout.panel.x, g_layout.panel.y, g_layout.panel.w, g_layout.panel.h);
        ui_mark_dirty(
            g_layout.footer_runtime_line.x,
            g_layout.footer_runtime_line.y,
            g_layout.footer_runtime_line.w,
            g_layout.footer_runtime_line.h
        );
        ui_mark_dirty(
            g_layout.footer_status_line.x,
            g_layout.footer_status_line.y,
            g_layout.footer_status_line.w,
            g_layout.footer_status_line.h
        );
    }

    if (was_dragging != g_wm.dragging) {
        ui_mark_dirty(g_layout.footer_runtime_line.x, g_layout.footer_runtime_line.y, g_layout.footer_runtime_line.w, g_layout.footer_runtime_line.h);
    }
}

void ui_set_kernel_health(video_info_t* info, uint8_t idt_ready, uint32_t heartbeat) {
    if (g_runtime_stats.idt_ready == idt_ready && g_runtime_stats.heartbeat == heartbeat) {
        return;
    }

    g_runtime_stats.idt_ready = idt_ready;
    g_runtime_stats.heartbeat = heartbeat;

    ui_mark_dirty(g_layout.content.x, g_layout.content.y, g_layout.content.w, g_layout.content.h);
    ui_mark_dirty((uint16_t)(info->width - 150), g_layout.footer_runtime_line.y, 150, g_layout.footer_runtime_line.h);
}

void ui_set_irq_stats(video_info_t* info, uint32_t keyboard_irq, uint32_t mouse_irq) {
    if (g_runtime_stats.keyboard_irq == keyboard_irq && g_runtime_stats.mouse_irq == mouse_irq) {
        return;
    }

    g_runtime_stats.keyboard_irq = keyboard_irq;
    g_runtime_stats.mouse_irq = mouse_irq;

    ui_mark_dirty((uint16_t)(info->width - 310), g_layout.footer_runtime_line.y, 170, g_layout.footer_runtime_line.h);
    ui_mark_dirty(g_layout.content.x, g_layout.content.y, g_layout.content.w, g_layout.content.h);
}

void ui_set_memory_stats(video_info_t* info, uint8_t pmm_ready, uint64_t total_pages, uint64_t free_pages) {
    (void)info;
    if (g_runtime_stats.pmm_ready == pmm_ready
        && g_runtime_stats.pmm_total_pages == total_pages
        && g_runtime_stats.pmm_free_pages == free_pages) {
        return;
    }

    g_runtime_stats.pmm_ready = pmm_ready;
    g_runtime_stats.pmm_total_pages = total_pages;
    g_runtime_stats.pmm_free_pages = free_pages;

    ui_mark_dirty(g_layout.footer.x, g_layout.footer.y, g_layout.footer.w, g_layout.footer.h);
}

void ui_set_runtime_stats(
    video_info_t* info,
    uint16_t dirty_count,
    uint64_t heap_used,
    uint64_t heap_free,
    uint8_t virtio_detected,
    uint8_t virtio_active
) {
    (void)info;
    if (g_runtime_stats.dirty_count == dirty_count
        && g_runtime_stats.heap_used == heap_used
        && g_runtime_stats.heap_free == heap_free
        && g_runtime_stats.virtio_detected == virtio_detected
        && g_runtime_stats.virtio_active == virtio_active) {
        return;
    }

    g_runtime_stats.dirty_count = dirty_count;
    g_runtime_stats.heap_used = heap_used;
    g_runtime_stats.heap_free = heap_free;
    g_runtime_stats.virtio_detected = virtio_detected;
    g_runtime_stats.virtio_active = virtio_active;

    ui_mark_dirty(g_layout.footer.x, g_layout.footer.y, g_layout.footer.w, g_layout.footer.h);
}

void ui_set_storage_stats(video_info_t* info, uint8_t storage_ready, uint8_t last_read_ok, uint32_t last_lba, uint8_t boot_signature_valid) {
    (void)info;
    if (g_runtime_stats.storage_ready == storage_ready
        && g_runtime_stats.storage_last_read_ok == last_read_ok
        && g_runtime_stats.storage_last_lba == last_lba
        && g_runtime_stats.boot_signature_valid == boot_signature_valid) {
        return;
    }

    g_runtime_stats.storage_ready = storage_ready;
    g_runtime_stats.storage_last_read_ok = last_read_ok;
    g_runtime_stats.storage_last_lba = last_lba;
    g_runtime_stats.boot_signature_valid = boot_signature_valid;

    ui_mark_dirty(g_layout.footer.x, g_layout.footer.y, g_layout.footer.w, g_layout.footer.h);
    ui_mark_dirty(g_layout.content.x, g_layout.content.y, g_layout.content.w, g_layout.content.h);
}
