#ifndef WOOS_ICONS_H
#define WOOS_ICONS_H

#include "woos.h"

static inline void icon_fill_rect(uint32_t* buf, int bw, int bh, int rx, int ry, int rw, int rh, uint32_t color) {
    if (!buf || rw <= 0 || rh <= 0) return;
    for (int y = ry; y < ry + rh; y++) {
        if (y < 0 || y >= bh) continue;
        uint32_t* row = buf + y * bw;
        for (int x = rx; x < rx + rw; x++) {
            if (x < 0 || x >= bw) continue;
            row[x] = color;
        }
    }
}

static inline void icon_draw_rect(uint32_t* buf, int bw, int bh, int rx, int ry, int rw, int rh, uint32_t color) {
    icon_fill_rect(buf, bw, bh, rx, ry, rw, 1, color);
    icon_fill_rect(buf, bw, bh, rx, ry + rh - 1, rw, 1, color);
    icon_fill_rect(buf, bw, bh, rx, ry, 1, rh, color);
    icon_fill_rect(buf, bw, bh, rx + rw - 1, ry, 1, rh, color);
}

// 1. Calculator Icon
static inline void draw_calc_icon(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    int pad = s / 10;
    if (pad < 1) pad = 1;
    int w = s - pad * 2;
    int h = s - pad * 2;
    int ox = x + pad;
    int oy = y + pad;

    // Body
    icon_fill_rect(buf, bw, bh, ox, oy, w, h, 0x2D3436);
    icon_draw_rect(buf, bw, bh, ox, oy, w, h, 0x636E72);

    // LCD Screen
    int lcd_h = h / 4;
    int lcd_w = w - 4;
    icon_fill_rect(buf, bw, bh, ox + 2, oy + 2, lcd_w, lcd_h, 0x00B894);
    icon_draw_rect(buf, bw, bh, ox + 2, oy + 2, lcd_w, lcd_h, 0x00624A);
    // LCD digits simulation (dark segments)
    icon_fill_rect(buf, bw, bh, ox + lcd_w - 6, oy + 4, 4, lcd_h - 4, 0x004130);

    // Grid of buttons
    int btn_start_y = oy + lcd_h + 4;
    int btn_area_h = h - (lcd_h + 6);
    int rows = 3;
    int cols = 3;
    int btn_w = (w - 4 - (cols - 1) * 2) / cols;
    int btn_h = (btn_area_h - (rows - 1) * 2) / rows;
    if (btn_w < 1) btn_w = 1;
    if (btn_h < 1) btn_h = 1;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int bx = ox + 2 + c * (btn_w + 2);
            int by = btn_start_y + r * (btn_h + 2);
            uint32_t col = (c == cols - 1 || r == rows - 1) ? 0xE17055 : 0xDFE6E9;
            icon_fill_rect(buf, bw, bh, bx, by, btn_w, btn_h, col);
        }
    }
}

// 2. Terminal Icon
static inline void draw_term_icon(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    int pad = s / 10;
    if (pad < 1) pad = 1;
    int w = s - pad * 2;
    int h = s - pad * 2;
    int ox = x + pad;
    int oy = y + pad;

    // Monitor / window body
    icon_fill_rect(buf, bw, bh, ox, oy, w, h, 0x1E272E);
    icon_draw_rect(buf, bw, bh, ox, oy, w, h, 0x485460);

    // Titlebar
    int th = (h >= 24) ? 5 : 3;
    icon_fill_rect(buf, bw, bh, ox + 1, oy + 1, w - 2, th, 0x2F3542);
    // 3 small dots
    if (w >= 16) {
        icon_fill_rect(buf, bw, bh, ox + 3, oy + 2, 2, 2, 0xFF4757);
        icon_fill_rect(buf, bw, bh, ox + 6, oy + 2, 2, 2, 0xFFA502);
        icon_fill_rect(buf, bw, bh, ox + 9, oy + 2, 2, 2, 0x2ED573);
    }

    // Terminal prompt ">_"
    int px = ox + 3;
    int py = oy + th + 4;
    // Draw '>'
    for (int i = 0; i < 4; i++) {
        icon_fill_rect(buf, bw, bh, px + i, py + i, 1, 1, 0x00FF9F);
        icon_fill_rect(buf, bw, bh, px + i, py + 6 - i, 1, 1, 0x00FF9F);
    }
    // Draw cursor '_'
    if (w >= 16) {
        icon_fill_rect(buf, bw, bh, px + 6, py + 6, 5, 2, 0x00FF9F);
    }
}

