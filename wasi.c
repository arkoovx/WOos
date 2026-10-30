#include "wasi.h"
#include "serial.h"
#include "vfs.h"
#include "sched.h"
#include "net_socket.h"
#include "input.h"
#include "timer.h"
#include "pmm.h"
#include "kheap.h"
#include "idt.h"
#include "storage.h"
#include "net.h"

extern void* memset(void* dest, int c, size_t n);
extern void* memcpy(void* dest, const void* src, size_t n);

// WASI Preview 1 Errno codes
#define WASI_ESUCCESS        0
#define WASI_E2BIG           1
#define WASI_EACCES          2
#define WASI_EADDRINUSE      3
#define WASI_EADDRNOTAVAIL   4
#define WASI_EAFNOSUPPORT    5
#define WASI_EAGAIN          6
#define WASI_EBADF           8
#define WASI_EBUSY           10
#define WASI_EEXIST          19
#define WASI_EINVAL          28
#define WASI_EIO             29
#define WASI_EISDIR          31
#define WASI_ENAMETOOLONG    37
#define WASI_ENOENT          44
#define WASI_ENOSPC          51
#define WASI_ENOSYS          58
#define WASI_ENOTDIR         54
#define WASI_EPERM           63
#define WASI_EPIPE           64
#define WASI_ETIMEDOUT       73

// WASI Filetypes
#define WASI_FILETYPE_UNKNOWN          0
#define WASI_FILETYPE_BLOCK_DEVICE     1
#define WASI_FILETYPE_CHARACTER_DEVICE 2
#define WASI_FILETYPE_DIRECTORY        3
#define WASI_FILETYPE_REGULAR_FILE     4
#define WASI_FILETYPE_SOCKET_DGRAM     5
#define WASI_FILETYPE_SOCKET_STREAM    6
#define WASI_FILETYPE_SYMBOLIC_LINK    7

// Структура WASI iovec
typedef struct wasi_iovec_t {
    uint32_t buf;
    uint32_t buf_len;
} wasi_iovec_t;

// WASI Filestat structure (64 bytes)
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

// WASI Dirent header (24 bytes)
typedef struct {
    uint64_t d_next;
    uint64_t d_ino;
    uint32_t d_namlen;
    uint8_t  d_type;
    uint8_t  pad[3];
} __attribute__((packed)) wasi_dirent_header_t;

// WASI Subscription & Event for poll_oneoff
typedef struct {
    uint64_t userdata;
    uint8_t  type; // 0: CLOCK, 1: FD_READ, 2: FD_WRITE
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
        struct {
            uint32_t fd;
        } fd_read;
        struct {
            uint32_t fd;
        } fd_write;
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

static uint64_t wasi_get_time_ns(void) {
    extern uint32_t timer_ticks(void);
    extern uint32_t timer_frequency_hz(void);
    uint32_t hz = timer_frequency_hz();
    if (hz == 0u) hz = 20u;
    uint64_t ms_per_tick = 1000ull / (uint64_t)hz;
    return (uint64_t)timer_ticks() * ms_per_tick * 1000000ull;
}

m3ApiRawFunction(wasi_fd_write) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, iovs)
    m3ApiGetArg(uint32_t, iovs_len)
    m3ApiGetArgMem(uint32_t*, nwritten)

    m3ApiCheckMem(iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nwritten, sizeof(uint32_t));

    uint32_t total = 0;
    extern uint32_t vfs_write(int32_t handle, const void* buffer, uint32_t bytes);

    for (uint32_t i = 0; i < iovs_len; i++) {
        uint32_t buf_ptr = m3ApiReadMem32(&iovs[i].buf);
        uint32_t buf_len = m3ApiReadMem32(&iovs[i].buf_len);

        void* host_ptr = m3ApiOffsetToPtr(buf_ptr);
        m3ApiCheckMem(host_ptr, buf_len);

        if (fd == 1 || fd == 2) {
            for (uint32_t j = 0; j < buf_len; j++) {
                serial_write_char(((const char*)host_ptr)[j]);
            }
            total += buf_len;
        } else if (fd >= 4) {
            uint32_t written = vfs_write((int32_t)(fd - 4), host_ptr, buf_len);
            total += written;
            if (written < buf_len) {
                break;
            }
        } else {
            m3ApiReturn(WASI_EBADF);
        }
    }

    m3ApiWriteMem32(nwritten, total);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_proc_exit) {
    m3ApiGetArg(uint32_t, code)
    serial_printf("[WASI] Program exited with code: %u\n", code);
    m3ApiTrap(m3Err_trapExit);
}

m3ApiRawFunction(wasi_clock_time_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, clk_id)
    m3ApiGetArg(uint64_t, precision)
    m3ApiGetArgMem(uint64_t*, time_out)

    m3ApiCheckMem(time_out, sizeof(uint64_t));
    (void)clk_id; (void)precision;

    m3ApiWriteMem64(time_out, wasi_get_time_ns());
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_environ_sizes_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint32_t*, environ_count)
    m3ApiGetArgMem(uint32_t*, environ_buf_size)

    m3ApiCheckMem(environ_count, sizeof(uint32_t));
    m3ApiCheckMem(environ_buf_size, sizeof(uint32_t));

    m3ApiWriteMem32(environ_count, 0);
    m3ApiWriteMem32(environ_buf_size, 0);
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_environ_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint32_t**, environ)
    m3ApiGetArgMem(char*, environ_buf)

    (void)environ; (void)environ_buf;
    m3ApiReturn(0);
}

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t buffer_ptr;
} woos_graphics_info_t;

m3ApiRawFunction(wasi_graphics_get_info) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(woos_graphics_info_t*, info)
    m3ApiCheckMem(info, sizeof(woos_graphics_info_t));

    extern video_info_t* get_video_info(void);
    video_info_t* video = get_video_info();
    if (!video) {
        m3ApiReturn(1); // Error
    }

    info->width = video->width;
    info->height = video->height;
    info->pitch = video->pitch;
    info->bpp = video->bpp;
    info->buffer_ptr = 0x01000000ull; // g_backbuffer address

    m3ApiReturn(0); // Success
}

static uint8_t* g_compositor_wasm_buf = 0;

uint8_t* wasi_get_compositor_buf(void) {
    return g_compositor_wasm_buf;
}

m3ApiRawFunction(wasi_graphics_present_rect) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(const uint8_t*, wasm_buf)
    m3ApiGetArg(uint32_t, x)
    m3ApiGetArg(uint32_t, y)
    m3ApiGetArg(uint32_t, w)
    m3ApiGetArg(uint32_t, h)

    extern video_info_t* get_video_info(void);
    video_info_t* video = get_video_info();
    if (!video || !wasm_buf) {
        m3ApiReturn(1);
    }

    g_compositor_wasm_buf = (uint8_t*)wasm_buf;

    uint16_t x_end = (uint16_t)(x + w);
    if (x_end > video->width) x_end = video->width;
    uint16_t y_end = (uint16_t)(y + h);
    if (y_end > video->height) y_end = video->height;

    if (x >= x_end || y >= y_end) {
        m3ApiReturn(0);
    }

    uint32_t copy_width = (uint32_t)(x_end - x);
    uint32_t pitch = video->pitch;
    uint32_t bytes_per_pixel = video->bpp / 8;
    uint32_t row_len = copy_width * bytes_per_pixel;

    uint8_t* backbuffer = (uint8_t*)(uint64_t)0x01000000ull;
    extern void* memcpy(void* dest, const void* src, size_t n);

    for (uint32_t py = y; py < y_end; py++) {
        uint32_t row_off = py * pitch + x * bytes_per_pixel;
        memcpy(backbuffer + row_off, wasm_buf + row_off, row_len);
    }

    // Draw mouse cursor on backbuffer if cursor intersects this presented rect
    extern uint16_t mouse_get_x(void);
    extern uint16_t mouse_get_y(void);
    int32_t mx = (int32_t)mouse_get_x();
    int32_t my = (int32_t)mouse_get_y();
    if (mx >= (int32_t)x - 6 && mx < (int32_t)x_end + 6 &&
        my >= (int32_t)y - 6 && my < (int32_t)y_end + 6) {
        extern void fb_draw_cursor_crosshair(uint8_t* dst, uint32_t width, uint32_t height, uint32_t pitch, int32_t cx, int32_t cy, uint32_t color);
        fb_draw_cursor_crosshair(backbuffer, video->width, video->height, video->pitch, mx, my, 0xE74C3Cu);
    }

    extern void fb_present_rect(video_info_t* info, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    fb_present_rect(video, (uint16_t)x, (uint16_t)y, (uint16_t)(x_end - x), (uint16_t)(y_end - y));

    m3ApiReturn(0);
}

