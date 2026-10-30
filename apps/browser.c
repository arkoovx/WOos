#include "woos.h"
#include "icons.h"
#include "woos_dialog.h"

#define DEFAULT_WIN_W 760
#define DEFAULT_WIN_H 520
#define MAX_WIN_W 1280
#define MAX_WIN_H 1024

#define TOPBAR_H 34
#define BOOKMARKS_H 26
#define STATUSBAR_H 22
#define HEADER_H (TOPBAR_H + BOOKMARKS_H)

#define CHAR_W 8
#define LINE_H 15

#define COLOR_TOOLBAR_BG    0x2C3A47
#define COLOR_BOOKMARK_BG   0x1E272E
#define COLOR_BTN_BG        0x3B4A5A
#define COLOR_BTN_BORDER    0x576574
#define COLOR_BTN_HOVER     0x4B6584
#define COLOR_TEXT_WHITE    0xFFFFFF
#define COLOR_TEXT_MUTED    0x8395A7
#define COLOR_STATUS_BG     0x222F3E
#define COLOR_ADDR_BG       0xFFFFFF
#define COLOR_ADDR_TEXT     0x111111
#define COLOR_ADDR_BORDER   0x10AC84
#define COLOR_CONTENT_BG    0xFAFAFA
#define COLOR_TEXT_NORMAL   0x222222
#define COLOR_TEXT_HEADER   0x0C2461
#define COLOR_LINK          0x0984E3
#define COLOR_LINK_HOVER    0xD63031
#define COLOR_SCROLL_TRACK  0xDFE6E9
#define COLOR_SCROLL_THUMB  0xB2BEC3

#define MAX_HTML_SIZE 65536
#define MAX_LINES 1500
#define MAX_LINE_LEN 160
#define MAX_LINKS 160

typedef struct {
    char text[MAX_LINE_LEN];
    int len;
    uint32_t color;
    int indent;
    uint8_t is_header;
    uint8_t is_hr;
    uint8_t is_code;
} browser_line_t;

typedef struct {
    int line_idx;
    int col_start;
    int col_len;
    char url[128];
} browser_link_t;

static int g_win_idx = -1;
static int g_win_w = DEFAULT_WIN_W;
static int g_win_h = DEFAULT_WIN_H;
static uint32_t g_fb[MAX_WIN_W * MAX_WIN_H];

static char g_url[128] = "/WELCOME.HTM";
static int g_url_len = 12;
static int g_cursor_pos = 12;
static int g_scroll_x = 0;
static int g_url_focus = 0;

static char g_html[MAX_HTML_SIZE];
static uint32_t g_html_len = 0;
static char g_page_title[64] = "Web Browser";

static browser_line_t g_lines[MAX_LINES];
static int g_num_lines = 0;

static browser_link_t g_links[MAX_LINKS];
static int g_num_links = 0;
static int g_hovered_link = -1;

static int g_scroll_y = 0;
static char g_status_msg[128] = "Ready. Welcome to WoOS Web Browser (HTTPS supported).";

static void redraw(void);
static void navigate_to(const char* target_url, int add_history);
static void parse_html_to_lines(void);

// History Stack
#define MAX_HISTORY 32
static char g_history[MAX_HISTORY][128];
static int g_history_count = 0;
static int g_history_idx = -1;

static void set_status(const char* msg) {
    int i = 0;
    while (msg[i] != '\0' && i < 127) {
        g_status_msg[i] = msg[i];
        i++;
    }
    g_status_msg[i] = '\0';
}

static int is_downloadable(const char* url) {
    if (!url) return 0;
    int len = (int)strlen(url);
    const char* exts[] = { ".zip", ".ZIP", ".png", ".PNG", ".jpg", ".JPG", ".jpeg", ".JPEG",
                           ".bmp", ".BMP", ".wasm", ".WASM", ".bin", ".BIN", ".iso", ".ISO",
                           ".tar", ".TAR", ".gz", ".GZ", ".mp3", ".MP3" };
    for (int i = 0; i < 21; i++) {
        int elen = (int)strlen(exts[i]);
        if (len >= elen && strcmp(url + len - elen, exts[i]) == 0) return 1;
    }
    return 0;
}

