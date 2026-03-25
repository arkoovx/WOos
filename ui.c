#include "ui.h"
#include "fb.h"

#define COLOR_BG_DARK    0x0E1116u
#define COLOR_BG_LIGHT   0x141A22u
#define COLOR_PANEL      0x1F2630u
#define COLOR_ACCENT     0x58A6FFu
#define COLOR_ACCENT_HOVER 0x79C0FFu
#define COLOR_ACCENT_PRESSED 0x1F6FEBu
#define COLOR_WINDOW     0xE6EDF3u
#define COLOR_BORDER     0x2B3442u
#define COLOR_TEXT_LIGHT 0xE6EDF3u

#define UI_DIRTY_CAPACITY 16u

static ui_dirty_rect_t g_dirty_queue[UI_DIRTY_CAPACITY];
static uint16_t g_dirty_count = 0;
static uint16_t g_last_dirty_count = 0;

#define CURSOR_W 10u
#define CURSOR_H 16u
#define COLOR_CURSOR 0xFFFFFFu

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

static ui_runtime_stats_state_t g_runtime_stats = {0};

#define PANEL_BTN_X 12u
#define PANEL_BTN_Y 8u
#define PANEL_BTN_W 136u
#define PANEL_BTN_H 18u


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

static void draw_background_band(video_info_t* info, const ui_dirty_rect_t* clip) {
    // Однотонный фон: без полос по всей высоте экрана.
    fb_rect(info, clip->x, clip->y, clip->w, clip->h, COLOR_BG_DARK);
}

static void draw_top_panel(video_info_t* info, const ui_dirty_rect_t* clip) {
    (void)clip;
    uint32_t panel_button_color = COLOR_ACCENT;

    if (g_ui_state.panel_pressed) {
        panel_button_color = COLOR_ACCENT_PRESSED;
    } else if (g_ui_state.panel_hover) {
        panel_button_color = COLOR_ACCENT_HOVER;
    }

    fb_rect(info, 0, 0, info->width, 34, COLOR_PANEL);
    fb_rect(info, PANEL_BTN_X, PANEL_BTN_Y, PANEL_BTN_W, PANEL_BTN_H, panel_button_color);
    fb_draw_text(info, 18, 13, "WOOS 1.20.6", COLOR_TEXT_LIGHT, panel_button_color);
    fb_draw_text(info, (uint16_t)(info->width - 80), 13, "DEV BUILD", COLOR_TEXT_LIGHT, COLOR_PANEL);
}

static void draw_status_window(video_info_t* info, const ui_dirty_rect_t* clip) {
    (void)clip;
    uint16_t win_w = (uint16_t)(info->width / 3);
    uint16_t win_h = (uint16_t)(info->height / 3);
    uint16_t win_x = (uint16_t)(info->width - win_w - 24);
    uint16_t win_y = 56;

    fb_rect(info, win_x, win_y, win_w, win_h, COLOR_WINDOW);
    fb_frame(info, win_x, win_y, win_w, win_h, 2, COLOR_BORDER);
    uint32_t title_color = g_runtime_stats.idt_ready ? COLOR_ACCENT : COLOR_ACCENT_PRESSED;
    const char* title = g_runtime_stats.idt_ready ? "STATUS: IDT READY" : "STATUS: IDT BAD";

    fb_rect(info, win_x, win_y, win_w, 22, title_color);
    fb_draw_text(info, (uint16_t)(win_x + 8), (uint16_t)(win_y + 7), title, COLOR_TEXT_LIGHT, title_color);
}

static void write_decimal_padded(char* buffer, uint16_t last_index, uint64_t value, uint16_t digits) {
    for (uint16_t i = 0; i < digits; i++) {
        buffer[last_index - i] = (char)('0' + (value % 10u));
        value /= 10u;
    }
}

