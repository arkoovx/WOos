#include "woos.h"
#include "woos_mem.h"
#include "image.h"
#include "icons.h"
#include "woos_dialog.h"

// NetSurf subsystems
#include "netsurf/plotters.h"
#include "netsurf/plot_style.h"
#include "netsurf/types.h"
#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_event.h"
#include "nsfb.h"
typedef struct nsfb_bbox_s bbox_t;
struct fbtk_bitmap;
#include "framebuffer.h"
#include "fbtk.h"
#include "render/box.h"
#include "svgtiny.h"

// NetSurf Layout and Redraw declarations
int layout_document_tree(struct box *root, int viewport_width);
void redraw_box_tree(struct box *b, int parent_x, int parent_y, int scroll_y, int viewport_h, const struct redraw_context *ctx);

static struct box *g_doc_root = NULL;
static nsfb_t *g_nsfb = NULL;
static fbtk_widget_t *g_fbtk = NULL;

#define DEFAULT_WIN_W 760
#define DEFAULT_WIN_H 520
#define MAX_WIN_W 1280
#define MAX_WIN_H 1024

#define TOPBAR_H 34
#define BOOKMARKS_H 26
#define STATUSBAR_H 22
#define HEADER_H (TOPBAR_H + BOOKMARKS_H)

#define CHAR_W 8
#define LINE_H 18

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
#define COLOR_CODE_BG       0xEEF2F7
#define COLOR_INPUT_BG      0xFFFFFF
#define COLOR_INPUT_BORDER  0xCBD5E1
#define COLOR_INPUT_FOCUS   0x10AC84
#define COLOR_SCROLL_TRACK  0xDFE6E9
#define COLOR_SCROLL_THUMB  0xB2BEC3

#define MAX_HTML_SIZE 65536
#define MAX_BOXES 2048
#define MAX_LINKS 160
#define MAX_IMAGES 16
#define MAX_INPUTS 8
#define MAX_HISTORY 32

typedef enum {
    ITEM_TEXT,
    ITEM_HEADER,
    ITEM_HR,
    ITEM_IMAGE,
    ITEM_INPUT,
    ITEM_BUTTON
} layout_item_type_t;

typedef struct {
    layout_item_type_t type;
    int x, y, w, h;
    char text[160];
    int text_len;
    uint32_t color;
    uint32_t bg_color;
    uint8_t is_bold;
    uint8_t is_code;
    uint8_t is_header;
    int link_idx;
    int elem_idx;
} layout_box_t;

typedef struct {
    char url[160];
} browser_link_t;

typedef struct {
    char url[160];
    woos_image_t* img;
    int disp_w, disp_h;
    int loaded; // 0=none, 1=loaded, -1=failed
} browser_image_t;

typedef struct {
    char name[32];
    char value[128];
    char placeholder[64];
    int cursor_pos;
    int scroll_x;
    int is_focused;
    char form_action[160];
    char form_method[8];
} browser_input_t;

static int g_win_idx = -1;
static int g_win_w = DEFAULT_WIN_W;
static int g_win_h = DEFAULT_WIN_H;
static uint32_t g_fb[MAX_WIN_W * MAX_WIN_H];

static char g_url[160] = "/WELCOME.HTM";
static int g_url_len = 12;
static int g_cursor_pos = 12;
static int g_scroll_x = 0;
static int g_url_focus = 0;

static char g_html[MAX_HTML_SIZE];
static uint32_t g_html_len = 0;
static char g_page_title[64] = "NetSurf Browser";

static layout_box_t g_boxes[MAX_BOXES];
static int g_num_boxes = 0;
static int g_doc_height = 0;

static browser_link_t g_links[MAX_LINKS];
static int g_num_links = 0;
static int g_hovered_link = -1;

static browser_image_t g_images[MAX_IMAGES];
static int g_num_images = 0;

static browser_input_t g_inputs[MAX_INPUTS];
static int g_num_inputs = 0;
static int g_focused_input = -1;

static int g_scroll_y = 0;
static char g_status_msg[128] = "Ready. NetSurf layout engine (HTML5, CSS, Forms, Images).";

static woos_file_dialog_t g_dialog;

static char g_history[MAX_HISTORY][160];
static int g_history_count = 0;
static int g_history_idx = -1;

static void redraw(void);
static void navigate_to(const char* target_url, int add_history);
static void parse_html_to_boxes(int fetch_images);

static void set_status(const char* msg) {
    int i = 0;
    while (msg[i] != '\0' && i < 127) {
        g_status_msg[i] = msg[i];
        i++;
    }
    g_status_msg[i] = '\0';
}

static void free_page_images(void) {
    for (int i = 0; i < g_num_images; i++) {
        if (g_images[i].img) {
            image_free(g_images[i].img);
            g_images[i].img = 0;
        }
        g_images[i].loaded = 0;
    }
    g_num_images = 0;
}

static int is_downloadable(const char* url) {
    if (!url) return 0;
    int len = (int)strlen(url);
    const char* exts[] = { ".zip", ".ZIP", ".wasm", ".WASM", ".bin", ".BIN",
                           ".iso", ".ISO", ".tar", ".TAR", ".gz", ".GZ", ".mp3", ".MP3" };
    for (int i = 0; i < 14; i++) {
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
            strncat(out_url, href, max_len - (int)strlen(out_url) - 1);
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
        strncat(out_url, href, max_len - (int)strlen(out_url) - 1);
    } else {
        strncpy(out_url, href, max_len);
    }
}

static void url_encode(const char* src, char* dst, int max_len) {
    if (!src || !dst || max_len < 4) return;
    int di = 0;
    for (int si = 0; src[si] != '\0' && di + 4 < max_len; si++) {
        char c = src[si];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = c;
        } else if (c == ' ') {
            dst[di++] = '+';
        } else {
            const char hex[] = "0123456789ABCDEF";
            dst[di++] = '%';
            dst[di++] = hex[(c >> 4) & 0x0F];
            dst[di++] = hex[c & 0x0F];
        }
    }
    dst[di] = '\0';
}