static void resolve_url(const char* base_url, const char* href, char* out_url, int max_len) {
    if (!href || href[0] == '\0') {
        strncpy(out_url, base_url, max_len);
        return;
    }
    if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0 || strncmp(href, "file://", 7) == 0) {
        strncpy(out_url, href, max_len);
        return;
    }
    if (href[0] == '/') {
        int proto_len = 0;
        if (strncmp(base_url, "http://", 7) == 0) proto_len = 7;
        else if (strncmp(base_url, "https://", 8) == 0) proto_len = 8;
        if (proto_len > 0) {
            const char* slash = strchr(base_url + proto_len, '/');
            int host_end = slash ? (int)(slash - base_url) : (int)strlen(base_url);
            int copy_len = host_end < max_len - 1 ? host_end : max_len - 1;
            strncpy(out_url, base_url, copy_len);
            out_url[copy_len] = '\0';
            strncat(out_url, href, max_len - strlen(out_url) - 1);
            return;
        }
        strncpy(out_url, href, max_len);
        return;
    }
    int last_slash = -1;
    for (int i = 0; base_url[i] != '\0'; i++) {
        if (base_url[i] == '/') last_slash = i;
    }
    if (last_slash >= 0) {
        int copy_len = last_slash + 1 < max_len - 1 ? last_slash + 1 : max_len - 1;
        strncpy(out_url, base_url, copy_len);
        out_url[copy_len] = '\0';
        strncat(out_url, href, max_len - strlen(out_url) - 1);
    } else {
        strncpy(out_url, href, max_len);
    }
}

static void add_line(int indent, uint32_t color, uint8_t is_hdr, uint8_t is_hr, uint8_t is_code) {
    if (g_num_lines >= MAX_LINES) return;
    browser_line_t* l = &g_lines[g_num_lines++];
    l->text[0] = '\0';
    l->len = 0;
    l->indent = indent;
    l->color = color;
    l->is_header = is_hdr;
    l->is_hr = is_hr;
    l->is_code = is_code;
}

static void append_word(const char* word, int wlen, int max_cols, int in_link, const char* link_url) {
    if (g_num_lines == 0) {
        add_line(0, COLOR_TEXT_NORMAL, 0, 0, 0);
    }
    browser_line_t* cur = &g_lines[g_num_lines - 1];
    int avail = max_cols - cur->indent;

    if (cur->len > 0 && cur->len + 1 + wlen > avail) {
        uint32_t col = cur->color;
        int ind = cur->indent;
        uint8_t hdr = cur->is_header;
        uint8_t cod = cur->is_code;
        add_line(ind, col, hdr, 0, cod);
        cur = &g_lines[g_num_lines - 1];
    }

    if (cur->len > 0 && cur->len + 1 < MAX_LINE_LEN) {
        cur->text[cur->len++] = ' ';
        cur->text[cur->len] = '\0';
    }

    int col_start = cur->len;
    for (int i = 0; i < wlen && cur->len + 1 < MAX_LINE_LEN; i++) {
        cur->text[cur->len++] = word[i];
    }
    cur->text[cur->len] = '\0';

    if (in_link && g_num_links < MAX_LINKS && link_url[0] != '\0') {
        browser_link_t* bl = &g_links[g_num_links++];
        bl->line_idx = g_num_lines - 1;
        bl->col_start = col_start;
        bl->col_len = cur->len - col_start;
        resolve_url(g_url, link_url, bl->url, sizeof(bl->url));
    }
}

