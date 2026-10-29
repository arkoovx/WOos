#ifndef WOOS_APP_H
#define WOOS_APP_H

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef signed int     int32_t;
typedef unsigned long  size_t;

#define NULL ((void*)0)

// IPC Message types
#define MSG_CLICK        1
#define MSG_CLOSE        3
#define MSG_KEY_DOWN     4

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

// Basic libc-like helpers for standalone WASM
static inline void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static inline void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dest;
}

static inline void* memmove(void* dest, const void* src, size_t n) {
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

#endif
