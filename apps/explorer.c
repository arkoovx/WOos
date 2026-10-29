#include "woos.h"

#define WIN_W 640
#define WIN_H 420

#define COLOR_WINDOW_BG   0xECE9D8
#define COLOR_SIDEBAR_BG  0xF6F6F6
#define COLOR_LIST_BG     0xFFFFFF
#define COLOR_TEXT        0x000000
#define COLOR_TEXT_MUTED  0x555555
#define COLOR_SELECT_BG   0x316AC5
#define COLOR_SELECT_TEXT 0xFFFFFF
#define COLOR_BORDER_DARK 0x716F64
#define COLOR_BORDER_LGT  0xFFFFFF
#define COLOR_HEADER_BG   0xE4E2D5

#define SIDEBAR_W   145
#define TOOLBAR_H   50
#define STATUSBAR_H 22
#define HEADER_H    20
#define ROW_H       20

#define MAX_ITEMS   128

typedef struct {
    char name[24];
    uint8_t is_dir;
    uint32_t size;
} file_item_t;

static uint32_t g_fb[WIN_W * WIN_H];
static file_item_t g_items[MAX_ITEMS];
static int g_item_count = 0;
static int g_selected_idx = 0;
static int g_scroll_offset = 0;
static char g_current_path[64] = "/";
static uint32_t g_last_click_tick = 0;
static int g_last_click_idx = -1;

static void int_to_str(int val, char* dst) {
    if (val == 0) {
        dst[0] = '0';
        dst[1] = '\0';
        return;
    }
    char tmp[16];
    int idx = 0;
    while (val > 0) {
        tmp[idx++] = (char)('0' + (val % 10));
        val /= 10;
    }
    int out = 0;
    for (int i = idx - 1; i >= 0; i--) {
        dst[out++] = tmp[i];
    }
    dst[out] = '\0';
}

static void format_size(uint32_t bytes, char* out) {
    if (bytes < 1024) {
        char s[16];
        int_to_str((int)bytes, s);
        int p = 0;
        for (int i = 0; s[i]; i++) out[p++] = s[i];
        out[p++] = ' ';
        out[p++] = 'B';
        out[p] = '\0';
    } else {
        uint32_t kb = bytes / 1024;
        uint32_t frac = (bytes % 1024) * 10 / 1024;
        char s[16];
        int_to_str((int)kb, s);
        int p = 0;
        for (int i = 0; s[i]; i++) out[p++] = s[i];
        out[p++] = '.';
        out[p++] = (char)('0' + frac);
        out[p++] = ' ';
        out[p++] = 'K';
        out[p++] = 'B';
        out[p] = '\0';
    }
}

static const char* get_file_type(const char* name, int is_dir) {
    if (is_dir) return "File Folder";
    int len = (int)strlen(name);
    if (len >= 4) {
        const char* ext = &name[len - 4];
        if (strcmp(ext, ".WAS") == 0 || strcmp(ext, ".was") == 0) return "WoOS Application";
        if (strcmp(ext, ".TXT") == 0 || strcmp(ext, ".txt") == 0) return "Text Document";
        if (strcmp(ext, ".BIN") == 0 || strcmp(ext, ".bin") == 0) return "Binary Image";
        if (strcmp(ext, ".BMP") == 0 || strcmp(ext, ".bmp") == 0) return "Bitmap Image";
    }
    return "System File";
}

static void draw_folder_icon(int x, int y) {
    // Top tab
    fill_rect(g_fb, WIN_W, WIN_H, x + 1, y, 6, 3, 0xD4AC0D);
    // Folder back
    fill_rect(g_fb, WIN_W, WIN_H, x, y + 2, 14, 10, 0xF4D03F);
    // Darker front flap
    fill_rect(g_fb, WIN_W, WIN_H, x, y + 5, 14, 7, 0xF39C12);
    // Border
    draw_rect(g_fb, WIN_W, WIN_H, x, y + 2, 14, 10, 0xB7950B);
}

static void draw_app_icon(int x, int y) {
    // Window frame
    fill_rect(g_fb, WIN_W, WIN_H, x, y, 13, 13, 0xBDC3C7);
    draw_rect(g_fb, WIN_W, WIN_H, x, y, 13, 13, 0x2C3E50);
    // Blue titlebar
    fill_rect(g_fb, WIN_W, WIN_H, x + 1, y + 1, 11, 3, 0x2980B9);
    // Window body
    fill_rect(g_fb, WIN_W, WIN_H, x + 1, y + 4, 11, 8, 0xFFFFFF);
    // Inner gear / graphic
    fill_rect(g_fb, WIN_W, WIN_H, x + 3, y + 6, 7, 4, 0xE74C3C);
}

