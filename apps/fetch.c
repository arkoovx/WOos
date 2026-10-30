#include "woos.h"

#define WIN_W 580
#define WIN_H 380

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

static uint32_t g_fb[WIN_W * WIN_H];

static char g_url[128] = "http://10.0.2.2:8080/";
static int g_url_len = 21;
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
    woos_draw_text(g_fb, WIN_W, WIN_H, 10, WIN_H - STATUSBAR_H + 5, g_status_msg, 0x0055AA);

    int32_t res = woos_net_fetch(g_url, g_response, sizeof(g_response));
    if (res < 0) {
        g_response[0] = '\0';
        g_resp_len = 0;
        parse_response_lines();
        set_status("Fetch error: unable to connect or receive response.");
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
    return (WIN_H - TOPBAR_H - STATUSBAR_H) / LINE_H;
}

static void draw_btn(int x, int y, int w, int h, const char* label, uint32_t bg, uint32_t fg, uint32_t border) {
    fill_rect(g_fb, WIN_W, WIN_H, x, y, w, h, bg);
    draw_rect(g_fb, WIN_W, WIN_H, x, y, w, h, border);
    int len = 0;
    while (label[len] != '\0') len++;
    int tx = x + (w - len * CHAR_W) / 2;
    int ty = y + (h - 12) / 2;
    woos_draw_text(g_fb, WIN_W, WIN_H, tx, ty, label, fg);
}

static void draw_ui(void) {
    // 1. Background
    fill_rect(g_fb, WIN_W, WIN_H, 0, 0, WIN_W, WIN_H, COLOR_BG);

    // 2. Top Toolbar
    fill_rect(g_fb, WIN_W, WIN_H, 0, 0, WIN_W, TOPBAR_H, COLOR_TOPBAR);
    draw_rect(g_fb, WIN_W, WIN_H, 0, TOPBAR_H - 1, WIN_W, 1, COLOR_BORDER);

    woos_draw_text(g_fb, WIN_W, WIN_H, 12, 16, "URL:", 0x333333);

    // URL input box
    int input_x = 48;
    int input_y = 10;
    int input_w = 280;
    int input_h = 24;
    fill_rect(g_fb, WIN_W, WIN_H, input_x, input_y, input_w, input_h, COLOR_INPUT_BG);
    draw_rect(g_fb, WIN_W, WIN_H, input_x, input_y, input_w, input_h, g_url_focus ? 0x0078D7 : COLOR_INPUT_BORDER);

    char disp_url[140];
    int d_idx = 0;
    while (g_url[d_idx] != '\0' && d_idx < 32) {
        disp_url[d_idx] = g_url[d_idx];
        d_idx++;
    }
    if (g_url_focus) {
        disp_url[d_idx++] = '_';
    }
    disp_url[d_idx] = '\0';
    woos_draw_text(g_fb, WIN_W, WIN_H, input_x + 6, input_y + 6, disp_url, COLOR_TEXT);

    // Action buttons
    draw_btn(338, 10, 64, 24, "Fetch", COLOR_BTN_BG, COLOR_BTN_FG, 0x1A5BB8);
    draw_btn(408, 10, 54, 24, "Save", COLOR_BTN_SEC_BG, COLOR_BTN_SEC_FG, COLOR_BTN_BORDER);
    draw_btn(468, 10, 54, 24, "Clear", COLOR_BTN_SEC_BG, COLOR_BTN_SEC_FG, COLOR_BTN_BORDER);

    // Scroll up/down small buttons
    draw_btn(528, 10, 20, 24, "^", COLOR_BTN_SEC_BG, COLOR_BTN_SEC_FG, COLOR_BTN_BORDER);
    draw_btn(552, 10, 20, 24, "v", COLOR_BTN_SEC_BG, COLOR_BTN_SEC_FG, COLOR_BTN_BORDER);

    // 3. Response Text View Area
    int content_y = TOPBAR_H + 4;
    int vis = visible_rows();
    int max_scroll = g_num_rows > vis ? (g_num_rows - vis) : 0;
    if (g_scroll_y > max_scroll) g_scroll_y = max_scroll;
    if (g_scroll_y < 0) g_scroll_y = 0;

    if (g_num_rows == 0) {
        woos_draw_text(g_fb, WIN_W, WIN_H, 20, content_y + 20, "Enter an HTTP URL above and click [Fetch] to download content.", 0x888888);
        woos_draw_text(g_fb, WIN_W, WIN_H, 20, content_y + 40, "Example: http://10.0.2.2:8080/ or 10.0.2.2", 0x888888);
    } else {
        for (int i = 0; i < vis; i++) {
            int row_idx = g_scroll_y + i;
            if (row_idx >= g_num_rows) break;
            int py = content_y + i * LINE_H;
            woos_draw_text(g_fb, WIN_W, WIN_H, 12, py, g_rows[row_idx], COLOR_TEXT);
        }
    }

    // 4. Status Bar
    fill_rect(g_fb, WIN_W, WIN_H, 0, WIN_H - STATUSBAR_H, WIN_W, STATUSBAR_H, COLOR_STATUSBAR);
    draw_rect(g_fb, WIN_W, WIN_H, 0, WIN_H - STATUSBAR_H, WIN_W, 1, COLOR_BORDER);
    woos_draw_text(g_fb, WIN_W, WIN_H, 10, WIN_H - STATUSBAR_H + 5, g_status_msg, 0x444444);
}