static void parse_html_to_lines(void) {
    g_num_lines = 0;
    g_num_links = 0;
    g_hovered_link = -1;

    int max_cols = (g_win_w - 40) / CHAR_W;
    if (max_cols < 20) max_cols = 20;

    const char* p = g_html;
    int in_tag = 0;
    int in_script = 0;
    int in_style = 0;
    int in_title = 0;
    int in_link = 0;
    char link_url[128] = "";
    int indent = 0;
    uint32_t cur_col = COLOR_TEXT_NORMAL;
    uint8_t cur_hdr = 0;

    char tag_buf[64];
    int tag_len = 0;

    char word_buf[64];
    int word_len = 0;

    char title_buf[64];
    int title_len = 0;

    while (*p != '\0') {
        if (*p == '<') {
            if (word_len > 0) {
                append_word(word_buf, word_len, max_cols, in_link, link_url);
                word_len = 0;
            }
            in_tag = 1;
            tag_len = 0;
            p++;
            continue;
        }

        if (in_tag) {
            if (*p == '>') {
                in_tag = 0;
                tag_buf[tag_len] = '\0';

                // Check tag
                if (strncmp(tag_buf, "script", 6) == 0 || strncmp(tag_buf, "SCRIPT", 6) == 0) in_script = 1;
                else if (strncmp(tag_buf, "/script", 7) == 0 || strncmp(tag_buf, "/SCRIPT", 7) == 0) in_script = 0;
                else if (strncmp(tag_buf, "style", 5) == 0 || strncmp(tag_buf, "STYLE", 5) == 0) in_style = 1;
                else if (strncmp(tag_buf, "/style", 6) == 0 || strncmp(tag_buf, "/STYLE", 6) == 0) in_style = 0;
                else if (strncmp(tag_buf, "title", 5) == 0 || strncmp(tag_buf, "TITLE", 5) == 0) { in_title = 1; title_len = 0; }
                else if (strncmp(tag_buf, "/title", 6) == 0 || strncmp(tag_buf, "/TITLE", 6) == 0) {
                    in_title = 0;
                    title_buf[title_len] = '\0';
                    strncpy(g_page_title, title_buf, sizeof(g_page_title) - 1);
                    if (g_win_idx >= 0) woos_graphics_set_window_title(g_win_idx, g_page_title);
                }
                else if (tag_buf[0] == 'h' && tag_buf[1] >= '1' && tag_buf[1] <= '6') {
                    if (g_num_lines > 0 && g_lines[g_num_lines - 1].len > 0) add_line(indent, cur_col, 0, 0, 0);
                    cur_col = COLOR_TEXT_HEADER;
                    cur_hdr = 1;
                    add_line(indent, cur_col, 1, 0, 0);
                }
                else if (tag_buf[0] == '/' && tag_buf[1] == 'h' && tag_buf[2] >= '1' && tag_buf[2] <= '6') {
                    cur_col = COLOR_TEXT_NORMAL;
                    cur_hdr = 0;
                    add_line(indent, cur_col, 0, 0, 0);
                }
                else if (strncmp(tag_buf, "p", 1) == 0 || strncmp(tag_buf, "div", 3) == 0 ||
                         strncmp(tag_buf, "section", 7) == 0 || strncmp(tag_buf, "article", 7) == 0) {
                    if (g_num_lines > 0 && g_lines[g_num_lines - 1].len > 0) add_line(indent, cur_col, 0, 0, 0);
                }
                else if (strncmp(tag_buf, "br", 2) == 0 || strncmp(tag_buf, "BR", 2) == 0) {
                    add_line(indent, cur_col, cur_hdr, 0, 0);
                }
                else if (strncmp(tag_buf, "hr", 2) == 0 || strncmp(tag_buf, "HR", 2) == 0) {
                    if (g_num_lines > 0 && g_lines[g_num_lines - 1].len > 0) add_line(indent, cur_col, 0, 0, 0);
                    add_line(indent, 0xBDC3C7, 0, 1, 0);
                    add_line(indent, cur_col, 0, 0, 0);
                }
                else if (strncmp(tag_buf, "ul", 2) == 0 || strncmp(tag_buf, "ol", 2) == 0) {
                    indent += 2;
                }
                else if (strncmp(tag_buf, "/ul", 3) == 0 || strncmp(tag_buf, "/ol", 3) == 0) {
                    if (indent >= 2) indent -= 2;
                }
                else if (strncmp(tag_buf, "li", 2) == 0) {
                    add_line(indent, cur_col, cur_hdr, 0, 0);
                    append_word("•", 1, max_cols, 0, "");
                }
                else if (tag_buf[0] == 'a' && (tag_buf[1] == ' ' || tag_buf[1] == '\t')) {
                    in_link = 1;
                    link_url[0] = '\0';
                    const char* hpos = strstr(tag_buf, "href=");
                    if (hpos) {
                        hpos += 5;
                        char quote = 0;
                        if (*hpos == '"' || *hpos == '\'') quote = *hpos++;
                        int uidx = 0;
                        while (*hpos != '\0' && ((quote && *hpos != quote) || (!quote && *hpos != ' ' && *hpos != '>')) && uidx < 127) {
                            link_url[uidx++] = *hpos++;
                        }
                        link_url[uidx] = '\0';
                    }
                }
                else if (strncmp(tag_buf, "/a", 2) == 0) {
                    in_link = 0;
                    link_url[0] = '\0';
                }
            } else {
                if (tag_len + 1 < (int)sizeof(tag_buf)) tag_buf[tag_len++] = *p;
            }
            p++;
            continue;
        }

        if (in_script || in_style) {
            p++;
            continue;
        }

        if (in_title) {
            if (title_len + 1 < (int)sizeof(title_buf)) title_buf[title_len++] = *p;
            p++;
            continue;
        }

        // Entity decoding
        if (*p == '&') {
            if (strncmp(p, "&nbsp;", 6) == 0) { p += 6; if (word_len + 1 < 64) word_buf[word_len++] = ' '; continue; }
            if (strncmp(p, "&amp;", 5) == 0) { p += 5; if (word_len + 1 < 64) word_buf[word_len++] = '&'; continue; }
            if (strncmp(p, "&lt;", 4) == 0) { p += 4; if (word_len + 1 < 64) word_buf[word_len++] = '<'; continue; }
            if (strncmp(p, "&gt;", 4) == 0) { p += 4; if (word_len + 1 < 64) word_buf[word_len++] = '>'; continue; }
            if (strncmp(p, "&quot;", 6) == 0) { p += 6; if (word_len + 1 < 64) word_buf[word_len++] = '"'; continue; }
            if (strncmp(p, "&#39;", 5) == 0) { p += 5; if (word_len + 1 < 64) word_buf[word_len++] = '\''; continue; }
        }

        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (word_len > 0) {
                append_word(word_buf, word_len, max_cols, in_link, link_url);
                word_len = 0;
            }
        } else {
            if (word_len + 1 < 64) {
                word_buf[word_len++] = *p;
            }
        }
        p++;
    }

    if (word_len > 0) {
        append_word(word_buf, word_len, max_cols, in_link, link_url);
    }

    if (g_num_lines == 0) {
        add_line(0, COLOR_TEXT_MUTED, 0, 0, 0);
        append_word("Empty page", 10, max_cols, 0, "");
    }
}