// 3. Notepad Icon
static inline void draw_notepad_icon(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    int pad = s / 10;
    if (pad < 1) pad = 1;
    int w = s - pad * 2;
    int h = s - pad * 2;
    int ox = x + pad;
    int oy = y + pad;

    // Paper sheet
    icon_fill_rect(buf, bw, bh, ox, oy, w, h, 0xFFFFFF);
    icon_draw_rect(buf, bw, bh, ox, oy, w, h, 0xB2BEC3);

    // Top spiral binding bar
    int spiral_h = (h >= 24) ? 5 : 3;
    icon_fill_rect(buf, bw, bh, ox, oy, w, spiral_h, 0x0984E3);
    // Spiral rings
    for (int rx = ox + 3; rx < ox + w - 2; rx += 4) {
        icon_fill_rect(buf, bw, bh, rx, oy - 1, 2, spiral_h + 1, 0xDFE6E9);
    }

    // Ruled lines
    int line_gap = (h >= 24) ? 4 : 3;
    for (int ly = oy + spiral_h + 3; ly < oy + h - 3; ly += line_gap) {
        icon_fill_rect(buf, bw, bh, ox + 3, ly, w - 8, 1, 0x74B9FF);
    }

    // Yellow pencil resting at bottom right
    if (w >= 16) {
        int pen_x = ox + w - 6;
        int pen_y = oy + h - 10;
        icon_fill_rect(buf, bw, bh, pen_x, pen_y, 4, 8, 0xF1C40F);
        icon_fill_rect(buf, bw, bh, pen_x, pen_y, 4, 2, 0xE74C3C); // red eraser
        icon_fill_rect(buf, bw, bh, pen_x + 1, pen_y + 8, 2, 2, 0x2D3436); // tip
    }
}

// 4. File Explorer Icon
static inline void draw_explorer_icon(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    int pad = s / 10;
    if (pad < 1) pad = 1;
    int w = s - pad * 2;
    int h = s - pad * 2;
    int ox = x + pad;
    int oy = y + pad;

    // Folder tab
    int tab_w = w / 2;
    int tab_h = h / 4;
    icon_fill_rect(buf, bw, bh, ox, oy, tab_w, tab_h, 0xE67E22);

    // Back body
    icon_fill_rect(buf, bw, bh, ox, oy + tab_h - 1, w, h - tab_h + 1, 0xF39C12);
    icon_draw_rect(buf, bw, bh, ox, oy + tab_h - 1, w, h - tab_h + 1, 0xD35400);

    // Inner documents peeking out
    icon_fill_rect(buf, bw, bh, ox + 3, oy + 2, w - 8, tab_h + 2, 0xFFFFFF);
    icon_draw_rect(buf, bw, bh, ox + 3, oy + 2, w - 8, tab_h + 2, 0xBDC3C7);

    // Front folder flap (lighter orange with 3D slope)
    int flap_y = oy + tab_h + 3;
    int flap_h = h - tab_h - 3;
    icon_fill_rect(buf, bw, bh, ox, flap_y, w, flap_h, 0xF1C40F);
    icon_draw_rect(buf, bw, bh, ox, flap_y, w, flap_h, 0xD4AC0D);

    // Drive badge on flap
    if (w >= 16) {
        icon_fill_rect(buf, bw, bh, ox + 3, flap_y + 3, 6, 4, 0x34495E);
        icon_fill_rect(buf, bw, bh, ox + 4, flap_y + 4, 2, 2, 0x2ECC71); // green led
    }
}

// 5. Network Fetch Icon
static inline void draw_fetch_icon(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    int pad = s / 10;
    if (pad < 1) pad = 1;
    int w = s - pad * 2;
    int h = s - pad * 2;
    int ox = x + pad;
    int oy = y + pad;

    // Blue Globe
    icon_fill_rect(buf, bw, bh, ox, oy, w, h, 0x0984E3);
    icon_draw_rect(buf, bw, bh, ox, oy, w, h, 0x74B9FF);

    // Equator & meridian
    icon_fill_rect(buf, bw, bh, ox, oy + h / 2, w, 1, 0x81ECEC);
    icon_fill_rect(buf, bw, bh, ox + w / 2, oy, 1, h, 0x81ECEC);

    // Green Down-Arrow
    int ax = ox + w / 2;
    int ay = oy + 3;
    int arrow_h = h - 6;
    icon_fill_rect(buf, bw, bh, ax - 1, ay, 3, arrow_h - 3, 0x00FF9F);
    // Arrow head
    for (int i = 0; i < 4; i++) {
        icon_fill_rect(buf, bw, bh, ax - 3 + i, ay + arrow_h - 4 + i, 1, 1, 0x00FF9F);
        icon_fill_rect(buf, bw, bh, ax + 3 - i, ay + arrow_h - 4 + i, 1, 1, 0x00FF9F);
    }
}

