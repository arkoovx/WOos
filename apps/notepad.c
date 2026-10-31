#include "woos.h"
#include "woos_dialog.h"

#define DEFAULT_WIN_W 580
#define DEFAULT_WIN_H 380
#define MAX_WIN_W 1280
#define MAX_WIN_H 1024

static int g_win_w = DEFAULT_WIN_W;
static int g_win_h = DEFAULT_WIN_H;

#define TOOLBAR_H 24
#define STATUSBAR_H 20
#define CHAR_W 8
#define LINE_H 12
#define MARGIN_LEFT 44  // 4 digits line numbers + separator

#define COLOR_BG          0xFFFFFF
#define COLOR_TEXT        0x1A1A1A
#define COLOR_TOOLBAR     0xF0F0F0
#define COLOR_STATUSBAR   0xE8E8E8
#define COLOR_BORDER      0xD0D0D0
#define COLOR_BTN_BG      0xE0E0E0
#define COLOR_BTN_BORDER  0x999999
#define COLOR_BTN_TEXT    0x000000
#define COLOR_LINE_NUM_BG 0xF7F7F7
#define COLOR_LINE_NUM    0x888888
#define COLOR_CURSOR      0x0055CC

#define MAX_ROWS 1000
#define MAX_COLS 256

static uint32_t g_fb[MAX_WIN_W * MAX_WIN_H];
static char g_rows[MAX_ROWS][MAX_COLS];
static int g_row_len[MAX_ROWS];
static int g_num_rows = 0;

static int g_cx = 0;
static int g_cy = 0;
static int g_row_off = 0;
static int g_col_off = 0;
static int g_dirty = 0;

static char g_filename[64] = "/DESKTOP/UNTITLED.TXT";
static int g_is_untitled = 1;
static char g_status_msg[128] = "Press Ctrl+S or click [Save] to save file";
static int g_cursor_blink = 1;

static woos_file_dialog_t g_dialog;

static int g_is_dragging_scrollbar = 0;
static int g_scrollbar_drag_start_y = 0;
static int g_scrollbar_drag_start_row = 0;

static int visible_rows(void) {
    return (g_win_h - TOOLBAR_H - STATUSBAR_H) / LINE_H;
}

static int visible_cols(void) {
    return (g_win_w - MARGIN_LEFT - 4) / CHAR_W;
}

static inline int utf8_charlen(const char* s, int max_len) {
    int count = 0;
    int i = 0;
    while (i < max_len && s[i] != '\0') {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) i += 1;
        else if ((c & 0xE0) == 0xC0) i += 2;
        else if ((c & 0xF0) == 0xE0) i += 3;
        else if ((c & 0xF8) == 0xF0) i += 4;
        else i += 1;
        count++;
    }
    return count;
}

static void set_status(const char* msg) {
    strncpy(g_status_msg, msg, sizeof(g_status_msg) - 1);
    g_status_msg[sizeof(g_status_msg) - 1] = '\0';
}

static void row_insert_char(int at_row, int at_col, char c) {
    if (at_row < 0 || at_row >= MAX_ROWS) return;
    int len = g_row_len[at_row];
    if (len >= MAX_COLS - 2) return;
    if (at_col < 0) at_col = 0;
    if (at_col > len) at_col = len;

    for (int i = len; i > at_col; i--) {
        g_rows[at_row][i] = g_rows[at_row][i - 1];
    }
    g_rows[at_row][at_col] = c;
    g_row_len[at_row]++;
    g_rows[at_row][g_row_len[at_row]] = '\0';
    g_dirty = 1;
}

static void row_delete_char(int at_row, int at_col) {
    if (at_row < 0 || at_row >= g_num_rows) return;
    int len = g_row_len[at_row];
    if (at_col < 0 || at_col >= len) return;

    for (int i = at_col; i < len - 1; i++) {
        g_rows[at_row][i] = g_rows[at_row][i + 1];
    }
    g_row_len[at_row]--;
    g_rows[at_row][g_row_len[at_row]] = '\0';
    g_dirty = 1;
}

