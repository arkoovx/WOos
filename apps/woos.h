#ifndef WOOS_APP_H
#define WOOS_APP_H

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef signed int     int32_t;
typedef unsigned long  size_t;
typedef unsigned long long uint64_t;
typedef signed long long   int64_t;

#define NULL ((void*)0)

static inline void int_to_str(int val, char* dst) {
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

// IPC Message types
#define MSG_CLICK        1
#define MSG_RIGHT_CLICK  2
#define MSG_CLOSE        3
#define MSG_KEY_DOWN     4
#define MSG_OPEN_FILE    10

// VFS Modes
#define VFS_MODE_READ    0x01
#define VFS_MODE_WRITE   0x02
#define VFS_MODE_CREATE  0x04
#define VFS_MODE_TRUNC   0x08

// IPC Message Structure
typedef struct {
    uint32_t sender_id;
    uint32_t dest_id;
    uint32_t type;
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint32_t arg4;
    uint32_t arg5;
} __attribute__((packed)) woos_msg_t;

// WoOS Environment Imports
#define WOOS_API(name) __attribute__((import_module("env"), import_name(name)))

WOOS_API("woos_graphics_create_window_ex")
int woos_create_window(int w, int h, const char* title, int flags);

WOOS_API("woos_graphics_destroy_window")
int woos_destroy_window(int win_id);
#define woos_graphics_destroy_window woos_destroy_window

WOOS_API("woos_graphics_blit_window")
int woos_blit_window(int win_id, const void* buffer, int w, int h);

WOOS_API("woos_graphics_draw_text_to_buffer")
int woos_draw_text(void* buf, int w, int h, int x, int y, const char* text, uint32_t color);

WOOS_API("woos_ipc_recv")
int woos_ipc_recv(woos_msg_t* msg);

WOOS_API("woos_ipc_wait_message")
int woos_ipc_wait_message(void);

WOOS_API("woos_thread_get_id")
int woos_thread_get_id(void);

WOOS_API("woos_system_spawn")
int woos_system_spawn(const char* path);

WOOS_API("woos_system_set_open_file")
int woos_system_set_open_file(const char* path);

WOOS_API("woos_system_get_open_file")
int woos_system_get_open_file(char* buf, int max_len);

WOOS_API("woos_system_clear_open_file")
int woos_system_clear_open_file(void);

WOOS_API("woos_vfs_open")
int woos_vfs_open(const char* path, int mode);

WOOS_API("woos_vfs_read")
uint32_t woos_vfs_read(int handle, void* buf, uint32_t size);

WOOS_API("woos_vfs_write")
uint32_t woos_vfs_write(int handle, const void* buf, uint32_t size);

WOOS_API("woos_vfs_close")
int woos_vfs_close(int handle);

WOOS_API("woos_vfs_size")
uint32_t woos_vfs_size(int handle);

typedef struct woos_dirent {
    char name[24];
    uint8_t is_dir;
    uint32_t size;
} woos_dirent_t;

WOOS_API("woos_vfs_readdir")
int woos_vfs_readdir(int handle, woos_dirent_t* entry);

WOOS_API("woos_vfs_mkdir")
int woos_vfs_mkdir(const char* path);

WOOS_API("woos_vfs_unlink")
int woos_vfs_unlink(const char* path);

WOOS_API("woos_vfs_rename")
int woos_vfs_rename(const char* old_path, const char* new_path);

// Network APIs
WOOS_API("woos_net_resolve")
int woos_net_resolve(const char* hostname, char* out_ip, uint32_t max_len);

WOOS_API("woos_net_get_config")
int woos_net_get_config(char* out_buf, uint32_t max_len);

WOOS_API("woos_net_ping")
int woos_net_ping(const char* host, uint32_t count, char* out_buf, uint32_t max_len);

WOOS_API("woos_net_stat")
int woos_net_stat(char* out_buf, uint32_t max_len);

WOOS_API("woos_net_arp")
int woos_net_arp(char* out_buf, uint32_t max_len);

WOOS_API("woos_net_nslookup")
int woos_net_nslookup(const char* host, char* out_buf, uint32_t max_len);

WOOS_API("woos_net_fetch")
int woos_net_fetch(const char* url, char* out_buf, uint32_t max_len);

// Compositor & Window Manager APIs
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t buffer_ptr;
} woos_graphics_info_t;