// 6. Image Viewer Icon
static inline void draw_imageview_icon(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    int pad = s / 10;
    if (pad < 1) pad = 1;
    int w = s - pad * 2;
    int h = s - pad * 2;
    int ox = x + pad;
    int oy = y + pad;

    // White photo frame border
    icon_fill_rect(buf, bw, bh, ox, oy, w, h, 0xDFE6E9);
    icon_draw_rect(buf, bw, bh, ox, oy, w, h, 0xB2BEC3);

    // Inner picture canvas (Sky)
    int pw = w - 4;
    int ph = h - 4;
    icon_fill_rect(buf, bw, bh, ox + 2, oy + 2, pw, ph, 0x74B9FF);

    // Golden Sun
    int sun_sz = (w >= 24) ? 4 : 2;
    icon_fill_rect(buf, bw, bh, ox + pw - sun_sz - 1, oy + 4, sun_sz, sun_sz, 0xF1C40F);

    // Mountain peaks (green & violet)
    int m_base = oy + 2 + ph;
    int mw = pw / 2;
    for (int i = 0; i < mw; i++) {
        int m1_h = (i * ph) / mw;
        icon_fill_rect(buf, bw, bh, ox + 2 + i, m_base - m1_h, 1, m1_h, 0x6C5CE7);
        int m2_h = ((mw - i) * ph) / (mw + 1);
        icon_fill_rect(buf, bw, bh, ox + 2 + mw + i, m_base - m2_h, 1, m2_h, 0x00CEC9);
    }
}

// 7. WASI Test Icon (Flask)
static inline void draw_testwasi_icon(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    int pad = s / 10;
    if (pad < 1) pad = 1;
    int w = s - pad * 2;
    int h = s - pad * 2;
    int ox = x + pad;
    int oy = y + pad;

    // Dark background plate
    icon_fill_rect(buf, bw, bh, ox, oy, w, h, 0x2C3E50);
    icon_draw_rect(buf, bw, bh, ox, oy, w, h, 0x34495E);

    // Glass Flask neck
    int neck_w = w / 4;
    int neck_h = h / 3;
    icon_fill_rect(buf, bw, bh, ox + (w - neck_w) / 2, oy + 2, neck_w, neck_h, 0xECF0F1);

    // Flask triangular body
    int body_y = oy + 2 + neck_h;
    int body_h = h - neck_h - 4;
    for (int dy = 0; dy < body_h; dy++) {
        int span = neck_w + (dy * (w - 6 - neck_w)) / body_h;
        int sx = ox + (w - span) / 2;
        uint32_t col = (dy > body_h / 3) ? 0x9B59B6 : 0xBDC3C7;
        icon_fill_rect(buf, bw, bh, sx, body_y + dy, span, 1, col);
    }
}

// 8. Recycle Bin Icon
static inline void draw_recycle_bin_icon_s(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    int pad = s / 10;
    if (pad < 1) pad = 1;
    int w = s - pad * 2;
    int h = s - pad * 2;
    int ox = x + pad;
    int oy = y + pad;

    // Wastebasket rim
    icon_fill_rect(buf, bw, bh, ox + 1, oy + 1, w - 2, 3, 0x8FA4BA);
    icon_draw_rect(buf, bw, bh, ox + 1, oy + 1, w - 2, 3, 0x485A6E);

    // Body
    icon_fill_rect(buf, bw, bh, ox + 2, oy + 4, w - 4, h - 5, 0x4B6584);
    icon_draw_rect(buf, bw, bh, ox + 2, oy + 4, w - 4, h - 5, 0x2C3A47);

    // Green recycling emblem
    int gw = w / 2;
    int gh = h / 3;
    icon_fill_rect(buf, bw, bh, ox + (w - gw) / 2, oy + (h - gh) / 2 + 1, gw, gh, 0x20BF6B);
}