static void navigate_to(const char* target_url, int add_history);

static void do_download(const char* url) {
    if (!url || url[0] == '\0') return;

    const char* last_slash = strrchr(url, '/');
    const char* fname = last_slash ? (last_slash + 1) : url;
    if (fname[0] == '\0' || strchr(fname, '?')) {
        fname = "DOWNLOAD.BIN";
    }

    char dest_path[64] = "/DESKTOP/";
    strncat(dest_path, fname, sizeof(dest_path) - strlen(dest_path) - 1);

    char stat[128] = "Downloading ";
    strcat(stat, fname);
    strcat(stat, " to /DESKTOP/...");
    set_status(stat);
    if (g_win_idx >= 0) { redraw(); woos_blit_window(g_win_idx, g_fb, g_win_w, g_win_h); }

    int32_t bytes = woos_net_download(url, dest_path);
    if (bytes >= 0) {
        char msg[128] = "Downloaded ";
        strcat(msg, fname);
        strcat(msg, " (");
        char nbuf[16];
        int_to_str(bytes, nbuf);
        strcat(msg, nbuf);
        strcat(msg, " bytes) to /DESKTOP/");
        set_status(msg);
    } else {
        set_status("Download failed: connection error or write failure.");
    }
}

static void navigate_to(const char* target_url, int add_history) {
    if (!target_url || target_url[0] == '\0') return;

    if (is_downloadable(target_url)) {
        do_download(target_url);
        return;
    }

    strncpy(g_url, target_url, sizeof(g_url) - 1);
    g_url[sizeof(g_url) - 1] = '\0';
    g_url_len = (int)strlen(g_url);
    g_cursor_pos = g_url_len;
    g_scroll_x = 0;
    g_scroll_y = 0;

    if (add_history) {
        if (g_history_idx < MAX_HISTORY - 1) {
            g_history_idx++;
            strncpy(g_history[g_history_idx], g_url, 127);
            g_history_count = g_history_idx + 1;
        }
    }

    // Local file loading
    if (g_url[0] == '/' || strncmp(g_url, "file://", 7) == 0) {
        const char* fpath = g_url;
        if (strncmp(g_url, "file://", 7) == 0) fpath = g_url + 7;
        int32_t fd = woos_vfs_open(fpath, 1);
        if (fd >= 0) {
            uint32_t bytes = woos_vfs_read(fd, g_html, sizeof(g_html) - 1);
            woos_vfs_close(fd);
            g_html[bytes] = '\0';
            g_html_len = bytes;
            parse_html_to_lines();
            char sbuf[64] = "Loaded local file: ";
            strcat(sbuf, fpath);
            set_status(sbuf);
            return;
        }
    }

    // Network loading (HTTP or HTTPS)
    char stat[128] = "Connecting to ";
    strncat(stat, g_url, 60);
    strcat(stat, "...");
    set_status(stat);
    if (g_win_idx >= 0) { redraw(); woos_blit_window(g_win_idx, g_fb, g_win_w, g_win_h); }

    int32_t res = woos_net_fetch(g_url, g_html, sizeof(g_html) - 1);

    if (res < 0) {
        g_html[0] = '\0';
        g_html_len = 0;
        parse_html_to_lines();
        set_status("Failed to connect or fetch webpage. Check network/URL.");
    } else {
        g_html_len = (uint32_t)res;
        g_html[g_html_len] = '\0';
        parse_html_to_lines();

        char sbuf[128] = "Loaded ";
        char nbuf[16];
        int_to_str(res, nbuf);
        strcat(sbuf, nbuf);
        strcat(sbuf, " bytes");
        set_status(sbuf);
    }
}

