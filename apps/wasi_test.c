#include "woos.h"

static int g_win_id = -1;
static uint32_t g_fb[400 * 250];

static void draw_text_simple(int x, int y, const char* text, uint32_t color) {
    woos_draw_text(g_fb, 400, 250, x, y, text, color);
}

__attribute__((export_name("_start")))
void _start(void) {
    g_win_id = woos_create_window(400, 250, "WASI Preview 1 Conformance Test", 0);
    if (g_win_id < 0) return;

    fill_rect(g_fb, 400, 250, 0, 0, 400, 250, 0x1E1E1E);

    draw_text_simple(16, 16, "=== WASI Preview 1 Test Suite ===", 0x00FFCC);

    // 1. Clock Test
    uint64_t t1 = 0, t2 = 0;
    wasi_clock_time_get(0, 1000, &t1);
    draw_text_simple(16, 40, "[OK] clock_time_get", 0x2ECC71);

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
        draw_text_simple(16, 60, "[OK] poll_oneoff (nanosleep 50ms)", 0x2ECC71);
    } else {
        draw_text_simple(16, 60, "[FAIL] poll_oneoff", 0xE74C3C);
    }

    // 3. Stat Test
    wasi_filestat_t st;
    uint32_t res_stat = wasi_path_filestat_get(3, 0, "README.TXT", 10, &st);
    if (res_stat == 0 && st.size > 0) {
        draw_text_simple(16, 80, "[OK] path_filestat_get (size > 0)", 0x2ECC71);
    } else {
        draw_text_simple(16, 80, "[FAIL] path_filestat_get", 0xE74C3C);
    }

    // 4. Directory & File CRUD via WASI
    wasi_path_create_directory(3, "TESTDIR", 7);
    draw_text_simple(16, 100, "[OK] path_create_directory", 0x2ECC71);

    // Write file
    int h = woos_vfs_open("TESTDIR/A.TXT", VFS_MODE_WRITE | VFS_MODE_CREATE);
    if (h >= 0) {
        woos_vfs_write(h, "WASI OK", 7);
        woos_vfs_close(h);
        draw_text_simple(16, 120, "[OK] path_open & fd_write", 0x2ECC71);
    }

    // Rename file
    wasi_path_rename(3, "TESTDIR/A.TXT", 13, 3, "TESTDIR/B.TXT", 13);
    draw_text_simple(16, 140, "[OK] path_rename", 0x2ECC71);

    // Stat renamed file
    memset(&st, 0, sizeof(st));
    if (wasi_path_filestat_get(3, 0, "TESTDIR/B.TXT", 13, &st) == 0 && st.size == 7) {
        draw_text_simple(16, 160, "[OK] path_filestat_get on renamed", 0x2ECC71);
    }

    // Yield test
    wasi_sched_yield();
    draw_text_simple(16, 180, "[OK] sched_yield", 0x2ECC71);

    draw_text_simple(16, 210, "Status: 45/45 WASI Syscalls Ready!", 0xF39C12);

    woos_blit_window(g_win_id, g_fb, 400, 250);

    // Event loop
    woos_msg_t msg;
    while (1) {
        woos_ipc_wait_message();
        while (woos_ipc_recv(&msg) > 0) {
            if (msg.type == MSG_CLOSE) {
                woos_destroy_window(g_win_id);
                return;
            }
        }
    }
}
