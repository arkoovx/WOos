#include "woos.h"
#include "icons.h"
#include "woos_dialog.h"

#define DEFAULT_WIN_W 640
#define DEFAULT_WIN_H 420
#define MAX_WIN_W 1280
#define MAX_WIN_H 1024

static int g_win_w = DEFAULT_WIN_W;
static int g_win_h = DEFAULT_WIN_H;

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
#define COLOR_MENU_BG     0xECE9D8
#define COLOR_MENU_HOVER  0x316AC5

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

static uint32_t g_fb[MAX_WIN_W * MAX_WIN_H];
static file_item_t g_items[MAX_ITEMS];
static int g_item_count = 0;
static int g_selected_idx = 0;
static int g_scroll_offset = 0;
static char g_current_path[64] = "/";
static uint32_t g_last_click_tick = 0;
static int g_last_click_idx = -1;

// Context Menu State
static int g_ctx_open = 0;
static int g_ctx_x = 0;
static int g_ctx_y = 0;
static int g_ctx_w = 175;
static int g_ctx_h = 0;
static int g_ctx_target_idx = -1; // -1 = background, >= 0 = file item
static int g_ctx_hover = -1;
static int g_ctx_submenu = 0; // 1 = Open with, 2 = New

// Clipboard
static char g_clipboard_path[64] = "";
static int g_clipboard_is_cut = 0;

// Modal Dialogs
static int g_dialog_mode = 0; // 0 = none, 1 = properties
static woos_file_dialog_t g_file_dialog;

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
        if (strcasecmp_custom(ext, ".WAS") == 0) return "WoOS Application";
        if (strcasecmp_custom(ext, ".TXT") == 0) return "Text Document";
        if (strcasecmp_custom(ext, ".BIN") == 0) return "Binary Image";
        if (strcasecmp_custom(ext, ".BMP") == 0) return "Bitmap Image";
        if (strcasecmp_custom(ext, ".PNG") == 0) return "PNG Image";
        if (strcasecmp_custom(ext, ".JPG") == 0) return "JPEG Image";
    }
    if (len >= 5 && strcasecmp_custom(&name[len - 5], ".JPEG") == 0) return "JPEG Image";
    return "System File";
}

static int is_image_file(const char* fn) {
    if (!fn) return 0;
    int len = (int)strlen(fn);
    if (len < 4) return 0;
    const char* ext = &fn[len - 4];
    if (strcasecmp_custom(ext, ".BMP") == 0 ||
        strcasecmp_custom(ext, ".PNG") == 0 ||
        strcasecmp_custom(ext, ".JPG") == 0) {
        return 1;
    }
    if (len >= 5 && strcasecmp_custom(&fn[len - 5], ".JPEG") == 0) {
        return 1;
    }
    return 0;
}

static void draw_folder_icon(int x, int y) {
    fill_rect(g_fb, g_win_w, g_win_h, x + 1, y, 6, 3, 0xD4AC0D);
    fill_rect(g_fb, g_win_w, g_win_h, x, y + 2, 14, 10, 0xF4D03F);
    fill_rect(g_fb, g_win_w, g_win_h, x, y + 5, 14, 7, 0xF39C12);
    draw_rect(g_fb, g_win_w, g_win_h, x, y + 2, 14, 10, 0xB7950B);
}

static void draw_recycle_icon(int x, int y) {
    fill_rect(g_fb, g_win_w, g_win_h, x + 1, y + 1, 12, 3, 0x8FA4BA);
    fill_rect(g_fb, g_win_w, g_win_h, x + 2, y + 4, 10, 9, 0x4B6584);
    draw_rect(g_fb, g_win_w, g_win_h, x + 2, y + 4, 10, 9, 0x2C3A47);
    fill_rect(g_fb, g_win_w, g_win_h, x + 5, y + 7, 4, 3, 0x20BF6B);
}

static void draw_app_icon(int x, int y) {
    fill_rect(g_fb, g_win_w, g_win_h, x, y, 13, 13, 0xBDC3C7);
    draw_rect(g_fb, g_win_w, g_win_h, x, y, 13, 13, 0x2C3E50);
    fill_rect(g_fb, g_win_w, g_win_h, x + 1, y + 1, 11, 3, 0x2980B9);
    fill_rect(g_fb, g_win_w, g_win_h, x + 1, y + 4, 11, 8, 0xFFFFFF);
    fill_rect(g_fb, g_win_w, g_win_h, x + 3, y + 6, 7, 4, 0xE74C3C);
}

static void draw_text_icon(int x, int y) {
    fill_rect(g_fb, g_win_w, g_win_h, x, y, 11, 13, 0xFFFFFF);
    draw_rect(g_fb, g_win_w, g_win_h, x, y, 11, 13, 0x7F8C8D);
    fill_rect(g_fb, g_win_w, g_win_h, x + 7, y, 4, 4, 0xBDC3C7);
    fill_rect(g_fb, g_win_w, g_win_h, x + 2, y + 4, 5, 1, 0x34495E);
    fill_rect(g_fb, g_win_w, g_win_h, x + 2, y + 6, 7, 1, 0x34495E);
    fill_rect(g_fb, g_win_w, g_win_h, x + 2, y + 8, 7, 1, 0x34495E);
    fill_rect(g_fb, g_win_w, g_win_h, x + 2, y + 10, 4, 1, 0x34495E);
}

