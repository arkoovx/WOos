#ifndef WOOS_DIALOG_H
#define WOOS_DIALOG_H

#include "woos.h"

#define WOOS_DIALOG_RES_NONE    0
#define WOOS_DIALOG_RES_OK      1
#define WOOS_DIALOG_RES_CANCEL -1

#define DIALOG_MODE_NONE    0
#define DIALOG_MODE_SAVE_AS 1
#define DIALOG_MODE_OPEN    2
#define DIALOG_MODE_NEW     3
#define DIALOG_MODE_RENAME  4

typedef struct {
    int active;             // 1 if dialog is shown
    int mode;               // DIALOG_MODE_*
    char title[48];         // Window title (e.g. "Save As...", "Open File...")
    char prompt[64];        // Prompt label (e.g. "File name:")
    char ok_label[16];      // e.g. "Save", "Open", "OK"
    char input[128];        // File path or name buffer
    int input_len;          // Number of characters
    int cursor_pos;         // Cursor index (0..input_len)
    int cursor_tick;        // Blinking timer tick
} woos_file_dialog_t;

// Initialize or open the universal dialog
static inline void woos_dialog_open(woos_file_dialog_t* d, int mode, const char* title, const char* prompt, const char* default_text, const char* ok_label) {
    if (!d) return;
    d->active = 1;
    d->mode = mode;

    // Title
    d->title[0] = '\0';
    if (title && title[0] != '\0') {
        strncpy(d->title, title, sizeof(d->title) - 1);
    } else {
        strcpy(d->title, (mode == DIALOG_MODE_SAVE_AS) ? "Save As..." : "Open File...");
    }
    d->title[sizeof(d->title) - 1] = '\0';

    // Prompt
    d->prompt[0] = '\0';
    if (prompt && prompt[0] != '\0') {
        strncpy(d->prompt, prompt, sizeof(d->prompt) - 1);
    } else {
        strcpy(d->prompt, "Enter file path or name:");
    }
    d->prompt[sizeof(d->prompt) - 1] = '\0';

    // OK Button label
    d->ok_label[0] = '\0';
    if (ok_label && ok_label[0] != '\0') {
        strncpy(d->ok_label, ok_label, sizeof(d->ok_label) - 1);
    } else {
        strcpy(d->ok_label, (mode == DIALOG_MODE_SAVE_AS) ? "Save" : ((mode == DIALOG_MODE_OPEN) ? "Open" : "OK"));
    }
    d->ok_label[sizeof(d->ok_label) - 1] = '\0';

    // Input text
    d->input[0] = '\0';
    if (default_text && default_text[0] != '\0') {
        strncpy(d->input, default_text, sizeof(d->input) - 1);
    }
    d->input[sizeof(d->input) - 1] = '\0';
    d->input_len = (int)strlen(d->input);
    d->cursor_pos = d->input_len; // Default cursor at the end
    d->cursor_tick = 0;
}

static inline void woos_dialog_close(woos_file_dialog_t* d) {
    if (d) d->active = 0;
}