static void draw_text_icon(int x, int y) {
    // Page body
    fill_rect(g_fb, WIN_W, WIN_H, x, y, 11, 13, 0xFFFFFF);
    draw_rect(g_fb, WIN_W, WIN_H, x, y, 11, 13, 0x7F8C8D);
    // Folded corner
    fill_rect(g_fb, WIN_W, WIN_H, x + 7, y, 4, 4, 0xBDC3C7);
    // Lines of text
    fill_rect(g_fb, WIN_W, WIN_H, x + 2, y + 4, 5, 1, 0x34495E);
    fill_rect(g_fb, WIN_W, WIN_H, x + 2, y + 6, 7, 1, 0x34495E);
    fill_rect(g_fb, WIN_W, WIN_H, x + 2, y + 8, 7, 1, 0x34495E);
    fill_rect(g_fb, WIN_W, WIN_H, x + 2, y + 10, 4, 1, 0x34495E);
}

static void draw_drive_icon(int x, int y) {
    fill_rect(g_fb, WIN_W, WIN_H, x, y + 1, 14, 9, 0xBDC3C7);
    draw_rect(g_fb, WIN_W, WIN_H, x, y + 1, 14, 9, 0x7F8C8D);
    fill_rect(g_fb, WIN_W, WIN_H, x + 2, y + 7, 8, 2, 0x34495E);
    fill_rect(g_fb, WIN_W, WIN_H, x + 11, y + 7, 2, 2, 0x2ECC71); // Green LED
}