static void draw_btn(int x, int y, int w, int h, const char* label, int hover, uint32_t bg_col) {
    fill_rect(g_fb, g_win_w, g_win_h, x, y, w, h, hover ? COLOR_BTN_HOVER : bg_col);
    draw_rect(g_fb, g_win_w, g_win_h, x, y, w, h, COLOR_BTN_BORDER);
    int tx = x + (w - (int)strlen(label) * CHAR_W) / 2;
    int ty = y + (h - 10) / 2;
    woos_draw_text(g_fb, g_win_w, g_win_h, tx, ty, label, COLOR_TEXT_WHITE);
}

static void redraw(void) {
    fill_rect(g_fb, g_win_w, g_win_h, 0, 0, g_win_w, g_win_h, COLOR_CONTENT_BG);

    // 1. Navigation Topbar (y = 0..34)
    fill_rect(g_fb, g_win_w, g_win_h, 0, 0, g_win_w, TOPBAR_H, COLOR_TOOLBAR_BG);

    // Navigation buttons: Back, Forward, Reload, Home
    draw_btn(8, 5, 26, 24, "<", 0, COLOR_BTN_BG);
    draw_btn(38, 5, 26, 24, ">", 0, COLOR_BTN_BG);
    draw_btn(68, 5, 26, 24, "R", 0, COLOR_BTN_BG);
    draw_btn(98, 5, 26, 24, "H", 0, COLOR_BTN_BG);

    // Address Bar
    int addr_x = 132;
    int addr_w = g_win_w - 132 - 140;
    if (addr_w < 100) addr_w = 100;
    fill_rect(g_fb, g_win_w, g_win_h, addr_x, 5, addr_w, 24, COLOR_ADDR_BG);
    draw_rect(g_fb, g_win_w, g_win_h, addr_x, 5, addr_w, 24, g_url_focus ? COLOR_ADDR_BORDER : 0x718093);

    // Text in Address Bar
    int text_avail = (addr_w - 12) / CHAR_W;
    if (g_cursor_pos < g_scroll_x) g_scroll_x = g_cursor_pos;
    if (g_cursor_pos > g_scroll_x + text_avail) g_scroll_x = g_cursor_pos - text_avail;
    if (g_scroll_x < 0) g_scroll_x = 0;

    char disp_url[128];
    int di = 0;
    for (int i = g_scroll_x; i < g_url_len && di < text_avail && di < 127; i++) {
        disp_url[di++] = g_url[i];
    }
    disp_url[di] = '\0';
    woos_draw_text(g_fb, g_win_w, g_win_h, addr_x + 6, 10, disp_url, COLOR_ADDR_TEXT);

    // Cursor in Address Bar
    if (g_url_focus) {
        int cx = addr_x + 6 + (g_cursor_pos - g_scroll_x) * CHAR_W;
        if (cx >= addr_x + 4 && cx < addr_x + addr_w - 4) {
            fill_rect(g_fb, g_win_w, g_win_h, cx, 8, 2, 18, 0x111111);
        }
    }

    // Go button & Download button
    int btn_go_x = addr_x + addr_w + 6;
    draw_btn(btn_go_x, 5, 38, 24, "Go", 0, 0x10AC84);
    int btn_dl_x = btn_go_x + 44;
    draw_btn(btn_dl_x, 5, 80, 24, "Download", 0, 0x0984E3);

    // 2. Bookmarks / Search Bar (y = 34..60)
    fill_rect(g_fb, g_win_w, g_win_h, 0, TOPBAR_H, g_win_w, BOOKMARKS_H, COLOR_BOOKMARK_BG);
    draw_rect(g_fb, g_win_w, g_win_h, 0, TOPBAR_H + BOOKMARKS_H - 1, g_win_w, 1, 0x353B48);

    draw_btn(8, TOPBAR_H + 3, 64, 20, "Google", 0, 0x2F3542);
    draw_btn(76, TOPBAR_H + 3, 72, 20, "FrogFind", 0, 0x2F3542);
    draw_btn(152, TOPBAR_H + 3, 54, 20, "Wiby", 0, 0x2F3542);
    draw_btn(210, TOPBAR_H + 3, 64, 20, "Welcome", 0, 0x2F3542);

    // 3. Render HTML Content Viewport (y = HEADER_H .. win_h - STATUSBAR_H)
    int view_y = HEADER_H;
    int view_h = g_win_h - HEADER_H - STATUSBAR_H;
    int vis_lines = view_h / LINE_H;

    for (int i = 0; i < vis_lines; i++) {
        int lidx = g_scroll_y + i;
        if (lidx >= g_num_lines) break;

        browser_line_t* bl = &g_lines[lidx];
        int py = view_y + i * LINE_H + 4;
        int px = 16 + bl->indent * CHAR_W;

        if (bl->is_hr) {
            fill_rect(g_fb, g_win_w, g_win_h, 16, py + 6, g_win_w - 40, 1, 0xDCDDE1);
            continue;
        }

        // Draw line text
        woos_draw_text(g_fb, g_win_w, g_win_h, px, py, bl->text, bl->color);

        // Underline and highlight any links on this line
        for (int k = 0; k < g_num_links; k++) {
            if (g_links[k].line_idx == lidx) {
                int lx = px + g_links[k].col_start * CHAR_W;
                int lw = g_links[k].col_len * CHAR_W;
                uint32_t lcol = (k == g_hovered_link) ? COLOR_LINK_HOVER : COLOR_LINK;

                // Redraw link text with vibrant link color
                char lstr[64];
                int slen = g_links[k].col_len;
                if (slen > 63) slen = 63;
                for (int c = 0; c < slen; c++) {
                    lstr[c] = bl->text[g_links[k].col_start + c];
                }
                lstr[slen] = '\0';
                woos_draw_text(g_fb, g_win_w, g_win_h, lx, py, lstr, lcol);
                // Underline
                fill_rect(g_fb, g_win_w, g_win_h, lx, py + LINE_H - 3, lw, 1, lcol);
            }
        }
    }

    // Vertical Scrollbar
    if (g_num_lines > vis_lines) {
        int sb_x = g_win_w - 14;
        fill_rect(g_fb, g_win_w, g_win_h, sb_x, view_y, 14, view_h, COLOR_SCROLL_TRACK);
        int thumb_h = (view_h * vis_lines) / g_num_lines;
        if (thumb_h < 20) thumb_h = 20;
        int max_s = g_num_lines - vis_lines;
        int thumb_y = view_y + (g_scroll_y * (view_h - thumb_h)) / (max_s > 0 ? max_s : 1);
        fill_rect(g_fb, g_win_w, g_win_h, sb_x + 1, thumb_y, 12, thumb_h, COLOR_SCROLL_THUMB);
    }

    // 4. Status Bar (y = win_h - STATUSBAR_H .. win_h)
    int sb_y = g_win_h - STATUSBAR_H;
    fill_rect(g_fb, g_win_w, g_win_h, 0, sb_y, g_win_w, STATUSBAR_H, COLOR_STATUS_BG);
    draw_rect(g_fb, g_win_w, g_win_h, 0, sb_y, g_win_w, 1, 0x353B48);

    if (g_hovered_link >= 0 && g_hovered_link < g_num_links) {
        char link_info[140] = "Link: ";
        strncat(link_info, g_links[g_hovered_link].url, 130);
        woos_draw_text(g_fb, g_win_w, g_win_h, 10, sb_y + 4, link_info, 0x74B9FF);
    } else {
        woos_draw_text(g_fb, g_win_w, g_win_h, 10, sb_y + 4, g_status_msg, COLOR_TEXT_MUTED);
    }
}

