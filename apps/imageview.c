#include "woos.h"
#include "image.h"
#include "woos_dialog.h"

#define DEFAULT_WIN_W 680
#define DEFAULT_WIN_H 460
#define MAX_WIN_W 1280
#define MAX_WIN_H 1024

#define TOOLBAR_H 30
#define STATUSBAR_H 22

#define COLOR_TOOLBAR_BG    0x2F3542
#define COLOR_TOOLBAR_BTN   0x404859
#define COLOR_BTN_HOVER     0x57606F
#define COLOR_BTN_BORDER    0x747D8C
#define COLOR_TEXT_WHITE    0xFFFFFF
#define COLOR_TEXT_MUTED    0xA4B0BE
#define COLOR_STATUSBAR_BG  0x1E272E
#define COLOR_CHECKER_LIGHT 0xFFFFFF
#define COLOR_CHECKER_DARK  0xDFE4EA

static int g_win_w = DEFAULT_WIN_W;
static int g_win_h = DEFAULT_WIN_H;
static uint32_t g_fb[MAX_WIN_W * MAX_WIN_H];
static woos_image_t* g_current_image = 0;
static char g_image_path[64] = "/WALLPAP.BMP";
static int g_zoom_pct = 100; // 100 = 100%
static int g_fit_mode = 1;   // 1 = fit to window
static int g_pan_x = 0;
static int g_pan_y = 0;
static int g_is_panning = 0;
static int g_last_mouse_x = 0;
static int g_last_mouse_y = 0;
static char g_status_msg[64] = "Ready";

static woos_file_dialog_t g_dialog;

static inline void set_status(const char* msg) {
    size_t i = 0;
    while (msg[i] != '\0' && i + 1 < sizeof(g_status_msg)) {
        g_status_msg[i] = msg[i];
        i++;
    }
    g_status_msg[i] = '\0';
}

static void load_image(const char* path) {
    if (!path || path[0] == '\0') return;

    woos_image_t* loaded = image_load_file(path);
    if (loaded) {
        if (g_current_image) {
            image_free(g_current_image);
        }
        g_current_image = loaded;
        size_t i = 0;
        while (path[i] != '\0' && i + 1 < sizeof(g_image_path)) {
            g_image_path[i] = path[i];
            i++;
        }
        g_image_path[i] = '\0';
        g_pan_x = 0;
        g_pan_y = 0;
        g_fit_mode = 1;
        set_status("Image loaded successfully");
    } else {
        set_status("Failed to open image file");
    }
}

static void draw_btn(int x, int y, int w, int h, const char* label) {
    int win_w = g_win_w;
    int win_h = g_win_h;
    for (int dy = 0; dy < h; dy++) {
        uint32_t* row = g_fb + (y + dy) * win_w;
        for (int dx = 0; dx < w; dx++) {
            row[x + dx] = COLOR_TOOLBAR_BTN;
        }
    }
    // 1px border
    for (int dx = 0; dx < w; dx++) {
        g_fb[y * win_w + x + dx] = COLOR_BTN_BORDER;
        g_fb[(y + h - 1) * win_w + x + dx] = COLOR_BTN_BORDER;
    }
    for (int dy = 0; dy < h; dy++) {
        g_fb[(y + dy) * win_w + x] = COLOR_BTN_BORDER;
        g_fb[(y + dy) * win_w + x + w - 1] = COLOR_BTN_BORDER;
    }

    int t_len = 0;
    while (label[t_len] != '\0') t_len++;
    int tx = x + (w - t_len * 8) / 2;
    int ty = y + (h - 12) / 2;
    woos_draw_text(g_fb, win_w, win_h, tx, ty, label, COLOR_TEXT_WHITE);
}