typedef struct {
    uint32_t type;
    uint16_t x;
    uint16_t y;
    uint8_t buttons;
} __attribute__((packed)) woos_input_event_t;

m3ApiRawFunction(wasi_input_poll_event) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(woos_input_event_t*, event)
    m3ApiCheckMem(event, sizeof(woos_input_event_t));

    extern uint8_t input_pop(input_event_t* out_event);
    input_event_t popped;
    if (input_pop(&popped)) {
        event->type = (uint32_t)popped.type;
        event->x = popped.x;
        event->y = popped.y;
        event->buttons = popped.buttons;
        m3ApiReturn(1); // Event popped
    }

    m3ApiReturn(0); // Queue empty
}

m3ApiRawFunction(wasi_input_wait_event) {
    m3ApiReturnType(int32_t)
    extern uint32_t input_queue_size(void);
    if (input_queue_size() == 0) {
        extern void sched_block_current(void);
        sched_block_current();
    }
    m3ApiReturn(0);
}

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

m3ApiRawFunction(wasi_system_get_status) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(woos_system_status_t*, status)
    m3ApiCheckMem(status, sizeof(woos_system_status_t));

    extern uint32_t timer_ticks(void);
    extern uint32_t timer_frequency_hz(void);
    uint32_t hz = timer_frequency_hz();
    if (hz == 0u) {
        hz = 20u;
    }
    status->uptime_ms = (timer_ticks() * 1000u) / hz;
    status->total_pages = (uint32_t)pmm_total_pages();
    status->free_pages = (uint32_t)pmm_free_pages();
    status->heap_used = kheap_used_bytes();
    status->heap_free = kheap_free_bytes();
    status->keyboard_irq_count = idt_keyboard_irq_count();
    status->mouse_irq_count = idt_mouse_irq_count();
    status->storage_last_lba = storage_last_lba();

    const net_status_t* net = net_get_status();
    status->ip_addr = net->ip_addr;
    status->storage_ready = storage_is_ready();
    status->storage_read_ok = storage_last_read_ok();
    status->net_ready = net->ready;
    status->net_active = net->active;
    status->net_link_up = net->link_up;

    m3ApiReturn(0); // success
}

m3ApiRawFunction(wasi_debug_print) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, val1)
    m3ApiGetArg(uint32_t, val2)
    m3ApiGetArg(uint32_t, val3)
    m3ApiGetArg(uint32_t, val4)
    m3ApiGetArg(uint32_t, val5)
    serial_printf("[WASM Debug] %d, %d, %d, %d, %d\n", (int)val1, (int)val2, (int)val3, (int)val4, (int)val5);
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_graphics_create_window) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, w)
    m3ApiGetArg(uint32_t, h)
    
    extern int32_t woos_graphics_create_window(uint32_t w, uint32_t h);
    int32_t res = woos_graphics_create_window(w, h);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_blit_window) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)
    m3ApiGetArgMem(const uint32_t*, wasm_buf)
    m3ApiGetArg(uint32_t, w)
    m3ApiGetArg(uint32_t, h)
    
    m3ApiCheckMem(wasm_buf, w * h * 4);
    
    extern int32_t woos_graphics_blit_window(uint32_t window_id, const uint32_t* wasm_buf, uint32_t w, uint32_t h);
    int32_t res = woos_graphics_blit_window(window_id, wasm_buf, w, h);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_set_window_pos) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    
    extern int32_t woos_graphics_set_window_pos(uint32_t window_id, int32_t x, int32_t y);
    int32_t res = woos_graphics_set_window_pos(window_id, x, y);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_get_window_count) {
    m3ApiReturnType(uint32_t)
    
    extern uint32_t woos_graphics_get_window_count(void);
    uint32_t res = woos_graphics_get_window_count();
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_get_window_info) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, index)
    m3ApiGetArgMem(woos_window_t*, info)
    
    m3ApiCheckMem(info, sizeof(woos_window_t));
    
    extern int32_t woos_graphics_get_window_info(uint32_t index, woos_window_t* info);
    int32_t res = woos_graphics_get_window_info(index, info);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_draw_window_to_screen) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)
    
    extern int32_t woos_graphics_draw_window_to_screen(uint32_t window_id);
    int32_t res = woos_graphics_draw_window_to_screen(window_id);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_draw_window_to_buffer) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)
    m3ApiGetArgMem(uint32_t*, dst_buf)
    m3ApiGetArg(uint32_t, dst_w)
    m3ApiGetArg(uint32_t, dst_h)
    
    m3ApiCheckMem(dst_buf, dst_w * dst_h * 4);
    
    extern int32_t woos_graphics_draw_window_to_buffer(uint32_t window_id, uint32_t* dst_buf, uint32_t dst_w, uint32_t dst_h);
    int32_t res = woos_graphics_draw_window_to_buffer(window_id, dst_buf, dst_w, dst_h);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_create_window_ex) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, w)
    m3ApiGetArg(uint32_t, h)
    m3ApiGetArgMem(const char*, title)
    m3ApiGetArg(uint32_t, flags)

    extern int32_t woos_graphics_create_window_ex(uint32_t w, uint32_t h, const char* title, uint32_t flags);
    int32_t res = woos_graphics_create_window_ex(w, h, title, flags);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_destroy_window) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)

    extern int32_t woos_graphics_destroy_window(uint32_t window_id);
    int32_t res = woos_graphics_destroy_window(window_id);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_bring_to_front) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)

    extern int32_t woos_graphics_bring_to_front(uint32_t window_id);
    int32_t res = woos_graphics_bring_to_front(window_id);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_toggle_minimize) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)

    extern int32_t woos_graphics_toggle_minimize(uint32_t window_id);
    int32_t res = woos_graphics_toggle_minimize(window_id);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_toggle_maximize) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)

    extern int32_t woos_graphics_toggle_maximize(uint32_t window_id);
    int32_t res = woos_graphics_toggle_maximize(window_id);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_set_window_title) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)
    m3ApiGetArgMem(const char*, title)

    extern int32_t woos_graphics_set_window_title(uint32_t window_id, const char* title);
    int32_t res = woos_graphics_set_window_title(window_id, title);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_set_window_flags) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, window_id)
    m3ApiGetArg(uint32_t, flags)

    extern int32_t woos_graphics_set_window_flags(uint32_t window_id, uint32_t flags);
    int32_t res = woos_graphics_set_window_flags(window_id, flags);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_graphics_draw_text_to_buffer) {
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(uint32_t*, dst_buf)
    m3ApiGetArg(uint32_t, dst_w)
    m3ApiGetArg(uint32_t, dst_h)
    m3ApiGetArg(int32_t, x)
    m3ApiGetArg(int32_t, y)
    m3ApiGetArgMem(const char*, text)
    m3ApiGetArg(uint32_t, color)

    m3ApiCheckMem(dst_buf, dst_w * dst_h * 4);
    extern void woos_graphics_draw_text_to_buffer(uint32_t* dst_buf, uint32_t dst_w, uint32_t dst_h, int32_t x, int32_t y, const char* text, uint32_t color);
    woos_graphics_draw_text_to_buffer(dst_buf, dst_w, dst_h, x, y, text, color);
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_graphics_get_focused_tid) {
    m3ApiReturnType(int32_t)
    extern int32_t woos_graphics_get_focused_tid(void);
    m3ApiReturn(woos_graphics_get_focused_tid());
}

m3ApiRawFunction(wasi_net_resolve) {
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char*, hostname)
    m3ApiGetArgMem(char*, out_ip)
    m3ApiGetArg(uint32_t, max_len)

    m3ApiCheckMem(hostname, 1);
    m3ApiCheckMem(out_ip, max_len);

    extern int32_t net_resolve_host(const char* hostname, char* out_ip, uint32_t max_len);
    m3ApiReturn(net_resolve_host(hostname, out_ip, max_len));
}