static void handle_click(int x, int y) {
    // 1. Navigation Topbar
    if (y < TOPBAR_H) {
        // [ < ] Back
        if (x >= 8 && x <= 34) {
            if (g_history_idx > 0) {
                g_history_idx--;
                navigate_to(g_history[g_history_idx], 0);
            }
            return;
        }
        // [ > ] Forward
        if (x >= 38 && x <= 64) {
            if (g_history_idx < g_history_count - 1) {
                g_history_idx++;
                navigate_to(g_history[g_history_idx], 0);
            }
            return;
        }
        // [ R ] Reload
        if (x >= 68 && x <= 94) {
            navigate_to(g_url, 0);
            return;
        }
        // [ H ] Home
        if (x >= 98 && x <= 124) {
            navigate_to("/WELCOME.HTM", 1);
            return;
        }

        // Address Bar
        int addr_x = 132;
        int addr_w = g_win_w - 132 - 140;
        if (addr_w < 100) addr_w = 100;
        if (x >= addr_x && x <= addr_x + addr_w && y >= 5 && y <= 29) {
            g_url_focus = 1;
            int col = (x - (addr_x + 6) + CHAR_W / 2) / CHAR_W;
            g_cursor_pos = g_scroll_x + col;
            if (g_cursor_pos < 0) g_cursor_pos = 0;
            if (g_cursor_pos > g_url_len) g_cursor_pos = g_url_len;
            return;
        }

        // [ Go ]
        int btn_go_x = addr_x + addr_w + 6;
        if (x >= btn_go_x && x <= btn_go_x + 38 && y >= 5 && y <= 29) {
            g_url_focus = 0;
            navigate_to(g_url, 1);
            return;
        }

        // [ Download ]
        int btn_dl_x = btn_go_x + 44;
        if (x >= btn_dl_x && x <= btn_dl_x + 80 && y >= 5 && y <= 29) {
            g_url_focus = 0;
            do_download(g_url);
            return;
        }
    }

    // 2. Bookmarks Bar
    if (y >= TOPBAR_H && y < HEADER_H) {
        g_url_focus = 0;
        if (x >= 8 && x <= 72) { navigate_to("https://www.google.com", 1); return; }
        if (x >= 76 && x <= 148) { navigate_to("http://frogfind.com", 1); return; }
        if (x >= 152 && x <= 206) { navigate_to("https://wiby.me", 1); return; }
        if (x >= 210 && x <= 274) { navigate_to("/WELCOME.HTM", 1); return; }
    }

    // 3. Content Viewport
    if (y >= HEADER_H && y < g_win_h - STATUSBAR_H) {
        g_url_focus = 0;
        int view_y = HEADER_H;
        int lidx = (y - view_y) / LINE_H + g_scroll_y;
        if (lidx >= 0 && lidx < g_num_lines) {
            int px = 16 + g_lines[lidx].indent * CHAR_W;
            int col = (x - px) / CHAR_W;
            for (int k = 0; k < g_num_links; k++) {
                if (g_links[k].line_idx == lidx && col >= g_links[k].col_start && col < g_links[k].col_start + g_links[k].col_len) {
                    navigate_to(g_links[k].url, 1);
                    return;
                }
            }
        }
    }
}

