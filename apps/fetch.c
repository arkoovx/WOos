#include "woos.h"

#define DEFAULT_WIN_W 580
#define DEFAULT_WIN_H 380
#define MAX_WIN_W 1280
#define MAX_WIN_H 1024

#define TOPBAR_H 44
#define STATUSBAR_H 22
#define CHAR_W 8
#define LINE_H 14

#define COLOR_BG          0xFFFFFF
#define COLOR_TEXT        0x222222
#define COLOR_TOPBAR      0xF0F0F0
#define COLOR_STATUSBAR   0xE4E4E4
#define COLOR_BORDER      0xD0D0D0
#define COLOR_BTN_BG      0x2D72D9
#define COLOR_BTN_FG      0xFFFFFF
#define COLOR_BTN_SEC_BG  0xE8E8E8
#define COLOR_BTN_SEC_FG  0x222222
#define COLOR_BTN_BORDER  0xABABAB
#define COLOR_INPUT_BG    0xFFFFFF
#define COLOR_INPUT_BORDER 0x7A7A7A

#define MAX_ROWS 1000
#define MAX_COLS 160
#define MAX_RESP_SIZE 32768

static int g_win_w = DEFAULT_WIN_W;
static int g_win_h = DEFAULT_WIN_H;
static uint32_t g_fb[MAX_WIN_W * MAX_WIN_H];

static char g_url[128] = "http://10.0.2.2:8080/";
static int g_url_len = 21;
static int g_cursor_pos = 21;
static int g_scroll_x = 0;
static int g_url_focus = 1;

static char g_response[MAX_RESP_SIZE];
static uint32_t g_resp_len = 0;

static char g_rows[MAX_ROWS][MAX_COLS];
static int g_num_rows = 0;
static int g_scroll_y = 0;

static char g_status_msg[128] = "Ready. Click [Fetch] or press Enter to download.";

static void set_status(const char* msg) {
    int i = 0;
    while (msg[i] != '\0' && i < 127) {
        g_status_msg[i] = msg[i];
        i++;
    }
    g_status_msg[i] = '\0';
}

static void parse_response_lines(void) {
    g_num_rows = 0;
    const char* p = g_response;
    while (*p != '\0' && g_num_rows < MAX_ROWS) {
        int col = 0;
        while (*p != '\0' && *p != '\n' && *p != '\r' && col < MAX_COLS - 1) {
            g_rows[g_num_rows][col++] = *p++;
        }
        g_rows[g_num_rows][col] = '\0';
        g_num_rows++;

        if (*p == '\r') p++;
        if (*p == '\n') p++;
    }
    if (g_num_rows == 0) {
        g_rows[0][0] = '\0';
        g_num_rows = 1;
    }
    g_scroll_y = 0;
}

static void do_fetch(void) {
    if (g_url_len == 0) {
        set_status("Error: URL cannot be empty");
        return;
    }

    set_status("Connecting and fetching data...");
    woos_draw_text(g_fb, g_win_w, g_win_h, 10, g_win_h - STATUSBAR_H + 5, g_status_msg, 0x0055AA);

    int32_t res = woos_net_fetch(g_url, g_response, sizeof(g_response) - 1);
    if (res < 0) {
        char err_msg[128] = "Fetch error: unable to connect";
        if (g_response[0] != '\0') {
            err_msg[0] = '\0';
            strncat(err_msg, g_response, 120);
        }
        g_response[0] = '\0';
        g_resp_len = 0;
        parse_response_lines();
        set_status(err_msg);
    } else {
        g_resp_len = (uint32_t)res;
        parse_response_lines();

        char stat_buf[64] = "Done: received ";
        char num_buf[16];
        int_to_str(res, num_buf);
        int s_idx = 15;
        int n_idx = 0;
        while (num_buf[n_idx] != '\0' && s_idx < 50) {
            stat_buf[s_idx++] = num_buf[n_idx++];
        }
        const char* tail = " bytes";
        n_idx = 0;
        while (tail[n_idx] != '\0' && s_idx < 63) {
            stat_buf[s_idx++] = tail[n_idx++];
        }
        stat_buf[s_idx] = '\0';
        set_status(stat_buf);
    }
}

static void do_save(void) {
    if (g_resp_len == 0) {
        set_status("Nothing to save: response buffer is empty.");
        return;
    }

    int h = woos_vfs_open("/DESKTOP/FETCHED.TXT", VFS_MODE_WRITE | VFS_MODE_CREATE | VFS_MODE_TRUNC);
    if (h >= 0) {
        woos_vfs_write(h, g_response, g_resp_len);
        woos_vfs_close(h);
        set_status("Successfully saved response to /DESKTOP/FETCHED.TXT");
    } else {
        set_status("Error: failed to open /DESKTOP/FETCHED.TXT for writing");
    }
}

