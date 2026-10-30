#include "woos.h"
#include "icons.h"
#include "woos_dialog.h"

#define SCREEN_W 1280
#define SCREEN_H 1024
#define TASKBAR_H 36

#define GRID_MARGIN_X 24
#define GRID_MARGIN_Y 24
#define GRID_CELL_W   86
#define GRID_CELL_H   92
#define GRID_COLS     14
#define GRID_ROWS     10

#define MAX_DESKTOP_ITEMS 64

#define COLOR_WALLPAPER       0x1C2331
#define COLOR_TASKBAR         0x232B38
#define COLOR_TASKBAR_BORDER  0x3D485C
#define COLOR_TEXT_WHITE      0xFFFFFF
#define COLOR_TEXT_BLACK      0x000000
#define COLOR_TEXT_MUTED      0x8C9AA8
#define COLOR_SELECT_RECT     0x2D5B99
#define COLOR_MENU_BG         0xECE9D8
#define COLOR_MENU_BORDER     0x716F64
#define COLOR_MENU_HOVER      0x316AC5

typedef struct {
    char name[28];
    char full_path[64];
    uint8_t is_dir;
    uint8_t is_recycle_bin;
    uint32_t size;
    int col;
    int row;
} desktop_item_t;

// Framebuffer
static uint32_t g_screen[SCREEN_W * SCREEN_H];

// Wallpaper buffer
static uint32_t g_wallpaper[SCREEN_W * (SCREEN_H - TASKBAR_H)];
static int g_has_wallpaper = 0;

// Desktop State
static desktop_item_t g_items[MAX_DESKTOP_ITEMS];
static int g_item_count = 0;
static int g_selected_icon_idx = -1;
static uint32_t g_last_click_time = 0;
static int g_last_clicked_icon = -1;
static uint32_t g_last_titlebar_click_time = 0;
static int g_last_titlebar_click_win = -1;

// Dragging Desktop Icon
static int g_is_dragging_icon = 0;
static int g_drag_icon_idx = -1;
static int g_drag_dx = 0;
static int g_drag_dy = 0;
static int g_potential_drag_idx = -1;
static int g_drag_start_x = 0;
static int g_drag_start_y = 0;

// Dragging Window
static int g_drag_win_id = -1;
static int g_drag_win_w = 240;
static int g_drag_win_h = 320;
static int g_drag_win_off_x = 0;
static int g_drag_win_off_y = 0;

// Cursor Coordinates & Dirty Rect Tracking
static int g_cursor_x = 640;
static int g_cursor_y = 512;
static int g_old_x = 640;
static int g_old_y = 512;
static int g_mouse_dirty_min_x = 9999;
static int g_mouse_dirty_max_x = -9999;
static int g_mouse_dirty_min_y = 9999;
static int g_mouse_dirty_max_y = -9999;
static int g_mouse_has_moved = 0;
static uint32_t g_last_uptime_sec = 0;

// Menus State
static int g_start_menu_open = 0;
static int g_ctx_menu_open = 0;
static int g_ctx_x = 0;
static int g_ctx_y = 0;
static int g_ctx_w = 175;
static int g_ctx_h = 0;
static int g_ctx_target_item = -1; // -1 = desktop background, >= 0 = item
static int g_ctx_hover_idx = -1;
static int g_ctx_submenu_open = 0; // 1 = Open with, 2 = New

// Clipboard
static char g_clipboard_path[64] = "";
static int g_clipboard_is_cut = 0;

// Modal Dialogs (Properties / Rename / New File)
static int g_dialog_mode = 0; // 0 = none, 1 = Properties
static int g_dialog_target_idx = -1;
static woos_file_dialog_t g_desktop_dialog;

// Single-instance launcher helper
static void launch_or_focus(const char* path, const char* title) {
    int win_count = woos_graphics_get_window_count();
    woos_window_info_t win;
    for (int i = 0; i < win_count; i++) {
        if (woos_graphics_get_window_info(i, &win) == 0) {
            if (strcmp(win.title, title) == 0) {
                woos_graphics_bring_to_front(win.id);
                return;
            }
        }
    }
    woos_system_spawn(path);
}

static void open_file_in_notepad(const char* filepath) {
    woos_system_set_open_file(filepath);
    int win_count = woos_graphics_get_window_count();
    woos_window_info_t win;
    int found_tid = -1;
    int found_id = -1;
    for (int i = 0; i < win_count; i++) {
        if (woos_graphics_get_window_info(i, &win) == 0) {
            if (strcmp(win.title, "Notepad") == 0 || strncmp(win.title, "Notepad", 7) == 0) {
                found_id = win.id;
                found_tid = win.thread_id;
                break;
            }
        }
    }

    if (found_id >= 0 && found_tid >= 0) {
        woos_graphics_bring_to_front(found_id);
        woos_ipc_send(found_tid, MSG_OPEN_FILE, 0, 0, 0, 0, 0);
    } else {
        woos_system_spawn("/NOTEPAD.WAS");
    }
}

static int is_image_file(const char* name) {
    if (!name) return 0;
    int len = (int)strlen(name);
    if (len >= 4 && (strcmp(&name[len - 4], ".bmp") == 0 || strcmp(&name[len - 4], ".BMP") == 0 ||
                     strcmp(&name[len - 4], ".png") == 0 || strcmp(&name[len - 4], ".PNG") == 0 ||
                     strcmp(&name[len - 4], ".jpg") == 0 || strcmp(&name[len - 4], ".JPG") == 0)) {
        return 1;
    }
    if (len >= 5 && (strcmp(&name[len - 5], ".jpeg") == 0 || strcmp(&name[len - 5], ".JPEG") == 0)) {
        return 1;
    }
    return 0;
}

static int is_html_file(const char* name) {
    if (!name) return 0;
    int len = (int)strlen(name);
    if (len >= 5 && (strcmp(&name[len - 5], ".html") == 0 || strcmp(&name[len - 5], ".HTML") == 0)) return 1;
    if (len >= 4 && (strcmp(&name[len - 4], ".htm") == 0 || strcmp(&name[len - 4], ".HTM") == 0)) return 1;
    return 0;
}

static void open_file_in_browser(const char* filepath) {
    woos_system_set_open_file(filepath);
    int win_count = woos_graphics_get_window_count();
    woos_window_info_t win;
    int found_tid = -1;
    int found_id = -1;
    for (int i = 0; i < win_count; i++) {
        if (woos_graphics_get_window_info(i, &win) == 0) {
            if (strncmp(win.title, "Web Browser", 11) == 0) {
                found_id = win.id;
                found_tid = win.thread_id;
                break;
            }
        }
    }

    if (found_id >= 0 && found_tid >= 0) {
        woos_graphics_bring_to_front(found_id);
        woos_ipc_send(found_tid, MSG_OPEN_FILE, 0, 0, 0, 0, 0);
    } else {
        woos_system_spawn("/BROWSER.WAS");
    }
}

static void open_file_in_imageview(const char* filepath) {
    woos_system_set_open_file(filepath);
    int win_count = woos_graphics_get_window_count();
    woos_window_info_t win;
    int found_tid = -1;
    int found_id = -1;
    for (int i = 0; i < win_count; i++) {
        if (woos_graphics_get_window_info(i, &win) == 0) {
            if (strncmp(win.title, "Image Viewer", 12) == 0) {
                found_id = win.id;
                found_tid = win.thread_id;
                break;
            }
        }
    }

    if (found_id >= 0 && found_tid >= 0) {
        woos_graphics_bring_to_front(found_id);
        woos_ipc_send(found_tid, MSG_OPEN_FILE, 0, 0, 0, 0, 0);
    } else {
        woos_system_spawn("/IMGVIEW.WAS");
    }
}