static void render_screen(void) {
    int win_w = g_win_w;
    int win_h = g_win_h;

    // 1. Viewport Canvas (y = TOOLBAR_H .. win_h - STATUSBAR_H)
    int canvas_y = TOOLBAR_H;
    int canvas_h = win_h - TOOLBAR_H - STATUSBAR_H;
    int canvas_w = win_w;
    if (canvas_h < 10) canvas_h = 10;

    // Checkerboard pattern
    for (int y = 0; y < canvas_h; y++) {
        int py = canvas_y + y;
        uint32_t* row = g_fb + py * win_w;
        for (int x = 0; x < canvas_w; x++) {
            int check = ((x / 12) ^ (y / 12)) & 1;
            row[x] = check ? COLOR_CHECKER_LIGHT : COLOR_CHECKER_DARK;
        }
    }

    // Draw Image (Strictly clipped to viewport canvas!)
    if (g_current_image && g_current_image->pixels && g_current_image->width > 0 && g_current_image->height > 0) {
        int disp_w = g_current_image->width;
        int disp_h = g_current_image->height;

        if (g_fit_mode) {
            image_calc_fit(g_current_image->width, g_current_image->height, canvas_w - 16, canvas_h - 16, &disp_w, &disp_h);
            g_zoom_pct = (disp_w * 100) / g_current_image->width;
        } else {
            disp_w = (g_current_image->width * g_zoom_pct) / 100;
            disp_h = (g_current_image->height * g_zoom_pct) / 100;
        }

        if (disp_w <= 0) disp_w = 1;
        if (disp_h <= 0) disp_h = 1;

        int img_x = (canvas_w - disp_w) / 2 + g_pan_x;
        int img_y = canvas_y + (canvas_h - disp_h) / 2 + g_pan_y;

        // Scaled buffer
        uint32_t* scaled = (uint32_t*)woos_malloc((size_t)disp_w * disp_h * sizeof(uint32_t));
        if (scaled) {
            image_scale_bilinear(g_current_image->pixels, g_current_image->width, g_current_image->height,
                                 scaled, disp_w, disp_h);

            // Blit scaled image strictly clipped inside canvas_y .. canvas_y + canvas_h
            image_blit_alpha_clipped(g_fb, win_w, win_h, img_x, img_y, scaled, disp_w, disp_h, disp_w,
                                     0, canvas_y, canvas_w, canvas_h);
            woos_free(scaled);
        }
    } else {
        // Placeholder text
        woos_draw_text(g_fb, win_w, win_h, canvas_w / 2 - 120, canvas_y + canvas_h / 2 - 8,
                       "No image loaded - click [Open]", 0x57606F);
    }

    // 2. Toolbar background (y = 0..TOOLBAR_H) - drawn on top of canvas
    for (int y = 0; y < TOOLBAR_H; y++) {
        uint32_t* row = g_fb + y * win_w;
        for (int x = 0; x < win_w; x++) {
            row[x] = COLOR_TOOLBAR_BG;
        }
    }
    // Toolbar bottom divider
    for (int x = 0; x < win_w; x++) {
        g_fb[(TOOLBAR_H - 1) * win_w + x] = 0x1E272E;
    }

    // Toolbar buttons
    draw_btn(8, 4, 60, 22, "Open");
    draw_btn(72, 4, 72, 22, "Save As");
    draw_btn(148, 4, 56, 22, "Zoom +");
    draw_btn(208, 4, 56, 22, "Zoom -");
    draw_btn(268, 4, 46, 22, "1:1");
    draw_btn(318, 4, 46, 22, "Fit");
    draw_btn(368, 4, 64, 22, "Rotate");
    draw_btn(436, 4, 114, 22, "Set Wallpaper");

    // 3. Status Bar (y = win_h - STATUSBAR_H .. win_h) - drawn on top
    int sb_y = win_h - STATUSBAR_H;
    for (int y = sb_y; y < win_h; y++) {
        uint32_t* row = g_fb + y * win_w;
        for (int x = 0; x < win_w; x++) {
            row[x] = COLOR_STATUSBAR_BG;
        }
    }
    // Top border
    for (int x = 0; x < win_w; x++) {
        g_fb[sb_y * win_w + x] = 0x2F3542;
    }

    // Status metrics string
    char status_str[128];
    status_str[0] = '\0';
    if (g_current_image) {
        char dim_str[32];
        char w_str[12], h_str[12], z_str[12];
        int_to_str(g_current_image->width, w_str);
        int_to_str(g_current_image->height, h_str);
        int_to_str(g_zoom_pct, z_str);

        size_t p = 0;
        const char* pth = g_image_path;
        while (pth[p] != '\0' && p < 32) { status_str[p] = pth[p]; p++; }
        status_str[p++] = ' '; status_str[p++] = '|'; status_str[p++] = ' ';
        size_t d = 0; while (w_str[d] != '\0') status_str[p++] = w_str[d++];
        status_str[p++] = 'x';
        d = 0; while (h_str[d] != '\0') status_str[p++] = h_str[d++];
        status_str[p++] = ' '; status_str[p++] = '|'; status_str[p++] = ' ';
        d = 0; while (z_str[d] != '\0') status_str[p++] = z_str[d++];
        status_str[p++] = '%';
        status_str[p++] = ' '; status_str[p++] = '|'; status_str[p++] = ' ';
        d = 0;
        const char* sm = g_status_msg;
        while (sm[d] != '\0' && p + 1 < sizeof(status_str)) status_str[p++] = sm[d++];
        status_str[p] = '\0';
    } else {
        const char* sm = g_status_msg;
        size_t p = 0;
        while (sm[p] != '\0' && p + 1 < sizeof(status_str)) { status_str[p] = sm[p]; p++; }
        status_str[p] = '\0';
    }
    woos_draw_text(g_fb, win_w, win_h, 8, sb_y + 5, status_str, COLOR_TEXT_MUTED);

    // 4. Modal Dialog (Open / Save As)
    if (g_dialog.active) {
        woos_dialog_draw(g_fb, win_w, win_h, &g_dialog);
    }
}