static void submit_form(int input_idx) {
    if (input_idx < 0 || input_idx >= g_num_inputs) return;
    browser_input_t* inp = &g_inputs[input_idx];

    char enc_val[256];
    url_encode(inp->value, enc_val, sizeof(enc_val));

    char target_url[256];
    target_url[0] = '\0';

    if (inp->form_action[0] == '\0') {
        // Fallback: search on DuckDuckGo
        strcpy(target_url, "https://html.duckduckgo.com/html/?q=");
        strncat(target_url, enc_val, sizeof(target_url) - (int)strlen(target_url) - 1);
    } else {
        resolve_url(g_url, inp->form_action, target_url, sizeof(target_url));
        if (inp->value[0] != '\0') {
            int has_query = (strchr(target_url, '?') != 0);
            strncat(target_url, has_query ? "&" : "?", sizeof(target_url) - (int)strlen(target_url) - 1);
            if (inp->name[0] != '\0') {
                strncat(target_url, inp->name, sizeof(target_url) - (int)strlen(target_url) - 1);
            } else {
                strncat(target_url, "q", sizeof(target_url) - (int)strlen(target_url) - 1);
            }
            strncat(target_url, "=", sizeof(target_url) - (int)strlen(target_url) - 1);
            strncat(target_url, enc_val, sizeof(target_url) - (int)strlen(target_url) - 1);

            // If searching Google, request pure HTML basic view (&gbv=1)
            if (strstr(target_url, "google.com") && !strstr(target_url, "gbv=")) {
                strncat(target_url, "&gbv=1", sizeof(target_url) - (int)strlen(target_url) - 1);
            }
        }
    }

    navigate_to(target_url, 1);
}

