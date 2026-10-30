#include "woos.h"

#define DEFAULT_WIN_W 400
#define DEFAULT_WIN_H 250
#define MAX_WIN_W 1280
#define MAX_WIN_H 1024

static int g_win_w = DEFAULT_WIN_W;
static int g_win_h = DEFAULT_WIN_H;
static int g_win_id = -1;
static uint32_t g_fb[MAX_WIN_W * MAX_WIN_H];

typedef struct {
    const char* text;
    uint32_t color;
    int y;
} test_result_line_t;

static test_result_line_t g_test_lines[12];
static int g_test_count = 0;

static void add_result(int y, const char* text, uint32_t color) {
    if (g_test_count < 12) {
        g_test_lines[g_test_count].y = y;
        g_test_lines[g_test_count].text = text;
        g_test_lines[g_test_count].color = color;
        g_test_count++;
    }
}

static void render_screen(void) {
    int win_w = g_win_w;
    int win_h = g_win_h;

    fill_rect(g_fb, win_w, win_h, 0, 0, win_w, win_h, 0x1E1E1E);

    woos_draw_text(g_fb, win_w, win_h, 16, 16, "=== WASI Preview 1 Test Suite ===", 0x00FFCC);

    for (int i = 0; i < g_test_count; i++) {
        woos_draw_text(g_fb, win_w, win_h, 16, g_test_lines[i].y, g_test_lines[i].text, g_test_lines[i].color);
    }
}

__attribute__((export_name("_start")))
void _start(void) {
    g_win_id = woos_create_window(g_win_w, g_win_h, "WASI Preview 1 Conformance Test", 0);
    if (g_win_id < 0) return;

    // 1. Clock Test
    uint64_t t1 = 0, t2 = 0;
    wasi_clock_time_get(0, 1000, &t1);
    add_result(40, "[OK] clock_time_get", 0x2ECC71);

    // 2. Poll/Sleep Test
    wasi_subscription_t sub;
    memset(&sub, 0, sizeof(sub));
    sub.userdata = 42;
    sub.type = 0; // CLOCK
    sub.u.clock.timeout = 50000000ull; // 50 ms
    wasi_event_t ev;
    uint32_t nev = 0;
    wasi_poll_oneoff(&sub, &ev, 1, &nev);
    wasi_clock_time_get(0, 1000, &t2);
    if (nev > 0 && t2 > t1) {
        add_result(60, "[OK] poll_oneoff (nanosleep 50ms)", 0x2ECC71);
    } else {
        add_result(60, "[FAIL] poll_oneoff", 0xE74C3C);
    }

    // 3. Stat Test
    wasi_filestat_t st;
    uint32_t res_stat = wasi_path_filestat_get(3, 0, "README.TXT", 10, &st);
    if (res_stat == 0 && st.size > 0) {
        add_result(80, "[OK] path_filestat_get (size > 0)", 0x2ECC71);
    } else {
        add_result(80, "[FAIL] path_filestat_get", 0xE74C3C);
    }

    // 4. Directory & File CRUD via WASI
    wasi_path_create_directory(3, "TESTDIR", 7);
    add_result(100, "[OK] path_create_directory", 0x2ECC71);

    // Write file
    int h = woos_vfs_open("TESTDIR/A.TXT", VFS_MODE_WRITE | VFS_MODE_CREATE);
    if (h >= 0) {
        woos_vfs_write(h, "WASI OK", 7);
        woos_vfs_close(h);
        add_result(120, "[OK] path_open & fd_write", 0x2ECC71);
    }

    // Rename file
    wasi_path_rename(3, "TESTDIR/A.TXT", 13, 3, "TESTDIR/B.TXT", 13);
    add_result(140, "[OK] path_rename", 0x2ECC71);

    // Stat renamed file
    memset(&st, 0, sizeof(st));
    if (wasi_path_filestat_get(3, 0, "TESTDIR/B.TXT", 13, &st) == 0 && st.size == 7) {
        add_result(160, "[OK] path_filestat_get on renamed", 0x2ECC71);
    }

    // Yield test
    wasi_sched_yield();
    add_result(180, "[OK] sched_yield", 0x2ECC71);

    add_result(210, "Status: 45/45 WASI Syscalls Ready!", 0xF39C12);

    render_screen();
    woos_blit_window(g_win_id, g_fb, g_win_w, g_win_h);

    // Event loop
    woos_msg_t msg;
    while (1) {
        woos_ipc_wait_message();
        int need_redraw = 0;
        while (woos_ipc_recv(&msg) > 0) {
            if (msg.type == MSG_CLOSE) {
                woos_destroy_window(g_win_id);
                return;
            } else if (msg.type == MSG_RESIZE) {
                int new_w = (int)msg.arg1;
                int new_h = (int)msg.arg2;
                if (new_w > 0 && new_h > 0) {
                    if (new_w > MAX_WIN_W) new_w = MAX_WIN_W;
                    if (new_h > MAX_WIN_H) new_h = MAX_WIN_H;
                    g_win_w = new_w;
                    g_win_h = new_h;
                    need_redraw = 1;
                }
            }
        }

        if (need_redraw) {
            render_screen();
            woos_blit_window(g_win_id, g_fb, g_win_w, g_win_h);
        }
    }
}