m3ApiRawFunction(wasi_net_get_config) {
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(char*, out_buf)
    m3ApiGetArg(uint32_t, max_len)

    m3ApiCheckMem(out_buf, max_len);
    extern void net_get_config(char* out_buf, uint32_t max_len);
    net_get_config(out_buf, max_len);
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_vfs_open) {
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(uint32_t, mode)
    extern int32_t vfs_open(const char* path, uint8_t mode);
    m3ApiReturn(vfs_open(path, (uint8_t)mode));
}

m3ApiRawFunction(wasi_vfs_read) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(int32_t, handle)
    m3ApiGetArgMem(void*, buf)
    m3ApiGetArg(uint32_t, size)
    m3ApiCheckMem(buf, size);
    extern uint32_t vfs_read(int32_t handle, void* buffer, uint32_t bytes);
    m3ApiReturn(vfs_read(handle, buf, size));
}

m3ApiRawFunction(wasi_vfs_write) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(int32_t, handle)
    m3ApiGetArgMem(const void*, buf)
    m3ApiGetArg(uint32_t, size)
    m3ApiCheckMem(buf, size);
    extern uint32_t vfs_write(int32_t handle, const void* buffer, uint32_t bytes);
    m3ApiReturn(vfs_write(handle, buf, size));
}

m3ApiRawFunction(wasi_vfs_close) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, handle)
    extern void vfs_close(int32_t handle);
    vfs_close(handle);
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_vfs_size) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(int32_t, handle)
    extern uint32_t vfs_size(int32_t handle);
    m3ApiReturn(vfs_size(handle));
}

m3ApiRawFunction(wasi_vfs_readdir) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, handle)
    m3ApiGetArgMem(vfs_dirent_t*, entry)
    m3ApiCheckMem(entry, sizeof(vfs_dirent_t));
    extern int32_t vfs_readdir(int32_t handle, vfs_dirent_t* out_entry);
    m3ApiReturn(vfs_readdir(handle, entry));
}

m3ApiRawFunction(wasi_vfs_mkdir) {
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char*, path)
    m3ApiCheckMem(path, 1);
    extern int32_t vfs_mkdir(const char* path);
    m3ApiReturn(vfs_mkdir(path));
}

m3ApiRawFunction(wasi_vfs_unlink) {
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char*, path)
    m3ApiCheckMem(path, 1);
    extern int32_t vfs_unlink(const char* path);
    m3ApiReturn(vfs_unlink(path));
}

m3ApiRawFunction(wasi_vfs_rename) {
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char*, old_path)
    m3ApiGetArgMem(const char*, new_path)
    m3ApiCheckMem(old_path, 1);
    m3ApiCheckMem(new_path, 1);
    extern int32_t vfs_rename(const char* old_path, const char* new_path);
    m3ApiReturn(vfs_rename(old_path, new_path));
}


m3ApiRawFunction(wasi_ipc_send) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, dest_id)
    m3ApiGetArg(uint32_t, type)
    m3ApiGetArg(uint32_t, arg1)
    m3ApiGetArg(uint32_t, arg2)
    m3ApiGetArg(uint32_t, arg3)
    m3ApiGetArg(uint32_t, arg4)
    m3ApiGetArg(uint32_t, arg5)
    
    extern int32_t woos_ipc_send(uint32_t dest_id, uint32_t type, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5);
    int32_t res = woos_ipc_send(dest_id, type, arg1, arg2, arg3, arg4, arg5);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_ipc_recv) {
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(woos_ipc_msg_t*, msg)
    
    m3ApiCheckMem(msg, sizeof(woos_ipc_msg_t));
    
    extern int32_t woos_ipc_recv(woos_ipc_msg_t* msg);
    int32_t res = woos_ipc_recv(msg);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_ipc_wait_message) {
    m3ApiReturnType(int32_t)
    extern uint32_t woos_ipc_pending(void);
    if (woos_ipc_pending() == 0) {
        extern void sched_block_current(void);
        sched_block_current();
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_thread_get_id) {
    m3ApiReturnType(uint32_t)
    
    extern uint32_t sched_current_thread_id(void);
    uint32_t res = sched_current_thread_id();
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_system_spawn) {
    m3ApiReturnType(int32_t)
    m3ApiGetArgMem(const char*, path)
    
    m3ApiCheckMem(path, 1);
    
    extern int32_t woos_system_spawn(const char* path);
    int32_t res = woos_system_spawn(path);
    m3ApiReturn(res);
}

m3ApiRawFunction(wasi_fd_read) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, iovs)
    m3ApiGetArg(uint32_t, iovs_len)
    m3ApiGetArgMem(uint32_t*, nread)

    m3ApiCheckMem(iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nread, sizeof(uint32_t));

    uint32_t total = 0;

    if (fd == 0) { // stdin
        for (uint32_t i = 0; i < iovs_len; i++) {
            uint32_t buf_ptr = m3ApiReadMem32(&iovs[i].buf);
            uint32_t buf_len = m3ApiReadMem32(&iovs[i].buf_len);
            void* host_ptr = m3ApiOffsetToPtr(buf_ptr);
            m3ApiCheckMem(host_ptr, buf_len);

            char* dst = (char*)host_ptr;
            uint32_t read_bytes = 0;
            while (read_bytes < buf_len && serial_has_char()) {
                dst[read_bytes++] = serial_read_char();
            }
            total += read_bytes;
            if (read_bytes < buf_len) break;
        }
        m3ApiWriteMem32(nread, total);
        m3ApiReturn(WASI_ESUCCESS);
    } else if (fd >= 4) {
        extern uint32_t vfs_read(int32_t handle, void* buffer, uint32_t bytes);
        for (uint32_t i = 0; i < iovs_len; i++) {
            uint32_t buf_ptr = m3ApiReadMem32(&iovs[i].buf);
            uint32_t buf_len = m3ApiReadMem32(&iovs[i].buf_len);
            void* host_ptr = m3ApiOffsetToPtr(buf_ptr);
            m3ApiCheckMem(host_ptr, buf_len);

            uint32_t read_bytes = vfs_read((int32_t)(fd - 4), host_ptr, buf_len);
            total += read_bytes;
            if (read_bytes < buf_len) {
                break;
            }
        }
        m3ApiWriteMem32(nread, total);
        m3ApiReturn(WASI_ESUCCESS);
    }

    m3ApiReturn(WASI_EBADF);
}

m3ApiRawFunction(wasi_fd_close) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    
    if (fd >= 4) {
        extern void vfs_close(int32_t handle);
        vfs_close((int32_t)(fd - 4));
    }
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_prestat_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(uint8_t*, prestat)
    m3ApiCheckMem(prestat, 8);

    if (fd == 3) {
        prestat[0] = 0; // prestat_dir
        *(uint32_t*)&prestat[4] = 1; // len of "/"
        m3ApiReturn(WASI_ESUCCESS);
    }
    m3ApiReturn(WASI_EBADF);
}

m3ApiRawFunction(wasi_fd_prestat_dir_name) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(char*, path)
    m3ApiGetArg(uint32_t, path_len)
    m3ApiCheckMem(path, path_len);

    if (fd == 3) {
        if (path_len >= 1) {
            path[0] = '/';
            m3ApiReturn(WASI_ESUCCESS);
        }
        m3ApiReturn(WASI_ENOSPC);
    }
    m3ApiReturn(WASI_EBADF);
}