typedef struct {
    uint32_t type;
    uint16_t x;
    uint16_t y;
    uint8_t buttons;
} __attribute__((packed)) woos_input_event_t;

#define WOOS_WIN_TITLE_LEN 32

typedef struct {
    uint32_t id;
    uint32_t thread_id;
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t flags;
    int32_t saved_x;
    int32_t saved_y;
    uint32_t saved_w;
    uint32_t saved_h;
    char title[WOOS_WIN_TITLE_LEN];
    uint32_t active;
    uint32_t* buffer;
} __attribute__((packed)) woos_window_info_t;

typedef struct {
    uint32_t uptime_ms;
    uint32_t total_pages;
    uint32_t free_pages;
    uint64_t heap_used;
    uint64_t heap_free;
    uint32_t keyboard_irq_count;
    uint32_t mouse_irq_count;
    uint32_t storage_last_lba;
    uint32_t ip_addr;
    uint8_t storage_ready;
    uint8_t storage_read_ok;
    uint8_t net_ready;
    uint8_t net_active;
    uint8_t net_link_up;
} __attribute__((packed)) woos_system_status_t;

WOOS_API("woos_graphics_get_info")
int woos_graphics_get_info(woos_graphics_info_t* info);

WOOS_API("woos_graphics_present_rect")
int woos_graphics_present_rect(const void* buf, int x, int y, int w, int h);

WOOS_API("woos_input_poll_event")
int woos_input_poll_event(woos_input_event_t* event);

WOOS_API("woos_input_wait_event")
int woos_input_wait_event(void);

WOOS_API("woos_system_get_status")
int woos_system_get_status(woos_system_status_t* status);

WOOS_API("woos_graphics_set_window_pos")
int woos_graphics_set_window_pos(int win_id, int x, int y);

WOOS_API("woos_graphics_get_window_count")
int woos_graphics_get_window_count(void);

WOOS_API("woos_graphics_get_window_info")
int woos_graphics_get_window_info(int index, woos_window_info_t* info);

WOOS_API("woos_graphics_draw_window_to_buffer")
int woos_graphics_draw_window_to_buffer(int win_id, void* dst_buf, int dst_w, int dst_h);

WOOS_API("woos_graphics_bring_to_front")
int woos_graphics_bring_to_front(int win_id);

WOOS_API("woos_graphics_toggle_minimize")
int woos_graphics_toggle_minimize(int win_id);

WOOS_API("woos_graphics_toggle_maximize")
int woos_graphics_toggle_maximize(int win_id);

WOOS_API("woos_graphics_get_focused_tid")
int woos_graphics_get_focused_tid(void);

WOOS_API("woos_ipc_send")
int woos_ipc_send(int dest_id, int type, int arg1, int arg2, int arg3, int arg4, int arg5);

// WASI Snapshot Preview 1 Imports
#define WASI_API(name) __attribute__((import_module("wasi_snapshot_preview1"), import_name(name)))

typedef struct {
    uint32_t buf;
    uint32_t buf_len;
} wasi_app_iovec_t;

typedef struct {
    uint64_t dev;
    uint64_t ino;
    uint8_t  filetype;
    uint8_t  pad[7];
    uint64_t nlink;
    uint64_t size;
    uint64_t atim;
    uint64_t mtim;
    uint64_t ctim;
} __attribute__((packed)) wasi_filestat_t;

typedef struct {
    uint64_t userdata;
    uint8_t  type;
    uint8_t  pad[7];
    union {
        struct {
            uint32_t clock_id;
            uint32_t pad;
            uint64_t timeout;
            uint64_t precision;
            uint16_t sub_flags;
            uint8_t  pad2[6];
        } clock;
        struct { uint32_t fd; } fd_read;
        struct { uint32_t fd; } fd_write;
    } u;
} __attribute__((packed)) wasi_subscription_t;

typedef struct {
    uint64_t userdata;
    uint16_t error;
    uint8_t  type;
    uint8_t  pad[5];
    union {
        struct {
            uint64_t nbytes;
            uint16_t flags;
            uint8_t  pad[6];
        } fd_readwrite;
    } u;
} __attribute__((packed)) wasi_event_t;

WASI_API("clock_time_get")
uint32_t wasi_clock_time_get(uint32_t clk_id, uint64_t precision, uint64_t* time_out);

WASI_API("poll_oneoff")
uint32_t wasi_poll_oneoff(const wasi_subscription_t* in_subs, wasi_event_t* out_events, uint32_t nsubscriptions, uint32_t* nevents);