static void handle_mouse_move(int x, int y) {
    if (y >= HEADER_H && y < g_win_h - STATUSBAR_H) {
        int view_y = HEADER_H;
        int lidx = (y - view_y) / LINE_H + g_scroll_y;
        int found = -1;
        if (lidx >= 0 && lidx < g_num_lines) {
            int px = 16 + g_lines[lidx].indent * CHAR_W;
            int col = (x - px) / CHAR_W;
            for (int k = 0; k < g_num_links; k++) {
                if (g_links[k].line_idx == lidx && col >= g_links[k].col_start && col < g_links[k].col_start + g_links[k].col_len) {
                    found = k;
                    break;
                }
            }
        }
        g_hovered_link = found;
    } else {
        g_hovered_link = -1;
    }
}

static void handle_key(int ascii, int scancode) {
    if (g_url_focus) {
        // Enter
        if (ascii == 10 || ascii == 13 || scancode == 0x1C) {
            g_url_focus = 0;
            navigate_to(g_url, 1);
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

        // Printable char
        if (ascii >= 32 && ascii <= 126 && g_url_len < 126) {
            for (int i = g_url_len; i >= g_cursor_pos; i--) {
                g_url[i + 1] = g_url[i];
            }
            g_url[g_cursor_pos] = (char)ascii;
            g_url_len++;
            g_cursor_pos++;
            return;
        }
    } else {
        // Viewport scroll keys
        int view_h = g_win_h - HEADER_H - STATUSBAR_H;
        int vis = view_h / LINE_H;
        int max_s = g_num_lines > vis ? (g_num_lines - vis) : 0;

        // Up arrow
        if (scancode == 0x48) {
            if (g_scroll_y > 0) g_scroll_y -= 2;
            if (g_scroll_y < 0) g_scroll_y = 0;
            return;
        }
        // Down arrow
        if (scancode == 0x50) {
            if (g_scroll_y < max_s) g_scroll_y += 2;
            if (g_scroll_y > max_s) g_scroll_y = max_s;
            return;
        }
        // Page Up
        if (scancode == 0x49) {
            g_scroll_y -= (vis - 2);
            if (g_scroll_y < 0) g_scroll_y = 0;
            return;
        }
        // Page Down
        if (scancode == 0x51) {
            g_scroll_y += (vis - 2);
            if (g_scroll_y > max_s) g_scroll_y = max_s;
            return;
        }
    }
}

__attribute__((export_name("_start")))
void _start(void) {
    g_win_idx = woos_create_window(g_win_w, g_win_h, "Web Browser", 0);
    if (g_win_idx < 0) return;

    // Check if system opened a file
    char open_file[64];
    if (woos_system_get_open_file(open_file, sizeof(open_file)) == 0 && open_file[0] != '\0') {
        navigate_to(open_file, 1);
    } else {
        navigate_to("/WELCOME.HTM", 1);
    }

    redraw();
    woos_blit_window(g_win_idx, g_fb, g_win_w, g_win_h);

    woos_msg_t msg;

    while (1) {
        int need_redraw = 0;

        while (woos_ipc_recv(&msg) > 0) {
            if (msg.type == MSG_CLOSE) {
                woos_destroy_window(g_win_idx);
                return;
            } else if (msg.type == MSG_RESIZE) {
                int new_w = (int)msg.arg1;
                int new_h = (int)msg.arg2;
                if (new_w > 0 && new_h > 0) {
                    if (new_w > MAX_WIN_W) new_w = MAX_WIN_W;
                    if (new_h > MAX_WIN_H) new_h = MAX_WIN_H;
                    g_win_w = new_w;
                    g_win_h = new_h;
                    parse_html_to_lines();
                    need_redraw = 1;
                }
            } else if (msg.type == MSG_CLICK) {
                handle_click((int)msg.arg1, (int)msg.arg2);
                need_redraw = 1;
            } else if (msg.type == MSG_KEY_DOWN) {
                handle_key((int)msg.arg1, (int)msg.arg2);
                need_redraw = 1;
            } else if (msg.type == MSG_OPEN_FILE) {
                char path[64];
                if (woos_system_get_open_file(path, sizeof(path)) == 0 && path[0] != '\0') {
                    navigate_to(path, 1);
                    need_redraw = 1;
                }
            }
        }

        if (need_redraw) {
            redraw();
            woos_blit_window(g_win_idx, g_fb, g_win_w, g_win_h);
        }

        woos_ipc_wait_message();
    }
}