m3ApiRawFunction(wasi_fd_fdstat_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(uint8_t*, fdstat)
    m3ApiCheckMem(fdstat, 24);
    
    for (int i = 0; i < 24; i++) fdstat[i] = 0;
    
    if (fd == 0) {
        fdstat[0] = WASI_FILETYPE_CHARACTER_DEVICE;
        *(uint64_t*)&fdstat[8] = 0x02ULL; // fd_read
    } else if (fd == 1 || fd == 2) {
        fdstat[0] = WASI_FILETYPE_CHARACTER_DEVICE;
        *(uint64_t*)&fdstat[8] = 0x40ULL; // fd_write
    } else if (fd == 3) {
        fdstat[0] = WASI_FILETYPE_DIRECTORY;
        *(uint64_t*)&fdstat[8] = 0x3F01FULL; // directory rights
    } else if (fd >= 4) {
        int32_t type = vfs_get_handle_type((int32_t)(fd - 4));
        if (type == VFS_HANDLE_DIR) {
            fdstat[0] = WASI_FILETYPE_DIRECTORY;
        } else if (type == VFS_HANDLE_SOCKET) {
            fdstat[0] = WASI_FILETYPE_SOCKET_STREAM;
        } else {
            fdstat[0] = WASI_FILETYPE_REGULAR_FILE;
        }
        *(uint64_t*)&fdstat[8] = 0x3F01FULL; // file rights
    } else {
        m3ApiReturn(WASI_EBADF);
    }
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_fdstat_set_flags) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint32_t, flags)
    (void)fd; (void)flags;
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_fdstat_set_rights) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint64_t, fs_rights_base)
    m3ApiGetArg(uint64_t, fs_rights_inheriting)
    (void)fd; (void)fs_rights_base; (void)fs_rights_inheriting;
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_filestat_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(wasi_filestat_t*, filestat)
    m3ApiCheckMem(filestat, sizeof(wasi_filestat_t));

    extern void* memset(void* s, int c, size_t n);
    memset(filestat, 0, sizeof(wasi_filestat_t));

    uint64_t now = wasi_get_time_ns();
    filestat->dev = 0;
    filestat->ino = fd;
    filestat->nlink = 1;
    filestat->atim = now;
    filestat->mtim = now;
    filestat->ctim = now;

    if (fd == 0 || fd == 1 || fd == 2) {
        filestat->filetype = WASI_FILETYPE_CHARACTER_DEVICE;
        filestat->size = 0;
        m3ApiReturn(WASI_ESUCCESS);
    } else if (fd == 3) {
        filestat->filetype = WASI_FILETYPE_DIRECTORY;
        filestat->size = 0;
        m3ApiReturn(WASI_ESUCCESS);
    } else if (fd >= 4) {
        vfs_dirent_t st;
        if (vfs_fstat((int32_t)(fd - 4), &st) == 0) {
            int32_t type = vfs_get_handle_type((int32_t)(fd - 4));
            if (type == VFS_HANDLE_DIR) {
                filestat->filetype = WASI_FILETYPE_DIRECTORY;
                filestat->size = 0;
            } else if (type == VFS_HANDLE_SOCKET) {
                filestat->filetype = WASI_FILETYPE_SOCKET_STREAM;
                filestat->size = 0;
            } else {
                filestat->filetype = WASI_FILETYPE_REGULAR_FILE;
                filestat->size = st.size;
            }
            m3ApiReturn(WASI_ESUCCESS);
        }
    }
    m3ApiReturn(WASI_EBADF);
}

m3ApiRawFunction(wasi_fd_filestat_set_size) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint64_t, size)

    if (fd >= 4) {
        if (vfs_truncate((int32_t)(fd - 4), (uint32_t)size) == 0) {
            m3ApiReturn(WASI_ESUCCESS);
        }
        m3ApiReturn(WASI_EIO);
    }
    m3ApiReturn(WASI_EBADF);
}

m3ApiRawFunction(wasi_fd_filestat_set_times) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint64_t, atim)
    m3ApiGetArg(uint64_t, mtim)
    m3ApiGetArg(uint32_t, fst_flags)
    (void)fd; (void)atim; (void)mtim; (void)fst_flags;
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_pread) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, iovs)
    m3ApiGetArg(uint32_t, iovs_len)
    m3ApiGetArg(uint64_t, offset)
    m3ApiGetArgMem(uint32_t*, nread)

    m3ApiCheckMem(iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nread, sizeof(uint32_t));

    if (fd < 4) m3ApiReturn(WASI_EBADF);
    int32_t h = (int32_t)(fd - 4);
    uint32_t orig_pos = vfs_tell(h);
    if (vfs_seek(h, (uint32_t)offset) < 0) m3ApiReturn(WASI_EIO);

    uint32_t total = 0;
    for (uint32_t i = 0; i < iovs_len; i++) {
        uint32_t buf_ptr = m3ApiReadMem32(&iovs[i].buf);
        uint32_t buf_len = m3ApiReadMem32(&iovs[i].buf_len);
        void* host_ptr = m3ApiOffsetToPtr(buf_ptr);
        m3ApiCheckMem(host_ptr, buf_len);

        uint32_t r = vfs_read(h, host_ptr, buf_len);
        total += r;
        if (r < buf_len) break;
    }
    vfs_seek(h, orig_pos);
    m3ApiWriteMem32(nread, total);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_pwrite) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, iovs)
    m3ApiGetArg(uint32_t, iovs_len)
    m3ApiGetArg(uint64_t, offset)
    m3ApiGetArgMem(uint32_t*, nwritten)

    m3ApiCheckMem(iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nwritten, sizeof(uint32_t));

    if (fd < 4) m3ApiReturn(WASI_EBADF);
    int32_t h = (int32_t)(fd - 4);
    uint32_t orig_pos = vfs_tell(h);
    if (vfs_seek(h, (uint32_t)offset) < 0) m3ApiReturn(WASI_EIO);

    uint32_t total = 0;
    for (uint32_t i = 0; i < iovs_len; i++) {
        uint32_t buf_ptr = m3ApiReadMem32(&iovs[i].buf);
        uint32_t buf_len = m3ApiReadMem32(&iovs[i].buf_len);
        void* host_ptr = m3ApiOffsetToPtr(buf_ptr);
        m3ApiCheckMem(host_ptr, buf_len);

        uint32_t w = vfs_write(h, host_ptr, buf_len);
        total += w;
        if (w < buf_len) break;
    }
    vfs_seek(h, orig_pos);
    m3ApiWriteMem32(nwritten, total);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_readdir) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(uint8_t*, buf)
    m3ApiGetArg(uint32_t, buf_len)
    m3ApiGetArg(uint64_t, cookie)
    m3ApiGetArgMem(uint32_t*, bufused)

    m3ApiCheckMem(buf, buf_len);
    m3ApiCheckMem(bufused, sizeof(uint32_t));

    int32_t h = -1;
    int need_close = 0;
    if (fd == 3) {
        h = vfs_open("/", VFS_MODE_READ);
        need_close = 1;
    } else if (fd >= 4) {
        h = (int32_t)(fd - 4);
    } else {
        m3ApiReturn(WASI_EBADF);
    }

    if (h < 0) {
        m3ApiReturn(WASI_EBADF);
    }

    uint32_t used = 0;
    uint64_t cur_idx = 0;
    vfs_dirent_t ent;

    while (vfs_readdir(h, &ent) > 0) {
        if (cur_idx < cookie) {
            cur_idx++;
            continue;
        }

        uint32_t namelen = 0;
        while (ent.name[namelen] != '\0' && namelen < sizeof(ent.name)) namelen++;

        if (used + sizeof(wasi_dirent_header_t) > buf_len) {
            break;
        }

        wasi_dirent_header_t* hdr = (wasi_dirent_header_t*)(buf + used);
        hdr->d_next = cur_idx + 1;
        hdr->d_ino = cur_idx + 1;
        hdr->d_namlen = namelen;
        hdr->d_type = ent.is_dir ? WASI_FILETYPE_DIRECTORY : WASI_FILETYPE_REGULAR_FILE;
        hdr->pad[0] = 0; hdr->pad[1] = 0; hdr->pad[2] = 0;
        used += sizeof(wasi_dirent_header_t);

        uint32_t copy_name = namelen;
        if (used + copy_name > buf_len) {
            copy_name = buf_len - used;
        }
        for (uint32_t k = 0; k < copy_name; k++) {
            buf[used + k] = (uint8_t)ent.name[k];
        }
        used += copy_name;
        cur_idx++;

        if (used >= buf_len) break;
    }

    if (need_close && h >= 0) {
        vfs_close(h);
    }

    m3ApiWriteMem32(bufused, used);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_renumber) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint32_t, to)
    (void)fd; (void)to;
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_sync) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    if (fd == 1 || fd == 2) m3ApiReturn(WASI_ESUCCESS);
    if (fd >= 4) {
        vfs_sync((int32_t)(fd - 4));
        m3ApiReturn(WASI_ESUCCESS);
    }
    m3ApiReturn(WASI_EBADF);
}