static void do_clear(void) {
    g_response[0] = '\0';
    g_resp_len = 0;
    g_num_rows = 0;
    g_scroll_y = 0;
    set_status("Response cleared.");
}

static int visible_rows(void) {
    return (g_win_h - TOPBAR_H - STATUSBAR_H) / LINE_H;
}

static void draw_btn(int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg, uint32_t border) {
    fill_rect(g_fb, g_win_w, g_win_h, x, y, w, h, bg);
    draw_rect(g_fb, g_win_w, g_win_h, x, y, w, h, border);
    int len = 0;
    while (label[len] != '\0') len++;
    int tx = x + (w - len * CHAR_W) / 2;
    int ty = y + (h - 12) / 2;
    woos_draw_text(g_fb, g_win_w, g_win_h, tx, ty, label, fg);
}

static void get_layout(int* out_input_x, int* out_input_y, int* out_input_w, int* out_input_h,
                       int* out_btn_fetch_x, int* out_btn_save_x, int* out_btn_clear_x,
                       int* out_btn_up_x, int* out_btn_dn_x) {
    int ix = 48;
    int iy = 10;
    int iw = g_win_w - 48 - 250;
    if (iw < 140) iw = 140;
    int ih = 24;

    if (out_input_x) *out_input_x = ix;
    if (out_input_y) *out_input_y = iy;
    if (out_input_w) *out_input_w = iw;
    if (out_input_h) *out_input_h = ih;

    int bx = ix + iw + 8;
    if (out_btn_fetch_x) *out_btn_fetch_x = bx;
    bx += 58;
    if (out_btn_save_x) *out_btn_save_x = bx;
    bx += 52;
    if (out_btn_clear_x) *out_btn_clear_x = bx;
    bx += 52;
    if (out_btn_up_x) *out_btn_up_x = bx;
    bx += 26;
    if (out_btn_dn_x) *out_btn_dn_x = bx;
}