static void handle_click(int x, int y) {
    // Check URL input click
    if (x >= 48 && x <= 328 && y >= 10 && y <= 34) {
        g_url_focus = 1;
        return;
    }

    // [ Fetch ]
    if (x >= 338 && x <= 402 && y >= 10 && y <= 34) {
        g_url_focus = 0;
        do_fetch();
        return;
    }

    // [ Save ]
    if (x >= 408 && x <= 462 && y >= 10 && y <= 34) {
        do_save();
        return;
    }

    // [ Clear ]
    if (x >= 468 && x <= 522 && y >= 10 && y <= 34) {
        do_clear();
        return;
    }

    // [ ^ ]
    if (x >= 528 && x <= 548 && y >= 10 && y <= 34) {
        if (g_scroll_y > 0) g_scroll_y -= 5;
        if (g_scroll_y < 0) g_scroll_y = 0;
        return;
    }

    // [ v ]
    if (x >= 552 && x <= 572 && y >= 10 && y <= 34) {
        int vis = visible_rows();
        int max_s = g_num_rows > vis ? (g_num_rows - vis) : 0;
        if (g_scroll_y < max_s) g_scroll_y += 5;
        if (g_scroll_y > max_s) g_scroll_y = max_s;
        return;
    }

    // Click inside content area unfocuses URL input
    if (y > TOPBAR_H && y < WIN_H - STATUSBAR_H) {
        g_url_focus = 0;
    }
}

static void handle_key(int key, int ascii) {
    if (g_url_focus) {
        if (key == 13 || ascii == 13 || ascii == 10) { // Enter
            g_url_focus = 0;
            do_fetch();
            return;
        }
        if (key == 8 || ascii == 8) { // Backspace
            if (g_url_len > 0) {
                g_url_len--;
                g_url[g_url_len] = '\0';
            }
            return;
        }
        if (ascii >= 32 && ascii <= 126) {
            if (g_url_len < (int)sizeof(g_url) - 2) {
                g_url[g_url_len++] = (char)ascii;
                g_url[g_url_len] = '\0';
            }
            return;
        }
    }

    // Arrow keys & scrolling
    int vis = visible_rows();
    int max_s = g_num_rows > vis ? (g_num_rows - vis) : 0;

    if (key == 38) { // Up arrow
        if (g_scroll_y > 0) g_scroll_y--;
    } else if (key == 40) { // Down arrow
        if (g_scroll_y < max_s) g_scroll_y++;
    } else if (key == 33) { // Page Up
        g_scroll_y -= vis;
        if (g_scroll_y < 0) g_scroll_y = 0;
    } else if (key == 34) { // Page Down
        g_scroll_y += vis;
        if (g_scroll_y > max_s) g_scroll_y = max_s;
    }
}

__attribute__((export_name("_start")))
void _start(void) {
    int win_id = woos_create_window(WIN_W, WIN_H, "HTTP Fetch", 0);
    if (win_id < 0) return;

    draw_ui();
    woos_blit_window(win_id, g_fb, WIN_W, WIN_H);

    woos_msg_t msg;

    while (1) {
        int needs_redraw = 0;

        while (woos_ipc_recv(&msg) > 0) {
            if (msg.type == MSG_CLOSE) {
                woos_destroy_window(win_id);
                return;
            } else if (msg.type == MSG_CLICK) {
                handle_click((int)msg.arg1, (int)msg.arg2);
                needs_redraw = 1;
            } else if (msg.type == MSG_KEY_DOWN) {
                handle_key((int)msg.arg1, (int)msg.arg2);
                needs_redraw = 1;
            }
        }

        if (needs_redraw) {
            draw_ui();
            woos_blit_window(win_id, g_fb, WIN_W, WIN_H);
        }

        woos_ipc_wait_message();
    }
}