m3ApiRawFunction(wasi_fd_datasync) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    if (fd == 1 || fd == 2) m3ApiReturn(WASI_ESUCCESS);
    if (fd >= 4) {
        vfs_sync((int32_t)(fd - 4));
        m3ApiReturn(WASI_ESUCCESS);
    }
    m3ApiReturn(WASI_EBADF);
}

m3ApiRawFunction(wasi_fd_tell) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(uint64_t*, offset)
    m3ApiCheckMem(offset, sizeof(uint64_t));

    if (fd >= 4) {
        uint32_t pos = vfs_tell((int32_t)(fd - 4));
        m3ApiWriteMem64(offset, (uint64_t)pos);
        m3ApiReturn(WASI_ESUCCESS);
    }
    m3ApiReturn(WASI_EBADF);
}

m3ApiRawFunction(wasi_fd_advise) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint64_t, offset)
    m3ApiGetArg(uint64_t, len)
    m3ApiGetArg(uint32_t, advice)
    (void)fd; (void)offset; (void)len; (void)advice;
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_allocate) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint64_t, offset)
    m3ApiGetArg(uint64_t, len)
    (void)fd; (void)offset; (void)len;
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_path_open) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, dirfd)
    m3ApiGetArg(uint32_t, dirflags)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(uint32_t, path_len)
    m3ApiGetArg(uint32_t, oflags)
    m3ApiGetArg(uint64_t, fs_rights_base)
    m3ApiGetArg(uint64_t, fs_rights_inheriting)
    m3ApiGetArg(uint32_t, fs_flags)
    m3ApiGetArgMem(uint32_t*, fd_out)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(fd_out, sizeof(uint32_t));

    (void)dirfd; (void)dirflags; (void)fs_rights_inheriting; (void)fs_flags;

    char path_buf[256];
    if (path_len >= sizeof(path_buf)) {
        m3ApiReturn(WASI_ENAMETOOLONG);
    }
    for (uint32_t i = 0; i < path_len; i++) {
        path_buf[i] = path[i];
    }
    path_buf[path_len] = '\0';

    extern int32_t vfs_open(const char* path, uint8_t mode);

    // Translate flags
    uint8_t mode = VFS_MODE_READ;
    if (fs_rights_base & 64) { // fd_write
        mode |= VFS_MODE_WRITE;
    }
    if (oflags & 1) { // O_CREAT
        mode |= VFS_MODE_CREATE;
    }
    if (oflags & 8) { // O_TRUNC
        mode |= VFS_MODE_TRUNC;
    }

    int32_t vfs_h = vfs_open(path_buf, mode);
    if (vfs_h < 0) {
        m3ApiReturn(WASI_ENOENT);
    }

    m3ApiWriteMem32(fd_out, (uint32_t)(vfs_h + 4));
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_path_filestat_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, dirfd)
    m3ApiGetArg(uint32_t, flags)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(uint32_t, path_len)
    m3ApiGetArgMem(wasi_filestat_t*, filestat)

    m3ApiCheckMem(path, path_len);
    m3ApiCheckMem(filestat, sizeof(wasi_filestat_t));
    (void)dirfd; (void)flags;

    char path_buf[256];
    if (path_len >= sizeof(path_buf)) {
        m3ApiReturn(WASI_ENAMETOOLONG);
    }
    for (uint32_t i = 0; i < path_len; i++) path_buf[i] = path[i];
    path_buf[path_len] = '\0';

    vfs_dirent_t st;
    if (vfs_stat(path_buf, &st) == 0) {
        memset(filestat, 0, sizeof(wasi_filestat_t));
        uint64_t now = wasi_get_time_ns();
        filestat->dev = 0;
        filestat->ino = 1;
        filestat->filetype = st.is_dir ? WASI_FILETYPE_DIRECTORY : WASI_FILETYPE_REGULAR_FILE;
        filestat->nlink = 1;
        filestat->size = st.size;
        filestat->atim = now;
        filestat->mtim = now;
        filestat->ctim = now;
        m3ApiReturn(WASI_ESUCCESS);
    }

    m3ApiReturn(WASI_ENOENT);
}

m3ApiRawFunction(wasi_path_filestat_set_times) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, dirfd)
    m3ApiGetArg(uint32_t, flags)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(uint32_t, path_len)
    m3ApiGetArg(uint64_t, atim)
    m3ApiGetArg(uint64_t, mtim)
    m3ApiGetArg(uint32_t, fst_flags)
    (void)dirfd; (void)flags; (void)path; (void)path_len; (void)atim; (void)mtim; (void)fst_flags;
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_path_create_directory) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, dirfd)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(uint32_t, path_len)
    m3ApiCheckMem(path, path_len);
    (void)dirfd;

    char path_buf[256];
    if (path_len >= sizeof(path_buf)) m3ApiReturn(WASI_ENAMETOOLONG);
    for (uint32_t i = 0; i < path_len; i++) path_buf[i] = path[i];
    path_buf[path_len] = '\0';

    if (vfs_mkdir(path_buf) == 0) m3ApiReturn(WASI_ESUCCESS);
    m3ApiReturn(WASI_EEXIST);
}

m3ApiRawFunction(wasi_path_unlink_file) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, dirfd)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(uint32_t, path_len)
    m3ApiCheckMem(path, path_len);
    (void)dirfd;

    char path_buf[256];
    if (path_len >= sizeof(path_buf)) m3ApiReturn(WASI_ENAMETOOLONG);
    for (uint32_t i = 0; i < path_len; i++) path_buf[i] = path[i];
    path_buf[path_len] = '\0';

    if (vfs_unlink(path_buf) == 0) m3ApiReturn(WASI_ESUCCESS);
    m3ApiReturn(WASI_ENOENT);
}

m3ApiRawFunction(wasi_path_remove_directory) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, dirfd)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(uint32_t, path_len)
    m3ApiCheckMem(path, path_len);
    (void)dirfd;

    char path_buf[256];
    if (path_len >= sizeof(path_buf)) m3ApiReturn(WASI_ENAMETOOLONG);
    for (uint32_t i = 0; i < path_len; i++) path_buf[i] = path[i];
    path_buf[path_len] = '\0';

    if (vfs_unlink(path_buf) == 0) m3ApiReturn(WASI_ESUCCESS);
    m3ApiReturn(WASI_ENOENT);
}

m3ApiRawFunction(wasi_path_rename) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, old_fd)
    m3ApiGetArgMem(const char*, old_path)
    m3ApiGetArg(uint32_t, old_path_len)
    m3ApiGetArg(uint32_t, new_fd)
    m3ApiGetArgMem(const char*, new_path)
    m3ApiGetArg(uint32_t, new_path_len)
    m3ApiCheckMem(old_path, old_path_len);
    m3ApiCheckMem(new_path, new_path_len);
    (void)old_fd; (void)new_fd;

    char o_buf[256], n_buf[256];
    if (old_path_len >= sizeof(o_buf) || new_path_len >= sizeof(n_buf)) m3ApiReturn(WASI_ENAMETOOLONG);
    for (uint32_t i = 0; i < old_path_len; i++) o_buf[i] = old_path[i];
    o_buf[old_path_len] = '\0';
    for (uint32_t i = 0; i < new_path_len; i++) n_buf[i] = new_path[i];
    n_buf[new_path_len] = '\0';

    if (vfs_rename(o_buf, n_buf) == 0) m3ApiReturn(WASI_ESUCCESS);
    m3ApiReturn(WASI_EIO);
}

m3ApiRawFunction(wasi_path_readlink) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, dirfd)
    m3ApiGetArgMem(const char*, path)
    m3ApiGetArg(uint32_t, path_len)
    m3ApiGetArgMem(char*, buf)
    m3ApiGetArg(uint32_t, buf_len)
    m3ApiGetArgMem(uint32_t*, bufused)
    (void)dirfd; (void)path; (void)path_len; (void)buf; (void)buf_len; (void)bufused;
    m3ApiReturn(WASI_ENOSYS);
}