WASI_API("sched_yield")
uint32_t wasi_sched_yield(void);

WASI_API("path_filestat_get")
uint32_t wasi_path_filestat_get(uint32_t dirfd, uint32_t flags, const char* path, uint32_t path_len, wasi_filestat_t* buf);

WASI_API("path_create_directory")
uint32_t wasi_path_create_directory(uint32_t dirfd, const char* path, uint32_t path_len);

WASI_API("path_unlink_file")
uint32_t wasi_path_unlink_file(uint32_t dirfd, const char* path, uint32_t path_len);

WASI_API("path_rename")
uint32_t wasi_path_rename(uint32_t old_fd, const char* old_path, uint32_t old_path_len, uint32_t new_fd, const char* new_path, uint32_t new_path_len);

// Basic libc-like helpers for standalone WASM
__attribute__((used))
void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

__attribute__((used))
void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

__attribute__((used))
void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

static inline size_t strlen(const char* s) {
    size_t len = 0;
    while (s && *s++) len++;
    return len;
}

static inline char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

static inline char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

static inline char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

static inline int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

static inline int strcasecmp_custom(const char* s1, const char* s2) {
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return (int)((unsigned char)c1 - (unsigned char)c2);
        s1++;
        s2++;
    }
    char c1 = *s1;
    char c2 = *s2;
    if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
    if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
    return (int)((unsigned char)c1 - (unsigned char)c2);
}

static inline int is_system_item(const char* name) {
    if (!name || !name[0]) return 0;
    const char* base = name;
    for (const char* p = name; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    if (!base[0]) return 0;

    if (strcasecmp_custom(base, "DESKTOP") == 0) return 1;
    if (strcasecmp_custom(base, "RECYCLE") == 0) return 1;
    if (strcasecmp_custom(base, "Recycle Bin") == 0) return 1;
    if (strcasecmp_custom(base, "RECYCLE BIN") == 0) return 1;
    if (strcasecmp_custom(base, "EXPLORER.WAS") == 0) return 1;
    if (strcasecmp_custom(base, "COMPOSIT.WAS") == 0) return 1;
    if (strcasecmp_custom(base, "CALC.WAS") == 0) return 1;
    if (strcasecmp_custom(base, "NOTEPAD.WAS") == 0) return 1;
    if (strcasecmp_custom(base, "TERM.WAS") == 0) return 1;
    if (strcasecmp_custom(base, "FETCH.WAS") == 0) return 1;
    if (strcasecmp_custom(base, "TESTWASI.WAS") == 0) return 1;
    return 0;
}

static inline void fill_rect(uint32_t* buf, int buf_w, int buf_h, int x, int y, int rw, int rh, uint32_t color) {
    if (!buf) return;
    int x1 = x < 0 ? 0 : x;
    int y1 = y < 0 ? 0 : y;
    int x2 = x + rw > buf_w ? buf_w : x + rw;
    int y2 = y + rh > buf_h ? buf_h : y + rh;
    for (int py = y1; py < y2; py++) {
        uint32_t* row = &buf[py * buf_w];
        for (int px = x1; px < x2; px++) {
            row[px] = color;
        }
    }
}

static inline void draw_rect(uint32_t* buf, int buf_w, int buf_h, int x, int y, int rw, int rh, uint32_t color) {
    fill_rect(buf, buf_w, buf_h, x, y, rw, 1, color);
    fill_rect(buf, buf_w, buf_h, x, y + rh - 1, rw, 1, color);
    fill_rect(buf, buf_w, buf_h, x, y, 1, rh, color);
    fill_rect(buf, buf_w, buf_h, x + rw - 1, y, 1, rh, color);
}

static inline void draw_3d_rect(uint32_t* buf, int buf_w, int buf_h, int x, int y, int rw, int rh, int sunken) {
    uint32_t top_left = sunken ? 0x808080 : 0xFFFFFF;
    uint32_t bot_right = sunken ? 0xFFFFFF : 0x808080;
    fill_rect(buf, buf_w, buf_h, x, y, rw, 1, top_left);
    fill_rect(buf, buf_w, buf_h, x, y, 1, rh, top_left);
    fill_rect(buf, buf_w, buf_h, x, y + rh - 1, rw, 1, bot_right);
    fill_rect(buf, buf_w, buf_h, x + rw - 1, y, 1, rh, bot_right);
}

#endif