// Draw the universal dialog box
static inline void woos_dialog_draw(uint32_t* buf, int win_w, int win_h, woos_file_dialog_t* d) {
    if (!d || !d->active || !buf) return;

    int dw = 380;
    int dh = 124;
    int dx = (win_w - dw) / 2;
    int dy = (win_h - dh) / 2;

    // 1. Semi-transparent dark drop shadow (offset +3 px)
    for (int sy = dy + 3; sy < dy + dh + 3; sy++) {
        if (sy < 0 || sy >= win_h) continue;
        uint32_t* row = buf + sy * win_w;
        for (int sx = dx + 3; sx < dx + dw + 3; sx++) {
            if (sx < 0 || sx >= win_w) continue;
            uint32_t p = row[sx];
            row[sx] = ((p >> 1) & 0x7F7F7F);
        }
    }

    // 2. Dialog Body Background & 3D borders
    fill_rect(buf, win_w, win_h, dx, dy, dw, dh, 0xECE9D8);
    draw_3d_rect(buf, win_w, win_h, dx, dy, dw, dh, 0);

    // 3. Titlebar
    fill_rect(buf, win_w, win_h, dx + 2, dy + 2, dw - 4, 22, 0x2A62C9);
    woos_draw_text(buf, win_w, win_h, dx + 8, dy + 6, d->title, 0xFFFFFF);

    // Close button [X]
    int close_x = dx + dw - 20;
    int close_y = dy + 4;
    fill_rect(buf, win_w, win_h, close_x, close_y, 16, 16, 0xC75050);
    draw_rect(buf, win_w, win_h, close_x, close_y, 16, 16, 0x8C2020);
    woos_draw_text(buf, win_w, win_h, close_x + 5, close_y + 2, "x", 0xFFFFFF);

    // 4. Prompt label
    woos_draw_text(buf, win_w, win_h, dx + 16, dy + 32, d->prompt, 0x222222);

    // 5. Text Input Box (Edit Control)
    int bx = dx + 16;
    int by = dy + 52;
    int bw = dw - 32;
    int bh = 24;

    // Recessed 3D text box
    draw_3d_rect(buf, win_w, win_h, bx, by, bw, bh, 1);
    fill_rect(buf, win_w, win_h, bx + 1, by + 1, bw - 2, bh - 2, 0xFFFFFF);

    // Calculate visible text window & scroll offset
    int max_vis_chars = (bw - 12) / 8; // ~42 characters
    int scroll = 0;
    if (d->cursor_pos >= max_vis_chars) {
        scroll = d->cursor_pos - max_vis_chars + 1;
    }

    char disp_buf[64];
    int p = 0;
    for (int i = scroll; i < d->input_len && p < max_vis_chars && p < 63; i++) {
        disp_buf[p++] = d->input[i];
    }
    disp_buf[p] = '\0';
    woos_draw_text(buf, win_w, win_h, bx + 6, by + 6, disp_buf, 0x000000);

    // Vertical Caret (Cursor)
    int caret_x = bx + 6 + (d->cursor_pos - scroll) * 8;
    if (caret_x >= bx + 4 && caret_x <= bx + bw - 6) {
        fill_rect(buf, win_w, win_h, caret_x, by + 4, 2, bh - 8, 0x000000);
    }

    // 6. Action Buttons: [OK / Save] and [Cancel]
    int btn_w = 68;
    int btn_h = 22;
    int btn_y = dy + dh - 32;

    // [OK / Save] Button
    int ok_x = dx + dw - 156;
    draw_3d_rect(buf, win_w, win_h, ok_x, btn_y, btn_w, btn_h, 0);
    fill_rect(buf, win_w, win_h, ok_x + 1, btn_y + 1, btn_w - 2, btn_h - 2, 0xE4E2D5);
    int ok_text_len = (int)strlen(d->ok_label);
    int ok_tx = ok_x + (btn_w - ok_text_len * 8) / 2;
    woos_draw_text(buf, win_w, win_h, ok_tx, btn_y + 5, d->ok_label, 0x000000);

    // [Cancel] Button
    int cancel_x = dx + dw - 80;
    draw_3d_rect(buf, win_w, win_h, cancel_x, btn_y, btn_w, btn_h, 0);
    fill_rect(buf, win_w, win_h, cancel_x + 1, btn_y + 1, btn_w - 2, btn_h - 2, 0xE4E2D5);
    int cancel_tx = cancel_x + (btn_w - 6 * 8) / 2;
    woos_draw_text(buf, win_w, win_h, cancel_tx, btn_y + 5, "Cancel", 0x000000);
}