static void draw_ui(void) {
    int win_w = g_win_w;
    int win_h = g_win_h;

    // 1. Background
    fill_rect(g_fb, win_w, win_h, 0, 0, win_w, win_h, COLOR_BG);

    // 2. Top Toolbar
    fill_rect(g_fb, win_w, win_h, 0, 0, win_w, TOPBAR_H, COLOR_TOPBAR);
    draw_rect(g_fb, win_w, win_h, 0, TOPBAR_H - 1, win_w, 1, COLOR_BORDER);

    woos_draw_text(g_fb, win_w, win_h, 12, 16, "URL:", 0x333333);

    // Layout
    int input_x, input_y, input_w, input_h;
    int btn_fetch_x, btn_save_x, btn_clear_x, btn_up_x, btn_dn_x;
    get_layout(&input_x, &input_y, &input_w, &input_h,
               &btn_fetch_x, &btn_save_x, &btn_clear_x, &btn_up_x, &btn_dn_x);

    fill_rect(g_fb, win_w, win_h, input_x, input_y, input_w, input_h, COLOR_INPUT_BG);
    draw_rect(g_fb, win_w, win_h, input_x, input_y, input_w, input_h, g_url_focus ? 0x0078D7 : COLOR_INPUT_BORDER);

    // Visible text and cursor
    int max_chars = (input_w - 12) / CHAR_W;
    if (max_chars < 1) max_chars = 1;
    if (g_cursor_pos < g_scroll_x) g_scroll_x = g_cursor_pos;
    if (g_cursor_pos >= g_scroll_x + max_chars) g_scroll_x = g_cursor_pos - max_chars + 1;
    if (g_scroll_x < 0) g_scroll_x = 0;

    char disp_url[140];
    int copy_len = g_url_len - g_scroll_x;
    if (copy_len > max_chars) copy_len = max_chars;
    if (copy_len < 0) copy_len = 0;
    for (int i = 0; i < copy_len; i++) {
        disp_url[i] = g_url[g_scroll_x + i];
    }
    disp_url[copy_len] = '\0';
    woos_draw_text(g_fb, win_w, win_h, input_x + 6, input_y + 6, disp_url, COLOR_TEXT);

    // Cursor
    if (g_url_focus) {
        int cx = input_x + 6 + (g_cursor_pos - g_scroll_x) * CHAR_W;
        if (cx >= input_x + 4 && cx <= input_x + input_w - 4) {
            fill_rect(g_fb, win_w, win_h, cx, input_y + 3, 2, input_h - 6, 0x0078D7);
        }
    }

    // Action buttons
    draw_btn(btn_fetch_x, 10, 54, 24, "Fetch", COLOR_BTN_BG, COLOR_BTN_FG, 0x1A5BB8);
    draw_btn(btn_save_x, 10, 48, 24, "Save", COLOR_BTN_SEC_BG, COLOR_BTN_SEC_FG, COLOR_BTN_BORDER);
    draw_btn(btn_clear_x, 10, 48, 24, "Clear", COLOR_BTN_SEC_BG, COLOR_BTN_SEC_FG, COLOR_BTN_BORDER);

    // Scroll buttons
    draw_btn(btn_up_x, 10, 22, 24, "^", COLOR_BTN_SEC_BG, COLOR_BTN_SEC_FG, COLOR_BTN_BORDER);
    draw_btn(btn_dn_x, 10, 22, 24, "v", COLOR_BTN_SEC_BG, COLOR_BTN_SEC_FG, COLOR_BTN_BORDER);

    // 3. Response Text View Area
    int content_y = TOPBAR_H + 4;
    int vis = visible_rows();
    int max_scroll = g_num_rows > vis ? (g_num_rows - vis) : 0;
    if (g_scroll_y > max_scroll) g_scroll_y = max_scroll;
    if (g_scroll_y < 0) g_scroll_y = 0;

    if (g_num_rows == 0) {
        woos_draw_text(g_fb, win_w, win_h, 20, content_y + 20, "Enter an HTTP URL above and click [Fetch] to download content.", 0x888888);
        woos_draw_text(g_fb, win_w, win_h, 20, content_y + 40, "Example: http://10.0.2.2:8080/ or 10.0.2.2", 0x888888);
    } else {
        for (int i = 0; i < vis; i++) {
            int row_idx = g_scroll_y + i;
            if (row_idx >= g_num_rows) break;
            int py = content_y + i * LINE_H;
            woos_draw_text(g_fb, win_w, win_h, 12, py, g_rows[row_idx], COLOR_TEXT);
        }
    }

    // 4. Status Bar
    fill_rect(g_fb, win_w, win_h, 0, win_h - STATUSBAR_H, win_w, STATUSBAR_H, COLOR_STATUSBAR);
    draw_rect(g_fb, win_w, win_h, 0, win_h - STATUSBAR_H, win_w, 1, COLOR_BORDER);
    woos_draw_text(g_fb, win_w, win_h, 10, win_h - STATUSBAR_H + 5, g_status_msg, 0x444444);
}

static void handle_click(int x, int y) {
    int input_x, input_y, input_w, input_h;
    int btn_fetch_x, btn_save_x, btn_clear_x, btn_up_x, btn_dn_x;
    get_layout(&input_x, &input_y, &input_w, &input_h,
               &btn_fetch_x, &btn_save_x, &btn_clear_x, &btn_up_x, &btn_dn_x);

    // Check URL input click (click-to-cursor)
    if (x >= input_x && x <= input_x + input_w && y >= input_y && y <= input_y + input_h) {
        g_url_focus = 1;
        int col = (x - (input_x + 6) + CHAR_W / 2) / CHAR_W;
        g_cursor_pos = g_scroll_x + col;
        if (g_cursor_pos < 0) g_cursor_pos = 0;
        if (g_cursor_pos > g_url_len) g_cursor_pos = g_url_len;
        return;
    }

    // [ Fetch ]
    if (x >= btn_fetch_x && x <= btn_fetch_x + 54 && y >= 10 && y <= 34) {
        g_url_focus = 0;
        do_fetch();
        return;
    }

    // [ Save ]
    if (x >= btn_save_x && x <= btn_save_x + 48 && y >= 10 && y <= 34) {
        do_save();
        return;
    }

    // [ Clear ]
    if (x >= btn_clear_x && x <= btn_clear_x + 48 && y >= 10 && y <= 34) {
        do_clear();
        return;
    }

    // [ ^ ]
    if (x >= btn_up_x && x <= btn_up_x + 22 && y >= 10 && y <= 34) {
        if (g_scroll_y > 0) g_scroll_y -= 5;
        if (g_scroll_y < 0) g_scroll_y = 0;
        return;
    }

    // [ v ]
    if (x >= btn_dn_x && x <= btn_dn_x + 22 && y >= 10 && y <= 34) {
        int vis = visible_rows();
        int max_s = g_num_rows > vis ? (g_num_rows - vis) : 0;
        if (g_scroll_y < max_s) g_scroll_y += 5;
        if (g_scroll_y > max_s) g_scroll_y = max_s;
        return;
    }

    // Click inside content area unfocuses URL input
    if (y > TOPBAR_H && y < g_win_h - STATUSBAR_H) {
        g_url_focus = 0;
    }
}