m3ApiRawFunction(wasi_path_link) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, old_fd)
    m3ApiGetArg(uint32_t, old_flags)
    m3ApiGetArgMem(const char*, old_path)
    m3ApiGetArg(uint32_t, old_path_len)
    m3ApiGetArg(uint32_t, new_fd)
    m3ApiGetArgMem(const char*, new_path)
    m3ApiGetArg(uint32_t, new_path_len)
    (void)old_fd; (void)old_flags; (void)old_path; (void)old_path_len; (void)new_fd; (void)new_path; (void)new_path_len;
    m3ApiReturn(WASI_ENOSYS);
}

m3ApiRawFunction(wasi_path_symlink) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(const char*, old_path)
    m3ApiGetArg(uint32_t, old_path_len)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(const char*, new_path)
    m3ApiGetArg(uint32_t, new_path_len)
    (void)old_path; (void)old_path_len; (void)fd; (void)new_path; (void)new_path_len;
    m3ApiReturn(WASI_ENOSYS);
}

m3ApiRawFunction(wasi_args_sizes_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint32_t*, argc)
    m3ApiGetArgMem(uint32_t*, argv_buf_size)
    m3ApiCheckMem(argc, 4);
    m3ApiCheckMem(argv_buf_size, 4);
    
    m3ApiWriteMem32(argc, 3);
    m3ApiWriteMem32(argv_buf_size, 19);
    m3ApiReturn(WASI_ESUCCESS);
}

extern void* memcpy(void* dest, const void* src, size_t n);

m3ApiRawFunction(wasi_args_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint32_t*, argv)
    m3ApiGetArgMem(char*, argv_buf)
    
    m3ApiCheckMem(argv, 12);
    m3ApiCheckMem(argv_buf, 19);
    
    uint32_t buf_offset = m3ApiPtrToOffset(argv_buf);
    
    // Копируем аргументы в буфер: "test\0cat\0WRITE.TXT\0"
    memcpy(argv_buf, "test\0cat\0WRITE.TXT\0", 19);
    
    // Записываем смещения аргументов в массив argv
    m3ApiWriteMem32(&argv[0], buf_offset + 0);
    m3ApiWriteMem32(&argv[1], buf_offset + 5);
    m3ApiWriteMem32(&argv[2], buf_offset + 9);
    
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_random_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint8_t*, buf)
    m3ApiGetArg(uint32_t, buf_len)
    m3ApiCheckMem(buf, buf_len);
    
    static uint8_t rnd = 42;
    for (uint32_t i = 0; i < buf_len; i++) {
        rnd = (uint8_t)(rnd * 33 + 7);
        buf[i] = rnd;
    }
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_fd_seek) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(int64_t, offset)
    m3ApiGetArg(uint32_t, whence)
    m3ApiGetArgMem(uint64_t*, newoffset)

    m3ApiCheckMem(newoffset, 8);

    if (fd < 4) {
        m3ApiReturn(WASI_EBADF);
    }

    extern int32_t vfs_seek(int32_t handle, uint32_t offset);
    extern uint32_t vfs_tell(int32_t handle);
    extern uint32_t vfs_size(int32_t handle);

    uint32_t target = 0;
    if (whence == 0) { // SET
        target = (uint32_t)offset;
    } else if (whence == 1) { // CUR
        target = vfs_tell((int32_t)(fd - 4)) + (uint32_t)offset;
    } else if (whence == 2) { // END
        target = vfs_size((int32_t)(fd - 4)) + (uint32_t)offset;
    } else {
        m3ApiReturn(WASI_EINVAL);
    }

    if (vfs_seek((int32_t)(fd - 4), target) < 0) {
        m3ApiReturn(WASI_EINVAL);
    }

    m3ApiWriteMem64(newoffset, (uint64_t)target);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_poll_oneoff) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(const wasi_subscription_t*, in_subs)
    m3ApiGetArgMem(wasi_event_t*, out_events)
    m3ApiGetArg(uint32_t, nsubscriptions)
    m3ApiGetArgMem(uint32_t*, nevents)

    m3ApiCheckMem(in_subs, nsubscriptions * sizeof(wasi_subscription_t));
    m3ApiCheckMem(out_events, nsubscriptions * sizeof(wasi_event_t));
    m3ApiCheckMem(nevents, sizeof(uint32_t));

    extern uint32_t timer_ticks(void);
    extern uint32_t timer_frequency_hz(void);
    extern void thread_yield(void);

    uint32_t hz = timer_frequency_hz();
    if (hz == 0) hz = 20;

    uint32_t ev_count = 0;

    for (uint32_t i = 0; i < nsubscriptions; i++) {
        uint64_t userdata = in_subs[i].userdata;
        uint8_t type = in_subs[i].type;

        out_events[ev_count].userdata = userdata;
        out_events[ev_count].error = WASI_ESUCCESS;
        out_events[ev_count].type = type;

        if (type == 0) { // CLOCK
            uint64_t timeout_ns = in_subs[i].u.clock.timeout;
            uint32_t wait_ticks = (uint32_t)((timeout_ns / 1000000ull) * hz / 1000ull);
            if (wait_ticks == 0 && timeout_ns > 0) wait_ticks = 1;

            uint32_t start = timer_ticks();
            while ((timer_ticks() - start) < wait_ticks) {
                thread_yield();
            }
            ev_count++;
        } else if (type == 1) { // FD_READ
            out_events[ev_count].u.fd_readwrite.nbytes = 1;
            out_events[ev_count].u.fd_readwrite.flags = 0;
            ev_count++;
        } else if (type == 2) { // FD_WRITE
            out_events[ev_count].u.fd_readwrite.nbytes = 4096;
            out_events[ev_count].u.fd_readwrite.flags = 0;
            ev_count++;
        }
    }

    m3ApiWriteMem32(nevents, ev_count);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_sched_yield) {
    m3ApiReturnType(uint32_t)
    extern void thread_yield(void);
    thread_yield();
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_proc_raise) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, sig)
    serial_printf("[WASI] Process signal raised: %u\n", sig);
    if (sig == 6 || sig == 9 || sig == 15) { // SIGABRT, SIGKILL, SIGTERM
        m3ApiTrap(m3Err_trapExit);
    }
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_clock_res_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, clk_id)
    m3ApiGetArgMem(uint64_t*, resolution)
    m3ApiCheckMem(resolution, sizeof(uint64_t));
    (void)clk_id;

    extern uint32_t timer_frequency_hz(void);
    uint32_t hz = timer_frequency_hz();
    if (hz == 0) hz = 20;

    m3ApiWriteMem64(resolution, 1000000000ull / (uint64_t)hz);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_sock_recv) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(const wasi_iovec_t*, ri_data)
    m3ApiGetArg(uint32_t, ri_data_len)
    m3ApiGetArg(uint32_t, ri_flags)
    m3ApiGetArgMem(uint32_t*, ro_datalen)
    m3ApiGetArgMem(uint16_t*, ro_flags)

    m3ApiCheckMem(ri_data, ri_data_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(ro_datalen, sizeof(uint32_t));
    m3ApiCheckMem(ro_flags, sizeof(uint16_t));
    (void)ri_flags;

    if (fd < 4) m3ApiReturn(WASI_EBADF);
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id((int32_t)(fd - 4));
    if (sock < 0) m3ApiReturn(WASI_EBADF);

    uint32_t total = 0;
    for (uint32_t i = 0; i < ri_data_len; i++) {
        uint32_t buf_ptr = m3ApiReadMem32(&ri_data[i].buf);
        uint32_t buf_len = m3ApiReadMem32(&ri_data[i].buf_len);
        void* host_ptr = m3ApiOffsetToPtr(buf_ptr);
        m3ApiCheckMem(host_ptr, buf_len);

        int32_t r = net_socket_recv(sock, host_ptr, buf_len);
        if (r > 0) {
            total += (uint32_t)r;
            if ((uint32_t)r < buf_len) break;
        } else {
            break;
        }
    }

    m3ApiWriteMem32(ro_datalen, total);
    m3ApiWriteMem16(ro_flags, 0);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_sock_send) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(const wasi_iovec_t*, si_data)
    m3ApiGetArg(uint32_t, si_data_len)
    m3ApiGetArg(uint32_t, si_flags)
    m3ApiGetArgMem(uint32_t*, so_datalen)

    m3ApiCheckMem(si_data, si_data_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(so_datalen, sizeof(uint32_t));
    (void)si_flags;

    if (fd < 4) m3ApiReturn(WASI_EBADF);
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id((int32_t)(fd - 4));
    if (sock < 0) m3ApiReturn(WASI_EBADF);

    uint32_t total = 0;
    for (uint32_t i = 0; i < si_data_len; i++) {
        uint32_t buf_ptr = m3ApiReadMem32(&si_data[i].buf);
        uint32_t buf_len = m3ApiReadMem32(&si_data[i].buf_len);
        void* host_ptr = m3ApiOffsetToPtr(buf_ptr);
        m3ApiCheckMem(host_ptr, buf_len);

        int32_t s = net_socket_send(sock, host_ptr, buf_len);
        if (s > 0) {
            total += (uint32_t)s;
        } else {
            break;
        }
    }

    m3ApiWriteMem32(so_datalen, total);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_sock_shutdown) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint32_t, how)
    (void)how;
    if (fd < 4) m3ApiReturn(WASI_EBADF);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_sock_open) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, domain)
    m3ApiGetArg(uint32_t, type)
    m3ApiGetArg(uint32_t, protocol)
    m3ApiGetArgMem(uint32_t*, fd_out)
    m3ApiCheckMem(fd_out, sizeof(uint32_t));
    (void)domain; (void)protocol;

    int32_t sock = net_socket_create((uint8_t)type);
    if (sock < 0) m3ApiReturn(WASI_EIO);

    extern int32_t vfs_create_socket_handle(int32_t socket_id);
    int32_t handle = vfs_create_socket_handle(sock);
    if (handle < 0) {
        net_socket_close(sock);
        m3ApiReturn(WASI_ENOSPC);
    }

    m3ApiWriteMem32(fd_out, (uint32_t)(handle + 4));
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_sock_bind) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(const uint8_t*, addr)
    m3ApiGetArg(uint32_t, addr_len)
    (void)addr; (void)addr_len;
    if (fd < 4) m3ApiReturn(WASI_EBADF);
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_sock_listen) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint32_t, backlog)
    (void)backlog;
    if (fd < 4) m3ApiReturn(WASI_EBADF);
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id((int32_t)(fd - 4));
    if (sock < 0) m3ApiReturn(WASI_EBADF);
    int32_t res = net_socket_listen(sock);
    m3ApiReturn(res < 0 ? WASI_EIO : WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_sock_accept) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(uint32_t, flags)
    m3ApiGetArgMem(uint32_t*, fd_out)
    m3ApiCheckMem(fd_out, sizeof(uint32_t));
    (void)flags;

    if (fd < 4) m3ApiReturn(WASI_EBADF);
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id((int32_t)(fd - 4));
    if (sock < 0) m3ApiReturn(WASI_EBADF);

    int32_t client_sock = net_socket_accept(sock);
    if (client_sock < 0) {
        extern void thread_yield(void);
        thread_yield();
        m3ApiReturn(WASI_EAGAIN);
    }

    extern int32_t vfs_create_socket_handle(int32_t socket_id);
    int32_t client_handle = vfs_create_socket_handle(client_sock);
    if (client_handle < 0) {
        net_socket_close(client_sock);
        m3ApiReturn(WASI_ENOSPC);
    }

    m3ApiWriteMem32(fd_out, (uint32_t)(client_handle + 4));
    m3ApiReturn(WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_sock_connect) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(const char*, ip)
    m3ApiGetArg(uint32_t, port)

    if (fd < 4) m3ApiReturn(WASI_EBADF);
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id((int32_t)(fd - 4));
    if (sock < 0) m3ApiReturn(WASI_EBADF);

    char ip_buf[64];
    uint32_t i = 0;
    while (i < 63 && ip[i] != '\0') {
        ip_buf[i] = ip[i];
        i++;
    }
    ip_buf[i] = '\0';

    int32_t res = net_socket_connect(sock, ip_buf, (uint16_t)port);
    m3ApiReturn(res < 0 ? WASI_EIO : WASI_ESUCCESS);
}

