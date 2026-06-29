#include "wasi.h"
#include "serial.h"
#include "vfs.h"
#include "net_socket.h"

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
            m3ApiReturn(8); // BADF
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
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(wasi_iovec_t*, iovs)
    m3ApiGetArg(uint32_t, iovs_len)
    m3ApiGetArgMem(uint32_t*, nread)

    m3ApiCheckMem(iovs, iovs_len * sizeof(wasi_iovec_t));
    m3ApiCheckMem(nread, sizeof(uint32_t));

    if (fd < 4) {
        m3ApiReturn(8); // BADF
    }

    uint32_t total = 0;
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
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_fd_close) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    
    if (fd >= 4) {
        extern void vfs_close(int32_t handle);
        vfs_close((int32_t)(fd - 4));
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_fd_prestat_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(uint8_t*, prestat)
    m3ApiCheckMem(prestat, 8);

    if (fd == 3) {
        prestat[0] = 0; // prestat_dir
        *(uint32_t*)&prestat[4] = 1; // len of "/"
        m3ApiReturn(0);
    }
    m3ApiReturn(8); // BADF
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
            m3ApiReturn(0);
        }
        m3ApiReturn(25); // ENOSPC
    }
    m3ApiReturn(8); // BADF
}

m3ApiRawFunction(wasi_fd_fdstat_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArgMem(uint8_t*, fdstat)
    m3ApiCheckMem(fdstat, 24);
    
    for (int i = 0; i < 24; i++) fdstat[i] = 0;
    
    if (fd == 1 || fd == 2) {
        fdstat[0] = 2; // character device
        *(uint64_t*)&fdstat[8] = 64; // fd_write
    } else if (fd == 3) {
        fdstat[0] = 3; // directory
        *(uint64_t*)&fdstat[8] = 0x3F01FULL; // directory rights
    } else if (fd >= 4) {
        fdstat[0] = 4; // regular file
        *(uint64_t*)&fdstat[8] = 0x3F01FULL; // file rights
    } else {
        m3ApiReturn(8); // BADF
    }
    m3ApiReturn(0);
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
        m3ApiReturn(44); // ENAMETOOLONG
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
        m3ApiReturn(44); // ENOENT (typically 44, or general error)
    }

    m3ApiWriteMem32(fd_out, (uint32_t)(vfs_h + 4));
    m3ApiReturn(0);
}

m3ApiRawFunction(wasi_args_sizes_get) {
    m3ApiReturnType(uint32_t)
    m3ApiGetArgMem(uint32_t*, argc)
    m3ApiGetArgMem(uint32_t*, argv_buf_size)
    m3ApiCheckMem(argc, 4);
    m3ApiCheckMem(argv_buf_size, 4);
    
    m3ApiWriteMem32(argc, 3);
    m3ApiWriteMem32(argv_buf_size, 19);
    m3ApiReturn(0);
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
    m3ApiGetArg(uint32_t, fd)
    m3ApiGetArg(int64_t, offset)
    m3ApiGetArg(uint32_t, whence)
    m3ApiGetArgMem(uint64_t*, newoffset)

    m3ApiCheckMem(newoffset, 8);

    if (fd < 4) {
        m3ApiReturn(8); // BADF
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
        m3ApiReturn(28); // EINVAL
    }

    if (vfs_seek((int32_t)(fd - 4), target) < 0) {
        m3ApiReturn(28); // EINVAL
    }

    m3ApiWriteMem64(newoffset, (uint64_t)target);
    m3ApiReturn(0);
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
    M3Result res = link_raw_func(module, "wasi_snapshot_preview1", "fd_write", "i(i*i*)", &wasi_fd_write);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "proc_exit", "v(i)", &wasi_proc_exit);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "clock_time_get", "i(iI*)", &wasi_clock_time_get);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_read", "i(i*i*)", &wasi_fd_read);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_close", "i(i)", &wasi_fd_close);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_prestat_get", "i(i*)", &wasi_fd_prestat_get);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_prestat_dir_name", "i(i*i)", &wasi_fd_prestat_dir_name);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_fdstat_get", "i(i*)", &wasi_fd_fdstat_get);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "path_open", "i(ii*iiIIi*)", &wasi_path_open);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "args_sizes_get", "i(**)", &wasi_args_sizes_get);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "args_get", "i(**)", &wasi_args_get);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "random_get", "i(*i)", &wasi_random_get);
    if (res) return res;

    res = link_raw_func(module, "wasi_snapshot_preview1", "fd_seek", "i(iIi*)", &wasi_fd_seek);
    if (res) return res;

    // Ссылка на сетевые функции WoOS в пространстве env
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

    return m3Err_none;
}