static void insert_row(int at_row, const char* s, int len) {
    if (g_num_rows >= MAX_ROWS - 1 || at_row < 0 || at_row > g_num_rows) return;

    for (int i = g_num_rows; i > at_row; i--) {
        memcpy(g_rows[i], g_rows[i - 1], MAX_COLS);
        g_row_len[i] = g_row_len[i - 1];
    }

    if (len >= MAX_COLS) len = MAX_COLS - 1;
    if (s && len > 0) {
        memcpy(g_rows[at_row], s, len);
    }
    g_rows[at_row][len] = '\0';
    g_row_len[at_row] = len;
    g_num_rows++;
    g_dirty = 1;
}

static void delete_row(int at_row) {
    if (at_row < 0 || at_row >= g_num_rows) return;

    for (int i = at_row; i < g_num_rows - 1; i++) {
        memcpy(g_rows[i], g_rows[i + 1], MAX_COLS);
        g_row_len[i] = g_row_len[i + 1];
    }
    g_num_rows--;
    if (g_num_rows == 0) {
        insert_row(0, "", 0);
    }
    g_dirty = 1;
}

static void load_file(const char* path) {
    g_num_rows = 0;
    strncpy(g_filename, path, sizeof(g_filename) - 1);
    g_filename[sizeof(g_filename) - 1] = '\0';

    int handle = woos_vfs_open(path, VFS_MODE_READ);
    if (handle < 0) {
        insert_row(0, "", 0);
        set_status("New file (empty)");
        g_dirty = 0;
        return;
    }

    uint32_t file_size = woos_vfs_size(handle);
    static char buf[4096];
    uint32_t remaining = file_size;

    char line_buf[MAX_COLS];
    int line_len = 0;

    while (remaining > 0) {
        uint32_t to_read = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        uint32_t bytes_read = woos_vfs_read(handle, buf, to_read);
        if (bytes_read == 0) break;

        for (uint32_t i = 0; i < bytes_read; i++) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                insert_row(g_num_rows, line_buf, line_len);
                line_len = 0;
                if (g_num_rows >= MAX_ROWS - 1) break;
            } else {
                // Binary and control characters handling
                if (c == '\0') {
                    c = '.';
                } else if ((unsigned char)c < 32 && c != '\t') {
                    c = '.';
                } else if ((unsigned char)c > 126) {
                    c = '?';
                }

                if (line_len < MAX_COLS - 2) {
                    line_buf[line_len++] = c;
                }
                // Automatically wrap lines at 72 chars for binary streams without newlines
                if (line_len >= 72) {
                    insert_row(g_num_rows, line_buf, line_len);
                    line_len = 0;
                    if (g_num_rows >= MAX_ROWS - 1) break;
                }
            }
        }
        remaining -= bytes_read;
        if (g_num_rows >= MAX_ROWS - 1) break;
    }

    if (line_len > 0 || g_num_rows == 0) {
        insert_row(g_num_rows, line_buf, line_len);
    }

    woos_vfs_close(handle);
    g_cx = 0;
    g_cy = 0;
    g_row_off = 0;
    g_col_off = 0;
    g_dirty = 0;
    g_is_untitled = 0;
    set_status("File loaded successfully");
}

static void new_file(void) {
    g_num_rows = 0;
    insert_row(0, "", 0);
    g_cx = 0;
    g_cy = 0;
    g_row_off = 0;
    g_col_off = 0;
    g_dirty = 0;
    g_is_untitled = 1;
    strcpy(g_filename, "/DESKTOP/UNTITLED.TXT");
    set_status("New document (empty)");
}

static void save_file(void) {
    int handle = woos_vfs_open(g_filename, VFS_MODE_WRITE | VFS_MODE_CREATE | VFS_MODE_TRUNC);
    if (handle < 0) {
        set_status("Error: Failed to open file for writing!");
        return;
    }

    uint32_t total_written = 0;
    for (int i = 0; i < g_num_rows; i++) {
        if (g_row_len[i] > 0) {
            total_written += woos_vfs_write(handle, g_rows[i], g_row_len[i]);
        }
        total_written += woos_vfs_write(handle, "\n", 1);
    }

    woos_vfs_close(handle);
    g_dirty = 0;
    g_is_untitled = 0;
    set_status("File saved successfully to FAT32 storage!");
}