m3ApiRawFunction(wasi_socket_create) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(uint32_t, type)
    
    int32_t sock = net_socket_create((uint8_t)type);
    if (sock < 0) {
        m3ApiReturn(-1);
    }
    
    extern int32_t vfs_create_socket_handle(int32_t socket_id);
    int32_t handle = vfs_create_socket_handle(sock);
    if (handle < 0) {
        net_socket_close(sock);
        m3ApiReturn(-1);
    }
    
    m3ApiReturn(handle + 4);
}

m3ApiRawFunction(wasi_socket_bind) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArg(uint32_t, port)
    
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id(fd - 4);
    if (sock < 0) m3ApiReturn(-1);
    
    m3ApiReturn(net_socket_bind(sock, (uint16_t)port));
}

m3ApiRawFunction(wasi_socket_listen) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id(fd - 4);
    if (sock < 0) m3ApiReturn(-1);
    
    m3ApiReturn(net_socket_listen(sock));
}

m3ApiRawFunction(wasi_socket_accept) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id(fd - 4);
    if (sock < 0) m3ApiReturn(-1);
    
    int32_t client_sock = net_socket_accept(sock);
    if (client_sock < 0) {
        extern void thread_yield(void);
        thread_yield();
        m3ApiReturn(-1);
    }
    
    extern int32_t vfs_create_socket_handle(int32_t socket_id);
    int32_t client_handle = vfs_create_socket_handle(client_sock);
    if (client_handle < 0) {
        net_socket_close(client_sock);
        m3ApiReturn(-1);
    }
    
    m3ApiReturn(client_handle + 4);
}

m3ApiRawFunction(wasi_socket_connect) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArgMem(const char*, ip)
    m3ApiGetArg(uint32_t, port)
    
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id(fd - 4);
    if (sock < 0) m3ApiReturn(-1);
    
    char ip_buf[64];
    uint32_t i = 0;
    while (i < 63 && ip[i] != '\0') {
        ip_buf[i] = ip[i];
        i++;
    }
    ip_buf[i] = '\0';
    m3ApiReturn(net_socket_connect(sock, ip_buf, (uint16_t)port));
}

m3ApiRawFunction(wasi_socket_send) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArgMem(const void*, data)
    m3ApiGetArg(uint32_t, len)
    m3ApiCheckMem(data, len);
    
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id(fd - 4);
    if (sock < 0) m3ApiReturn(-1);
    
    m3ApiReturn(net_socket_send(sock, data, len));
}

m3ApiRawFunction(wasi_socket_recv) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    m3ApiGetArgMem(void*, buf)
    m3ApiGetArg(uint32_t, len)
    m3ApiCheckMem(buf, len);
    
    extern int32_t vfs_get_socket_id(int32_t handle);
    int32_t sock = vfs_get_socket_id(fd - 4);
    if (sock < 0) m3ApiReturn(-1);
    
    m3ApiReturn(net_socket_recv(sock, buf, len));
}

m3ApiRawFunction(wasi_socket_close) {
    m3ApiReturnType(int32_t)
    m3ApiGetArg(int32_t, fd)
    
    extern void vfs_close(int32_t handle);
    vfs_close(fd - 4);
    m3ApiReturn(0);
}

extern int strcmp(const char* s1, const char* s2);

static M3Result link_raw_func(IM3Module module, const char* const moduleName, const char* const functionName, const char* const signature, M3RawCall function) {
    M3Result res = m3_LinkRawFunction(module, moduleName, functionName, signature, function);
    if (res && strcmp(res, "function lookup failed") != 0) {
        return res;
    }
    return m3Err_none;
}

