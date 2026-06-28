#include "wasi.h"
#include "serial.h"

// Структура WASI iovec
typedef struct wasi_iovec_t {
    uint32_t buf;
    uint32_t buf_len;
} wasi_iovec_t;

m3ApiRawFunction(wasi_fd_write) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, iovs)
    m3ApiGetArg(uint32_t, iovs_len)
    m3ApiGetArgMem(uint32_t*, nwritten)

    m3ApiCheckMem(iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nwritten, sizeof(uint32_t));

    uint32_t total = 0;
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
        }
    }

    m3ApiWriteMem32(nwritten, total);
    m3ApiReturn(0); // success
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

    extern uint64_t timer_ticks(void);
    m3ApiWriteMem64(time_out, timer_ticks() * 1000000ull); // ms to ns
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_fd_read) {
    m3ApiReturnType(uint32_t)
    m3ApiReturn(58); // __WASI_ERRNO_NOSYS = 58
}

m3ApiRawFunction(wasi_fd_close) {
    m3ApiReturnType(uint32_t)
    m3ApiReturn(0); // success
}

m3ApiRawFunction(wasi_fd_prestat_get) {
    m3ApiReturnType(uint32_t)
    m3ApiReturn(8); // __WASI_ERRNO_BADF = 8
}

m3ApiRawFunction(wasi_fd_prestat_dir_name) {
    m3ApiReturnType(uint32_t)
    m3ApiReturn(8); // __WASI_ERRNO_BADF = 8
}

m3ApiRawFunction(wasi_fd_fdstat_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(uint8_t*, fdstat)
    m3ApiCheckMem(fdstat, 24);
    
    (void)fd;
    for (int i = 0; i < 24; i++) fdstat[i] = 0;
    fdstat[0] = 2; // fs_filetype = character device
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_path_open) {
    m3ApiReturnType(uint32_t)
    m3ApiReturn(58); // __WASI_ERRNO_NOSYS = 58
}

m3ApiRawFunction(wasi_args_sizes_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint32_t*, argc)
    m3ApiGetArgMem(uint32_t*, argv_buf_size)
    m3ApiCheckMem(argc, 4);
    m3ApiCheckMem(argv_buf_size, 4);
    
    m3ApiWriteMem32(argc, 0);
    m3ApiWriteMem32(argv_buf_size, 0);
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_args_get) {
    m3ApiReturnType(uint32_t)
    m3ApiReturn(0);
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
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_fd_seek) {
    m3ApiReturnType(uint32_t)
    m3ApiReturn(58); // __WASI_ERRNO_NOSYS = 58
}

M3Result link_wasi(IM3Module module) {
    M3Result res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_write", "i(i*i*)", &wasi_fd_write);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "proc_exit", "v(i)", &wasi_proc_exit);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "clock_time_get", "i(iI*)", &wasi_clock_time_get);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_read", "i(i*i*)", &wasi_fd_read);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_close", "i(i)", &wasi_fd_close);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_prestat_get", "i(i*)", &wasi_fd_prestat_get);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_prestat_dir_name", "i(i*i)", &wasi_fd_prestat_dir_name);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_fdstat_get", "i(i*)", &wasi_fd_fdstat_get);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "path_open", "i(ii*iiIIi*)", &wasi_path_open);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "args_sizes_get", "i(**)", &wasi_args_sizes_get);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "args_get", "i(**)", &wasi_args_get);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "random_get", "i(*i)", &wasi_random_get);
    if (res) return res;

    res = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_seek", "i(iIi*)", &wasi_fd_seek);
    if (res) return res;

    return m3Err_none;
}