static void do_download(const char* url) {
    if (!url || url[0] == '\0') return;

    const char* last_slash = strrchr(url, '/');
    const char* fname = last_slash ? (last_slash + 1) : url;
    if (fname[0] == '\0' || strchr(fname, '?')) {
        fname = "DOWNLOAD.BIN";
    }

    char dest_path[64] = "/DESKTOP/";
    strncat(dest_path, fname, sizeof(dest_path) - (int)strlen(dest_path) - 1);

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

// Temporary buffer for image fetching
static uint8_t g_img_fetch_buf[524288];

static void load_image_for_slot(int slot, const char* img_url) {
    if (slot < 0 || slot >= MAX_IMAGES || !img_url || img_url[0] == '\0') return;
    browser_image_t* bimg = &g_images[slot];
    strncpy(bimg->url, img_url, sizeof(bimg->url) - 1);
    bimg->loaded = 0;

    if (img_url[0] == '/' || strncmp(img_url, "file://", 7) == 0) {
        const char* fpath = img_url;
        if (strncmp(img_url, "file://", 7) == 0) fpath = img_url + 7;
        bimg->img = image_load_file(fpath);
    } else {
        int32_t bytes = woos_net_fetch(img_url, (char*)g_img_fetch_buf, sizeof(g_img_fetch_buf));
        if (bytes > 0) {
            bimg->img = image_load_from_memory(g_img_fetch_buf, (size_t)bytes);
        }
    }

    if (bimg->img && bimg->img->width > 0 && bimg->img->height > 0) {
        bimg->loaded = 1;
        int max_w = g_win_w - 60;
        if (max_w < 100) max_w = 100;
        int dw = bimg->img->width;
        int dh = bimg->img->height;

        if (dw > max_w) {
            dh = (dh * max_w) / dw;
            dw = max_w;
        }
        if (dh > 260) {
            dw = (dw * 260) / dh;
            dh = 260;
        }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;

        if (dw != bimg->img->width || dh != bimg->img->height) {
            uint32_t* scaled = (uint32_t*)woos_malloc((size_t)dw * dh * sizeof(uint32_t));
            if (scaled) {
                image_scale_bilinear(bimg->img->pixels, bimg->img->width, bimg->img->height, scaled, dw, dh);
                woos_free(bimg->img->pixels);
                bimg->img->pixels = scaled;
                bimg->img->width = dw;
                bimg->img->height = dh;
            }
        }
        bimg->disp_w = bimg->img->width;
        bimg->disp_h = bimg->img->height;
    } else {
        bimg->loaded = -1;
        bimg->disp_w = 140;
        bimg->disp_h = 32;
    }
}

static void parse_html_to_boxes(int fetch_images) {
    if (g_doc_root) {
        box_free_tree(g_doc_root);
        g_doc_root = NULL;
    }
    g_doc_root = box_create(BOX_BLOCK);

    g_num_boxes = 0;
    g_num_links = 0;
    g_num_inputs = 0;
    g_focused_input = -1;
    g_hovered_link = -1;
    g_doc_height = 0;

    if (fetch_images) {
        free_page_images();
    }

    int cur_x = 20;
    int cur_y = 16;
    int line_h = LINE_H;

    int in_tag = 0;
    int in_script = 0;
    int in_style = 0;
    int in_title = 0;
    int in_link = 0;
    int cur_link_idx = -1;
    int in_bold = 0;
    int in_code = 0;
    int in_header = 0;
    int in_list = 0;

    char cur_form_action[160] = "";
    char cur_form_method[8] = "GET";

    char tag_buf[256];
    int tag_len = 0;

    char word_buf[128];
    int word_len = 0;

    char title_buf[64];
    int title_len = 0;

    const char* p = g_html;

    while (*p != '\0') {
        if (*p == '<') {
            if (word_len > 0) {
                word_buf[word_len] = '\0';
                int ww = word_len * CHAR_W;
                if (cur_x + ww > g_win_w - 36) {
                    cur_y += line_h;
                    cur_x = in_list ? 36 : 20;
                }
                if (g_num_boxes < MAX_BOXES) {
                    layout_box_t* b = &g_boxes[g_num_boxes++];
                    b->type = in_header ? ITEM_HEADER : ITEM_TEXT;
                    b->x = cur_x;
                    b->y = cur_y;
                    b->w = ww;
                    b->h = line_h;
                    strncpy(b->text, word_buf, sizeof(b->text) - 1);
                    b->text_len = word_len;
                    b->color = in_link ? COLOR_LINK : (in_header ? COLOR_TEXT_HEADER : COLOR_TEXT_NORMAL);
                    b->bg_color = in_code ? COLOR_CODE_BG : 0;
                    b->is_bold = in_bold || in_header;
                    b->is_code = in_code;
                    b->is_header = in_header;
                    b->link_idx = in_link ? cur_link_idx : -1;
                    b->elem_idx = -1;
                    cur_x += ww + CHAR_W;
                    if (cur_y + line_h > g_doc_height) g_doc_height = cur_y + line_h;

                    if (g_doc_root) {
                        struct box* nb = box_create(in_header ? BOX_BLOCK : BOX_TEXT);
                        if (nb) {
                            nb->text = strdup(word_buf);
                            nb->length = word_len;
                            nb->width = ww;
                            nb->height = line_h;
                            nb->background_colour = in_code ? COLOR_CODE_BG : NS_TRANSPARENT;
                            box_add_child(g_doc_root, nb);
                        }
                    }
                }
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

                // Tag handling
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
                    if (cur_x > 20) { cur_y += line_h + 8; cur_x = 20; }
                    in_header = 1;
                    line_h = 24;
                }
                else if (tag_buf[0] == '/' && tag_buf[1] == 'h' && tag_buf[2] >= '1' && tag_buf[2] <= '6') {
                    cur_y += line_h + 6;
                    cur_x = 20;
                    in_header = 0;
                    line_h = LINE_H;
                }
                else if (strncmp(tag_buf, "p", 1) == 0 || strncmp(tag_buf, "div", 3) == 0 ||
                         strncmp(tag_buf, "section", 7) == 0 || strncmp(tag_buf, "article", 7) == 0) {
                    if (cur_x > 20) { cur_y += line_h + 6; cur_x = 20; }
                }
                else if (strncmp(tag_buf, "br", 2) == 0 || strncmp(tag_buf, "BR", 2) == 0) {
                    cur_y += line_h;
                    cur_x = in_list ? 36 : 20;
                }
                else if (strncmp(tag_buf, "hr", 2) == 0 || strncmp(tag_buf, "HR", 2) == 0) {
                    if (cur_x > 20) { cur_y += line_h + 6; cur_x = 20; }
                    if (g_num_boxes < MAX_BOXES) {
                        layout_box_t* b = &g_boxes[g_num_boxes++];
                        b->type = ITEM_HR;
                        b->x = 20;
                        b->y = cur_y;
                        b->w = g_win_w - 40;
                        b->h = 1;
                        b->color = 0xDFE4EA;
                        cur_y += 10;
                        cur_x = 20;
                        if (cur_y > g_doc_height) g_doc_height = cur_y;
                    }
                }
                else if (strncmp(tag_buf, "ul", 2) == 0 || strncmp(tag_buf, "ol", 2) == 0) {
                    in_list = 1;
                    if (cur_x > 20) { cur_y += line_h + 4; cur_x = 36; }
                }
                else if (strncmp(tag_buf, "/ul", 3) == 0 || strncmp(tag_buf, "/ol", 3) == 0) {
                    in_list = 0;
                    if (cur_x > 20) { cur_y += line_h + 4; cur_x = 20; }
                }
                else if (strncmp(tag_buf, "li", 2) == 0) {
                    cur_y += line_h + 2;
                    cur_x = 20;
                    if (g_num_boxes < MAX_BOXES) {
                        layout_box_t* b = &g_boxes[g_num_boxes++];
                        b->type = ITEM_TEXT;
                        b->x = cur_x;
                        b->y = cur_y;
                        b->w = 12;
                        b->h = line_h;
                        strcpy(b->text, "•");
                        b->text_len = 1;
                        b->color = COLOR_TEXT_MUTED;
                        b->link_idx = -1;
                        b->elem_idx = -1;
                    }
                    cur_x = 36;
                }
                else if (strncmp(tag_buf, "b", 1) == 0 || strncmp(tag_buf, "strong", 6) == 0) {
                    in_bold = 1;
                }
                else if (strncmp(tag_buf, "/b", 2) == 0 || strncmp(tag_buf, "/strong", 7) == 0) {
                    in_bold = 0;
                }
                else if (strncmp(tag_buf, "code", 4) == 0 || strncmp(tag_buf, "pre", 3) == 0) {
                    in_code = 1;
                }
                else if (strncmp(tag_buf, "/code", 5) == 0 || strncmp(tag_buf, "/pre", 4) == 0) {
                    in_code = 0;
                }
                else if (tag_buf[0] == 'a' && (tag_buf[1] == ' ' || tag_buf[1] == '\t')) {
                    const char* hpos = strstr(tag_buf, "href=");
                    if (hpos && g_num_links < MAX_LINKS) {
                        hpos += 5;
                        char quote = 0;
                        if (*hpos == '"' || *hpos == '\'') quote = *hpos++;
                        char raw_href[160];
                        int uidx = 0;
                        while (*hpos != '\0' && ((quote && *hpos != quote) || (!quote && *hpos != ' ' && *hpos != '>')) && uidx < 159) {
                            raw_href[uidx++] = *hpos++;
                        }
                        raw_href[uidx] = '\0';
                        browser_link_t* bl = &g_links[g_num_links++];
                        resolve_url(g_url, raw_href, bl->url, sizeof(bl->url));
                        in_link = 1;
                        cur_link_idx = g_num_links - 1;
                    }
                }
                else if (strncmp(tag_buf, "/a", 2) == 0) {
                    in_link = 0;
                    cur_link_idx = -1;
                }
                else if (strncmp(tag_buf, "form", 4) == 0) {
                    const char* apos = strstr(tag_buf, "action=");
                    if (apos) {
                        apos += 7;
                        char quote = 0;
                        if (*apos == '"' || *apos == '\'') quote = *apos++;
                        int fidx = 0;
                        while (*apos != '\0' && ((quote && *apos != quote) || (!quote && *apos != ' ')) && fidx < 159) {
                            cur_form_action[fidx++] = *apos++;
                        }
                        cur_form_action[fidx] = '\0';
                    }
                }
                else if (strncmp(tag_buf, "input", 5) == 0) {
                    char type_str[24] = "text";
                    char name_str[32] = "q";
                    char val_str[128] = "";
                    char ph_str[64] = "Search...";

                    const char* tpos = strstr(tag_buf, "type=");
                    if (tpos) {
                        tpos += 5; char q = 0; if (*tpos == '"' || *tpos == '\'') q = *tpos++;
                        int ti = 0; while (*tpos != '\0' && ((q && *tpos != q) || (!q && *tpos != ' ')) && ti < 23) type_str[ti++] = *tpos++;
                        type_str[ti] = '\0';
                    }
                    const char* npos = strstr(tag_buf, "name=");
                    if (npos) {
                        npos += 5; char q = 0; if (*npos == '"' || *npos == '\'') q = *npos++;
                        int ni = 0; while (*npos != '\0' && ((q && *npos != q) || (!q && *npos != ' ')) && ni < 31) name_str[ni++] = *npos++;
                        name_str[ni] = '\0';
                    }
                    const char* vpos = strstr(tag_buf, "value=");
                    if (vpos) {
                        vpos += 6; char q = 0; if (*vpos == '"' || *vpos == '\'') q = *vpos++;
                        int vi = 0; while (*vpos != '\0' && ((q && *vpos != q) || (!q && *vpos != ' ')) && vi < 127) val_str[vi++] = *vpos++;
                        val_str[vi] = '\0';
                    }
                    const char* ppos = strstr(tag_buf, "placeholder=");
                    if (ppos) {
                        ppos += 12; char q = 0; if (*ppos == '"' || *ppos == '\'') q = *ppos++;
                        int pi = 0; while (*ppos != '\0' && ((q && *ppos != q) || (!q && *ppos != ' ')) && pi < 63) ph_str[pi++] = *ppos++;
                        ph_str[pi] = '\0';
                    }

                    if (strcmp(type_str, "submit") == 0 || strcmp(type_str, "button") == 0) {
                        int bw = 80;
                        if (cur_x + bw > g_win_w - 36) { cur_y += 32; cur_x = 20; }
                        if (g_num_boxes < MAX_BOXES) {
                            layout_box_t* b = &g_boxes[g_num_boxes++];
                            b->type = ITEM_BUTTON;
                            b->x = cur_x;
                            b->y = cur_y;
                            b->w = bw;
                            b->h = 24;
                            strncpy(b->text, val_str[0] ? val_str : "Search", sizeof(b->text) - 1);
                            b->elem_idx = g_num_inputs > 0 ? (g_num_inputs - 1) : -1;
                            b->link_idx = -1;
                            cur_x += bw + 10;
                            if (cur_y + 26 > g_doc_height) g_doc_height = cur_y + 26;
                        }
                    } else if (strcmp(type_str, "hidden") != 0) {
                        int iw = 220;
                        if (cur_x + iw > g_win_w - 36) { cur_y += 32; cur_x = 20; }
                        if (g_num_inputs < MAX_INPUTS && g_num_boxes < MAX_BOXES) {
                            int inp_idx = g_num_inputs++;
                            browser_input_t* inp = &g_inputs[inp_idx];
                            strncpy(inp->name, name_str, sizeof(inp->name) - 1);
                            strncpy(inp->value, val_str, sizeof(inp->value) - 1);
                            strncpy(inp->placeholder, ph_str, sizeof(inp->placeholder) - 1);
                            strncpy(inp->form_action, cur_form_action, sizeof(inp->form_action) - 1);
                            strncpy(inp->form_method, cur_form_method, sizeof(inp->form_method) - 1);
                            inp->cursor_pos = (int)strlen(inp->value);
                            inp->scroll_x = 0;
                            inp->is_focused = 0;

                            layout_box_t* b = &g_boxes[g_num_boxes++];
                            b->type = ITEM_INPUT;
                            b->x = cur_x;
                            b->y = cur_y;
                            b->w = iw;
                            b->h = 24;
                            b->elem_idx = inp_idx;
                            b->link_idx = -1;
                            cur_x += iw + 10;
                            if (cur_y + 26 > g_doc_height) g_doc_height = cur_y + 26;
                        }
                    }
                }
                else if (strncmp(tag_buf, "img", 3) == 0) {
                    const char* spos = strstr(tag_buf, "src=");
                    if (spos) {
                        spos += 4; char q = 0; if (*spos == '"' || *spos == '\'') q = *spos++;
                        char raw_src[160];
                        int sidx = 0;
                        while (*spos != '\0' && ((q && *spos != q) || (!q && *spos != ' ')) && sidx < 159) raw_src[sidx++] = *spos++;
                        raw_src[sidx] = '\0';

                        char abs_img_url[160];
                        resolve_url(g_url, raw_src, abs_img_url, sizeof(abs_img_url));

                        int slot = -1;
                        for (int k = 0; k < g_num_images; k++) {
                            if (strcmp(g_images[k].url, abs_img_url) == 0) {
                                slot = k;
                                break;
                            }
                        }
                        if (slot < 0 && g_num_images < MAX_IMAGES) {
                            slot = g_num_images++;
                            if (fetch_images && slot < 8) {
                                load_image_for_slot(slot, abs_img_url);
                            }
                        }

                        if (slot >= 0 && g_num_boxes < MAX_BOXES) {
                            if (cur_x > 20) { cur_y += line_h + 6; cur_x = 20; }
                            int img_w = g_images[slot].disp_w > 0 ? g_images[slot].disp_w : 140;
                            int img_h = g_images[slot].disp_h > 0 ? g_images[slot].disp_h : 32;

                            layout_box_t* b = &g_boxes[g_num_boxes++];
                            b->type = ITEM_IMAGE;
                            b->x = cur_x;
                            b->y = cur_y;
                            b->w = img_w;
                            b->h = img_h;
                            b->elem_idx = slot;
                            b->link_idx = in_link ? cur_link_idx : -1;
                            cur_y += img_h + 8;
                            cur_x = 20;
                            if (cur_y > g_doc_height) g_doc_height = cur_y;
                        }
                    }
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
            if (strncmp(p, "&nbsp;", 6) == 0) { p += 6; if (word_len + 1 < 127) word_buf[word_len++] = ' '; continue; }
            if (strncmp(p, "&amp;", 5) == 0) { p += 5; if (word_len + 1 < 127) word_buf[word_len++] = '&'; continue; }
            if (strncmp(p, "&lt;", 4) == 0) { p += 4; if (word_len + 1 < 127) word_buf[word_len++] = '<'; continue; }
            if (strncmp(p, "&gt;", 4) == 0) { p += 4; if (word_len + 1 < 127) word_buf[word_len++] = '>'; continue; }
            if (strncmp(p, "&quot;", 6) == 0) { p += 6; if (word_len + 1 < 127) word_buf[word_len++] = '"'; continue; }
            if (strncmp(p, "&#39;", 5) == 0) { p += 5; if (word_len + 1 < 127) word_buf[word_len++] = '\''; continue; }
        }

        if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (word_len > 0) {
                word_buf[word_len] = '\0';
                int ww = word_len * CHAR_W;
                if (cur_x + ww > g_win_w - 36) {
                    cur_y += line_h;
                    cur_x = in_list ? 36 : 20;
                }
                if (g_num_boxes < MAX_BOXES) {
                    layout_box_t* b = &g_boxes[g_num_boxes++];
                    b->type = in_header ? ITEM_HEADER : ITEM_TEXT;
                    b->x = cur_x;
                    b->y = cur_y;
                    b->w = ww;
                    b->h = line_h;
                    strncpy(b->text, word_buf, sizeof(b->text) - 1);
                    b->text_len = word_len;
                    b->color = in_link ? COLOR_LINK : (in_header ? COLOR_TEXT_HEADER : COLOR_TEXT_NORMAL);
                    b->bg_color = in_code ? COLOR_CODE_BG : 0;
                    b->is_bold = in_bold || in_header;
                    b->is_code = in_code;
                    b->is_header = in_header;
                    b->link_idx = in_link ? cur_link_idx : -1;
                    b->elem_idx = -1;
                    cur_x += ww + CHAR_W;
                    if (cur_y + line_h > g_doc_height) g_doc_height = cur_y + line_h;

                    if (g_doc_root) {
                        struct box* nb = box_create(in_header ? BOX_BLOCK : BOX_TEXT);
                        if (nb) {
                            nb->text = strdup(word_buf);
                            nb->length = word_len;
                            nb->width = ww;
                            nb->height = line_h;
                            nb->background_colour = in_code ? COLOR_CODE_BG : NS_TRANSPARENT;
                            box_add_child(g_doc_root, nb);
                        }
                    }
                }
                word_len = 0;
            }
        } else {
            if (word_len + 1 < 127) {
                word_buf[word_len++] = *p;
            }
        }
        p++;
    }

    if (word_len > 0) {
        word_buf[word_len] = '\0';
        int ww = word_len * CHAR_W;
        if (g_num_boxes < MAX_BOXES) {
            layout_box_t* b = &g_boxes[g_num_boxes++];
            b->type = in_header ? ITEM_HEADER : ITEM_TEXT;
            b->x = cur_x;
            b->y = cur_y;
            b->w = ww;
            b->h = line_h;
            strncpy(b->text, word_buf, sizeof(b->text) - 1);
            b->text_len = word_len;
            b->color = in_link ? COLOR_LINK : (in_header ? COLOR_TEXT_HEADER : COLOR_TEXT_NORMAL);
            b->bg_color = in_code ? COLOR_CODE_BG : 0;
            b->is_bold = in_bold || in_header;
            b->is_code = in_code;
            b->is_header = in_header;
            b->link_idx = in_link ? cur_link_idx : -1;
            b->elem_idx = -1;
            if (cur_y + line_h > g_doc_height) g_doc_height = cur_y + line_h;
        }
    }

    if (g_num_boxes == 0) {
        layout_box_t* b = &g_boxes[g_num_boxes++];
        b->type = ITEM_TEXT;
        b->x = 20; b->y = 20; b->w = 120; b->h = LINE_H;
        strcpy(b->text, "Empty page.");
        b->text_len = 11;
        b->color = COLOR_TEXT_MUTED;
        b->link_idx = -1;
        b->elem_idx = -1;
        g_doc_height = 40;
    }

    if (g_doc_root) {
        int netsurf_h = layout_document_tree(g_doc_root, g_win_w - 40);
        if (netsurf_h > g_doc_height) g_doc_height = netsurf_h;
    }
}

static void navigate_to(const char* target_url, int add_history) {
    if (!target_url || target_url[0] == '\0') return;

    // Normalize URL
    char norm_url[160];
    const char* p = target_url;
    while (*p == ' ' || *p == '\t') p++;
    int nlen = 0;
    while (p[nlen] != '\0' && nlen < 159) {
        norm_url[nlen] = p[nlen];
        nlen++;
    }
    while (nlen > 0 && (norm_url[nlen - 1] == ' ' || norm_url[nlen - 1] == '\r' || norm_url[nlen - 1] == '\n')) {
        nlen--;
    }
    norm_url[nlen] = '\0';

    if (is_downloadable(norm_url)) {
        do_download(norm_url);
        return;
    }

    strncpy(g_url, norm_url, sizeof(g_url) - 1);
    g_url[sizeof(g_url) - 1] = '\0';
    g_url_len = (int)strlen(g_url);
    g_cursor_pos = g_url_len;
    g_scroll_x = 0;
    g_scroll_y = 0;

    if (add_history) {
        if (g_history_idx < MAX_HISTORY - 1) {
            g_history_idx++;
            strncpy(g_history[g_history_idx], g_url, 159);
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
            parse_html_to_boxes(1);
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
        char err_msg[128] = "Connection failed. Check network or address.";
        if (g_html[0] != '\0') {
            int k = 0;
            while (g_html[k] != '\0' && g_html[k] != '\n' && k < 127) {
                err_msg[k] = g_html[k];
                k++;
            }
            err_msg[k] = '\0';
        }

        strcpy(g_html, "<html><body><h1>Unable to connect</h1><hr/>"
                       "<p>WoOS Browser could not load the requested webpage.</p>"
                       "<p><b>Diagnostic:</b> ");
        strcat(g_html, err_msg);
        strcat(g_html, "</p><p><a href=\"/WELCOME.HTM\">Return to Home</a></p></body></html>");
        g_html_len = (uint32_t)strlen(g_html);
        parse_html_to_boxes(1);
        set_status(err_msg);
    } else {
        g_html_len = (uint32_t)res;
        g_html[g_html_len] = '\0';
        parse_html_to_boxes(1);

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

    char disp_url[160];
    int di = 0;
    for (int i = g_scroll_x; i < g_url_len && di < text_avail && di < 159; i++) {
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

    // 2. Bookmarks Bar (y = 34..60)
    fill_rect(g_fb, g_win_w, g_win_h, 0, TOPBAR_H, g_win_w, BOOKMARKS_H, COLOR_BOOKMARK_BG);
    draw_rect(g_fb, g_win_w, g_win_h, 0, TOPBAR_H + BOOKMARKS_H - 1, g_win_w, 1, 0x353B48);

    draw_btn(8, TOPBAR_H + 3, 60, 20, "Google", 0, 0x2F3542);
    draw_btn(72, TOPBAR_H + 3, 84, 20, "DuckDuckGo", 0, 0x2F3542);
    draw_btn(160, TOPBAR_H + 3, 76, 20, "Wikipedia", 0, 0x2F3542);
    draw_btn(240, TOPBAR_H + 3, 68, 20, "FrogFind", 0, 0x2F3542);
    draw_btn(312, TOPBAR_H + 3, 48, 20, "Wiby", 0, 0x2F3542);
    draw_btn(364, TOPBAR_H + 3, 64, 20, "Welcome", 0, 0x2F3542);

    // 3. Render NetSurf Layout Boxes Viewport (y = HEADER_H .. win_h - STATUSBAR_H)
    int view_y = HEADER_H;
    int view_h = g_win_h - HEADER_H - STATUSBAR_H;

    if (g_doc_root && g_nsfb) {
        struct redraw_context ctx;
        ctx.interactive = true;
        ctx.background_images = true;
        ctx.plot = &fb_plotters;
        ctx.priv = g_nsfb;
        redraw_box_tree(g_doc_root, 20, HEADER_H, g_scroll_y, view_h, &ctx);
    }

    for (int i = 0; i < g_num_boxes; i++) {
        layout_box_t* b = &g_boxes[i];
        int sy = view_y + b->y - g_scroll_y;
        int sx = b->x;

        if (sy + b->h < view_y || sy > view_y + view_h) continue;

        switch (b->type) {
            case ITEM_TEXT:
            case ITEM_HEADER: {
                if (b->bg_color != 0) {
                    fill_rect(g_fb, g_win_w, g_win_h, sx - 2, sy - 1, b->w + 4, b->h + 2, b->bg_color);
                }
                uint32_t col = b->color;
                if (b->link_idx >= 0) {
                    col = (b->link_idx == g_hovered_link) ? COLOR_LINK_HOVER : COLOR_LINK;
                }
                woos_draw_text(g_fb, g_win_w, g_win_h, sx, sy + 2, b->text, col);
                if (b->link_idx >= 0) {
                    fill_rect(g_fb, g_win_w, g_win_h, sx, sy + b->h - 3, b->w, 1, col);
                }
                if (b->type == ITEM_HEADER && b->is_header && b->h >= 24) {
                    fill_rect(g_fb, g_win_w, g_win_h, sx, sy + b->h - 2, b->w > 120 ? b->w : 120, 2, 0x3867D6);
                }
                break;
            }

            case ITEM_HR: {
                fill_rect(g_fb, g_win_w, g_win_h, sx, sy, b->w, 1, b->color);
                break;
            }

            case ITEM_IMAGE: {
                if (b->elem_idx >= 0 && b->elem_idx < g_num_images) {
                    browser_image_t* bimg = &g_images[b->elem_idx];
                    if (bimg->loaded == 1 && bimg->img && bimg->img->pixels) {
                        image_blit_alpha_clipped(g_fb, g_win_w, g_win_h, sx, sy,
                                                 bimg->img->pixels, bimg->img->width, bimg->img->height, bimg->img->width,
                                                 0, view_y, g_win_w, view_h);
                    } else {
                        fill_rect(g_fb, g_win_w, g_win_h, sx, sy, b->w, b->h, 0xEDEDED);
                        draw_rect(g_fb, g_win_w, g_win_h, sx, sy, b->w, b->h, 0xCBD5E1);
                        woos_draw_text(g_fb, g_win_w, g_win_h, sx + 8, sy + (b->h - 10) / 2, "[Image]", 0x94A3B8);
                    }
                }
                break;
            }

            case ITEM_INPUT: {
                if (b->elem_idx >= 0 && b->elem_idx < g_num_inputs) {
                    browser_input_t* inp = &g_inputs[b->elem_idx];
                    fill_rect(g_fb, g_win_w, g_win_h, sx, sy, b->w, b->h, COLOR_INPUT_BG);
                    draw_rect(g_fb, g_win_w, g_win_h, sx, sy, b->w, b->h, inp->is_focused ? COLOR_INPUT_FOCUS : COLOR_INPUT_BORDER);

                    const char* show_txt = inp->value[0] ? inp->value : inp->placeholder;
                    uint32_t txt_col = inp->value[0] ? COLOR_ADDR_TEXT : COLOR_TEXT_MUTED;
                    woos_draw_text(g_fb, g_win_w, g_win_h, sx + 6, sy + 5, show_txt, txt_col);

                    if (inp->is_focused) {
                        int cx = sx + 6 + inp->cursor_pos * CHAR_W;
                        if (cx >= sx + 4 && cx < sx + b->w - 4) {
                            fill_rect(g_fb, g_win_w, g_win_h, cx, sy + 4, 2, b->h - 8, 0x111111);
                        }
                    }
                }
                break;
            }

            case ITEM_BUTTON: {
                draw_btn(sx, sy, b->w, b->h, b->text, 0, 0x3B4A5A);
                break;
            }
        }
    }

    // Vertical Scrollbar
    if (g_doc_height > view_h) {
        int sb_x = g_win_w - 14;
        fill_rect(g_fb, g_win_w, g_win_h, sb_x, view_y, 14, view_h, COLOR_SCROLL_TRACK);
        int thumb_h = (view_h * view_h) / g_doc_height;
        if (thumb_h < 20) thumb_h = 20;
        int max_s = g_doc_height - view_h;
        int thumb_y = view_y + (g_scroll_y * (view_h - thumb_h)) / (max_s > 0 ? max_s : 1);
        fill_rect(g_fb, g_win_w, g_win_h, sb_x + 1, thumb_y, 12, thumb_h, COLOR_SCROLL_THUMB);
    }

    // 4. Status Bar (y = win_h - STATUSBAR_H .. win_h)
    int sb_y = g_win_h - STATUSBAR_H;
    fill_rect(g_fb, g_win_w, g_win_h, 0, sb_y, g_win_w, STATUSBAR_H, COLOR_STATUS_BG);
    draw_rect(g_fb, g_win_w, g_win_h, 0, sb_y, g_win_w, 1, 0x353B48);

    if (g_hovered_link >= 0 && g_hovered_link < g_num_links) {
        char link_info[160] = "Link: ";
        strncat(link_info, g_links[g_hovered_link].url, 150);
        woos_draw_text(g_fb, g_win_w, g_win_h, 10, sb_y + 4, link_info, 0x74B9FF);
    } else {
        woos_draw_text(g_fb, g_win_w, g_win_h, 10, sb_y + 4, g_status_msg, COLOR_TEXT_MUTED);
    }

    // 5. Universal Dialog Overlay
    if (g_dialog.active) {
        woos_dialog_draw(g_fb, g_win_w, g_win_h, &g_dialog);
    }
}

static void handle_click(int x, int y) {
    if (g_dialog.active) {
        int res = woos_dialog_handle_click(&g_dialog, g_win_w, g_win_h, x, y);
        if (res == WOOS_DIALOG_RES_OK) {
            do_download(g_dialog.input);
        }
        return;
    }

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
            if (g_focused_input >= 0) { g_inputs[g_focused_input].is_focused = 0; g_focused_input = -1; }
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
            woos_dialog_open(&g_dialog, DIALOG_MODE_SAVE_AS, "Download File", "Download target URL:", g_url, "Download");
            return;
        }
    }

    // 2. Bookmarks Bar
    if (y >= TOPBAR_H && y < HEADER_H) {
        g_url_focus = 0;
        if (g_focused_input >= 0) { g_inputs[g_focused_input].is_focused = 0; g_focused_input = -1; }
        if (x >= 8 && x <= 68) { navigate_to("https://www.google.com/?gbv=1", 1); return; }
        if (x >= 72 && x <= 156) { navigate_to("https://html.duckduckgo.com/html/", 1); return; }
        if (x >= 160 && x <= 236) { navigate_to("https://en.m.wikipedia.org", 1); return; }
        if (x >= 240 && x <= 308) { navigate_to("http://frogfind.com", 1); return; }
        if (x >= 312 && x <= 360) { navigate_to("https://wiby.me", 1); return; }
        if (x >= 364 && x <= 428) { navigate_to("/WELCOME.HTM", 1); return; }
    }

    // 3. Content Viewport
    if (y >= HEADER_H && y < g_win_h - STATUSBAR_H) {
        g_url_focus = 0;
        int view_y = HEADER_H;
        int doc_y = y - view_y + g_scroll_y;
        int doc_x = x;

        // Check if clicked an input box
        int hit_input = -1;
        for (int i = 0; i < g_num_boxes; i++) {
            layout_box_t* b = &g_boxes[i];
            if (doc_x >= b->x && doc_x <= b->x + b->w && doc_y >= b->y && doc_y <= b->y + b->h) {
                if (b->type == ITEM_INPUT && b->elem_idx >= 0) {
                    hit_input = b->elem_idx;
                    break;
                } else if (b->type == ITEM_BUTTON && b->elem_idx >= 0) {
                    submit_form(b->elem_idx);
                    return;
                } else if (b->link_idx >= 0 && b->link_idx < g_num_links) {
                    navigate_to(g_links[b->link_idx].url, 1);
                    return;
                }
            }
        }

        if (g_focused_input >= 0) {
            g_inputs[g_focused_input].is_focused = 0;
        }
        g_focused_input = hit_input;
        if (g_focused_input >= 0) {
            browser_input_t* inp = &g_inputs[g_focused_input];
            inp->is_focused = 1;
            inp->cursor_pos = (int)strlen(inp->value);
        }
    }
}

static void handle_mouse_move(int x, int y) {
    if (g_dialog.active) return;

    if (y >= HEADER_H && y < g_win_h - STATUSBAR_H) {
        int view_y = HEADER_H;
        int doc_y = y - view_y + g_scroll_y;
        int doc_x = x;
        int found = -1;
        for (int i = 0; i < g_num_boxes; i++) {
            layout_box_t* b = &g_boxes[i];
            if (doc_x >= b->x && doc_x <= b->x + b->w && doc_y >= b->y && doc_y <= b->y + b->h) {
                if (b->link_idx >= 0 && b->link_idx < g_num_links) {
                    found = b->link_idx;
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
    if (g_dialog.active) {
        int res = woos_dialog_handle_key(&g_dialog, ascii, scancode);
        if (res == WOOS_DIALOG_RES_OK) {
            do_download(g_dialog.input);
        }
        return;
    }

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
                for (int i = g_cursor_pos - 1; i < g_url_len; i++) g_url[i] = g_url[i + 1];
                g_url_len--;
                g_cursor_pos--;
            }
            return;
        }

        // Delete
        if (scancode == 0x53) {
            if (g_cursor_pos < g_url_len) {
                for (int i = g_cursor_pos; i < g_url_len; i++) g_url[i] = g_url[i + 1];
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
        if (ascii >= 32 && ascii <= 126 && g_url_len < 158) {
            for (int i = g_url_len; i >= g_cursor_pos; i--) g_url[i + 1] = g_url[i];
            g_url[g_cursor_pos] = (char)ascii;
            g_url_len++;
            g_cursor_pos++;
            return;
        }
    } else if (g_focused_input >= 0 && g_focused_input < g_num_inputs) {
        browser_input_t* inp = &g_inputs[g_focused_input];
        int vlen = (int)strlen(inp->value);

        // Enter: Submit search form!
        if (ascii == 10 || ascii == 13 || scancode == 0x1C) {
            submit_form(g_focused_input);
            return;
        }

        // Backspace
        if (ascii == 8 || scancode == 0x0E) {
            if (inp->cursor_pos > 0) {
                for (int i = inp->cursor_pos - 1; i < vlen; i++) inp->value[i] = inp->value[i + 1];
                inp->cursor_pos--;
            }
            return;
        }

        // Delete
        if (scancode == 0x53) {
            if (inp->cursor_pos < vlen) {
                for (int i = inp->cursor_pos; i < vlen; i++) inp->value[i] = inp->value[i + 1];
            }
            return;
        }

        // Left / Right
        if (scancode == 0x4B && inp->cursor_pos > 0) { inp->cursor_pos--; return; }
        if (scancode == 0x4D && inp->cursor_pos < vlen) { inp->cursor_pos++; return; }

        // Printable char
        if (ascii >= 32 && ascii <= 126 && vlen < 126) {
            for (int i = vlen; i >= inp->cursor_pos; i--) inp->value[i + 1] = inp->value[i];
            inp->value[inp->cursor_pos] = (char)ascii;
            inp->cursor_pos++;
            return;
        }
    } else {
        // Document scroll keys
        int view_h = g_win_h - HEADER_H - STATUSBAR_H;
        int max_s = g_doc_height > view_h ? (g_doc_height - view_h) : 0;

        // Up arrow
        if (scancode == 0x48) {
            if (g_scroll_y > 0) g_scroll_y -= 24;
            if (g_scroll_y < 0) g_scroll_y = 0;
            return;
        }
        // Down arrow
        if (scancode == 0x50) {
            if (g_scroll_y < max_s) g_scroll_y += 24;
            if (g_scroll_y > max_s) g_scroll_y = max_s;
            return;
        }
        // Page Up
        if (scancode == 0x49) {
            g_scroll_y -= (view_h - 24);
            if (g_scroll_y < 0) g_scroll_y = 0;
            return;
        }
        // Page Down
        if (scancode == 0x51) {
            g_scroll_y += (view_h - 24);
            if (g_scroll_y > max_s) g_scroll_y = max_s;
            return;
        }
    }
}

__attribute__((export_name("_start")))
void _start(void) {
    g_win_idx = woos_create_window(g_win_w, g_win_h, "NetSurf Browser", 0);
    if (g_win_idx < 0) return;

    // Initialize NetSurf Framebuffer & Toolkit
    g_nsfb = framebuffer_initialise("ram", g_win_w, g_win_h, 32);
    if (g_nsfb) {
        g_nsfb->ptr = (uint8_t*)g_fb;
        g_fbtk = fbtk_init(g_nsfb);
    }

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
                free_page_images();
                if (g_doc_root) {
                    box_free_tree(g_doc_root);
                    g_doc_root = NULL;
                }
                framebuffer_finalise();
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
                    if (g_nsfb) {
                        framebuffer_resize(g_nsfb, g_win_w, g_win_h, 32);
                        g_nsfb->ptr = (uint8_t*)g_fb;
                    }
                    parse_html_to_boxes(0);
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