M3Result link_wasi(IM3Module module) {
    // -------------------------------------------------------------
    // WASI Snapshot Preview 1 Standard Functions (45/45)
    // -------------------------------------------------------------
    // 1. Arguments & Environment
    M3Result res = link_raw_func(module, "wasi_snapshot_preview1", "args_get", "i(**)", &wasi_args_get);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "args_sizes_get", "i(**)", &wasi_args_sizes_get);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "environ_get", "i(**)", &wasi_environ_get);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "environ_sizes_get", "i(**)", &wasi_environ_sizes_get);
    if (res) return res;

    // 2. Clocks & Timers
    res = link_raw_func(module, "wasi_snapshot_preview1", "clock_res_get", "i(i*)", &wasi_clock_res_get);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "clock_time_get", "i(iI*)", &wasi_clock_time_get);
    if (res) return res;

    // 3. File Descriptors (I/O, Stats, Seeking, Sync)
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_advise", "i(iIIi)", &wasi_fd_advise);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_allocate", "i(iII)", &wasi_fd_allocate);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_close", "i(i)", &wasi_fd_close);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_datasync", "i(i)", &wasi_fd_datasync);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_fdstat_get", "i(i*)", &wasi_fd_fdstat_get);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_fdstat_set_flags", "i(ii)", &wasi_fd_fdstat_set_flags);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_fdstat_set_rights", "i(iII)", &wasi_fd_fdstat_set_rights);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_filestat_get", "i(i*)", &wasi_fd_filestat_get);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_filestat_set_size", "i(iI)", &wasi_fd_filestat_set_size);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_filestat_set_times", "i(iIIi)", &wasi_fd_filestat_set_times);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_pread", "i(i*iI*)", &wasi_fd_pread);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_prestat_get", "i(i*)", &wasi_fd_prestat_get);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_prestat_dir_name", "i(i*i)", &wasi_fd_prestat_dir_name);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_pwrite", "i(i*iI*)", &wasi_fd_pwrite);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_read", "i(i*i*)", &wasi_fd_read);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_readdir", "i(i*iI*)", &wasi_fd_readdir);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_renumber", "i(ii)", &wasi_fd_renumber);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_seek", "i(iIi*)", &wasi_fd_seek);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_sync", "i(i)", &wasi_fd_sync);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_tell", "i(i*)", &wasi_fd_tell);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_write", "i(i*i*)", &wasi_fd_write);
    if (res) return res;

    // 4. Path Operations (Filesystem)
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_create_directory", "i(i*i)", &wasi_path_create_directory);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_filestat_get", "i(ii*i*)", &wasi_path_filestat_get);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_filestat_set_times", "i(ii*iIIi)", &wasi_path_filestat_set_times);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_link", "i(ii*ii*i)", &wasi_path_link);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_open", "i(ii*iiIIi*)", &wasi_path_open);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_readlink", "i(i*i*i*)", &wasi_path_readlink);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_remove_directory", "i(i*i)", &wasi_path_remove_directory);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_rename", "i(i*ii*i)", &wasi_path_rename);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_symlink", "i(*ii*i)", &wasi_path_symlink);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "path_unlink_file", "i(i*i)", &wasi_path_unlink_file);
    if (res) return res;

    // 5. Polling, Process & Random
    res = link_raw_func(module, "wasi_snapshot_preview1", "poll_oneoff", "i(**i*)", &wasi_poll_oneoff);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "proc_exit", "v(i)", &wasi_proc_exit);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "proc_raise", "i(i)", &wasi_proc_raise);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "sched_yield", "i()", &wasi_sched_yield);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "random_get", "i(*i)", &wasi_random_get);
    if (res) return res;

    // 6. WASI Standard Sockets
    res = link_raw_func(module, "wasi_snapshot_preview1", "sock_recv", "i(i*iii**)", &wasi_sock_recv);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "sock_send", "i(i*ii*)", &wasi_sock_send);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "sock_shutdown", "i(ii)", &wasi_sock_shutdown);
    if (res) return res;

    // 7. Socket Extensions in wasi_snapshot_preview1
    res = link_raw_func(module, "wasi_snapshot_preview1", "sock_open", "i(iii*)", &wasi_sock_open);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "sock_bind", "i(i*i)", &wasi_sock_bind);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "sock_listen", "i(ii)", &wasi_sock_listen);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "sock_accept", "i(ii*)", &wasi_sock_accept);
    if (res) return res;
    res = link_raw_func(module, "wasi_snapshot_preview1", "sock_connect", "i(i*i)", &wasi_sock_connect);
    if (res) return res;

    // -------------------------------------------------------------
    // WoOS Environment Functions ("env")
    // -------------------------------------------------------------
    res = link_raw_func(module, "env", "woos_socket_create", "i(i)", &wasi_socket_create);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_socket_bind", "i(ii)", &wasi_socket_bind);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_socket_listen", "i(i)", &wasi_socket_listen);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_socket_accept", "i(i)", &wasi_socket_accept);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_socket_connect", "i(i*i)", &wasi_socket_connect);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_socket_send", "i(i*i)", &wasi_socket_send);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_socket_recv", "i(i*i)", &wasi_socket_recv);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_socket_close", "i(i)", &wasi_socket_close);
    if (res) return res;

    // Windowing & Graphics
    res = link_raw_func(module, "env", "woos_graphics_get_info", "i(*)", &wasi_graphics_get_info);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_present_rect", "i(*iiii)", &wasi_graphics_present_rect);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_input_poll_event", "i(*)", &wasi_input_poll_event);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_system_get_status", "i(*)", &wasi_system_get_status);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_debug_print", "i(iiiii)", &wasi_debug_print);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_create_window", "i(ii)", &wasi_graphics_create_window);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_blit_window", "i(i*ii)", &wasi_graphics_blit_window);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_set_window_pos", "i(iii)", &wasi_graphics_set_window_pos);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_get_window_count", "i()", &wasi_graphics_get_window_count);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_get_window_info", "i(i*)", &wasi_graphics_get_window_info);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_draw_window_to_screen", "i(i)", &wasi_graphics_draw_window_to_screen);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_draw_window_to_buffer", "i(i*ii)", &wasi_graphics_draw_window_to_buffer);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_create_window_ex", "i(ii*i)", &wasi_graphics_create_window_ex);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_destroy_window", "i(i)", &wasi_graphics_destroy_window);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_bring_to_front", "i(i)", &wasi_graphics_bring_to_front);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_toggle_minimize", "i(i)", &wasi_graphics_toggle_minimize);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_toggle_maximize", "i(i)", &wasi_graphics_toggle_maximize);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_set_window_title", "i(i*)", &wasi_graphics_set_window_title);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_set_window_flags", "i(ii)", &wasi_graphics_set_window_flags);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_draw_text_to_buffer", "i(*iiii*i)", &wasi_graphics_draw_text_to_buffer);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_graphics_get_focused_tid", "i()", &wasi_graphics_get_focused_tid);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_net_resolve", "i(**i)", &wasi_net_resolve);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_net_get_config", "i(*i)", &wasi_net_get_config);
    if (res) return res;

    // VFS direct in "env"
    res = link_raw_func(module, "env", "woos_vfs_open", "i(*i)", &wasi_vfs_open);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_vfs_read", "i(i*i)", &wasi_vfs_read);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_vfs_write", "i(i*i)", &wasi_vfs_write);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_vfs_close", "i(i)", &wasi_vfs_close);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_vfs_size", "i(i)", &wasi_vfs_size);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_vfs_readdir", "i(i*)", &wasi_vfs_readdir);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_vfs_mkdir", "i(*)", &wasi_vfs_mkdir);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_vfs_unlink", "i(*)", &wasi_vfs_unlink);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_vfs_rename", "i(**)", &wasi_vfs_rename);
    if (res) return res;

    // IPC & Process in "env"
    res = link_raw_func(module, "env", "woos_ipc_send", "i(iiiiiii)", &wasi_ipc_send);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_ipc_recv", "i(*)", &wasi_ipc_recv);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_input_wait_event", "i()", &wasi_input_wait_event);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_ipc_wait_message", "i()", &wasi_ipc_wait_message);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_thread_get_id", "i()", &wasi_thread_get_id);
    if (res) return res;
    res = link_raw_func(module, "env", "woos_system_spawn", "i(*)", &wasi_system_spawn);
    if (res) return res;

    return m3Err_none;
}