static void draw_footer(video_info_t* info, const ui_dirty_rect_t* clip) {
    (void)clip;
    const char* status = "EVENTS: MOVE CURSOR OVER PANEL";

    if (g_ui_state.panel_pressed) {
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

    fb_draw_text(info, 16, (uint16_t)(info->height - 32), status, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
    fb_draw_text(info, 16, (uint16_t)(info->height - 20), dirty_text, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
    fb_draw_text(info, 104, (uint16_t)(info->height - 20), heap_text, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
    fb_draw_text(info, 292, (uint16_t)(info->height - 20), pmm_text, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
    fb_draw_text(info, (uint16_t)(info->width - 404), (uint16_t)(info->height - 32), video_text, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
    fb_draw_text(info, (uint16_t)(info->width - 300), (uint16_t)(info->height - 32), pmm_status, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
    fb_draw_text(info, (uint16_t)(info->width - 300), (uint16_t)(info->height - 20), irq_text, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
    fb_draw_text(info, (uint16_t)(info->width - 142), (uint16_t)(info->height - 20), heartbeat_text, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
    fb_draw_text(info, 16, (uint16_t)(info->height - 44), storage_status, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
    fb_draw_text(info, 176, (uint16_t)(info->height - 44), storage_lba_text, COLOR_TEXT_LIGHT, COLOR_BG_DARK);
}

static void ui_draw_region(video_info_t* info, const ui_dirty_rect_t* clip) {
    draw_background_band(info, clip);
    draw_top_panel(info, clip);
    draw_status_window(info, clip);
    draw_footer(info, clip);
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
                fb_writepixel(info, x, y, COLOR_CURSOR);
            }
        }
    }

    g_cursor.visible = 1;
}

static void present_cursor_delta(video_info_t* info, uint16_t old_x, uint16_t old_y, uint8_t had_visible) {
    (void)info;

    if (!had_visible) {
        ui_mark_dirty(g_cursor.x, g_cursor.y, CURSOR_W, CURSOR_H);
        return;
    }

    ui_dirty_rect_t old_rect = {old_x, old_y, CURSOR_W, CURSOR_H};
    ui_dirty_rect_t new_rect = {g_cursor.x, g_cursor.y, CURSOR_W, CURSOR_H};
    ui_dirty_rect_t union_rect = rect_union(&old_rect, &new_rect);
    // Для virtio-path немедленный sync flush на каждое движение курсора заметно
    // увеличивает latency. Вместо этого объединяем старую/новую область и
    // публикуем её штатно через ближайший ui_render_dirty() кадр.
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

    cursor_restore_underlay(info);
    g_cursor.x = x;
    g_cursor.y = y;
    g_cursor.buttons = buttons;
    cursor_draw(info);
    // Публикуем старую и новую область курсора одним прямоугольником,
    // чтобы не было «мигания» из-за двух последовательных flush-операций.
    present_cursor_delta(info, old_x, old_y, had_visible);
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

    if (g_cursor.visible) {
        cursor_restore_underlay(info);
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
        fb_present_rect(info, clip.x, clip.y, clip.w, clip.h);
    }

    g_dirty_count = 0;

    if (g_cursor.visible) {
        cursor_draw(info);
        fb_present_rect(info, g_cursor.x, g_cursor.y, CURSOR_W, CURSOR_H);
    }
}

uint16_t ui_last_dirty_count(void) {
    return g_last_dirty_count;
}

void ui_render_desktop(video_info_t* info) {
    ui_mark_dirty(0, 0, info->width, info->height);
    ui_render_dirty(info);
    ui_set_cursor(info, (uint16_t)(info->width / 2), (uint16_t)(info->height / 2), 0);
}

static uint8_t point_inside_panel_button(uint16_t x, uint16_t y) {
    uint16_t end_x = (uint16_t)(PANEL_BTN_X + PANEL_BTN_W);
    uint16_t end_y = (uint16_t)(PANEL_BTN_Y + PANEL_BTN_H);
    return (x >= PANEL_BTN_X && x < end_x && y >= PANEL_BTN_Y && y < end_y);
}

void ui_handle_mouse_move(video_info_t* info, uint16_t x, uint16_t y, uint8_t buttons) {
    uint8_t next_hover = point_inside_panel_button(x, y);

    if (next_hover != g_ui_state.panel_hover) {
        g_ui_state.panel_hover = next_hover;
        ui_mark_dirty(0, 0, info->width, 34);
        ui_mark_dirty(0, (uint16_t)(info->height - 24), info->width, 24);
    }

    ui_set_cursor(info, x, y, buttons);
}

void ui_handle_mouse_button(video_info_t* info, uint8_t buttons) {
    uint8_t left_pressed = (uint8_t)(buttons & 0x1u);
    uint8_t next_pressed = (uint8_t)(left_pressed && g_ui_state.panel_hover);

    if (next_pressed != g_ui_state.panel_pressed) {
        g_ui_state.panel_pressed = next_pressed;
        ui_mark_dirty(0, 0, info->width, 34);
        ui_mark_dirty(0, (uint16_t)(info->height - 24), info->width, 24);
    }
}

void ui_set_kernel_health(video_info_t* info, uint8_t idt_ready, uint32_t heartbeat) {
    if (g_runtime_stats.idt_ready == idt_ready && g_runtime_stats.heartbeat == heartbeat) {
        return;
    }

    g_runtime_stats.idt_ready = idt_ready;
    g_runtime_stats.heartbeat = heartbeat;

    uint16_t win_w = (uint16_t)(info->width / 3);
    uint16_t win_x = (uint16_t)(info->width - win_w - 24);
    ui_mark_dirty(win_x, 56, win_w, 22);
    ui_mark_dirty((uint16_t)(info->width - 150), (uint16_t)(info->height - 24), 150, 24);
}

void ui_set_irq_stats(video_info_t* info, uint32_t keyboard_irq, uint32_t mouse_irq) {
    if (g_runtime_stats.keyboard_irq == keyboard_irq && g_runtime_stats.mouse_irq == mouse_irq) {
        return;
    }

    g_runtime_stats.keyboard_irq = keyboard_irq;
    g_runtime_stats.mouse_irq = mouse_irq;

    ui_mark_dirty((uint16_t)(info->width - 310), (uint16_t)(info->height - 24), 170, 24);
}

void ui_set_memory_stats(video_info_t* info, uint8_t pmm_ready, uint64_t total_pages, uint64_t free_pages) {
    if (g_runtime_stats.pmm_ready == pmm_ready
        && g_runtime_stats.pmm_total_pages == total_pages
        && g_runtime_stats.pmm_free_pages == free_pages) {
        return;
    }

    g_runtime_stats.pmm_ready = pmm_ready;
    g_runtime_stats.pmm_total_pages = total_pages;
    g_runtime_stats.pmm_free_pages = free_pages;

    ui_mark_dirty(0, (uint16_t)(info->height - 48), info->width, 48);
}

void ui_set_runtime_stats(
    video_info_t* info,
    uint16_t dirty_count,
    uint64_t heap_used,
    uint64_t heap_free,
    uint8_t virtio_detected,
    uint8_t virtio_active
) {
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

    ui_mark_dirty(0, (uint16_t)(info->height - 48), info->width, 48);
}

void ui_set_storage_stats(video_info_t* info, uint8_t storage_ready, uint8_t last_read_ok, uint32_t last_lba, uint8_t boot_signature_valid) {
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

    ui_mark_dirty(0, (uint16_t)(info->height - 48), info->width, 48);
}