// 9. Text Document Icon (.TXT)
static inline void draw_text_doc_icon(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    int pad = s / 10;
    if (pad < 1) pad = 1;
    int w = s - pad * 2;
    int h = s - pad * 2;
    int ox = x + pad;
    int oy = y + pad;

    // Sheet of paper
    icon_fill_rect(buf, bw, bh, ox + 2, oy, w - 4, h, 0xFFFFFF);
    icon_draw_rect(buf, bw, bh, ox + 2, oy, w - 4, h, 0x95A5A6);

    // Folded corner
    int corner = (w >= 20) ? 6 : 4;
    icon_fill_rect(buf, bw, bh, ox + w - 2 - corner, oy, corner, corner, 0xBDC3C7);

    // Ruled lines
    for (int ly = oy + corner + 3; ly < oy + h - 3; ly += 3) {
        icon_fill_rect(buf, bw, bh, ox + 5, ly, w - 10, 1, 0x34495E);
    }
}

// 10. Image Document Icon (.BMP, .PNG, .JPG)
static inline void draw_image_doc_icon(uint32_t* buf, int bw, int bh, int x, int y, int s) {
    draw_imageview_icon(buf, bw, bh, x, y, s);
}

// Universal Icon Dispatcher
static inline void draw_icon_by_target(uint32_t* buf, int bw, int bh, int x, int y, int s, const char* name) {
    if (!name || name[0] == '\0') {
        draw_notepad_icon(buf, bw, bh, x, y, s);
        return;
    }

    if (strcmp(name, "CALC.WAS") == 0 || strcmp(name, "Calculator") == 0) {
        draw_calc_icon(buf, bw, bh, x, y, s);
    } else if (strcmp(name, "TERM.WAS") == 0 || strcmp(name, "Terminal") == 0 || strcmp(name, "WoOS Terminal") == 0) {
        draw_term_icon(buf, bw, bh, x, y, s);
    } else if (strcmp(name, "NOTEPAD.WAS") == 0 || strcmp(name, "Notepad") == 0) {
        draw_notepad_icon(buf, bw, bh, x, y, s);
    } else if (strcmp(name, "EXPLORER.WAS") == 0 || strcmp(name, "File Explorer") == 0 || strcmp(name, "Explorer") == 0) {
        draw_explorer_icon(buf, bw, bh, x, y, s);
    } else if (strcmp(name, "FETCH.WAS") == 0 || strcmp(name, "HTTP Fetch") == 0 || strcmp(name, "Fetch") == 0) {
        draw_fetch_icon(buf, bw, bh, x, y, s);
    } else if (strcmp(name, "IMGVIEW.WAS") == 0 || strcmp(name, "IMAGEVIEW.WAS") == 0 || strcmp(name, "Image Viewer") == 0) {
        draw_imageview_icon(buf, bw, bh, x, y, s);
    } else if (strcmp(name, "TESTWASI.WAS") == 0 || strcmp(name, "WASI Test") == 0) {
        draw_testwasi_icon(buf, bw, bh, x, y, s);
    } else if (strcmp(name, "Recycle Bin") == 0 || strcmp(name, "RECYCLE") == 0) {
        draw_recycle_bin_icon_s(buf, bw, bh, x, y, s);
    } else {
        int len = (int)strlen(name);
        if (len >= 4 && (strcmp(&name[len - 4], ".bmp") == 0 || strcmp(&name[len - 4], ".BMP") == 0 ||
                         strcmp(&name[len - 4], ".png") == 0 || strcmp(&name[len - 4], ".PNG") == 0 ||
                         strcmp(&name[len - 4], ".jpg") == 0 || strcmp(&name[len - 4], ".JPG") == 0)) {
            draw_image_doc_icon(buf, bw, bh, x, y, s);
        } else if (len >= 5 && (strcmp(&name[len - 5], ".jpeg") == 0 || strcmp(&name[len - 5], ".JPEG") == 0)) {
            draw_image_doc_icon(buf, bw, bh, x, y, s);
        } else if (len >= 4 && (strcmp(&name[len - 4], ".txt") == 0 || strcmp(&name[len - 4], ".TXT") == 0)) {
            draw_text_doc_icon(buf, bw, bh, x, y, s);
        } else {
            draw_explorer_icon(buf, bw, bh, x, y, s);
        }
    }
}

#endif