static void handle_key(int ascii, int scancode) {
    if (g_url_focus) {
        // Enter
        if (ascii == 10 || ascii == 13 || scancode == 0x1C) {
            g_url_focus = 0;
            do_fetch();
            return;
        }

        // Backspace
        if (ascii == 8 || scancode == 0x0E) {
            if (g_cursor_pos > 0) {
                for (int i = g_cursor_pos - 1; i < g_url_len; i++) {
                    g_url[i] = g_url[i + 1];
                }
                g_url_len--;
                g_cursor_pos--;
            }
            return;
        }

        // Delete
        if (scancode == 0x53) {
            if (g_cursor_pos < g_url_len) {
                for (int i = g_cursor_pos; i < g_url_len; i++) {
                    g_url[i] = g_url[i + 1];
                }
                g_url_len--;
            }
            return;
        }

        // Left Arrow
        if (scancode == 0x4B) {
            if (g_cursor_pos > 0) g_cursor_pos--;
            return;
        }

        // Right Arrow
        if (scancode == 0x4D) {
            if (g_cursor_pos < g_url_len) g_cursor_pos++;
            return;
        }

        // Home
        if (scancode == 0x47) {
            g_cursor_pos = 0;
            return;
        }

        // End
        if (scancode == 0x4F) {
            g_cursor_pos = g_url_len;
            return;
        }

        // Normal printable characters
        if (ascii >= 32 && ascii <= 126) {
            if (g_url_len < (int)sizeof(g_url) - 2) {
                for (int i = g_url_len; i >= g_cursor_pos; i--) {
                    g_url[i + 1] = g_url[i];
                }
                g_url[g_cursor_pos] = (char)ascii;
                g_url_len++;
                g_cursor_pos++;
            }
            return;
        }
    }

    // Response view scrolling
    int vis = visible_rows();
    int max_s = g_num_rows > vis ? (g_num_rows - vis) : 0;

    if (scancode == 0x48) { // Up arrow
        if (g_scroll_y > 0) g_scroll_y--;
    } else if (scancode == 0x50) { // Down arrow
        if (g_scroll_y < max_s) g_scroll_y++;
    } else if (scancode == 0x49) { // Page Up
        g_scroll_y -= vis;
        if (g_scroll_y < 0) g_scroll_y = 0;
    } else if (scancode == 0x51) { // Page Down
        g_scroll_y += vis;
        if (g_scroll_y > max_s) g_scroll_y = max_s;
    }
}

__attribute__((export_name("_start")))
void _start(void) {
    int win_id = woos_create_window(g_win_w, g_win_h, "HTTP Fetch", 0);
    if (win_id < 0) return;

    draw_ui();
    woos_blit_window(win_id, g_fb, g_win_w, g_win_h);

    woos_msg_t msg;

    while (1) {
        int needs_redraw = 0;

        while (woos_ipc_recv(&msg) > 0) {
            if (msg.type == MSG_CLOSE) {
                woos_destroy_window(win_id);
                return;
            } else if (msg.type == MSG_RESIZE) {
                int new_w = (int)msg.arg1;
                int new_h = (int)msg.arg2;
                if (new_w > 0 && new_h > 0) {
                    if (new_w > MAX_WIN_W) new_w = MAX_WIN_W;
                    if (new_h > MAX_WIN_H) new_h = MAX_WIN_H;
                    g_win_w = new_w;
                    g_win_h = new_h;
                    needs_redraw = 1;
                }
            } else if (msg.type == MSG_CLICK) {
                handle_click((int)msg.arg1, (int)msg.arg2);
                needs_redraw = 1;
            } else if (msg.type == MSG_KEY_DOWN) {
                handle_key((int)msg.arg1, (int)msg.arg2);
                needs_redraw = 1;
            } else if (msg.type == MSG_SCROLL) {
                int8_t dz = (int8_t)(int32_t)msg.arg3;
                g_scroll_y -= (int)dz * 3;
                if (g_scroll_y < 0) g_scroll_y = 0;
                int vis = (g_win_h - 110) / 16;
                int max_s = g_num_rows > vis ? (g_num_rows - vis) : 0;
                if (g_scroll_y > max_s) g_scroll_y = max_s;
                needs_redraw = 1;
            }
        }

        if (needs_redraw) {
            draw_ui();
            woos_blit_window(win_id, g_fb, g_win_w, g_win_h);
        }

        woos_ipc_wait_message();
    }
}