static void load_wallpaper(void) {
    g_has_wallpaper = 0;
    int32_t fd = woos_vfs_open("/WALLPAP.BMP", VFS_MODE_READ);
    if (fd < 0) return;

    uint8_t header[54];
    uint32_t r = woos_vfs_read(fd, header, 54);
    if (r < 54 || header[0] != 'B' || header[1] != 'M') {
        woos_vfs_close(fd);
        return;
    }

    uint32_t offset = *(const uint32_t*)&header[10];
    int32_t w = *(const int32_t*)&header[18];
    int32_t h = *(const int32_t*)&header[22];
    uint16_t bpp = *(const uint16_t*)&header[28];
    uint32_t comp = *(const uint32_t*)&header[30];

    if ((comp != 0 && comp != 3) || (bpp != 24 && bpp != 32) || w <= 0 || h == 0) {
        woos_vfs_close(fd);
        return;
    }

    int is_top_down = (h < 0);
    int abs_h = is_top_down ? -h : h;
    int target_w = SCREEN_W;
    int target_h = SCREEN_H - TASKBAR_H;

    int row_bytes = ((w * (bpp / 8) + 3) & ~3);
    static uint8_t row_buf[4096 * 4];
    int last_src_y = -1;

    for (int y = 0; y < target_h; y++) {
        int src_y = (y * abs_h) / target_h;
        if (!is_top_down) src_y = abs_h - 1 - src_y;

        if (src_y != last_src_y) {
            woos_vfs_seek(fd, offset + (uint32_t)src_y * row_bytes);
            if (woos_vfs_read(fd, row_buf, (uint32_t)row_bytes) <= 0) break;
            last_src_y = src_y;
        }

        uint32_t* dst_row = g_wallpaper + y * target_w;
        for (int x = 0; x < target_w; x++) {
            int sx = (x * w) / target_w;
            if (bpp == 32) {
                uint8_t b = row_buf[sx * 4 + 0];
                uint8_t g = row_buf[sx * 4 + 1];
                uint8_t r = row_buf[sx * 4 + 2];
                dst_row[x] = (0xFF << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else {
                uint8_t b = row_buf[sx * 3 + 0];
                uint8_t g = row_buf[sx * 3 + 1];
                uint8_t r = row_buf[sx * 3 + 2];
                dst_row[x] = (0xFF << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }
    woos_vfs_close(fd);
    g_has_wallpaper = 1;
}

// Icon Drawing Helpers
static void draw_folder_icon(int x, int y) {
    // Tab
    fill_rect(g_screen, SCREEN_W, SCREEN_H, x + 4, y + 4, 12, 5, 0xD4AC0D);
    // Folder back
    fill_rect(g_screen, SCREEN_W, SCREEN_H, x + 4, y + 8, 28, 22, 0xF4D03F);
    // Darker front flap
    fill_rect(g_screen, SCREEN_W, SCREEN_H, x + 4, y + 14, 28, 16, 0xF39C12);
    draw_rect(g_screen, SCREEN_W, SCREEN_H, x + 4, y + 8, 28, 22, 0xB7950B);
}

static void draw_item_icon(desktop_item_t* it, int x, int y) {
    if (it->is_recycle_bin) {
        draw_recycle_bin_icon_s(g_screen, SCREEN_W, SCREEN_H, x, y, 32);
    } else if (it->is_dir) {
        draw_folder_icon(x, y);
    } else {
        draw_icon_by_target(g_screen, SCREEN_W, SCREEN_H, x, y, 32, it->name);
    }
}

// Scans /DESKTOP/ and synchronizes desktop items while preserving existing grid coordinates
static int sync_desktop_items(void) {
    desktop_item_t old_items[MAX_DESKTOP_ITEMS];
    int old_count = g_item_count;
    if (old_count > MAX_DESKTOP_ITEMS) old_count = MAX_DESKTOP_ITEMS;
    for (int i = 0; i < old_count; i++) {
        old_items[i] = g_items[i];
    }

    uint8_t occupied[GRID_COLS][GRID_ROWS];
    for (int c = 0; c < GRID_COLS; c++) {
        for (int r = 0; r < GRID_ROWS; r++) {
            occupied[c][r] = 0;
        }
    }

    // Item 0 is always Recycle Bin
    g_items[0].name[0] = '\0';
    strcpy(g_items[0].name, "Recycle Bin");
    strcpy(g_items[0].full_path, "/RECYCLE");
    g_items[0].is_dir = 1;
    g_items[0].is_recycle_bin = 1;
    g_items[0].size = 0;
    if (old_count > 0 && old_items[0].is_recycle_bin &&
        old_items[0].col >= 0 && old_items[0].col < GRID_COLS &&
        old_items[0].row >= 0 && old_items[0].row < GRID_ROWS) {
        g_items[0].col = old_items[0].col;
        g_items[0].row = old_items[0].row;
    } else {
        g_items[0].col = 0;
        g_items[0].row = 0;
    }
    occupied[g_items[0].col][g_items[0].row] = 1;
    int new_count = 1;

    typedef struct {
        char name[28];
        uint8_t is_dir;
        uint32_t size;
        int matched_old_idx;
    } scanned_entry_t;

    scanned_entry_t scanned[MAX_DESKTOP_ITEMS];
    int scanned_count = 0;

    int handle = woos_vfs_open("/DESKTOP", VFS_MODE_READ);
    if (handle < 0) {
        woos_vfs_mkdir("/DESKTOP");
        handle = woos_vfs_open("/DESKTOP", VFS_MODE_READ);
    }

    if (handle >= 0) {
        woos_dirent_t entry;
        while (scanned_count < MAX_DESKTOP_ITEMS - 1) {
            int res = woos_vfs_readdir(handle, &entry);
            if (res <= 0) break;
            if (entry.name[0] == '.' && (entry.name[1] == '\0' || (entry.name[1] == '.' && entry.name[2] == '\0'))) {
                continue;
            }

            // Deduplicate
            int dup = 0;
            for (int k = 0; k < scanned_count; k++) {
                if (strcasecmp_custom(scanned[k].name, entry.name) == 0) {
                    dup = 1;
                    break;
                }
            }
            if (dup) continue;

            strncpy(scanned[scanned_count].name, entry.name, sizeof(scanned[scanned_count].name) - 1);
            scanned[scanned_count].name[sizeof(scanned[scanned_count].name) - 1] = '\0';
            scanned[scanned_count].is_dir = entry.is_dir;
            scanned[scanned_count].size = entry.size;
            scanned[scanned_count].matched_old_idx = -1;

            // Check if this file was already on desktop
            for (int j = 1; j < old_count; j++) {
                if (!old_items[j].is_recycle_bin && strcasecmp_custom(old_items[j].name, entry.name) == 0) {
                    scanned[scanned_count].matched_old_idx = j;
                    break;
                }
            }
            scanned_count++;
        }
        woos_vfs_close(handle);
    }

    // Pass 1: Retain grid positions for existing desktop items
    for (int i = 0; i < scanned_count; i++) {
        if (scanned[i].matched_old_idx >= 0) {
            int old_idx = scanned[i].matched_old_idx;
            int col = old_items[old_idx].col;
            int row = old_items[old_idx].row;

            if (col >= 0 && col < GRID_COLS && row >= 0 && row < GRID_ROWS && !occupied[col][row]) {
                desktop_item_t* it = &g_items[new_count++];
                strcpy(it->name, scanned[i].name);
                strcpy(it->full_path, "/DESKTOP/");
                strcat(it->full_path, it->name);
                it->is_dir = scanned[i].is_dir;
                it->is_recycle_bin = 0;
                it->size = scanned[i].size;
                it->col = col;
                it->row = row;
                occupied[col][row] = 1;
                scanned[i].matched_old_idx = -2; // Placed
            }
        }
    }

    // Pass 2: Place newly created files in the first free grid cell
    int cur_col = 0;
    int cur_row = 0;
    for (int i = 0; i < scanned_count; i++) {
        if (scanned[i].matched_old_idx != -2) {
            while (cur_col < GRID_COLS) {
                if (!occupied[cur_col][cur_row]) {
                    break;
                }
                cur_row++;
                if (cur_row >= GRID_ROWS) {
                    cur_row = 0;
                    cur_col++;
                }
            }
            if (cur_col >= GRID_COLS) break;

            desktop_item_t* it = &g_items[new_count++];
            strcpy(it->name, scanned[i].name);
            strcpy(it->full_path, "/DESKTOP/");
            strcat(it->full_path, it->name);
            it->is_dir = scanned[i].is_dir;
            it->is_recycle_bin = 0;
            it->size = scanned[i].size;
            it->col = cur_col;
            it->row = cur_row;
            occupied[cur_col][cur_row] = 1;

            cur_row++;
            if (cur_row >= GRID_ROWS) {
                cur_row = 0;
                cur_col++;
            }
        }
    }

    int changed = (new_count != old_count);
    if (!changed) {
        for (int i = 0; i < new_count; i++) {
            if (strcmp(g_items[i].name, old_items[i].name) != 0 ||
                g_items[i].col != old_items[i].col ||
                g_items[i].row != old_items[i].row) {
                changed = 1;
                break;
            }
        }
    }

    g_item_count = new_count;
    return changed;
}

static void load_desktop_items(void) {
    sync_desktop_items();
}

static void generate_unique_desktop_name(const char* base, const char* ext, char* out_name, int max_len) {
    char candidate[32];
    candidate[0] = '\0';
    strcat(candidate, base);
    strcat(candidate, ext);

    int exists = 0;
    for (int i = 0; i < g_item_count; i++) {
        if (strcasecmp_custom(g_items[i].name, candidate) == 0) {
            exists = 1;
            break;
        }
    }
    if (!exists) {
        strncpy(out_name, candidate, max_len - 1);
        out_name[max_len - 1] = '\0';
        return;
    }

    for (int num = 2; num < 100; num++) {
        candidate[0] = '\0';
        strcat(candidate, base);
        int l = (int)strlen(candidate);
        if (num >= 10) candidate[l++] = '0' + (num / 10);
        candidate[l++] = '0' + (num % 10);
        candidate[l] = '\0';
        strcat(candidate, ext);

        exists = 0;
        for (int i = 0; i < g_item_count; i++) {
            if (strcasecmp_custom(g_items[i].name, candidate) == 0) {
                exists = 1;
                break;
            }
        }
        if (!exists) {
            strncpy(out_name, candidate, max_len - 1);
            out_name[max_len - 1] = '\0';
            return;
        }
    }
    strncpy(out_name, candidate, max_len - 1);
    out_name[max_len - 1] = '\0';
}

// Check if a grid cell is occupied by any item (excluding ignore_idx)
static int is_cell_occupied(int col, int row, int ignore_idx) {
    for (int i = 0; i < g_item_count; i++) {
        if (i == ignore_idx) continue;
        if (g_items[i].col == col && g_items[i].row == row) return i;
    }
    return -1;
}

static void launch_item(int idx) {
    if (idx < 0 || idx >= g_item_count) return;
    desktop_item_t* it = &g_items[idx];

    if (it->is_recycle_bin) {
        launch_or_focus("/EXPLORER.WAS", "File Explorer");
        return;
    }

    if (it->is_dir) {
        launch_or_focus("/EXPLORER.WAS", "File Explorer");
        return;
    }

    int len = (int)strlen(it->name);
    if (len >= 4 && (strcmp(&it->name[len - 4], ".WAS") == 0 || strcmp(&it->name[len - 4], ".was") == 0)) {
        if (strcmp(it->name, "BROWSER.WAS") == 0) launch_or_focus(it->full_path, "Web Browser");
        else if (strcmp(it->name, "EXPLORER.WAS") == 0) launch_or_focus(it->full_path, "File Explorer");
        else if (strcmp(it->name, "NOTEPAD.WAS") == 0) launch_or_focus(it->full_path, "Notepad");
        else if (strcmp(it->name, "CALC.WAS") == 0) launch_or_focus(it->full_path, "Calculator");
        else if (strcmp(it->name, "TERM.WAS") == 0) launch_or_focus(it->full_path, "Terminal");
        else if (strcmp(it->name, "FETCH.WAS") == 0) launch_or_focus(it->full_path, "HTTP Fetch");
        else if (strcmp(it->name, "IMGVIEW.WAS") == 0) launch_or_focus(it->full_path, "Image Viewer");
        else woos_system_spawn(it->full_path);
    } else if (is_html_file(it->name)) {
        open_file_in_browser(it->full_path);
    } else if (is_image_file(it->name)) {
        open_file_in_imageview(it->full_path);
    } else {
        // Document: open with Notepad!
        open_file_in_notepad(it->full_path);
    }
}

static void delete_item_to_recycle(int idx) {
    if (idx <= 0 || idx >= g_item_count) return; // Cannot delete Recycle Bin!
    desktop_item_t* it = &g_items[idx];
    if (it->is_recycle_bin || is_system_item(it->name)) return; // Protected system file!

    char dest[64];
    dest[0] = '\0';
    strcpy(dest, "/RECYCLE/");
    strcat(dest, it->name);

    woos_vfs_rename(it->full_path, dest);
    load_desktop_items();
}

static void create_desktop_file(const char* filename, const char* content) {
    char path[64];
    path[0] = '\0';
    strcpy(path, "/DESKTOP/");
    strcat(path, filename);

    int h = woos_vfs_open(path, VFS_MODE_WRITE | VFS_MODE_CREATE | VFS_MODE_TRUNC);
    if (h >= 0) {
        if (content && content[0]) {
            woos_vfs_write(h, content, (uint32_t)strlen(content));
        }
        woos_vfs_close(h);
    }
    load_desktop_items();
}

static void create_desktop_folder(const char* foldername) {
    char path[64];
    path[0] = '\0';
    strcpy(path, "/DESKTOP/");
    strcat(path, foldername);
    woos_vfs_mkdir(path);
    load_desktop_items();
}

static void paste_clipboard(void) {
    if (!g_clipboard_path[0]) return;

    // Extract filename from source path
    const char* p = g_clipboard_path;
    const char* last_slash = p;
    while (*p) {
        if (*p == '/' || *p == '\\') last_slash = p + 1;
        p++;
    }

    char dest[64];
    dest[0] = '\0';
    strcpy(dest, "/DESKTOP/");
    strcat(dest, last_slash);

    if (g_clipboard_is_cut) {
        woos_vfs_rename(g_clipboard_path, dest);
        g_clipboard_path[0] = '\0';
    } else {
        // Copy file
        int src = woos_vfs_open(g_clipboard_path, VFS_MODE_READ);
        if (src >= 0) {
            uint32_t sz = woos_vfs_size(src);
            int dst = woos_vfs_open(dest, VFS_MODE_WRITE | VFS_MODE_CREATE);
            if (dst >= 0) {
                uint8_t buf[512];
                uint32_t rem = sz;
                while (rem > 0) {
                    uint32_t chunk = rem > sizeof(buf) ? sizeof(buf) : rem;
                    uint32_t rd = woos_vfs_read(src, buf, chunk);
                    if (rd == 0) break;
                    woos_vfs_write(dst, buf, rd);
                    rem -= rd;
                }
                woos_vfs_close(dst);
            }
            woos_vfs_close(src);
        }
    }
    load_desktop_items();
}

static void draw_window_decorations(woos_window_info_t* win) {
    int win_x = win->x;
    int win_y = win->y;
    int win_w = (int)win->w;
    int win_h = (int)win->h;
    int is_focused = (win->flags & 8) != 0;
    int is_max = (win->flags & 2) != 0;

    uint32_t frame_col = is_focused ? 0x0078D7 : 0x4B535D;
    uint32_t title_col = is_focused ? 0x0078D7 : 0x333A42;
    uint32_t text_col  = is_focused ? 0xFFFFFF : 0xBDC3C7;

    // 3px outer border (if not maximized)
    if (!is_max) {
        // Top border above titlebar
        fill_rect(g_screen, SCREEN_W, SCREEN_H, win_x - 3, win_y - 25, win_w + 6, 3, frame_col);
        // Left border
        fill_rect(g_screen, SCREEN_W, SCREEN_H, win_x - 3, win_y - 22, 3, win_h + 25, frame_col);
        // Right border
        fill_rect(g_screen, SCREEN_W, SCREEN_H, win_x + win_w, win_y - 22, 3, win_h + 25, frame_col);
        // Bottom border
        fill_rect(g_screen, SCREEN_W, SCREEN_H, win_x - 3, win_y + win_h, win_w + 6, 3, frame_col);
    }

    // Titlebar rectangle (22px high)
    fill_rect(g_screen, SCREEN_W, SCREEN_H, win_x, win_y - 22, win_w, 22, title_col);

    // Mini Window Icon (at win_x + 4, win_y - 18)
    draw_icon_by_target(g_screen, SCREEN_W, SCREEN_H, win_x + 4, win_y - 18, 14, win->title);

    // Window Title String
    woos_draw_text(g_screen, SCREEN_W, SCREEN_H, win_x + 22, win_y - 15, win->title, text_col);

    // [X] Close button
    int bx = win_x + win_w - 24;
    int by = win_y - 21;
    fill_rect(g_screen, SCREEN_W, SCREEN_H, bx, by, 23, 19, 0xC0392B);
    int cx = bx + 11;
    int cy = by + 9;
    for (int d = -4; d <= 4; d++) {
        fill_rect(g_screen, SCREEN_W, SCREEN_H, cx + d, cy + d, 1, 1, 0xFFFFFF);
        fill_rect(g_screen, SCREEN_W, SCREEN_H, cx + d, cy - d, 1, 1, 0xFFFFFF);
        fill_rect(g_screen, SCREEN_W, SCREEN_H, cx + d + 1, cy + d, 1, 1, 0xFFFFFF);
        fill_rect(g_screen, SCREEN_W, SCREEN_H, cx + d + 1, cy - d, 1, 1, 0xFFFFFF);
    }

    // [□] Maximize/Restore button
    int can_max = (strcmp(win->title, "Calculator") != 0);
    bx = win_x + win_w - 48;
    fill_rect(g_screen, SCREEN_W, SCREEN_H, bx, by, 23, 19, title_col);
    draw_rect(g_screen, SCREEN_W, SCREEN_H, bx, by, 23, 19, is_focused ? 0x2A8CE0 : 0x4B535D);
    cx = bx + 11;
    cy = by + 9;
    uint32_t max_icon_col = can_max ? 0xFFFFFF : 0x7F8C8D;
    if (is_max) {
        draw_rect(g_screen, SCREEN_W, SCREEN_H, cx - 2, cy - 4, 8, 7, max_icon_col);
        draw_rect(g_screen, SCREEN_W, SCREEN_H, cx - 4, cy - 2, 8, 7, max_icon_col);
    } else {
        draw_rect(g_screen, SCREEN_W, SCREEN_H, cx - 4, cy - 4, 9, 9, max_icon_col);
        fill_rect(g_screen, SCREEN_W, SCREEN_H, cx - 4, cy - 4, 9, 2, max_icon_col);
    }

    // [_] Minimize button
    bx = win_x + win_w - 72;
    fill_rect(g_screen, SCREEN_W, SCREEN_H, bx, by, 23, 19, title_col);
    draw_rect(g_screen, SCREEN_W, SCREEN_H, bx, by, 23, 19, is_focused ? 0x2A8CE0 : 0x4B535D);
    cx = bx + 11;
    cy = by + 9;
    fill_rect(g_screen, SCREEN_W, SCREEN_H, cx - 4, cy + 3, 9, 2, 0xFFFFFF);
}

// Rendering Main Function
static void render_screen(void) {
    // 1. Wallpaper background
    if (g_has_wallpaper) {
        for (int i = 0; i < SCREEN_W * (SCREEN_H - TASKBAR_H); i++) {
            g_screen[i] = g_wallpaper[i];
        }
    } else {
        fill_rect(g_screen, SCREEN_W, SCREEN_H, 0, 0, SCREEN_W, SCREEN_H - TASKBAR_H, COLOR_WALLPAPER);
        // Subtle grid guide dots
        for (int r = 0; r < GRID_ROWS; r++) {
            for (int c = 0; c < GRID_COLS; c++) {
                int gx = GRID_MARGIN_X + c * GRID_CELL_W + GRID_CELL_W / 2;
                int gy = GRID_MARGIN_Y + r * GRID_CELL_H + GRID_CELL_H / 2;
                fill_rect(g_screen, SCREEN_W, SCREEN_H, gx, gy, 1, 1, 0x2A3547);
            }
        }
    }

    // 2. Desktop Icons
    for (int i = 0; i < g_item_count; i++) {
        desktop_item_t* it = &g_items[i];
        int ix = GRID_MARGIN_X + it->col * GRID_CELL_W;
        int iy = GRID_MARGIN_Y + it->row * GRID_CELL_H;

        if (g_is_dragging_icon && i == g_drag_icon_idx) {
            // Draw ghost cell
            draw_rect(g_screen, SCREEN_W, SCREEN_H, ix + 2, iy + 2, GRID_CELL_W - 4, GRID_CELL_H - 4, 0x485A6E);
            continue;
        }

        // Selected highlight
        if (i == g_selected_icon_idx) {
            fill_rect(g_screen, SCREEN_W, SCREEN_H, ix + 2, iy + 2, GRID_CELL_W - 4, GRID_CELL_H - 4, COLOR_SELECT_RECT);
            draw_rect(g_screen, SCREEN_W, SCREEN_H, ix + 2, iy + 2, GRID_CELL_W - 4, GRID_CELL_H - 4, 0x64A0E8);
        }

        // Draw Icon graphic (centered horizontally in cell)
        int icon_x = ix + (GRID_CELL_W - 36) / 2;
        int icon_y = iy + 8;
        draw_item_icon(it, icon_x, icon_y);

        // Draw Label with dark pill background for readability
        int text_len = (int)strlen(it->name);
        int text_w = text_len * 8;
        if (text_w > GRID_CELL_W - 6) text_w = GRID_CELL_W - 6;
        int label_x = ix + (GRID_CELL_W - text_w) / 2;
        int label_y = iy + 48;

        fill_rect(g_screen, SCREEN_W, SCREEN_H, label_x - 3, label_y - 2, text_w + 6, 16, 0x111620);
        woos_draw_text(g_screen, SCREEN_W, SCREEN_H, label_x, label_y, it->name, COLOR_TEXT_WHITE);
    }

    // 3. Dragged Icon (if dragging)
    if (g_is_dragging_icon && g_drag_icon_idx >= 0) {
        desktop_item_t* it = &g_items[g_drag_icon_idx];
        int dx = g_cursor_x - g_drag_dx;
        int dy = g_cursor_y - g_drag_dy;

        draw_rect(g_screen, SCREEN_W, SCREEN_H, dx + 2, dy + 2, GRID_CELL_W - 4, GRID_CELL_H - 4, 0x00FFCC);
        int icon_x = dx + (GRID_CELL_W - 36) / 2;
        int icon_y = dy + 8;
        draw_item_icon(it, icon_x, icon_y);
        int label_x = dx + 6;
        int label_y = dy + 48;
        woos_draw_text(g_screen, SCREEN_W, SCREEN_H, label_x, label_y, it->name, 0x00FFCC);
    }

    // 4. Window list in z-order
    int win_count = woos_graphics_get_window_count();
    woos_window_info_t win;
    for (int i = 0; i < win_count; i++) {
        if (woos_graphics_get_window_info(i, &win) == 0) {
            if (!(win.flags & 1)) { // not minimized
                if (!(win.flags & 4)) { // not custom decor
                    draw_window_decorations(&win);
                }
                woos_graphics_draw_window_to_buffer(win.id, g_screen, SCREEN_W, SCREEN_H);
            }
        }
    }

    // 5. Taskbar (y = SCREEN_H - TASKBAR_H)
    int tb_y = SCREEN_H - TASKBAR_H;
    fill_rect(g_screen, SCREEN_W, SCREEN_H, 0, tb_y, SCREEN_W, TASKBAR_H, COLOR_TASKBAR);
    fill_rect(g_screen, SCREEN_W, SCREEN_H, 0, tb_y, SCREEN_W, 1, COLOR_TASKBAR_BORDER);

    // Start Button (x: 4, y: tb_y + 4, w: 76, h: 28)
    draw_3d_rect(g_screen, SCREEN_W, SCREEN_H, 4, tb_y + 4, 76, 28, g_start_menu_open);
    fill_rect(g_screen, SCREEN_W, SCREEN_H, 5, tb_y + 5, 74, 26, g_start_menu_open ? 0x1B212B : 0x2E3846);
    // WoOS Logo square
    fill_rect(g_screen, SCREEN_W, SCREEN_H, 10, tb_y + 9, 8, 8, 0x3498DB);
    fill_rect(g_screen, SCREEN_W, SCREEN_H, 19, tb_y + 9, 8, 8, 0xE74C3C);
    fill_rect(g_screen, SCREEN_W, SCREEN_H, 10, tb_y + 18, 8, 8, 0x2ECC71);
    fill_rect(g_screen, SCREEN_W, SCREEN_H, 19, tb_y + 18, 8, 8, 0xF1C40F);
    woos_draw_text(g_screen, SCREEN_W, SCREEN_H, 32, tb_y + 11, "Start", COLOR_TEXT_WHITE);

    // Window Tabs on Taskbar (Icons instead of text!)
    int hover_tab_idx = -1;
    for (int i = 0; i < win_count; i++) {
        if (woos_graphics_get_window_info(i, &win) == 0) {
            int tab_w = 42;
            int tab_gap = 4;
            int tab_x = 86 + i * (tab_w + tab_gap);
            int is_focused = (win.flags & 8) != 0;
            int is_min = (win.flags & 1) != 0;
            int is_hover = (g_cursor_x >= tab_x && g_cursor_x < tab_x + tab_w &&
                            g_cursor_y >= tb_y + 4 && g_cursor_y < tb_y + 32);
            if (is_hover) hover_tab_idx = i;

            uint32_t bg_col = is_focused ? 0x263345 : (is_hover ? 0x354359 : 0x1E2734);
            fill_rect(g_screen, SCREEN_W, SCREEN_H, tab_x, tb_y + 4, tab_w, 28, bg_col);
            draw_rect(g_screen, SCREEN_W, SCREEN_H, tab_x, tb_y + 4, tab_w, 28, is_focused ? 0x485C77 : 0x2A3547);

            // Draw custom 20x20 application icon inside the button
            int icon_x = tab_x + (tab_w - 20) / 2;
            int icon_y = tb_y + 4 + (28 - 20) / 2;
            draw_icon_by_target(g_screen, SCREEN_W, SCREEN_H, icon_x, icon_y, 20, win.title);

            // Active glowing underline bar
            if (is_focused) {
                fill_rect(g_screen, SCREEN_W, SCREEN_H, tab_x + 2, tb_y + 29, tab_w - 4, 2, 0x00D2FF);
            } else if (!is_min) {
                fill_rect(g_screen, SCREEN_W, SCREEN_H, tab_x + 6, tb_y + 29, tab_w - 12, 2, 0x5D718C);
            }
        }
    }

    // Tooltip popup for hovered taskbar button
    if (hover_tab_idx >= 0 && woos_graphics_get_window_info(hover_tab_idx, &win) == 0) {
        int tab_w = 42;
        int tab_gap = 4;
        int tab_x = 86 + hover_tab_idx * (tab_w + tab_gap);
        int tlen = (int)strlen(win.title);
        int tip_w = tlen * 8 + 12;
        int tip_h = 20;
        int tip_x = tab_x + tab_w / 2 - tip_w / 2;
        if (tip_x < 4) tip_x = 4;
        if (tip_x + tip_w > SCREEN_W - 4) tip_x = SCREEN_W - tip_w - 4;
        int tip_y = tb_y - tip_h - 4;

        fill_rect(g_screen, SCREEN_W, SCREEN_H, tip_x, tip_y, tip_w, tip_h, 0x1B212B);
        draw_rect(g_screen, SCREEN_W, SCREEN_H, tip_x, tip_y, tip_w, tip_h, 0x485A6E);
        woos_draw_text(g_screen, SCREEN_W, SCREEN_H, tip_x + 6, tip_y + 4, win.title, 0x00FFCC);
    }

    // System Tray Area (Status metrics & Clock)
    woos_system_status_t status;
    woos_system_get_status(&status);

    int tray_x = SCREEN_W - 320;
    fill_rect(g_screen, SCREEN_W, SCREEN_H, tray_x, tb_y + 4, 316, 28, 0x181F28);
    draw_rect(g_screen, SCREEN_W, SCREEN_H, tray_x, tb_y + 4, 316, 28, 0x2A3544);

    // RAM Free MB
    uint32_t ram_mb = (status.free_pages * 4) / 1024;
    char ram_str[24] = "RAM: ";
    char num_buf[16];
    int_to_str((int)ram_mb, num_buf);
    strcat(ram_str, num_buf);
    strcat(ram_str, "M");
    woos_draw_text(g_screen, SCREEN_W, SCREEN_H, tray_x + 8, tb_y + 11, ram_str, 0x2ECC71);

    // HEAP KB
    uint32_t heap_kb = (uint32_t)(status.heap_used / 1024);
    char heap_str[24] = "HP: ";
    int_to_str((int)heap_kb, num_buf);
    strcat(heap_str, num_buf);
    strcat(heap_str, "K");
    woos_draw_text(g_screen, SCREEN_W, SCREEN_H, tray_x + 90, tb_y + 11, heap_str, 0x3498DB);

    // Net link
    woos_draw_text(g_screen, SCREEN_W, SCREEN_H, tray_x + 170, tb_y + 11, status.net_link_up ? "NET:UP" : "NET:DN", status.net_link_up ? 0x2ECC71 : 0xE74C3C);

    // Clock (HH:MM:SS from uptime)
    uint32_t total_sec = status.uptime_ms / 1000;
    uint32_t sec = total_sec % 60;
    uint32_t min = (total_sec / 60) % 60;
    uint32_t hr = (total_sec / 3600) % 24;
    char time_str[16];
    time_str[0] = (char)('0' + (hr / 10));
    time_str[1] = (char)('0' + (hr % 10));
    time_str[2] = ':';
    time_str[3] = (char)('0' + (min / 10));
    time_str[4] = (char)('0' + (min % 10));
    time_str[5] = ':';
    time_str[6] = (char)('0' + (sec / 10));
    time_str[7] = (char)('0' + (sec % 10));
    time_str[8] = '\0';
    woos_draw_text(g_screen, SCREEN_W, SCREEN_H, tray_x + 236, tb_y + 11, time_str, COLOR_TEXT_WHITE);

    // 6. Start Menu Popup
    if (g_start_menu_open) {
        int sm_w = 200;
        int sm_h = 316;
        int sm_x = 4;
        int sm_y = SCREEN_H - TASKBAR_H - sm_h - 2;

        draw_3d_rect(g_screen, SCREEN_W, SCREEN_H, sm_x, sm_y, sm_w, sm_h, 0);
        fill_rect(g_screen, SCREEN_W, SCREEN_H, sm_x + 1, sm_y + 1, sm_w - 2, sm_h - 2, COLOR_MENU_BG);

        // Left banner
        fill_rect(g_screen, SCREEN_W, SCREEN_H, sm_x + 2, sm_y + 2, 28, sm_h - 4, 0x2A62C9);
        woos_draw_text(g_screen, SCREEN_W, SCREEN_H, sm_x + 8, sm_y + sm_h - 40, "W", COLOR_TEXT_WHITE);
        woos_draw_text(g_screen, SCREEN_W, SCREEN_H, sm_x + 8, sm_y + sm_h - 25, "O", COLOR_TEXT_WHITE);

        const char* sm_items[] = {
            "Web Browser",
            "File Explorer",
            "Notepad",
            "Terminal",
            "Calculator",
            "Image Viewer",
            "HTTP Fetch",
            "WASI Test"
        };

        for (int i = 0; i < 8; i++) {
            int iy = sm_y + 10 + i * 36;
            fill_rect(g_screen, SCREEN_W, SCREEN_H, sm_x + 34, iy - 2, sm_w - 38, 30, 0xE4E2D5);
            draw_icon_by_target(g_screen, SCREEN_W, SCREEN_H, sm_x + 38, iy + 3, 24, sm_items[i]);
            woos_draw_text(g_screen, SCREEN_W, SCREEN_H, sm_x + 72, iy + 8, sm_items[i], COLOR_TEXT_BLACK);
        }
    }

    // 7. Context Menu Popup
    if (g_ctx_menu_open) {
        draw_3d_rect(g_screen, SCREEN_W, SCREEN_H, g_ctx_x, g_ctx_y, g_ctx_w, g_ctx_h, 0);
        fill_rect(g_screen, SCREEN_W, SCREEN_H, g_ctx_x + 1, g_ctx_y + 1, g_ctx_w - 2, g_ctx_h - 2, COLOR_MENU_BG);

        if (g_ctx_target_item >= 0) {
            // Icon context menu
            const char* items[] = {
                "Open",
                "Open with... >",
                "Cut",
                "Copy",
                "Move to Recycle Bin",
                "Rename",
                "Properties"
            };
            int count = 7;
            int is_sys = (g_ctx_target_item == 0) || is_system_item(g_items[g_ctx_target_item].name);
            for (int i = 0; i < count; i++) {
                int iy = g_ctx_y + 4 + i * 22;
                int disabled = is_sys && (i == 2 || i == 4 || i == 5);
                int is_hover = (i == g_ctx_hover_idx) && !disabled;
                if (is_hover) {
                    fill_rect(g_screen, SCREEN_W, SCREEN_H, g_ctx_x + 2, iy, g_ctx_w - 4, 20, COLOR_MENU_HOVER);
                }
                uint32_t tcol = disabled ? COLOR_TEXT_MUTED : (is_hover ? COLOR_TEXT_WHITE : COLOR_TEXT_BLACK);
                woos_draw_text(g_screen, SCREEN_W, SCREEN_H, g_ctx_x + 12, iy + 3, items[i], tcol);
            }

            // Submenu for "Open with..."
            if (g_ctx_submenu_open == 1) {
                int sub_x = g_ctx_x + g_ctx_w - 4;
                int sub_y = g_ctx_y + 4 + 1 * 22;
                int sub_w = 140;
                int sub_h = 6 * 22 + 8;
                draw_3d_rect(g_screen, SCREEN_W, SCREEN_H, sub_x, sub_y, sub_w, sub_h, 0);
                fill_rect(g_screen, SCREEN_W, SCREEN_H, sub_x + 1, sub_y + 1, sub_w - 2, sub_h - 2, COLOR_MENU_BG);

                const char* sub_items[] = { "Web Browser", "Notepad", "Image Viewer", "File Explorer", "Terminal", "Calculator" };
                for (int s = 0; s < 6; s++) {
                    int sy = sub_y + 4 + s * 22;
                    woos_draw_text(g_screen, SCREEN_W, SCREEN_H, sub_x + 10, sy + 3, sub_items[s], COLOR_TEXT_BLACK);
                }
            }
        } else {
            // Desktop background context menu
            const char* items[] = {
                "New... >",
                "Paste",
                "Refresh",
                "Properties"
            };
            int count = 4;
            for (int i = 0; i < count; i++) {
                int iy = g_ctx_y + 4 + i * 22;
                int is_hover = (i == g_ctx_hover_idx);
                if (is_hover) {
                    fill_rect(g_screen, SCREEN_W, SCREEN_H, g_ctx_x + 2, iy, g_ctx_w - 4, 20, COLOR_MENU_HOVER);
                }
                uint32_t tcol = is_hover ? COLOR_TEXT_WHITE : COLOR_TEXT_BLACK;
                if (i == 1 && !g_clipboard_path[0]) tcol = COLOR_TEXT_MUTED; // Paste disabled
                woos_draw_text(g_screen, SCREEN_W, SCREEN_H, g_ctx_x + 12, iy + 3, items[i], tcol);
            }

            // Submenu for "New..."
            if (g_ctx_submenu_open == 2) {
                int sub_x = g_ctx_x + g_ctx_w - 4;
                int sub_y = g_ctx_y + 4;
                int sub_w = 140;
                int sub_h = 3 * 22 + 8;
                draw_3d_rect(g_screen, SCREEN_W, SCREEN_H, sub_x, sub_y, sub_w, sub_h, 0);
                fill_rect(g_screen, SCREEN_W, SCREEN_H, sub_x + 1, sub_y + 1, sub_w - 2, sub_h - 2, COLOR_MENU_BG);

                const char* sub_items[] = { "Text Document", "New File", "New Folder" };
                for (int s = 0; s < 3; s++) {
                    int sy = sub_y + 4 + s * 22;
                    woos_draw_text(g_screen, SCREEN_W, SCREEN_H, sub_x + 10, sy + 3, sub_items[s], COLOR_TEXT_BLACK);
                }
            }
        }
    }

    // 8. Modal Dialogs (Properties / Rename / New)
    if (g_desktop_dialog.active) {
        woos_dialog_draw(g_screen, SCREEN_W, SCREEN_H, &g_desktop_dialog);
    } else if (g_dialog_mode == 1) {
        int dw = 320;
        int dh = 220;
        int dx = (SCREEN_W - dw) / 2;
        int dy = (SCREEN_H - dh) / 2;

        draw_3d_rect(g_screen, SCREEN_W, SCREEN_H, dx, dy, dw, dh, 0);
        fill_rect(g_screen, SCREEN_W, SCREEN_H, dx + 1, dy + 1, dw - 2, dh - 2, COLOR_MENU_BG);

        // Titlebar
        fill_rect(g_screen, SCREEN_W, SCREEN_H, dx + 2, dy + 2, dw - 4, 20, 0x2A62C9);
        woos_draw_text(g_screen, SCREEN_W, SCREEN_H, dx + 6, dy + 4, "Properties", COLOR_TEXT_WHITE);

        // Properties details
        desktop_item_t* it = (g_dialog_target_idx >= 0 && g_dialog_target_idx < g_item_count) ? &g_items[g_dialog_target_idx] : NULL;
        if (it) {
            draw_item_icon(it, dx + 16, dy + 32);
            woos_draw_text(g_screen, SCREEN_W, SCREEN_H, dx + 64, dy + 34, it->name, COLOR_TEXT_BLACK);
            fill_rect(g_screen, SCREEN_W, SCREEN_H, dx + 16, dy + 70, dw - 32, 1, 0xCCCCCC);

            woos_draw_text(g_screen, SCREEN_W, SCREEN_H, dx + 16, dy + 80, "Type: Desktop Item", 0x333333);
            char pbuf[64] = "Location: ";
            strcat(pbuf, it->full_path);
            woos_draw_text(g_screen, SCREEN_W, SCREEN_H, dx + 16, dy + 104, pbuf, 0x333333);

            char sbuf[32] = "Size: ";
            char nbuf[16];
            int_to_str((int)it->size, nbuf);
            strcat(sbuf, nbuf);
            strcat(sbuf, " Bytes");
            woos_draw_text(g_screen, SCREEN_W, SCREEN_H, dx + 16, dy + 128, sbuf, 0x333333);
        } else {
            woos_draw_text(g_screen, SCREEN_W, SCREEN_H, dx + 16, dy + 34, "Desktop Environment", COLOR_TEXT_BLACK);
            woos_draw_text(g_screen, SCREEN_W, SCREEN_H, dx + 16, dy + 64, "FileSystem: FAT32 (64 MB)", 0x333333);
            woos_draw_text(g_screen, SCREEN_W, SCREEN_H, dx + 16, dy + 88, "Resolution: 1280 x 1024", 0x333333);
        }
        // OK button
        draw_3d_rect(g_screen, SCREEN_W, SCREEN_H, dx + dw / 2 - 35, dy + dh - 32, 70, 22, 0);
        fill_rect(g_screen, SCREEN_W, SCREEN_H, dx + dw / 2 - 34, dy + dh - 31, 68, 20, 0xE4E2D5);
        woos_draw_text(g_screen, SCREEN_W, SCREEN_H, dx + dw / 2 - 10, dy + dh - 28, "OK", COLOR_TEXT_BLACK);
    }
}

static void apply_desktop_dialog(void) {
    if (g_desktop_dialog.input[0]) {
        if (g_desktop_dialog.mode == DIALOG_MODE_RENAME && g_dialog_target_idx >= 0) {
            char new_p[64];
            strcpy(new_p, "/DESKTOP/");
            strcat(new_p, g_desktop_dialog.input);
            woos_vfs_rename(g_items[g_dialog_target_idx].full_path, new_p);
            load_desktop_items();
        } else if (g_desktop_dialog.mode == DIALOG_MODE_NEW) {
            create_desktop_file(g_desktop_dialog.input, "");
        } else if (g_desktop_dialog.mode == 4) { // New Folder
            create_desktop_folder(g_desktop_dialog.input);
        }
    }
}

// Find desktop icon at (x, y)
static int find_desktop_item_at(int x, int y) {
    for (int i = 0; i < g_item_count; i++) {
        desktop_item_t* it = &g_items[i];
        int ix = GRID_MARGIN_X + it->col * GRID_CELL_W;
        int iy = GRID_MARGIN_Y + it->row * GRID_CELL_H;
        if (x >= ix && x < ix + GRID_CELL_W && y >= iy && y < iy + GRID_CELL_H) {
            return i;
        }
    }
    return -1;
}

__attribute__((export_name("_start")))
void _start(void) {
    woos_graphics_info_t ginfo;
    woos_graphics_get_info(&ginfo);

    load_wallpaper();
    load_desktop_items();
    render_screen();
    woos_graphics_present_rect(g_screen, 0, 0, SCREEN_W, SCREEN_H);

    woos_input_event_t ev;

    while (1) {
        int need_redraw = 0;

        while (woos_input_poll_event(&ev)) {
            // 1. Mouse Move
            if (ev.type == 0) {
                g_old_x = g_cursor_x;
                g_old_y = g_cursor_y;
                g_cursor_x = ev.x;
                g_cursor_y = ev.y;

                g_mouse_has_moved = 1;
                int mx1 = (g_old_x < g_cursor_x ? g_old_x : g_cursor_x) - 16;
                int mx2 = (g_old_x > g_cursor_x ? g_old_x : g_cursor_x) + 16;
                int my1 = (g_old_y < g_cursor_y ? g_old_y : g_cursor_y) - 16;
                int my2 = (g_old_y > g_cursor_y ? g_old_y : g_cursor_y) + 16;

                if (mx1 < g_mouse_dirty_min_x) g_mouse_dirty_min_x = mx1;
                if (mx2 > g_mouse_dirty_max_x) g_mouse_dirty_max_x = mx2;
                if (my1 < g_mouse_dirty_min_y) g_mouse_dirty_min_y = my1;
                if (my2 > g_mouse_dirty_max_y) g_mouse_dirty_max_y = my2;

                if (g_drag_win_id != -1) {
                    int ny = g_cursor_y - g_drag_win_off_y;
                    if (ny < 25) ny = 25;
                    woos_graphics_set_window_pos(g_drag_win_id, g_cursor_x - g_drag_win_off_x, ny);
                    need_redraw = 1;
                } else if (g_potential_drag_idx >= 0 && !g_is_dragging_icon) {
                    int dx = g_cursor_x - g_drag_start_x;
                    int dy = g_cursor_y - g_drag_start_y;
                    if (dx > 6 || dx < -6 || dy > 6 || dy < -6) {
                        g_is_dragging_icon = 1;
                        g_drag_icon_idx = g_potential_drag_idx;
                        desktop_item_t* it = &g_items[g_drag_icon_idx];
                        g_drag_dx = g_drag_start_x - (GRID_MARGIN_X + it->col * GRID_CELL_W);
                        g_drag_dy = g_drag_start_y - (GRID_MARGIN_Y + it->row * GRID_CELL_H);
                        need_redraw = 1;
                    }
                } else if (g_is_dragging_icon) {
                    need_redraw = 1;
                } else if (g_ctx_menu_open) {
                    // Update hover item
                    if (g_cursor_x >= g_ctx_x && g_cursor_x < g_ctx_x + g_ctx_w) {
                        int h_idx = (g_cursor_y - (g_ctx_y + 4)) / 22;
                        if (h_idx != g_ctx_hover_idx) {
                            g_ctx_hover_idx = h_idx;
                            if (g_ctx_target_item >= 0) {
                                if (h_idx == 1) g_ctx_submenu_open = 1; // "Open with..."
                                else g_ctx_submenu_open = 0;
                            } else {
                                if (h_idx == 0) g_ctx_submenu_open = 2; // "New..."
                                else g_ctx_submenu_open = 0;
                            }
                            need_redraw = 1;
                        }
                    }
                }
            }

            // 2. Mouse Button Press
            else if (ev.type == 1) {
                int is_right = (ev.buttons & 2) != 0;

                // Handle Dialog click
                if (g_desktop_dialog.active) {
                    int res = woos_dialog_handle_click(&g_desktop_dialog, SCREEN_W, SCREEN_H, ev.x, ev.y);
                    if (res == WOOS_DIALOG_RES_OK) {
                        apply_desktop_dialog();
                    }
                    need_redraw = 1;
                    continue;
                }
                if (g_dialog_mode == 1) {
                    int dw = 320;
                    int dh = 220;
                    int dx = (SCREEN_W - dw) / 2;
                    int dy = (SCREEN_H - dh) / 2;
                    if (ev.x >= dx + dw / 2 - 35 && ev.x <= dx + dw / 2 + 35 && ev.y >= dy + dh - 32 && ev.y <= dy + dh - 10) {
                        g_dialog_mode = 0;
                        need_redraw = 1;
                    }
                    continue;
                }

                // Handle Context Menu click
                if (g_ctx_menu_open) {
                    int clicked_menu = 0;
                    if (g_cursor_x >= g_ctx_x && g_cursor_x < g_ctx_x + g_ctx_w && g_cursor_y >= g_ctx_y && g_cursor_y < g_ctx_y + g_ctx_h) {
                        int it_idx = (g_cursor_y - (g_ctx_y + 4)) / 22;
                        if (g_ctx_target_item >= 0) {
                            int is_sys = (g_ctx_target_item == 0) || is_system_item(g_items[g_ctx_target_item].name);
                            // Icon context actions
                            if (it_idx == 0) launch_item(g_ctx_target_item);
                            else if (it_idx == 2) { // Cut
                                if (!is_sys) {
                                    strcpy(g_clipboard_path, g_items[g_ctx_target_item].full_path);
                                    g_clipboard_is_cut = 1;
                                }
                            } else if (it_idx == 3) { // Copy
                                strcpy(g_clipboard_path, g_items[g_ctx_target_item].full_path);
                                g_clipboard_is_cut = 0;
                            } else if (it_idx == 4) { // Move to Recycle Bin
                                if (!is_sys) {
                                    delete_item_to_recycle(g_ctx_target_item);
                                }
                            } else if (it_idx == 5) { // Rename
                                if (!is_sys) {
                                    g_dialog_target_idx = g_ctx_target_item;
                                    woos_dialog_open(&g_desktop_dialog, DIALOG_MODE_RENAME, "Rename File", "Enter new name:", g_items[g_ctx_target_item].name, "Rename");
                                }
                            } else if (it_idx == 6) { // Properties
                                g_dialog_mode = 1;
                                g_dialog_target_idx = g_ctx_target_item;
                            }
                        } else {
                            // Desktop context actions
                            if (it_idx == 1) paste_clipboard();
                            else if (it_idx == 2) { load_wallpaper(); load_desktop_items(); }
                            else if (it_idx == 3) {
                                g_dialog_mode = 1;
                                g_dialog_target_idx = -1;
                            }
                        }
                        clicked_menu = 1;
                    }
                    // Submenu clicks
                    else if (g_ctx_submenu_open == 1) { // Open with...
                        int sub_x = g_ctx_x + g_ctx_w - 4;
                        int sub_y = g_ctx_y + 4 + 1 * 22;
                        if (g_cursor_x >= sub_x && g_cursor_x < sub_x + 140 && g_cursor_y >= sub_y && g_cursor_y < sub_y + 6 * 22) {
                            int s_idx = (g_cursor_y - (sub_y + 4)) / 22;
                            if (s_idx == 0) {
                                if (g_ctx_target_item >= 0) {
                                    open_file_in_browser(g_items[g_ctx_target_item].full_path);
                                } else {
                                    launch_or_focus("/BROWSER.WAS", "Web Browser");
                                }
                            }
                            else if (s_idx == 1) {
                                if (g_ctx_target_item >= 0) {
                                    open_file_in_notepad(g_items[g_ctx_target_item].full_path);
                                } else {
                                    launch_or_focus("/NOTEPAD.WAS", "Notepad");
                                }
                            }
                            else if (s_idx == 2) {
                                if (g_ctx_target_item >= 0) {
                                    open_file_in_imageview(g_items[g_ctx_target_item].full_path);
                                } else {
                                    launch_or_focus("/IMGVIEW.WAS", "Image Viewer");
                                }
                            }
                            else if (s_idx == 3) launch_or_focus("/EXPLORER.WAS", "File Explorer");
                            else if (s_idx == 4) launch_or_focus("/TERM.WAS", "Terminal");
                            else if (s_idx == 5) launch_or_focus("/CALC.WAS", "Calculator");
                            clicked_menu = 1;
                        }
                    } else if (g_ctx_submenu_open == 2) { // New...
                        int sub_x = g_ctx_x + g_ctx_w - 4;
                        int sub_y = g_ctx_y + 4;
                        if (g_cursor_x >= sub_x && g_cursor_x < sub_x + 140 && g_cursor_y >= sub_y && g_cursor_y < sub_y + 3 * 22) {
                            int s_idx = (g_cursor_y - (sub_y + 4)) / 22;
                            if (s_idx == 0) {
                                char doc_name[32];
                                generate_unique_desktop_name("NEWDOC", ".TXT", doc_name, sizeof(doc_name));
                                create_desktop_file(doc_name, "");
                            } else if (s_idx == 1) {
                                woos_dialog_open(&g_desktop_dialog, DIALOG_MODE_NEW, "Create New File", "Enter file name:", "NEWFILE.TXT", "Create");
                            } else if (s_idx == 2) {
                                woos_dialog_open(&g_desktop_dialog, 4, "Create New Folder", "Enter folder name:", "NEWDIR", "Create");
                            }
                            clicked_menu = 1;
                        }
                    }

                    g_ctx_menu_open = 0;
                    g_ctx_submenu_open = 0;
                    need_redraw = 1;
                    if (clicked_menu) continue;
                }

                // Handle Start Menu click
                if (g_start_menu_open) {
                    int sm_w = 200;
                    int sm_h = 316;
                    int sm_x = 4;
                    int sm_y = SCREEN_H - TASKBAR_H - sm_h - 2;

                    if (g_cursor_x >= sm_x && g_cursor_x < sm_x + sm_w && g_cursor_y >= sm_y && g_cursor_y < sm_y + sm_h) {
                        int it_idx = (g_cursor_y - (sm_y + 10)) / 36;
                        if (it_idx == 0) launch_or_focus("/BROWSER.WAS", "Web Browser");
                        else if (it_idx == 1) launch_or_focus("/EXPLORER.WAS", "File Explorer");
                        else if (it_idx == 2) launch_or_focus("/NOTEPAD.WAS", "Notepad");
                        else if (it_idx == 3) launch_or_focus("/TERM.WAS", "Terminal");
                        else if (it_idx == 4) launch_or_focus("/CALC.WAS", "Calculator");
                        else if (it_idx == 5) launch_or_focus("/IMGVIEW.WAS", "Image Viewer");
                        else if (it_idx == 6) launch_or_focus("/FETCH.WAS", "HTTP Fetch");
                        else if (it_idx == 7) launch_or_focus("/TESTWASI.WAS", "WASI Test");
                    }
                    g_start_menu_open = 0;
                    need_redraw = 1;
                    continue;
                }

                // Check Taskbar
                int tb_y = SCREEN_H - TASKBAR_H;
                if (g_cursor_y >= tb_y) {
                    // Start button: [4..80]
                    if (g_cursor_x >= 4 && g_cursor_x <= 80) {
                        g_start_menu_open = !g_start_menu_open;
                        need_redraw = 1;
                        continue;
                    }

                    // Window Tabs (Icons instead of text)
                    int win_count = woos_graphics_get_window_count();
                    for (int i = 0; i < win_count; i++) {
                        int tab_w = 42;
                        int tab_gap = 4;
                        int tab_x = 86 + i * (tab_w + tab_gap);
                        if (g_cursor_x >= tab_x && g_cursor_x <= tab_x + tab_w) {
                            woos_window_info_t win;
                            if (woos_graphics_get_window_info(i, &win) == 0) {
                                if (win.flags & 8) { // focused -> minimize
                                    woos_graphics_toggle_minimize(win.id);
                                } else {
                                    woos_graphics_bring_to_front(win.id);
                                }
                                need_redraw = 1;
                            }
                            break;
                        }
                    }
                    continue;
                }

                // Check window list in reverse z-order (front to back)
                int win_count = woos_graphics_get_window_count();
                int hit_window = 0;
                for (int i = win_count - 1; i >= 0; i--) {
                    woos_window_info_t win;
                    if (woos_graphics_get_window_info(i, &win) == 0) {
                        if (win.flags & 1) continue; // minimized

                        // Titlebar buttons
                        if (g_cursor_y >= win.y - 22 && g_cursor_y <= win.y) {
                            // [X] Close
                            if (g_cursor_x >= win.x + (int)win.w - 24 && g_cursor_x <= win.x + (int)win.w) {
                                woos_ipc_send(win.thread_id, MSG_CLOSE, 0, 0, 0, 0, 0);
                                woos_graphics_destroy_window(win.id);
                                hit_window = 1;
                                need_redraw = 1;
                                break;
                            }
                            // [□] Maximize
                            if (g_cursor_x >= win.x + (int)win.w - 48 && g_cursor_x <= win.x + (int)win.w - 25) {
                                if (strcmp(win.title, "Calculator") != 0) {
                                    woos_graphics_toggle_maximize(win.id);
                                }
                                hit_window = 1;
                                need_redraw = 1;
                                break;
                            }
                            // [_] Minimize
                            if (g_cursor_x >= win.x + (int)win.w - 72 && g_cursor_x <= win.x + (int)win.w - 49) {
                                woos_graphics_toggle_minimize(win.id);
                                hit_window = 1;
                                need_redraw = 1;
                                break;
                            }
                            // Titlebar Drag & Double-Click Maximize
                            if (g_cursor_x >= win.x - 3 && g_cursor_x <= win.x + (int)win.w - 73) {
                                woos_system_status_t st;
                                woos_system_get_status(&st);
                                if (strcmp(win.title, "Calculator") != 0 && g_last_titlebar_click_win == (int)win.id && (st.uptime_ms - g_last_titlebar_click_time) < 450) {
                                    woos_graphics_toggle_maximize(win.id);
                                    g_last_titlebar_click_win = -1;
                                    hit_window = 1;
                                    need_redraw = 1;
                                    break;
                                }
                                g_last_titlebar_click_win = (int)win.id;
                                g_last_titlebar_click_time = st.uptime_ms;

                                woos_graphics_bring_to_front(win.id);
                                if (!(win.flags & 2)) { // not maximized
                                    g_drag_win_id = win.id;
                                    g_drag_win_w = win.w;
                                    g_drag_win_h = win.h;
                                    g_drag_win_off_x = g_cursor_x - win.x;
                                    g_drag_win_off_y = g_cursor_y - win.y;
                                }
                                hit_window = 1;
                                need_redraw = 1;
                                break;
                            }
                        }

                        // Client Area Click
                        if (g_cursor_x >= win.x && g_cursor_x <= win.x + (int)win.w &&
                            g_cursor_y >= win.y && g_cursor_y <= win.y + (int)win.h) {
                            woos_graphics_bring_to_front(win.id);
                            int client_x = g_cursor_x - win.x;
                            int client_y = g_cursor_y - win.y;
                            int mtype = is_right ? MSG_RIGHT_CLICK : MSG_CLICK;
                            woos_ipc_send(win.thread_id, mtype, client_x, client_y, ev.buttons, 0, 0);
                            hit_window = 1;
                            need_redraw = 1;
                            break;
                        }
                    }
                }
                if (hit_window) continue;

                // Desktop Click
                if (!g_is_dragging_icon && sync_desktop_items()) {
                    need_redraw = 1;
                }
                int item_idx = find_desktop_item_at(g_cursor_x, g_cursor_y);
                if (is_right) {
                    // Right Click on Desktop or Desktop Icon
                    g_ctx_target_item = item_idx;
                    g_ctx_x = g_cursor_x;
                    g_ctx_y = g_cursor_y;
                    g_ctx_w = 175;
                    g_ctx_h = (item_idx >= 0) ? (7 * 22 + 8) : (4 * 22 + 8);
                    if (g_ctx_x + g_ctx_w > SCREEN_W) g_ctx_x = SCREEN_W - g_ctx_w - 8;
                    if (g_ctx_y + g_ctx_h > SCREEN_H - TASKBAR_H) g_ctx_y = SCREEN_H - TASKBAR_H - g_ctx_h - 4;
                    g_ctx_menu_open = 1;
                    g_ctx_hover_idx = -1;
                    g_ctx_submenu_open = 0;
                    need_redraw = 1;
                } else {
                    // Left Click on Desktop or Desktop Icon
                    if (item_idx >= 0) {
                        g_selected_icon_idx = item_idx;
                        woos_system_status_t st;
                        woos_system_get_status(&st);
                        if (g_last_clicked_icon == item_idx && (st.uptime_ms - g_last_click_time) < 450) {
                            // Double Click!
                            launch_item(item_idx);
                            g_last_clicked_icon = -1;
                            g_potential_drag_idx = -1;
                            g_is_dragging_icon = 0;
                        } else {
                            g_last_clicked_icon = item_idx;
                            g_last_click_time = st.uptime_ms;
                            // Set potential drag idx (requires >6px movement to activate)
                            g_potential_drag_idx = item_idx;
                            g_drag_start_x = g_cursor_x;
                            g_drag_start_y = g_cursor_y;
                            g_is_dragging_icon = 0;
                        }
                    } else {
                        g_selected_icon_idx = -1;
                        g_potential_drag_idx = -1;
                        g_is_dragging_icon = 0;
                    }
                    need_redraw = 1;
                }
            }

            // 3. Mouse Button Release
            else if (ev.type == 2) {
                if (g_drag_win_id != -1) {
                    g_drag_win_id = -1;
                    need_redraw = 1;
                }

                if (g_is_dragging_icon && g_drag_icon_idx >= 0) {
                    desktop_item_t* it = &g_items[g_drag_icon_idx];
                    int tcol = (g_cursor_x - GRID_MARGIN_X + GRID_CELL_W / 2) / GRID_CELL_W;
                    int trow = (g_cursor_y - GRID_MARGIN_Y + GRID_CELL_H / 2) / GRID_CELL_H;

                    if (tcol < 0) tcol = 0;
                    if (tcol >= GRID_COLS) tcol = GRID_COLS - 1;
                    if (trow < 0) trow = 0;
                    if (trow >= GRID_ROWS) trow = GRID_ROWS - 1;

                    // Dropping on Recycle Bin deletes the file!
                    if (tcol == 0 && trow == 0 && !it->is_recycle_bin) {
                        if (!is_system_item(it->name)) {
                            delete_item_to_recycle(g_drag_icon_idx);
                        }
                    } else {
                        int occ = is_cell_occupied(tcol, trow, g_drag_icon_idx);
                        if (occ >= 0) {
                            // Swap with occupant
                            g_items[occ].col = it->col;
                            g_items[occ].row = it->row;
                        }
                        it->col = tcol;
                        it->row = trow;
                    }

                    g_is_dragging_icon = 0;
                    g_drag_icon_idx = -1;
                    need_redraw = 1;
                }
                g_potential_drag_idx = -1;
                g_is_dragging_icon = 0;
            }

            // 4. Timer Tick
            else if (ev.type == 3) {
                woos_system_status_t st;
                if (woos_system_get_status(&st) == 0) {
                    uint32_t sec = st.uptime_ms / 1000;
                    if (sec != g_last_uptime_sec) {
                        g_last_uptime_sec = sec;
                        if (!g_is_dragging_icon && sync_desktop_items()) {
                            need_redraw = 1;
                        }
                        need_redraw = 1;
                    }
                }
            }

            // 5. Window Change Notification
            else if (ev.type == 6) {
                if (!g_is_dragging_icon && sync_desktop_items()) {
                    need_redraw = 1;
                }
                need_redraw = 1;
            }

            // 4. Keyboard Input
            else if (ev.type == 4) {
                int ascii = ev.x;
                int scancode = ev.y;

                if (g_desktop_dialog.active) {
                    int res = woos_dialog_handle_key(&g_desktop_dialog, ascii, scancode);
                    if (res == WOOS_DIALOG_RES_OK) {
                        apply_desktop_dialog();
                    }
                    need_redraw = 1;
                } else if (g_dialog_mode == 1) {
                    // Any key closes properties
                    g_dialog_mode = 0;
                    need_redraw = 1;
                } else {
                    if (scancode == 0x3F) { // F5 key: refresh desktop
                        sync_desktop_items();
                        need_redraw = 1;
                    } else if (scancode == 0x53 && woos_graphics_get_focused_tid() <= 0) { // Delete key on desktop
                        if (g_selected_icon_idx > 0 && g_selected_icon_idx < g_item_count) {
                            if (!g_items[g_selected_icon_idx].is_recycle_bin && !is_system_item(g_items[g_selected_icon_idx].name)) {
                                delete_item_to_recycle(g_selected_icon_idx);
                                g_selected_icon_idx = -1;
                                need_redraw = 1;
                            }
                        }
                    } else {
                        // Send keyboard input to focused window
                        int tid = woos_graphics_get_focused_tid();
                        if (tid > 0) {
                            woos_ipc_send(tid, MSG_KEY_DOWN, ascii, scancode, 0, 0, 0);
                        }
                    }
                }
            }
        }

        if (need_redraw) {
            render_screen();
            woos_graphics_present_rect(g_screen, 0, 0, SCREEN_W, SCREEN_H);
            g_mouse_has_moved = 0;
            g_mouse_dirty_min_x = 9999;
            g_mouse_dirty_max_x = -9999;
            g_mouse_dirty_min_y = 9999;
            g_mouse_dirty_max_y = -9999;
        } else if (g_mouse_has_moved) {
            if (g_mouse_dirty_min_x < 0) g_mouse_dirty_min_x = 0;
            if (g_mouse_dirty_min_y < 0) g_mouse_dirty_min_y = 0;
            if (g_mouse_dirty_max_x > SCREEN_W) g_mouse_dirty_max_x = SCREEN_W;
            if (g_mouse_dirty_max_y > SCREEN_H) g_mouse_dirty_max_y = SCREEN_H;

            int dw = g_mouse_dirty_max_x - g_mouse_dirty_min_x;
            int dh = g_mouse_dirty_max_y - g_mouse_dirty_min_y;
            if (dw > 0 && dh > 0) {
                woos_graphics_present_rect(g_screen, g_mouse_dirty_min_x, g_mouse_dirty_min_y, dw, dh);
            }
            g_mouse_has_moved = 0;
            g_mouse_dirty_min_x = 9999;
            g_mouse_dirty_max_x = -9999;
            g_mouse_dirty_min_y = 9999;
            g_mouse_dirty_max_y = -9999;
        }

        woos_input_wait_event();
    }
}