static void scroll(void) {
    int v_rows = visible_rows();
    int v_cols = visible_cols();

    if (g_cy < g_row_off) {
        g_row_off = g_cy;
    }
    if (g_cy >= g_row_off + v_rows) {
        g_row_off = g_cy - v_rows + 1;
    }
    if (g_cx < g_col_off) {
        g_col_off = g_cx;
    }
    if (g_cx >= g_col_off + v_cols) {
        g_col_off = g_cx - v_cols + 1;
    }
}


static void draw_editor(void) {
    scroll();

    int v_rows = visible_rows();
    int v_cols = visible_cols();
    int text_h = g_win_h - TOOLBAR_H - STATUSBAR_H;

    // 1. Toolbar background and border
    fill_rect(g_fb, g_win_w, g_win_h, 0, 0, g_win_w, TOOLBAR_H, COLOR_TOOLBAR);
    fill_rect(g_fb, g_win_w, g_win_h, 0, TOOLBAR_H - 1, g_win_w, 1, COLOR_BORDER);

    // Button [Save] (6..52)
    fill_rect(g_fb, g_win_w, g_win_h, 6, 3, 46, 18, COLOR_BTN_BG);
    draw_rect(g_fb, g_win_w, g_win_h, 6, 3, 46, 18, COLOR_BTN_BORDER);
    woos_draw_text(g_fb, g_win_w, g_win_h, 14, 8, "Save", COLOR_BTN_TEXT);

    // Button [Save As] (56..122)
    fill_rect(g_fb, g_win_w, g_win_h, 56, 3, 66, 18, COLOR_BTN_BG);
    draw_rect(g_fb, g_win_w, g_win_h, 56, 3, 66, 18, COLOR_BTN_BORDER);
    woos_draw_text(g_fb, g_win_w, g_win_h, 62, 8, "Save As", COLOR_BTN_TEXT);

    // Button [Open] (126..172)
    fill_rect(g_fb, g_win_w, g_win_h, 126, 3, 46, 18, COLOR_BTN_BG);
    draw_rect(g_fb, g_win_w, g_win_h, 126, 3, 46, 18, COLOR_BTN_BORDER);
    woos_draw_text(g_fb, g_win_w, g_win_h, 134, 8, "Open", COLOR_BTN_TEXT);

    // Button [Reload] (176..232)
    fill_rect(g_fb, g_win_w, g_win_h, 176, 3, 56, 18, COLOR_BTN_BG);
    draw_rect(g_fb, g_win_w, g_win_h, 176, 3, 56, 18, COLOR_BTN_BORDER);
    woos_draw_text(g_fb, g_win_w, g_win_h, 182, 8, "Reload", COLOR_BTN_TEXT);

    // Filename in toolbar
    char title_buf[80];
    int pos = 0;
    const char* fn = g_filename;
    while (*fn && pos < 40) title_buf[pos++] = *fn++;
    if (g_dirty) {
        title_buf[pos++] = ' ';
        title_buf[pos++] = '*';
    }
    title_buf[pos] = '\0';
    woos_draw_text(g_fb, g_win_w, g_win_h, 238, 8, title_buf, 0x003366);

    // 2. Line numbers margin
    fill_rect(g_fb, g_win_w, g_win_h, 0, TOOLBAR_H, MARGIN_LEFT, text_h, COLOR_LINE_NUM_BG);
    fill_rect(g_fb, g_win_w, g_win_h, MARGIN_LEFT - 1, TOOLBAR_H, 1, text_h, COLOR_BORDER);

    // 3. Text area background
    fill_rect(g_fb, g_win_w, g_win_h, MARGIN_LEFT, TOOLBAR_H, g_win_w - MARGIN_LEFT, text_h, COLOR_BG);

    // 4. Text rows
    for (int y = 0; y < v_rows; y++) {
        int file_row = g_row_off + y;
        int screen_y = TOOLBAR_H + y * LINE_H + 2;

        if (file_row < g_num_rows) {
            // Draw line number
            char num_str[16];
            int_to_str(file_row + 1, num_str);
            int num_len = (int)strlen(num_str);
            int num_x = MARGIN_LEFT - 8 - num_len * CHAR_W;
            if (num_x < 4) num_x = 4;
            woos_draw_text(g_fb, g_win_w, g_win_h, num_x, screen_y, num_str, COLOR_LINE_NUM);

            // Draw line text
            int len = g_row_len[file_row];
            if (len > g_col_off) {
                int draw_len = len - g_col_off;
                if (draw_len > v_cols) draw_len = v_cols;

                char visible_text[MAX_COLS];
                memcpy(visible_text, &g_rows[file_row][g_col_off], draw_len);
                visible_text[draw_len] = '\0';
                woos_draw_text(g_fb, g_win_w, g_win_h, MARGIN_LEFT + 4, screen_y, visible_text, COLOR_TEXT);
            }
        }
    }

    // 5. Draw cursor
    if (g_cursor_blink && g_cy >= g_row_off && g_cy < g_row_off + v_rows) {
        int visual_col = utf8_charlen(g_rows[g_cy], g_cx);
        if (visual_col >= g_col_off && visual_col <= g_col_off + v_cols) {
            int cursor_screen_x = MARGIN_LEFT + 4 + (visual_col - g_col_off) * CHAR_W;
            int cursor_screen_y = TOOLBAR_H + (g_cy - g_row_off) * LINE_H + 1;
            fill_rect(g_fb, g_win_w, g_win_h, cursor_screen_x, cursor_screen_y, 2, LINE_H - 1, COLOR_CURSOR);
        }
    }

    // Vertical scrollbar
    int editor_h = g_win_h - TOOLBAR_H - STATUSBAR_H;
    int max_s = (g_num_rows > v_rows) ? (g_num_rows - v_rows) : 0;
    if (max_s > 0) {
        int sb_w = 14;
        int sb_x = g_win_w - sb_w;
        fill_rect(g_fb, g_win_w, g_win_h, sb_x, TOOLBAR_H, sb_w, editor_h, 0xEEF2F7);
        fill_rect(g_fb, g_win_w, g_win_h, sb_x, TOOLBAR_H, 1, editor_h, 0xCBD5E1);
        int thumb_h = (editor_h * v_rows) / g_num_rows;
        if (thumb_h < 20) thumb_h = 20;
        int track_avail = editor_h - thumb_h;
        int thumb_y = TOOLBAR_H + (g_row_off * track_avail) / max_s;
        fill_rect(g_fb, g_win_w, g_win_h, sb_x + 2, thumb_y, sb_w - 4, thumb_h, g_is_dragging_scrollbar ? 0x64748B : 0x94A3B8);
    }

    // 6. Status bar
    int status_y = g_win_h - STATUSBAR_H;
    fill_rect(g_fb, g_win_w, g_win_h, 0, status_y, g_win_w, STATUSBAR_H, COLOR_STATUSBAR);
    fill_rect(g_fb, g_win_w, g_win_h, 0, status_y, g_win_w, 1, COLOR_BORDER);

    woos_draw_text(g_fb, g_win_w, g_win_h, 8, status_y + 6, g_status_msg, 0x333333);

    // Line / Column on right
    char pos_buf[32];
    char r_str[8], c_str[8];
    int_to_str(g_cy + 1, r_str);
    int_to_str(g_cx + 1, c_str);
    int p = 0;
    pos_buf[p++] = 'L'; pos_buf[p++] = 'n'; pos_buf[p++] = ' ';
    for (int i = 0; r_str[i]; i++) pos_buf[p++] = r_str[i];
    pos_buf[p++] = ','; pos_buf[p++] = ' '; pos_buf[p++] = 'C'; pos_buf[p++] = 'o'; pos_buf[p++] = 'l'; pos_buf[p++] = ' ';
    for (int i = 0; c_str[i]; i++) pos_buf[p++] = c_str[i];
    pos_buf[p] = '\0';

    int pos_x = g_win_w - (int)strlen(pos_buf) * CHAR_W - 12;
    woos_draw_text(g_fb, g_win_w, g_win_h, pos_x, status_y + 6, pos_buf, 0x222222);

    // 7. Modal Dialog (Save As / Open)
    if (g_dialog.active) {
        woos_dialog_draw(g_fb, g_win_w, g_win_h, &g_dialog);
    }
}