static void draw_drive_icon(int x, int y) {
    fill_rect(g_fb, g_win_w, g_win_h, x, y + 1, 14, 9, 0xBDC3C7);
    draw_rect(g_fb, g_win_w, g_win_h, x, y + 1, 14, 9, 0x7F8C8D);
    fill_rect(g_fb, g_win_w, g_win_h, x + 2, y + 7, 8, 2, 0x34495E);
    fill_rect(g_fb, g_win_w, g_win_h, x + 11, y + 7, 2, 2, 0x2ECC71); // Green LED
}

static void load_directory(const char* path) {
    g_item_count = 0;
    g_selected_idx = 0;
    g_scroll_offset = 0;
    strncpy(g_current_path, path, sizeof(g_current_path) - 1);
    g_current_path[sizeof(g_current_path) - 1] = '\0';

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

        // Deduplicate entries by name (case-insensitive)
        int duplicate = 0;
        for (int i = 0; i < g_item_count; i++) {
            if (strcasecmp_custom(g_items[i].name, entry.name) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) continue;

        memcpy(g_items[g_item_count].name, entry.name, 24);
        g_items[g_item_count].name[23] = '\0';
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
                if (strcasecmp_custom(g_items[i].name, g_items[j].name) > 0) {
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

static void open_file_in_imageview(const char* filepath) {
    woos_system_set_open_file(filepath);
    int win_count = woos_graphics_get_window_count();
    woos_window_info_t win;
    int found_tid = -1;
    int found_id = -1;
    for (int i = 0; i < win_count; i++) {
        if (woos_graphics_get_window_info(i, &win) == 0) {
            if (strcmp(win.title, "Image Viewer") == 0 || strncmp(win.title, "Image Viewer", 12) == 0) {
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
        if (is_image_file(fn)) {
            open_file_in_imageview(full_path);
        } else if (len >= 4 && (strcmp(&fn[len - 4], ".WAS") == 0 || strcmp(&fn[len - 4], ".was") == 0)) {
            woos_system_spawn(full_path);
        } else {
            open_file_in_notepad(full_path);
        }
    }
}

static void generate_unique_explorer_name(const char* base, const char* ext, char* out_name, int max_len) {
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

static int is_in_recycle(void) {
    return (strcmp(g_current_path, "/RECYCLE") == 0 || strcmp(g_current_path, "/RECYCLE/") == 0);
}

static void get_item_path(int idx, char* out) {
    if (idx < 0 || idx >= g_item_count) {
        out[0] = '\0';
        return;
    }
    if (strcmp(g_current_path, "/") == 0) {
        out[0] = '/';
        out[1] = '\0';
        strcat(out, g_items[idx].name);
    } else {
        strcpy(out, g_current_path);
        strcat(out, "/");
        strcat(out, g_items[idx].name);
    }
}

static void empty_recycle_bin(void) {
    int h = woos_vfs_open("/RECYCLE", VFS_MODE_READ);
    if (h < 0) return;

    woos_dirent_t ent;
    while (1) {
        int res = woos_vfs_readdir(h, &ent);
        if (res <= 0) break;
        if (ent.name[0] == '.' && (ent.name[1] == '\0' || (ent.name[1] == '.' && ent.name[2] == '\0'))) continue;
        if (is_system_item(ent.name)) continue;

        char del_p[64] = "/RECYCLE/";
        strcat(del_p, ent.name);
        woos_vfs_unlink(del_p);
    }
    woos_vfs_close(h);
    load_directory(g_current_path);
}

static void paste_to_current(void) {
    if (!g_clipboard_path[0]) return;

    const char* p = g_clipboard_path;
    const char* last_slash = p;
    while (*p) {
        if (*p == '/' || *p == '\\') last_slash = p + 1;
        p++;
    }

    char dest[64];
    if (strcmp(g_current_path, "/") == 0) {
        dest[0] = '/';
        dest[1] = '\0';
        strcat(dest, last_slash);
    } else {
        strcpy(dest, g_current_path);
        strcat(dest, "/");
        strcat(dest, last_slash);
    }

    if (g_clipboard_is_cut) {
        woos_vfs_rename(g_clipboard_path, dest);
        g_clipboard_path[0] = '\0';
    } else {
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
    load_directory(g_current_path);
}

static void draw_explorer(void) {
    // 1. Toolbar background
    fill_rect(g_fb, g_win_w, g_win_h, 0, 0, g_win_w, TOOLBAR_H, COLOR_WINDOW_BG);

    // Toolbar buttons: [Up], [Refresh], [Notepad], [Terminal]
    draw_3d_rect(g_fb, g_win_w, g_win_h, 6, 4, 52, 22, 0);
    fill_rect(g_fb, g_win_w, g_win_h, 7, 5, 50, 20, 0xE0DFD8);
    woos_draw_text(g_fb, g_win_w, g_win_h, 18, 9, "Up", COLOR_TEXT);

    draw_3d_rect(g_fb, g_win_w, g_win_h, 64, 4, 68, 22, 0);
    fill_rect(g_fb, g_win_w, g_win_h, 65, 5, 66, 20, 0xE0DFD8);
    woos_draw_text(g_fb, g_win_w, g_win_h, 72, 9, "Refresh", COLOR_TEXT);

    draw_3d_rect(g_fb, g_win_w, g_win_h, 138, 4, 68, 22, 0);
    fill_rect(g_fb, g_win_w, g_win_h, 139, 5, 66, 20, 0xE0DFD8);
    woos_draw_text(g_fb, g_win_w, g_win_h, 146, 9, "Notepad", COLOR_TEXT);

    draw_3d_rect(g_fb, g_win_w, g_win_h, 212, 4, 72, 22, 0);
    fill_rect(g_fb, g_win_w, g_win_h, 213, 5, 70, 20, 0xE0DFD8);
    woos_draw_text(g_fb, g_win_w, g_win_h, 220, 9, "Terminal", COLOR_TEXT);

    // Address Bar row (y=28..48)
    woos_draw_text(g_fb, g_win_w, g_win_h, 8, 33, "Address", COLOR_TEXT);
    draw_3d_rect(g_fb, g_win_w, g_win_h, 64, 28, g_win_w - 70, 20, 1);
    fill_rect(g_fb, g_win_w, g_win_h, 65, 29, g_win_w - 72, 18, 0xFFFFFF);

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
    woos_draw_text(g_fb, g_win_w, g_win_h, 90, 33, addr_str, COLOR_TEXT);

    fill_rect(g_fb, g_win_w, g_win_h, 0, TOOLBAR_H - 1, g_win_w, 1, 0xACA899);

    // 2. Left Sidebar (Navigation pane)
    int content_h = g_win_h - TOOLBAR_H - STATUSBAR_H;
    fill_rect(g_fb, g_win_w, g_win_h, 0, TOOLBAR_H, SIDEBAR_W, content_h, COLOR_SIDEBAR_BG);
    fill_rect(g_fb, g_win_w, g_win_h, SIDEBAR_W - 1, TOOLBAR_H, 1, content_h, 0xACA899);

    fill_rect(g_fb, g_win_w, g_win_h, 0, TOOLBAR_H, SIDEBAR_W - 1, 20, 0x2A62C9);
    woos_draw_text(g_fb, g_win_w, g_win_h, 8, TOOLBAR_H + 4, "Places", 0xFFFFFF);

    // Sidebar: Desktop
    int sb_y = TOOLBAR_H + 26;
    draw_app_icon(8, sb_y);
    woos_draw_text(g_fb, g_win_w, g_win_h, 26, sb_y, "Desktop", (strcmp(g_current_path, "/DESKTOP") == 0) ? 0x003399 : 0x222222);

    // Sidebar: Recycle Bin
    sb_y += 24;
    draw_recycle_icon(8, sb_y);
    woos_draw_text(g_fb, g_win_w, g_win_h, 26, sb_y, "Recycle Bin", is_in_recycle() ? 0x003399 : 0x222222);

    // Sidebar: System Drive (C:)
    sb_y += 24;
    draw_drive_icon(8, sb_y);
    woos_draw_text(g_fb, g_win_w, g_win_h, 26, sb_y, "Drive (C:)", (strcmp(g_current_path, "/") == 0) ? 0x003399 : 0x222222);

    sb_y += 32;
    fill_rect(g_fb, g_win_w, g_win_h, 6, sb_y, SIDEBAR_W - 14, 1, 0xCCCCCC);
    sb_y += 8;
    woos_draw_text(g_fb, g_win_w, g_win_h, 8, sb_y, "Disk Info:", 0x003366);
    sb_y += 18;
    woos_draw_text(g_fb, g_win_w, g_win_h, 8, sb_y, "FS: FAT32", 0x444444);
    sb_y += 16;
    woos_draw_text(g_fb, g_win_w, g_win_h, 8, sb_y, "Size: 64 MB", 0x444444);
    sb_y += 16;
    woos_draw_text(g_fb, g_win_w, g_win_h, 8, sb_y, "Free: ~62 MB", 0x27AE60);

    // 3. Right Pane: Header Bar
    int list_x = SIDEBAR_W;
    int list_w = g_win_w - SIDEBAR_W;
    int list_y = TOOLBAR_H;

    // Header buttons (Name, Size, Type)
    draw_3d_rect(g_fb, g_win_w, g_win_h, list_x, list_y, 220, HEADER_H, 0);
    fill_rect(g_fb, g_win_w, g_win_h, list_x + 1, list_y + 1, 218, HEADER_H - 2, COLOR_HEADER_BG);
    woos_draw_text(g_fb, g_win_w, g_win_h, list_x + 6, list_y + 4, "Name", COLOR_TEXT);

    draw_3d_rect(g_fb, g_win_w, g_win_h, list_x + 220, list_y, 110, HEADER_H, 0);
    fill_rect(g_fb, g_win_w, g_win_h, list_x + 221, list_y + 1, 108, HEADER_H - 2, COLOR_HEADER_BG);
    woos_draw_text(g_fb, g_win_w, g_win_h, list_x + 226, list_y + 4, "Size", COLOR_TEXT);

    int type_w = list_w - 330;
    draw_3d_rect(g_fb, g_win_w, g_win_h, list_x + 330, list_y, type_w, HEADER_H, 0);
    fill_rect(g_fb, g_win_w, g_win_h, list_x + 331, list_y + 1, type_w - 2, HEADER_H - 2, COLOR_HEADER_BG);
    woos_draw_text(g_fb, g_win_w, g_win_h, list_x + 336, list_y + 4, "Type", COLOR_TEXT);

    // 4. File List Body
    int file_list_y = list_y + HEADER_H;
    int file_list_h = content_h - HEADER_H;
    fill_rect(g_fb, g_win_w, g_win_h, list_x, file_list_y, list_w, file_list_h, COLOR_LIST_BG);

    int visible_rows = file_list_h / ROW_H;
    for (int r = 0; r < visible_rows; r++) {
        int idx = g_scroll_offset + r;
        if (idx >= g_item_count) break;

        int row_y = file_list_y + r * ROW_H;
        int is_sel = (idx == g_selected_idx);

        if (is_sel) {
            fill_rect(g_fb, g_win_w, g_win_h, list_x, row_y, list_w, ROW_H, COLOR_SELECT_BG);
        }

        uint32_t text_color = is_sel ? COLOR_SELECT_TEXT : COLOR_TEXT;

        // Draw Icon
        int icon_x = list_x + 6;
        int icon_y = row_y + 3;
        if (g_items[idx].is_dir) {
            draw_folder_icon(icon_x, icon_y);
        } else {
            draw_icon_by_target(g_fb, g_win_w, g_win_h, icon_x, icon_y - 1, 16, g_items[idx].name);
        }

        // Name
        woos_draw_text(g_fb, g_win_w, g_win_h, list_x + 26, row_y + 4, g_items[idx].name, text_color);

        // Size
        if (!g_items[idx].is_dir) {
            char size_buf[16];
            format_size(g_items[idx].size, size_buf);
            woos_draw_text(g_fb, g_win_w, g_win_h, list_x + 226, row_y + 4, size_buf, text_color);
        }

        // Type
        const char* type_str = get_file_type(g_items[idx].name, g_items[idx].is_dir);
        woos_draw_text(g_fb, g_win_w, g_win_h, list_x + 336, row_y + 4, type_str, text_color);
    }

    // 5. Status Bar
    int status_y = g_win_h - STATUSBAR_H;
    fill_rect(g_fb, g_win_w, g_win_h, 0, status_y, g_win_w, STATUSBAR_H, COLOR_WINDOW_BG);
    fill_rect(g_fb, g_win_w, g_win_h, 0, status_y, g_win_w, 1, 0xACA899);

    draw_3d_rect(g_fb, g_win_w, g_win_h, 2, status_y + 2, 240, 18, 1);
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
    woos_draw_text(g_fb, g_win_w, g_win_h, 8, status_y + 5, status_buf, 0x222222);

    draw_3d_rect(g_fb, g_win_w, g_win_h, 246, status_y + 2, g_win_w - 250, 18, 1);
    woos_draw_text(g_fb, g_win_w, g_win_h, 252, status_y + 5, "My Computer | Free: 62.0 MB (FAT32)", 0x222222);

    // 6. Context Menu Popup Overlay
    if (g_ctx_open) {
        draw_3d_rect(g_fb, g_win_w, g_win_h, g_ctx_x, g_ctx_y, g_ctx_w, g_ctx_h, 0);
        fill_rect(g_fb, g_win_w, g_win_h, g_ctx_x + 1, g_ctx_y + 1, g_ctx_w - 2, g_ctx_h - 2, COLOR_MENU_BG);

        if (g_ctx_target_idx >= 0 && g_ctx_target_idx < g_item_count) {
            int is_sys = is_system_item(g_items[g_ctx_target_idx].name);
            if (is_in_recycle()) {
                // Recycle Bin Item Menu
                const char* items[] = { "Restore", "Delete Permanently", "Properties" };
                for (int i = 0; i < 3; i++) {
                    int iy = g_ctx_y + 4 + i * 22;
                    int disabled = (i == 1 && is_sys);
                    int is_h = (i == g_ctx_hover && !disabled);
                    if (is_h) fill_rect(g_fb, g_win_w, g_win_h, g_ctx_x + 2, iy, g_ctx_w - 4, 20, COLOR_MENU_HOVER);
                    uint32_t col = disabled ? COLOR_TEXT_MUTED : (is_h ? 0xFFFFFF : 0x000000);
                    woos_draw_text(g_fb, g_win_w, g_win_h, g_ctx_x + 10, iy + 3, items[i], col);
                }
            } else {
                // Standard Item Menu
                const char* items[] = {
                    "Open",
                    "Open with... >",
                    "Cut",
                    "Copy",
                    "Move to Recycle Bin",
                    "Rename",
                    "Properties"
                };
                for (int i = 0; i < 7; i++) {
                    int iy = g_ctx_y + 4 + i * 22;
                    int disabled = is_sys && (i == 2 || i == 4 || i == 5);
                    int is_sub_active = (i == 1 && g_ctx_submenu == 1);
                    int is_h = ((i == g_ctx_hover) || is_sub_active) && !disabled;
                    if (is_h) fill_rect(g_fb, g_win_w, g_win_h, g_ctx_x + 2, iy, g_ctx_w - 4, 20, COLOR_MENU_HOVER);
                    uint32_t col = disabled ? COLOR_TEXT_MUTED : (is_h ? 0xFFFFFF : 0x000000);
                    woos_draw_text(g_fb, g_win_w, g_win_h, g_ctx_x + 10, iy + 3, items[i], col);
                }
                // Submenu: Open with...
                if (g_ctx_submenu == 1) {
                    int sw = 135;
                    int sh = 5 * 22 + 8;
                    int sx = (g_ctx_x + g_ctx_w - 4 + sw > g_win_w) ? (g_ctx_x - sw + 4) : (g_ctx_x + g_ctx_w - 4);
                    int sy = g_ctx_y + 4 + 1 * 22;
                    if (sy + sh > g_win_h - STATUSBAR_H) sy = g_win_h - STATUSBAR_H - sh - 2;
                    draw_3d_rect(g_fb, g_win_w, g_win_h, sx, sy, sw, sh, 0);
                    fill_rect(g_fb, g_win_w, g_win_h, sx + 1, sy + 1, sw - 2, sh - 2, COLOR_MENU_BG);
                    const char* sitems[] = { "Notepad", "Image Viewer", "File Explorer", "Terminal", "Calculator" };
                    for (int s = 0; s < 5; s++) {
                        int ssy = sy + 4 + s * 22;
                        woos_draw_text(g_fb, g_win_w, g_win_h, sx + 10, ssy + 3, sitems[s], 0x000000);
                    }
                }
            }
        } else {
            // Background Menu
            if (is_in_recycle()) {
                const char* items[] = { "Empty Recycle Bin", "Refresh", "Properties" };
                for (int i = 0; i < 3; i++) {
                    int iy = g_ctx_y + 4 + i * 22;
                    int is_h = (i == g_ctx_hover);
                    if (is_h) fill_rect(g_fb, g_win_w, g_win_h, g_ctx_x + 2, iy, g_ctx_w - 4, 20, COLOR_MENU_HOVER);
                    woos_draw_text(g_fb, g_win_w, g_win_h, g_ctx_x + 10, iy + 3, items[i], is_h ? 0xFFFFFF : 0x000000);
                }
            } else {
                const char* items[] = { "New... >", "Paste", "Refresh", "Properties" };
                for (int i = 0; i < 4; i++) {
                    int iy = g_ctx_y + 4 + i * 22;
                    int is_sub_active = (i == 0 && g_ctx_submenu == 2);
                    int is_h = (i == g_ctx_hover) || is_sub_active;
                    if (is_h) fill_rect(g_fb, g_win_w, g_win_h, g_ctx_x + 2, iy, g_ctx_w - 4, 20, COLOR_MENU_HOVER);
                    uint32_t col = is_h ? 0xFFFFFF : 0x000000;
                    if (i == 1 && !g_clipboard_path[0]) col = COLOR_TEXT_MUTED;
                    woos_draw_text(g_fb, g_win_w, g_win_h, g_ctx_x + 10, iy + 3, items[i], col);
                }
                // Submenu: New...
                if (g_ctx_submenu == 2) {
                    int sw = 135;
                    int sh = 3 * 22 + 8;
                    int sx = (g_ctx_x + g_ctx_w - 4 + sw > g_win_w) ? (g_ctx_x - sw + 4) : (g_ctx_x + g_ctx_w - 4);
                    int sy = g_ctx_y + 4;
                    if (sy + sh > g_win_h - STATUSBAR_H) sy = g_win_h - STATUSBAR_H - sh - 2;
                    draw_3d_rect(g_fb, g_win_w, g_win_h, sx, sy, sw, sh, 0);
                    fill_rect(g_fb, g_win_w, g_win_h, sx + 1, sy + 1, sw - 2, sh - 2, COLOR_MENU_BG);
                    const char* sitems[] = { "Text Document", "New File", "New Folder" };
                    for (int s = 0; s < 3; s++) {
                        int ssy = sy + 4 + s * 22;
                        woos_draw_text(g_fb, g_win_w, g_win_h, sx + 10, ssy + 3, sitems[s], 0x000000);
                    }
                }
            }
        }
    }

    // 7. Dialog Popups (Properties / Rename / New)
    if (g_file_dialog.active) {
        woos_dialog_draw(g_fb, g_win_w, g_win_h, &g_file_dialog);
    } else if (g_dialog_mode == 1) {
        int dw = 280;
        int dh = 200;
        int dx = (g_win_w - dw) / 2;
        int dy = (g_win_h - dh) / 2;

        draw_3d_rect(g_fb, g_win_w, g_win_h, dx, dy, dw, dh, 0);
        fill_rect(g_fb, g_win_w, g_win_h, dx + 1, dy + 1, dw - 2, dh - 2, COLOR_WINDOW_BG);

        // Header
        fill_rect(g_fb, g_win_w, g_win_h, dx + 2, dy + 2, dw - 4, 20, 0x2A62C9);
        woos_draw_text(g_fb, g_win_w, g_win_h, dx + 6, dy + 4, "Properties", 0xFFFFFF);

        if (g_ctx_target_idx >= 0 && g_ctx_target_idx < g_item_count) {
            file_item_t* it = &g_items[g_ctx_target_idx];
            woos_draw_text(g_fb, g_win_w, g_win_h, dx + 16, dy + 32, it->name, 0x000000);
            fill_rect(g_fb, g_win_w, g_win_h, dx + 16, dy + 52, dw - 32, 1, 0xCCCCCC);

            woos_draw_text(g_fb, g_win_w, g_win_h, dx + 16, dy + 62, get_file_type(it->name, it->is_dir), 0x333333);
            char pbuf[64] = "In: ";
            strcat(pbuf, g_current_path);
            woos_draw_text(g_fb, g_win_w, g_win_h, dx + 16, dy + 84, pbuf, 0x333333);

            char sbuf[32] = "Size: ";
            char nbuf[16];
            int_to_str((int)it->size, nbuf);
            strcat(sbuf, nbuf);
            strcat(sbuf, " Bytes");
            woos_draw_text(g_fb, g_win_w, g_win_h, dx + 16, dy + 106, sbuf, 0x333333);
        } else {
            woos_draw_text(g_fb, g_win_w, g_win_h, dx + 16, dy + 36, "Folder Properties", 0x000000);
            char pbuf[64] = "Path: ";
            strcat(pbuf, g_current_path);
            woos_draw_text(g_fb, g_win_w, g_win_h, dx + 16, dy + 64, pbuf, 0x333333);
        }

        draw_3d_rect(g_fb, g_win_w, g_win_h, dx + dw / 2 - 30, dy + dh - 30, 60, 20, 0);
        fill_rect(g_fb, g_win_w, g_win_h, dx + dw / 2 - 29, dy + dh - 29, 58, 18, 0xE4E2D5);
        woos_draw_text(g_fb, g_win_w, g_win_h, dx + dw / 2 - 8, dy + dh - 26, "OK", 0x000000);
    }
}

static void apply_explorer_dialog(void) {
    if (g_file_dialog.input[0]) {
        char full_new[64];
        if (strcmp(g_current_path, "/") == 0) {
            full_new[0] = '/';
            full_new[1] = '\0';
            strcat(full_new, g_file_dialog.input);
        } else {
            strcpy(full_new, g_current_path);
            strcat(full_new, "/");
            strcat(full_new, g_file_dialog.input);
        }

        if (g_file_dialog.mode == DIALOG_MODE_RENAME && g_ctx_target_idx >= 0 && g_ctx_target_idx < g_item_count) {
            if (!is_system_item(g_items[g_ctx_target_idx].name)) {
                char old_p[64];
                get_item_path(g_ctx_target_idx, old_p);
                woos_vfs_rename(old_p, full_new);
            }
        } else if (g_file_dialog.mode == DIALOG_MODE_NEW) {
            int h = woos_vfs_open(full_new, VFS_MODE_WRITE | VFS_MODE_CREATE);
            if (h >= 0) woos_vfs_close(h);
        } else if (g_file_dialog.mode == 4) { // New Folder
            woos_vfs_mkdir(full_new);
        }
        load_directory(g_current_path);
    }
}

static void handle_click(int x, int y) {
    // Handle Dialog
    if (g_file_dialog.active) {
        int res = woos_dialog_handle_click(&g_file_dialog, g_win_w, g_win_h, x, y);
        if (res == WOOS_DIALOG_RES_OK) {
            apply_explorer_dialog();
        }
        return;
    }
    if (g_dialog_mode == 1) {
        int dw = 280;
        int dh = 200;
        int dx = (g_win_w - dw) / 2;
        int dy = (g_win_h - dh) / 2;
        if (x >= dx + dw / 2 - 30 && x <= dx + dw / 2 + 30 && y >= dy + dh - 30 && y <= dy + dh - 10) {
            g_dialog_mode = 0;
        }
        return;
    }

    // Handle Context Menu Click
    if (g_ctx_open) {
        int clicked_menu = 0;

        if (g_ctx_submenu == 1) { // Submenu: Open with...
            int sw = 135;
            int sh = 5 * 22 + 8;
            int sx = (g_ctx_x + g_ctx_w - 4 + sw > g_win_w) ? (g_ctx_x - sw + 4) : (g_ctx_x + g_ctx_w - 4);
            int sy = g_ctx_y + 4 + 1 * 22;
            if (sy + sh > g_win_h - STATUSBAR_H) sy = g_win_h - STATUSBAR_H - sh - 2;

            if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
                int sidx = (y - (sy + 4)) / 22;
                char target_p[64];
                if (g_ctx_target_idx >= 0 && g_ctx_target_idx < g_item_count) {
                    get_item_path(g_ctx_target_idx, target_p);
                } else {
                    target_p[0] = '\0';
                }

                if (sidx == 0) {
                    if (target_p[0]) open_file_in_notepad(target_p);
                    else woos_system_spawn("/NOTEPAD.WAS");
                } else if (sidx == 1) {
                    if (target_p[0]) open_file_in_imageview(target_p);
                    else woos_system_spawn("/IMGVIEW.WAS");
                } else if (sidx == 2) {
                    woos_system_spawn("/EXPLORER.WAS");
                } else if (sidx == 3) {
                    woos_system_spawn("/TERM.WAS");
                } else if (sidx == 4) {
                    woos_system_spawn("/CALC.WAS");
                }
                g_ctx_open = 0;
                g_ctx_submenu = 0;
                return;
            }
        } else if (g_ctx_submenu == 2) { // Submenu: New...
            int sw = 135;
            int sh = 3 * 22 + 8;
            int sx = (g_ctx_x + g_ctx_w - 4 + sw > g_win_w) ? (g_ctx_x - sw + 4) : (g_ctx_x + g_ctx_w - 4);
            int sy = g_ctx_y + 4;
            if (sy + sh > g_win_h - STATUSBAR_H) sy = g_win_h - STATUSBAR_H - sh - 2;

            if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
                int sidx = (y - (sy + 4)) / 22;
                if (sidx == 0) { // Text Document
                    char doc_name[32];
                    generate_unique_explorer_name("NEWDOC", ".TXT", doc_name, sizeof(doc_name));
                    char new_p[64];
                    if (strcmp(g_current_path, "/") == 0) {
                        strcpy(new_p, "/");
                        strcat(new_p, doc_name);
                    } else {
                        strcpy(new_p, g_current_path);
                        strcat(new_p, "/");
                        strcat(new_p, doc_name);
                    }
                    int h = woos_vfs_open(new_p, VFS_MODE_WRITE | VFS_MODE_CREATE | VFS_MODE_TRUNC);
                    if (h >= 0) woos_vfs_close(h);
                    load_directory(g_current_path);
                } else if (sidx == 1) { // New File
                    woos_dialog_open(&g_file_dialog, DIALOG_MODE_NEW, "New File", "Enter file name:", "NEWFILE.TXT", "Create");
                } else if (sidx == 2) { // New Folder
                    woos_dialog_open(&g_file_dialog, 4, "New Folder", "Enter folder name:", "NEWDIR", "Create");
                }
                g_ctx_open = 0;
                g_ctx_submenu = 0;
                return;
            }
        }

        if (x >= g_ctx_x && x < g_ctx_x + g_ctx_w && y >= g_ctx_y && y < g_ctx_y + g_ctx_h) {
            int item_idx = (y - (g_ctx_y + 4)) / 22;
            if (g_ctx_target_idx >= 0 && g_ctx_target_idx < g_item_count) {
                int is_sys = is_system_item(g_items[g_ctx_target_idx].name);
                if (is_in_recycle()) {
                    if (item_idx == 0) { // Restore
                        char src[64];
                        get_item_path(g_ctx_target_idx, src);
                        char dst[64] = "/DESKTOP/";
                        strcat(dst, g_items[g_ctx_target_idx].name);
                        woos_vfs_rename(src, dst);
                        load_directory(g_current_path);
                    } else if (item_idx == 1) { // Delete Permanently
                        if (!is_sys) {
                            char src[64];
                            get_item_path(g_ctx_target_idx, src);
                            woos_vfs_unlink(src);
                            load_directory(g_current_path);
                        }
                    } else if (item_idx == 2) { // Properties
                        g_dialog_mode = 1;
                    }
                } else {
                    if (item_idx == 0) { // Open
                        open_item(g_ctx_target_idx);
                    } else if (item_idx == 1) { // Open with... >
                        g_ctx_submenu = (g_ctx_submenu == 1) ? 0 : 1;
                        return; // Keep menu open!
                    } else if (item_idx == 2) { // Cut
                        if (!is_sys) {
                            get_item_path(g_ctx_target_idx, g_clipboard_path);
                            g_clipboard_is_cut = 1;
                        }
                    } else if (item_idx == 3) { // Copy
                        get_item_path(g_ctx_target_idx, g_clipboard_path);
                        g_clipboard_is_cut = 0;
                    } else if (item_idx == 4) { // Move to Recycle Bin
                        if (!is_sys) {
                            char src[64];
                            get_item_path(g_ctx_target_idx, src);
                            char dst[64] = "/RECYCLE/";
                            strcat(dst, g_items[g_ctx_target_idx].name);
                            woos_vfs_rename(src, dst);
                            load_directory(g_current_path);
                        }
                    } else if (item_idx == 5) { // Rename
                        if (!is_sys) {
                            woos_dialog_open(&g_file_dialog, DIALOG_MODE_RENAME, "Rename", "Enter new name:", g_items[g_ctx_target_idx].name, "Rename");
                        }
                    } else if (item_idx == 6) { // Properties
                        g_dialog_mode = 1;
                    }
                }
            } else {
                if (is_in_recycle()) {
                    if (item_idx == 0) empty_recycle_bin();
                    else if (item_idx == 1) load_directory(g_current_path);
                    else if (item_idx == 2) g_dialog_mode = 1;
                } else {
                    if (item_idx == 0) { // New... >
                        g_ctx_submenu = (g_ctx_submenu == 2) ? 0 : 2;
                        return; // Keep menu open!
                    } else if (item_idx == 1) {
                        paste_to_current();
                    } else if (item_idx == 2) {
                        load_directory(g_current_path);
                    } else if (item_idx == 3) {
                        g_dialog_mode = 1;
                    }
                }
            }
            clicked_menu = 1;
        }

        g_ctx_open = 0;
        g_ctx_submenu = 0;
        if (clicked_menu) return;
    }

    // Check Toolbar buttons (y=4..26)
    if (y >= 4 && y <= 26) {
        if (x >= 6 && x <= 58) { navigate_up(); return; }
        if (x >= 64 && x <= 132) { load_directory(g_current_path); return; }
        if (x >= 138 && x <= 206) { woos_system_spawn("/NOTEPAD.WAS"); return; }
        if (x >= 212 && x <= 284) { woos_system_spawn("/TERM.WAS"); return; }
    }

    // Check Sidebar Places
    if (x < SIDEBAR_W && y >= TOOLBAR_H) {
        // Desktop
        if (y >= TOOLBAR_H + 24 && y <= TOOLBAR_H + 46) {
            load_directory("/DESKTOP");
            return;
        }
        // Recycle Bin
        if (y >= TOOLBAR_H + 48 && y <= TOOLBAR_H + 70) {
            load_directory("/RECYCLE");
            return;
        }
        // Drive (C:)
        if (y >= TOOLBAR_H + 72 && y <= TOOLBAR_H + 94) {
            load_directory("/");
            return;
        }
    }

    // Check File List items
    int file_list_y = TOOLBAR_H + HEADER_H;
    int file_list_h = g_win_h - TOOLBAR_H - STATUSBAR_H - HEADER_H;
    if (x >= SIDEBAR_W && y >= file_list_y && y < file_list_y + file_list_h) {
        int clicked_row = (y - file_list_y) / ROW_H;
        int idx = g_scroll_offset + clicked_row;
        if (idx >= 0 && idx < g_item_count) {
            if (g_selected_idx == idx) {
                open_item(idx);
            } else {
                g_selected_idx = idx;
            }
        }
    }
}

static void handle_right_click(int x, int y) {
    if (g_file_dialog.active || g_dialog_mode != 0) return;

    // Check if right clicked on file item
    int file_list_y = TOOLBAR_H + HEADER_H;
    int file_list_h = g_win_h - TOOLBAR_H - STATUSBAR_H - HEADER_H;
    int clicked_item = -1;

    if (x >= SIDEBAR_W && y >= file_list_y && y < file_list_y + file_list_h) {
        int clicked_row = (y - file_list_y) / ROW_H;
        int idx = g_scroll_offset + clicked_row;
        if (idx >= 0 && idx < g_item_count) {
            clicked_item = idx;
            g_selected_idx = idx;
        }
    }

    g_ctx_target_idx = clicked_item;
    g_ctx_x = x;
    g_ctx_y = y;
    g_ctx_w = 175;
    if (clicked_item >= 0) {
        g_ctx_h = is_in_recycle() ? (3 * 22 + 8) : (7 * 22 + 8);
    } else {
        g_ctx_h = is_in_recycle() ? (3 * 22 + 8) : (4 * 22 + 8);
    }

    if (g_ctx_x + g_ctx_w > g_win_w) g_ctx_x = g_win_w - g_ctx_w - 4;
    if (g_ctx_y + g_ctx_h > g_win_h - STATUSBAR_H) g_ctx_y = g_win_h - STATUSBAR_H - g_ctx_h - 2;

    g_ctx_open = 1;
    g_ctx_hover = -1;
    g_ctx_submenu = 0;
}

static void handle_key(int ascii, int scancode) {
    if (g_file_dialog.active) {
        int res = woos_dialog_handle_key(&g_file_dialog, ascii, scancode);
        if (res == WOOS_DIALOG_RES_OK) {
            apply_explorer_dialog();
        }
        return;
    }
    if (g_dialog_mode != 0) {
        if (g_dialog_mode == 1) {
            g_dialog_mode = 0;
            return;
        }
    }

    if (ascii == '\n' || ascii == '\r' || scancode == 0x1C) {
        open_item(g_selected_idx);
        return;
    }

    if (ascii == '\b' || scancode == 0x0E) {
        navigate_up();
        return;
    }

    // Delete key (scancode 0x53)
    if (scancode == 0x53) {
        if (g_selected_idx >= 0 && g_selected_idx < g_item_count) {
            if (is_system_item(g_items[g_selected_idx].name)) {
                return; // Protected system item!
            }
            char src[64];
            get_item_path(g_selected_idx, src);
            if (is_in_recycle()) {
                woos_vfs_unlink(src);
            } else {
                char dst[64] = "/RECYCLE/";
                strcat(dst, g_items[g_selected_idx].name);
                woos_vfs_rename(src, dst);
            }
            load_directory(g_current_path);
        }
        return;
    }

    // F5 key (scancode 0x3F): Refresh
    if (scancode == 0x3F) {
        load_directory(g_current_path);
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
            int visible_rows = (g_win_h - TOOLBAR_H - STATUSBAR_H - HEADER_H) / ROW_H;
            if (g_selected_idx >= g_scroll_offset + visible_rows) {
                g_scroll_offset = g_selected_idx - visible_rows + 1;
            }
        }
        return;
    }
}

__attribute__((export_name("_start")))
void _start(void) {
    int win_id = woos_create_window(g_win_w, g_win_h, "File Explorer", 0);
    if (win_id < 0) return;

    load_directory("/");
    draw_explorer();
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
            } else if (msg.type == MSG_CLICK || msg.type == MSG_RIGHT_CLICK) {
                int is_right = (msg.type == MSG_RIGHT_CLICK) || ((msg.arg3 & 2) != 0);
                if (is_right) {
                    handle_right_click((int)msg.arg1, (int)msg.arg2);
                } else {
                    handle_click((int)msg.arg1, (int)msg.arg2);
                }
                needs_redraw = 1;
            } else if (msg.type == MSG_KEY_DOWN) {
                handle_key((int)msg.arg1, (int)msg.arg2);
                needs_redraw = 1;
            } else if (msg.type == MSG_SCROLL) {
                int8_t dz = (int8_t)(int32_t)msg.arg3;
                g_scroll_offset -= (int)dz * 3;
                if (g_scroll_offset < 0) g_scroll_offset = 0;
                int visible_rows = (g_win_h - 100) / 22;
                int max_off = (g_item_count > visible_rows) ? (g_item_count - visible_rows) : 0;
                if (g_scroll_offset > max_off) g_scroll_offset = max_off;
                needs_redraw = 1;
            }
        }

        if (needs_redraw) {
            draw_explorer();
            woos_blit_window(win_id, g_fb, g_win_w, g_win_h);
        }

        woos_ipc_wait_message();
    }
}