// Handle mouse clicks on the dialog
static inline int woos_dialog_handle_click(woos_file_dialog_t* d, int win_w, int win_h, int mx, int my) {
    if (!d || !d->active) return WOOS_DIALOG_RES_NONE;

    int dw = 380;
    int dh = 124;
    int dx = (win_w - dw) / 2;
    int dy = (win_h - dh) / 2;

    // Check [X] close button
    int close_x = dx + dw - 20;
    int close_y = dy + 4;
    if (mx >= close_x && mx <= close_x + 16 && my >= close_y && my <= close_y + 16) {
        d->active = 0;
        return WOOS_DIALOG_RES_CANCEL;
    }

    // Check [Cancel] button
    int btn_w = 68;
    int btn_h = 22;
    int btn_y = dy + dh - 32;
    int cancel_x = dx + dw - 80;
    if (mx >= cancel_x && mx <= cancel_x + btn_w && my >= btn_y && my <= btn_y + btn_h) {
        d->active = 0;
        return WOOS_DIALOG_RES_CANCEL;
    }

    // Check [OK] button
    int ok_x = dx + dw - 156;
    if (mx >= ok_x && mx <= ok_x + btn_w && my >= btn_y && my <= btn_y + btn_h) {
        d->active = 0;
        return WOOS_DIALOG_RES_OK;
    }

    // Check Click inside Input Box -> set cursor position!
    int bx = dx + 16;
    int by = dy + 52;
    int bw = dw - 32;
    int bh = 24;

    if (mx >= bx && mx <= bx + bw && my >= by && my <= by + bh) {
        int max_vis_chars = (bw - 12) / 8;
        int scroll = 0;
        if (d->cursor_pos >= max_vis_chars) {
            scroll = d->cursor_pos - max_vis_chars + 1;
        }

        int rel_x = mx - (bx + 6);
        if (rel_x < 0) rel_x = 0;
        int char_idx = scroll + (rel_x + 4) / 8; // rounded to nearest character position
        if (char_idx < 0) char_idx = 0;
        if (char_idx > d->input_len) char_idx = d->input_len;

        d->cursor_pos = char_idx;
        return WOOS_DIALOG_RES_NONE;
    }

    // Modal swallow (clicks inside or outside dialog don't pass through to background)
    return WOOS_DIALOG_RES_NONE;
}

// Handle key presses on the dialog
static inline int woos_dialog_handle_key(woos_file_dialog_t* d, int ascii, int scancode) {
    if (!d || !d->active) return WOOS_DIALOG_RES_NONE;

    // 1. Enter: commit
    if (scancode == 0x1C || ascii == '\n' || ascii == '\r') {
        d->active = 0;
        return WOOS_DIALOG_RES_OK;
    }

    // 2. Escape: cancel
    if (scancode == 0x01) {
        d->active = 0;
        return WOOS_DIALOG_RES_CANCEL;
    }

    // 3. Backspace: delete character before cursor
    if (scancode == 0x0E || ascii == 8 || ascii == 127) {
        if (d->cursor_pos > 0 && d->input_len > 0) {
            for (int i = d->cursor_pos - 1; i < d->input_len; i++) {
                d->input[i] = d->input[i + 1];
            }
            d->cursor_pos--;
            d->input_len--;
            d->input[d->input_len] = '\0';
        }
        return WOOS_DIALOG_RES_NONE;
    }

    // 4. Delete: delete character under cursor
    if (scancode == 0x53) {
        if (d->cursor_pos < d->input_len) {
            for (int i = d->cursor_pos; i < d->input_len; i++) {
                d->input[i] = d->input[i + 1];
            }
            d->input_len--;
            d->input[d->input_len] = '\0';
        }
        return WOOS_DIALOG_RES_NONE;
    }

    // 5. Left Arrow
    if (scancode == 0x4B) {
        if (d->cursor_pos > 0) d->cursor_pos--;
        return WOOS_DIALOG_RES_NONE;
    }

    // 6. Right Arrow
    if (scancode == 0x4D) {
        if (d->cursor_pos < d->input_len) d->cursor_pos++;
        return WOOS_DIALOG_RES_NONE;
    }

    // 7. Home
    if (scancode == 0x47) {
        d->cursor_pos = 0;
        return WOOS_DIALOG_RES_NONE;
    }

    // 8. End
    if (scancode == 0x4F) {
        d->cursor_pos = d->input_len;
        return WOOS_DIALOG_RES_NONE;
    }

    // 9. Printable characters (32..126)
    if (ascii >= 32 && ascii <= 126) {
        if (d->input_len + 1 < (int)sizeof(d->input)) {
            // Shift characters right
            for (int i = d->input_len; i >= d->cursor_pos; i--) {
                d->input[i + 1] = d->input[i];
            }
            d->input[d->cursor_pos] = (char)ascii;
            d->cursor_pos++;
            d->input_len++;
            d->input[d->input_len] = '\0';
        }
        return WOOS_DIALOG_RES_NONE;
    }

    return WOOS_DIALOG_RES_NONE;
}

#endif