static void apply_dialog_result(void) {
    if (g_dialog.input[0]) {
        if (g_dialog.mode == DIALOG_MODE_SAVE_AS) {
            if (g_dialog.input[0] != '/') {
                strcpy(g_filename, "/DESKTOP/");
                strcat(g_filename, g_dialog.input);
            } else {
                strncpy(g_filename, g_dialog.input, sizeof(g_filename) - 1);
                g_filename[sizeof(g_filename) - 1] = '\0';
            }
            g_is_untitled = 0;
            save_file();
        } else if (g_dialog.mode == DIALOG_MODE_OPEN) {
            char open_path[64];
            if (g_dialog.input[0] != '/') {
                strcpy(open_path, "/DESKTOP/");
                strcat(open_path, g_dialog.input);
            } else {
                strncpy(open_path, g_dialog.input, sizeof(open_path) - 1);
                open_path[sizeof(open_path) - 1] = '\0';
            }
            load_file(open_path);
        }
    }
}

static void handle_click(int x, int y) {
    if (g_dialog.active) {
        int res = woos_dialog_handle_click(&g_dialog, g_win_w, g_win_h, x, y);
        if (res == WOOS_DIALOG_RES_OK) {
            apply_dialog_result();
        }
        return;
    }

    if (y < TOOLBAR_H) {
        // [Save]
        if (x >= 6 && x <= 52) {
            if (g_is_untitled) {
                woos_dialog_open(&g_dialog, DIALOG_MODE_SAVE_AS, "Save File As...", "Enter destination path on disk:", g_filename, "Save");
            } else {
                save_file();
            }
        }
        // [Save As]
        else if (x >= 56 && x <= 122) {
            woos_dialog_open(&g_dialog, DIALOG_MODE_SAVE_AS, "Save File As...", "Enter destination path on disk:", g_filename, "Save");
        }
        // [Open]
        else if (x >= 126 && x <= 172) {
            woos_dialog_open(&g_dialog, DIALOG_MODE_OPEN, "Open File...", "Enter file path to open:", "/DESKTOP/", "Open");
        }
        // [Reload]
        else if (x >= 176 && x <= 232) {
            load_file(g_filename);
        }
        return;
    }

    if (y >= TOOLBAR_H && y < g_win_h - STATUSBAR_H) {
        int v_rows = visible_rows();
        int max_s = (g_num_rows > v_rows) ? (g_num_rows - v_rows) : 0;
        if (max_s > 0 && x >= g_win_w - 16) {
            int editor_h = g_win_h - TOOLBAR_H - STATUSBAR_H;
            int thumb_h = (editor_h * v_rows) / g_num_rows;
            if (thumb_h < 20) thumb_h = 20;
            int track_avail = editor_h - thumb_h;
            int thumb_y = TOOLBAR_H + (g_row_off * track_avail) / max_s;
            if (y >= thumb_y && y <= thumb_y + thumb_h) {
                g_is_dragging_scrollbar = 1;
                g_scrollbar_drag_start_y = y;
                g_scrollbar_drag_start_row = g_row_off;
            } else if (y < thumb_y) {
                g_row_off -= (v_rows - 2);
                if (g_row_off < 0) g_row_off = 0;
            } else {
                g_row_off += (v_rows - 2);
                if (g_row_off > max_s) g_row_off = max_s;
            }
            return;
        }

        if (g_num_rows <= 0) {
            g_cy = 0;
            g_cx = 0;
        } else {
            int clicked_row = g_row_off + (y - TOOLBAR_H) / LINE_H;
            if (clicked_row < 0) clicked_row = 0;
            if (clicked_row >= g_num_rows) clicked_row = g_num_rows - 1;
            g_cy = clicked_row;

            int text_x = x - (MARGIN_LEFT + 4);
            int clicked_col = 0;
            if (text_x > 0) {
                clicked_col = g_col_off + (text_x + CHAR_W / 2) / CHAR_W;
            }
            if (clicked_col < 0) clicked_col = 0;
            if (clicked_col > g_row_len[g_cy]) clicked_col = g_row_len[g_cy];
            g_cx = clicked_col;
        }
        g_cursor_blink = 1;
        set_status("Cursor moved");
    }
}