static void load_directory(const char* path) {
    g_item_count = 0;
    g_selected_idx = 0;
    g_scroll_offset = 0;
    strncpy(g_current_path, path, sizeof(g_current_path) - 1);

    int handle = woos_vfs_open(path, VFS_MODE_READ);
    if (handle < 0) {
        return;
    }

    woos_dirent_t entry;
    while (g_item_count < MAX_ITEMS) {
        int res = woos_vfs_readdir(handle, &entry);
        if (res <= 0) break;
        if (entry.name[0] == '.' && (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) {
            continue;
        }
        memcpy(g_items[g_item_count].name, entry.name, 24);
        g_items[g_item_count].is_dir = entry.is_dir;
        g_items[g_item_count].size = entry.size;
        g_item_count++;
    }
    woos_vfs_close(handle);

    // Sort: directories first, then alphabetical
    for (int i = 0; i < g_item_count - 1; i++) {
        for (int j = i + 1; j < g_item_count; j++) {
            int swap = 0;
            if (g_items[j].is_dir && !g_items[i].is_dir) {
                swap = 1;
            } else if (g_items[i].is_dir == g_items[j].is_dir) {
                if (strcmp(g_items[i].name, g_items[j].name) > 0) {
                    swap = 1;
                }
            }
            if (swap) {
                file_item_t tmp = g_items[i];
                g_items[i] = g_items[j];
                g_items[j] = tmp;
            }
        }
    }
}

static void draw_explorer(void) {
    // 1. Toolbar background
    fill_rect(g_fb, WIN_W, WIN_H, 0, 0, WIN_W, TOOLBAR_H, COLOR_WINDOW_BG);

    // Toolbar buttons: [Up], [Refresh], [Notepad], [Terminal]
    // [Up] button
    draw_3d_rect(g_fb, WIN_W, WIN_H, 6, 4, 52, 22, 0);
    fill_rect(g_fb, WIN_W, WIN_H, 7, 5, 50, 20, 0xE0DFD8);
    woos_draw_text(g_fb, WIN_W, WIN_H, 18, 9, "Up", COLOR_TEXT);

    // [Refresh] button
    draw_3d_rect(g_fb, WIN_W, WIN_H, 64, 4, 68, 22, 0);
    fill_rect(g_fb, WIN_W, WIN_H, 65, 5, 66, 20, 0xE0DFD8);
    woos_draw_text(g_fb, WIN_W, WIN_H, 72, 9, "Refresh", COLOR_TEXT);

    // [Notepad] button
    draw_3d_rect(g_fb, WIN_W, WIN_H, 138, 4, 68, 22, 0);
    fill_rect(g_fb, WIN_W, WIN_H, 139, 5, 66, 20, 0xE0DFD8);
    woos_draw_text(g_fb, WIN_W, WIN_H, 146, 9, "Notepad", COLOR_TEXT);

    // [Terminal] button
    draw_3d_rect(g_fb, WIN_W, WIN_H, 212, 4, 72, 22, 0);
    fill_rect(g_fb, WIN_W, WIN_H, 213, 5, 70, 20, 0xE0DFD8);
    woos_draw_text(g_fb, WIN_W, WIN_H, 220, 9, "Terminal", COLOR_TEXT);

    // Address Bar row (y=28..48)
    woos_draw_text(g_fb, WIN_W, WIN_H, 8, 33, "Address", COLOR_TEXT);
    draw_3d_rect(g_fb, WIN_W, WIN_H, 64, 28, WIN_W - 70, 20, 1);
    fill_rect(g_fb, WIN_W, WIN_H, 65, 29, WIN_W - 72, 18, 0xFFFFFF);

    draw_drive_icon(70, 32);
    char addr_str[80];
    strcpy(addr_str, "C:\\");
    if (g_current_path[0] == '/' && g_current_path[1] != '\0') {
        int p = 3;
        for (int i = 1; g_current_path[i] && p < 75; i++) {
            addr_str[p++] = g_current_path[i] == '/' ? '\\' : g_current_path[i];
        }
        addr_str[p] = '\0';
    }
    woos_draw_text(g_fb, WIN_W, WIN_H, 90, 33, addr_str, COLOR_TEXT);

    // Horizontal separator
    fill_rect(g_fb, WIN_W, WIN_H, 0, TOOLBAR_H - 1, WIN_W, 1, 0xACA899);

    // 2. Left Sidebar (Folders pane)
    int content_h = WIN_H - TOOLBAR_H - STATUSBAR_H;
    fill_rect(g_fb, WIN_W, WIN_H, 0, TOOLBAR_H, SIDEBAR_W, content_h, COLOR_SIDEBAR_BG);
    fill_rect(g_fb, WIN_W, WIN_H, SIDEBAR_W - 1, TOOLBAR_H, 1, content_h, 0xACA899);

    // Sidebar Title
    fill_rect(g_fb, WIN_W, WIN_H, 0, TOOLBAR_H, SIDEBAR_W - 1, 20, 0x2A62C9);
    woos_draw_text(g_fb, WIN_W, WIN_H, 8, TOOLBAR_H + 4, "System Tasks", 0xFFFFFF);

    // Sidebar Items
    int sb_y = TOOLBAR_H + 28;
    draw_drive_icon(8, sb_y);
    woos_draw_text(g_fb, WIN_W, WIN_H, 26, sb_y, "WOOS_DRV (C:)", 0x003399);

    sb_y += 24;
    draw_folder_icon(8, sb_y);
    woos_draw_text(g_fb, WIN_W, WIN_H, 26, sb_y, "System Root", 0x333333);

    sb_y += 36;
    fill_rect(g_fb, WIN_W, WIN_H, 6, sb_y, SIDEBAR_W - 14, 1, 0xCCCCCC);
    sb_y += 8;
    woos_draw_text(g_fb, WIN_W, WIN_H, 8, sb_y, "Disk Info:", 0x003366);
    sb_y += 18;
    woos_draw_text(g_fb, WIN_W, WIN_H, 8, sb_y, "FS: FAT32", 0x444444);
    sb_y += 16;
    woos_draw_text(g_fb, WIN_W, WIN_H, 8, sb_y, "Size: 64 MB", 0x444444);
    sb_y += 16;
    woos_draw_text(g_fb, WIN_W, WIN_H, 8, sb_y, "Free: ~62 MB", 0x27AE60);

    // 3. Right Pane: Header Bar
    int list_x = SIDEBAR_W;
    int list_w = WIN_W - SIDEBAR_W;
    int list_y = TOOLBAR_H;

    // Header buttons (Name, Size, Type)
    // Name column: 220px
    draw_3d_rect(g_fb, WIN_W, WIN_H, list_x, list_y, 220, HEADER_H, 0);
    fill_rect(g_fb, WIN_W, WIN_H, list_x + 1, list_y + 1, 218, HEADER_H - 2, COLOR_HEADER_BG);
    woos_draw_text(g_fb, WIN_W, WIN_H, list_x + 6, list_y + 4, "Name", COLOR_TEXT);

    // Size column: 110px
    draw_3d_rect(g_fb, WIN_W, WIN_H, list_x + 220, list_y, 110, HEADER_H, 0);
    fill_rect(g_fb, WIN_W, WIN_H, list_x + 221, list_y + 1, 108, HEADER_H - 2, COLOR_HEADER_BG);
    woos_draw_text(g_fb, WIN_W, WIN_H, list_x + 226, list_y + 4, "Size", COLOR_TEXT);

    // Type column: rest
    int type_w = list_w - 330;
    draw_3d_rect(g_fb, WIN_W, WIN_H, list_x + 330, list_y, type_w, HEADER_H, 0);
    fill_rect(g_fb, WIN_W, WIN_H, list_x + 331, list_y + 1, type_w - 2, HEADER_H - 2, COLOR_HEADER_BG);
    woos_draw_text(g_fb, WIN_W, WIN_H, list_x + 336, list_y + 4, "Type", COLOR_TEXT);

    // 4. File List Body
    int file_list_y = list_y + HEADER_H;
    int file_list_h = content_h - HEADER_H;
    fill_rect(g_fb, WIN_W, WIN_H, list_x, file_list_y, list_w, file_list_h, COLOR_LIST_BG);

    int visible_rows = file_list_h / ROW_H;
    for (int r = 0; r < visible_rows; r++) {
        int idx = g_scroll_offset + r;
        if (idx >= g_item_count) break;

        int row_y = file_list_y + r * ROW_H;
        int is_sel = (idx == g_selected_idx);

        if (is_sel) {
            fill_rect(g_fb, WIN_W, WIN_H, list_x, row_y, list_w, ROW_H, COLOR_SELECT_BG);
        }

        uint32_t text_color = is_sel ? COLOR_SELECT_TEXT : COLOR_TEXT;

        // Draw Icon
        int icon_x = list_x + 6;
        int icon_y = row_y + 3;
        if (g_items[idx].is_dir) {
            draw_folder_icon(icon_x, icon_y);
        } else {
            const char* fn = g_items[idx].name;
            int len = (int)strlen(fn);
            if (len >= 4 && (strcmp(&fn[len - 4], ".WAS") == 0 || strcmp(&fn[len - 4], ".was") == 0)) {
                draw_app_icon(icon_x, icon_y);
            } else {
                draw_text_icon(icon_x, icon_y);
            }
        }

        // Draw Name
        woos_draw_text(g_fb, WIN_W, WIN_H, list_x + 26, row_y + 4, g_items[idx].name, text_color);

        // Draw Size
        if (!g_items[idx].is_dir) {
            char size_buf[16];
            format_size(g_items[idx].size, size_buf);
            woos_draw_text(g_fb, WIN_W, WIN_H, list_x + 226, row_y + 4, size_buf, text_color);
        }

        // Draw Type
        const char* type_str = get_file_type(g_items[idx].name, g_items[idx].is_dir);
        woos_draw_text(g_fb, WIN_W, WIN_H, list_x + 336, row_y + 4, type_str, text_color);
    }

    // 5. Status Bar
    int status_y = WIN_H - STATUSBAR_H;
    fill_rect(g_fb, WIN_W, WIN_H, 0, status_y, WIN_W, STATUSBAR_H, COLOR_WINDOW_BG);
    fill_rect(g_fb, WIN_W, WIN_H, 0, status_y, WIN_W, 1, 0xACA899);

    // Left pane in status bar
    draw_3d_rect(g_fb, WIN_W, WIN_H, 2, status_y + 2, 240, 18, 1);
    char status_buf[64];
    char count_str[16];
    int_to_str(g_item_count, count_str);
    int sp = 0;
    for (int i = 0; count_str[i]; i++) status_buf[sp++] = count_str[i];
    const char* obj_word = " object(s)";
    for (int i = 0; obj_word[i]; i++) status_buf[sp++] = obj_word[i];
    if (g_selected_idx >= 0 && g_selected_idx < g_item_count) {
        const char* sel_word = " (1 selected)";
        for (int i = 0; sel_word[i]; i++) status_buf[sp++] = sel_word[i];
    }
    status_buf[sp] = '\0';
    woos_draw_text(g_fb, WIN_W, WIN_H, 8, status_y + 5, status_buf, 0x222222);

    // Right pane in status bar
    draw_3d_rect(g_fb, WIN_W, WIN_H, 246, status_y + 2, WIN_W - 250, 18, 1);
    woos_draw_text(g_fb, WIN_W, WIN_H, 252, status_y + 5, "My Computer | Free: 62.0 MB (FAT32)", 0x222222);
}

static void open_item(int idx) {
    if (idx < 0 || idx >= g_item_count) return;

    if (g_items[idx].is_dir) {
        char new_path[64];
        if (strcmp(g_current_path, "/") == 0) {
            new_path[0] = '/';
            new_path[1] = '\0';
            strcat(new_path, g_items[idx].name);
        } else {
            strcpy(new_path, g_current_path);
            strcat(new_path, "/");
            strcat(new_path, g_items[idx].name);
        }
        load_directory(new_path);
    } else {
        // Build full path
        char full_path[64];
        if (strcmp(g_current_path, "/") == 0) {
            full_path[0] = '/';
            full_path[1] = '\0';
            strcat(full_path, g_items[idx].name);
        } else {
            strcpy(full_path, g_current_path);
            strcat(full_path, "/");
            strcat(full_path, g_items[idx].name);
        }

        const char* fn = g_items[idx].name;
        int len = (int)strlen(fn);
        if (len >= 4 && (strcmp(&fn[len - 4], ".WAS") == 0 || strcmp(&fn[len - 4], ".was") == 0)) {
            woos_system_spawn(full_path);
        } else {
            // Text or other document -> open with Notepad!
            woos_system_spawn("/NOTEPAD.WAS");
        }
    }
}

static void navigate_up(void) {
    if (strcmp(g_current_path, "/") == 0) return;

    int len = (int)strlen(g_current_path);
    while (len > 0 && g_current_path[len - 1] != '/') {
        len--;
    }
    if (len <= 1) {
        load_directory("/");
    } else {
        g_current_path[len - 1] = '\0';
        load_directory(g_current_path);
    }
}

static void handle_click(int x, int y) {
    // Check Toolbar buttons (y=4..26)
    if (y >= 4 && y <= 26) {
        // [Up]
        if (x >= 6 && x <= 58) {
            navigate_up();
            return;
        }
        // [Refresh]
        if (x >= 64 && x <= 132) {
            load_directory(g_current_path);
            return;
        }
        // [Notepad]
        if (x >= 138 && x <= 206) {
            woos_system_spawn("/NOTEPAD.WAS");
            return;
        }
        // [Terminal]
        if (x >= 212 && x <= 284) {
            woos_system_spawn("/TERM.WAS");
            return;
        }
    }

    // Check Sidebar (C: drive click)
    if (x < SIDEBAR_W && y >= TOOLBAR_H) {
        if (y >= TOOLBAR_H + 24 && y <= TOOLBAR_H + 48) {
            load_directory("/");
            return;
        }
    }

    // Check File List items (x >= SIDEBAR_W, y >= TOOLBAR_H + HEADER_H && y < WIN_H - STATUSBAR_H)
    int file_list_y = TOOLBAR_H + HEADER_H;
    int file_list_h = WIN_H - TOOLBAR_H - STATUSBAR_H - HEADER_H;
    if (x >= SIDEBAR_W && y >= file_list_y && y < file_list_y + file_list_h) {
        int clicked_row = (y - file_list_y) / ROW_H;
        int idx = g_scroll_offset + clicked_row;
        if (idx >= 0 && idx < g_item_count) {
            if (g_selected_idx == idx) {
                // Second click on same item -> open!
                open_item(idx);
            } else {
                g_selected_idx = idx;
            }
        }
    }
}

static void handle_key(int ascii, int scancode) {
    // Enter: open item
    if (ascii == '\n' || ascii == '\r' || scancode == 0x1C) {
        open_item(g_selected_idx);
        return;
    }

    // Backspace: navigate Up
    if (ascii == '\b' || scancode == 0x0E) {
        navigate_up();
        return;
    }

    // Up arrow
    if (scancode == 0x48) {
        if (g_selected_idx > 0) {
            g_selected_idx--;
            if (g_selected_idx < g_scroll_offset) {
                g_scroll_offset = g_selected_idx;
            }
        }
        return;
    }

    // Down arrow
    if (scancode == 0x50) {
        if (g_selected_idx < g_item_count - 1) {
            g_selected_idx++;
            int visible_rows = (WIN_H - TOOLBAR_H - STATUSBAR_H - HEADER_H) / ROW_H;
            if (g_selected_idx >= g_scroll_offset + visible_rows) {
                g_scroll_offset = g_selected_idx - visible_rows + 1;
            }
        }
        return;
    }
}

__attribute__((export_name("_start")))
void _start(void) {
    int win_id = woos_create_window(WIN_W, WIN_H, "Windows Explorer", 0);
    if (win_id < 0) return;

    load_directory("/");
    draw_explorer();
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
            draw_explorer();
            woos_blit_window(win_id, g_fb, WIN_W, WIN_H);
        }

        woos_ipc_wait_message();
    }
}