__attribute__((export_name("_start")))
void _start(void) {
    int win_id = woos_create_window(g_win_w, g_win_h, "Image Viewer", 0);
    if (win_id < 0) return;

    // Check if launched with an open file
    char open_path[64] = "";
    if (woos_system_get_open_file(open_path, sizeof(open_path)) > 0 && open_path[0] != '\0') {
        load_image(open_path);
        woos_system_clear_open_file();
    } else {
        // Default to /WALLPAP.BMP or sample
        load_image("/WALLPAP.BMP");
    }

    render_screen();
    woos_blit_window(win_id, g_fb, g_win_w, g_win_h);

    woos_msg_t msg;

    while (1) {
        int need_redraw = 0;

        while (woos_ipc_recv(&msg) > 0) {
            if (msg.type == MSG_CLOSE) {
                woos_destroy_window(win_id);
                if (g_current_image) {
                    image_free(g_current_image);
                }
                return;
            }

            if (msg.type == MSG_RESIZE) {
                int new_w = (int)msg.arg1;
                int new_h = (int)msg.arg2;
                if (new_w > 0 && new_h > 0) {
                    if (new_w > MAX_WIN_W) new_w = MAX_WIN_W;
                    if (new_h > MAX_WIN_H) new_h = MAX_WIN_H;
                    g_win_w = new_w;
                    g_win_h = new_h;
                    g_pan_x = 0;
                    g_pan_y = 0;
                    need_redraw = 1;
                }
            }

            if (msg.type == MSG_OPEN_FILE) {
                char req_path[64] = "";
                if (woos_system_get_open_file(req_path, sizeof(req_path)) > 0 && req_path[0] != '\0') {
                    load_image(req_path);
                    woos_system_clear_open_file();
                    need_redraw = 1;
                }
            }

            if (msg.type == MSG_CLICK) {
                int mx = (int)msg.arg1;
                int my = (int)msg.arg2;

                if (g_dialog.active) {
                    int res = woos_dialog_handle_click(&g_dialog, g_win_w, g_win_h, mx, my);
                    if (res == WOOS_DIALOG_RES_OK) {
                        if (g_dialog.mode == DIALOG_MODE_OPEN) {
                            load_image(g_dialog.input);
                        } else if (g_dialog.mode == DIALOG_MODE_SAVE_AS && g_current_image) {
                            if (g_dialog.input[0] != '\0') {
                                if (image_save_file(g_dialog.input, g_current_image->pixels,
                                                    g_current_image->width, g_current_image->height, 0) == 0) {
                                    strncpy(g_image_path, g_dialog.input, sizeof(g_image_path) - 1);
                                    g_image_path[sizeof(g_image_path) - 1] = '\0';
                                    set_status("Image saved successfully");
                                } else {
                                    set_status("Error saving image");
                                }
                            }
                        }
                    }
                    need_redraw = 1;
                } else {
                    // Toolbar buttons (y: 4..26)
                    if (my >= 4 && my <= 26) {
                        if (mx >= 8 && mx <= 68) { // Open
                            woos_dialog_open(&g_dialog, DIALOG_MODE_OPEN, "Open Image...", "Enter image path (.png, .jpg, .bmp):", g_image_path, "Open");
                            need_redraw = 1;
                        } else if (mx >= 72 && mx <= 144) { // Save As
                            woos_dialog_open(&g_dialog, DIALOG_MODE_SAVE_AS, "Save Image As...", "Enter destination file path (.png, .jpg, .bmp):", g_image_path, "Save");
                            need_redraw = 1;
                        } else if (mx >= 148 && mx <= 204) { // Zoom +
                            g_fit_mode = 0;
                            if (g_zoom_pct < 400) g_zoom_pct += 25;
                            set_status("Zoom in");
                            need_redraw = 1;
                        } else if (mx >= 208 && mx <= 264) { // Zoom -
                            g_fit_mode = 0;
                            if (g_zoom_pct > 25) g_zoom_pct -= 25;
                            set_status("Zoom out");
                            need_redraw = 1;
                        } else if (mx >= 268 && mx <= 314) { // 1:1
                            g_fit_mode = 0;
                            g_zoom_pct = 100;
                            g_pan_x = 0;
                            g_pan_y = 0;
                            set_status("100% scale");
                            need_redraw = 1;
                        } else if (mx >= 318 && mx <= 364) { // Fit
                            g_fit_mode = 1;
                            g_pan_x = 0;
                            g_pan_y = 0;
                            set_status("Fit to window");
                            need_redraw = 1;
                        } else if (mx >= 368 && mx <= 432) { // Rotate
                            if (g_current_image && g_current_image->pixels) {
                                uint32_t* rotated = image_rotate_90_cw(g_current_image->pixels,
                                                                       g_current_image->width,
                                                                       g_current_image->height);
                                if (rotated) {
                                    woos_free(g_current_image->pixels);
                                    g_current_image->pixels = rotated;
                                    int tmp = g_current_image->width;
                                    g_current_image->width = g_current_image->height;
                                    g_current_image->height = tmp;
                                    set_status("Rotated 90 deg clockwise");
                                    need_redraw = 1;
                                }
                            }
                        } else if (mx >= 436 && mx <= 550) { // Set Wallpaper
                            if (g_current_image && g_current_image->pixels) {
                                if (image_save_file("/WALLPAP.BMP", g_current_image->pixels,
                                                    g_current_image->width, g_current_image->height, "bmp") == 0) {
                                    set_status("Saved as /WALLPAP.BMP!");
                                } else {
                                    set_status("Failed to set wallpaper");
                                }
                                need_redraw = 1;
                            }
                        }
                    }
                }
            }

            if (msg.type == MSG_KEY_DOWN) {
                int ascii = (int)msg.arg1;
                int scancode = (int)msg.arg2;

                if (g_dialog.active) {
                    int res = woos_dialog_handle_key(&g_dialog, ascii, scancode);
                    if (res == WOOS_DIALOG_RES_OK) {
                        if (g_dialog.mode == DIALOG_MODE_OPEN) {
                            load_image(g_dialog.input);
                        } else if (g_dialog.mode == DIALOG_MODE_SAVE_AS && g_current_image) {
                            if (g_dialog.input[0] != '\0') {
                                if (image_save_file(g_dialog.input, g_current_image->pixels,
                                                    g_current_image->width, g_current_image->height, 0) == 0) {
                                    strncpy(g_image_path, g_dialog.input, sizeof(g_image_path) - 1);
                                    g_image_path[sizeof(g_image_path) - 1] = '\0';
                                    set_status("Image saved successfully");
                                } else {
                                    set_status("Error saving image");
                                }
                            }
                        }
                    }
                    need_redraw = 1;
                }
            }
        }

        if (need_redraw) {
            render_screen();
            woos_blit_window(win_id, g_fb, g_win_w, g_win_h);
        }

        woos_ipc_wait_message();
    }
}