static void handle_mouse_move(int x, int y) {
    (void)x;
    if (g_is_dragging_scrollbar) {
        int v_rows = visible_rows();
        int max_s = (g_num_rows > v_rows) ? (g_num_rows - v_rows) : 0;
        int editor_h = g_win_h - TOOLBAR_H - STATUSBAR_H;
        int thumb_h = (editor_h * v_rows) / (g_num_rows > 0 ? g_num_rows : 1);
        if (thumb_h < 20) thumb_h = 20;
        int track_avail = editor_h - thumb_h;
        if (track_avail > 0 && max_s > 0) {
            int dy = y - g_scrollbar_drag_start_y;
            int new_row = g_scrollbar_drag_start_row + (dy * max_s) / track_avail;
            if (new_row < 0) new_row = 0;
            if (new_row > max_s) new_row = max_s;
            g_row_off = new_row;
        }
    }
}

static void handle_mouse_up(int x, int y) {
    (void)x; (void)y;
    g_is_dragging_scrollbar = 0;
}

static void handle_key(int ascii, int scancode) {
    if (g_dialog.active) {
        int res = woos_dialog_handle_key(&g_dialog, ascii, scancode);
        if (res == WOOS_DIALOG_RES_OK) {
            apply_dialog_result();
        }
        return;
    }

    // Ctrl+S: Save
    if (ascii == 19 || (ascii == 's' && (scancode == 0x1F))) {
        if (g_is_untitled) {
            woos_dialog_open(&g_dialog, DIALOG_MODE_SAVE_AS, "Save File As...", "Enter destination path on disk:", g_filename, "Save");
        } else {
            save_file();
        }
        return;
    }

    // Enter
    if (ascii == '\n' || ascii == '\r' || scancode == 0x1C) {
        int len = g_row_len[g_cy];
        if (g_cx >= len) {
            insert_row(g_cy + 1, "", 0);
        } else {
            insert_row(g_cy + 1, &g_rows[g_cy][g_cx], len - g_cx);
            g_rows[g_cy][g_cx] = '\0';
            g_row_len[g_cy] = g_cx;
        }
        g_cy++;
        g_cx = 0;
        return;
    }

    // Delete key (scancode 0x53 or ascii 127)
    if (scancode == 0x53) {
        int len = g_row_len[g_cy];
        if (g_cx < len) {
            row_delete_char(g_cy, g_cx);
        } else if (g_cy < g_num_rows - 1) {
            int next_len = g_row_len[g_cy + 1];
            if (len + next_len < MAX_COLS - 1) {
                memcpy(&g_rows[g_cy][len], g_rows[g_cy + 1], next_len);
                g_row_len[g_cy] += next_len;
                g_rows[g_cy][g_row_len[g_cy]] = '\0';
                delete_row(g_cy + 1);
            }
        }
        return;
    }

    // Backspace
    if (ascii == '\b' || scancode == 0x0E) {
        if (g_cx > 0) {
            int del = 1;
            if (g_cx >= 2 && ((unsigned char)g_rows[g_cy][g_cx - 1] & 0xC0) == 0x80) {
                del = 2;
            }
            for (int k = 0; k < del; k++) {
                row_delete_char(g_cy, g_cx - 1);
                g_cx--;
            }
        } else if (g_cy > 0) {
            int prev_len = g_row_len[g_cy - 1];
            int cur_len = g_row_len[g_cy];
            if (prev_len + cur_len < MAX_COLS - 1) {
                memcpy(&g_rows[g_cy - 1][prev_len], g_rows[g_cy], cur_len);
                g_row_len[g_cy - 1] += cur_len;
                g_rows[g_cy - 1][g_row_len[g_cy - 1]] = '\0';
                delete_row(g_cy);
                g_cy--;
                g_cx = prev_len;
            }
        }
        return;
    }

    // Arrow keys
    // Up
    if (scancode == 0x48) {
        if (g_cy > 0) g_cy--;
        if (g_cx > g_row_len[g_cy]) g_cx = g_row_len[g_cy];
        return;
    }
    // Down
    if (scancode == 0x50) {
        if (g_cy < g_num_rows - 1) g_cy++;
        if (g_cx > g_row_len[g_cy]) g_cx = g_row_len[g_cy];
        return;
    }
    // Left
    if (scancode == 0x4B) {
        if (g_cx > 0) {
            g_cx--;
        } else if (g_cy > 0) {
            g_cy--;
            g_cx = g_row_len[g_cy];
        }
        return;
    }
    // Right
    if (scancode == 0x4D) {
        if (g_cx < g_row_len[g_cy]) {
            g_cx++;
        } else if (g_cy < g_num_rows - 1) {
            g_cy++;
            g_cx = 0;
        }
        return;
    }
    // Home
    if (scancode == 0x47) {
        g_cx = 0;
        return;
    }
    // End
    if (scancode == 0x4F) {
        g_cx = g_row_len[g_cy];
        return;
    }
    // Page Up
    if (scancode == 0x49) {
        g_cy -= visible_rows();
        if (g_cy < 0) g_cy = 0;
        if (g_cx > g_row_len[g_cy]) g_cx = g_row_len[g_cy];
        return;
    }
    // Page Down
    if (scancode == 0x51) {
        g_cy += visible_rows();
        if (g_cy >= g_num_rows) g_cy = g_num_rows - 1;
        if (g_cx > g_row_len[g_cy]) g_cx = g_row_len[g_cy];
        return;
    }

    // Normal printable character
    if (ascii >= 32 && ascii <= 126) {
        row_insert_char(g_cy, g_cx, (char)ascii);
        g_cx++;
        return;
    } else if (ascii >= 0x0400 && ascii <= 0x04FF) {
        uint8_t b1 = (uint8_t)(0xC0 | ((ascii >> 6) & 0x1F));
        uint8_t b2 = (uint8_t)(0x80 | (ascii & 0x3F));
        row_insert_char(g_cy, g_cx, (char)b1);
        row_insert_char(g_cy, g_cx + 1, (char)b2);
        g_cx += 2;
        return;
    }
}

__attribute__((export_name("_start")))
void _start(void) {
    int win_id = woos_create_window(g_win_w, g_win_h, "Notepad", 0);
    if (win_id < 0) return;

    char open_buf[64] = "";
    if (woos_system_get_open_file(open_buf, sizeof(open_buf)) > 0 && open_buf[0] != '\0') {
        load_file(open_buf);
        woos_system_clear_open_file();
    } else {
        new_file();
    }

    draw_editor();
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
            } else if (msg.type == MSG_OPEN_FILE) {
                char req_buf[64] = "";
                if (woos_system_get_open_file(req_buf, sizeof(req_buf)) > 0 && req_buf[0] != '\0') {
                    load_file(req_buf);
                    woos_system_clear_open_file();
                    needs_redraw = 1;
                }
            } else if (msg.type == MSG_CLICK) {
                handle_click((int)msg.arg1, (int)msg.arg2);
                needs_redraw = 1;
            } else if (msg.type == MSG_MOUSE_MOVE) {
                handle_mouse_move((int)msg.arg1, (int)msg.arg2);
                needs_redraw = 1;
            } else if (msg.type == MSG_MOUSE_UP) {
                handle_mouse_up((int)msg.arg1, (int)msg.arg2);
                needs_redraw = 1;
            } else if (msg.type == MSG_KEY_DOWN) {
                handle_key((int)msg.arg1, (int)msg.arg2);
                needs_redraw = 1;
            } else if (msg.type == MSG_SCROLL) {
                int8_t dz = (int8_t)(int32_t)msg.arg3;
                g_row_off -= (int)dz * 3;
                if (g_row_off < 0) g_row_off = 0;
                int max_s = (g_num_rows > visible_rows()) ? (g_num_rows - visible_rows()) : 0;
                if (g_row_off > max_s) g_row_off = max_s;
                needs_redraw = 1;
            }
        }

        if (needs_redraw) {
            draw_editor();
            woos_blit_window(win_id, g_fb, g_win_w, g_win_h);
        }

        woos_ipc_wait_message();
    }
}
