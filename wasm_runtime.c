#include "wasm_runtime.h"
#include "wasm3.h"
#include "wasi.h"
#include "serial.h"
#include "vfs.h"
#include "kheap.h"

static IM3Environment g_wasm_env = 0;
static IM3Runtime g_wasm_runtime = 0;

void wasm_runtime_init(void) {
    g_wasm_env = m3_NewEnvironment();
    if (!g_wasm_env) {
        serial_printf("[WASM] Failed to create environment.\n");
        return;
    }

    // Выделяем рантайм со стеком 64 KB
    g_wasm_runtime = m3_NewRuntime(g_wasm_env, 64 * 1024, 0);
    if (!g_wasm_runtime) {
        serial_printf("[WASM] Failed to create runtime.\n");
        return;
    }

    serial_printf("[WASM] WebAssembly runtime initialized (64KB stack).\n");
}

void wasm_runtime_run(const uint8_t* wasm_bytes, uint32_t wasm_size) {
    if (!g_wasm_runtime) {
        serial_printf("[WASM] Runtime not ready!\n");
        return;
    }

    IM3Module module = 0;
    M3Result res = m3_ParseModule(g_wasm_env, &module, wasm_bytes, wasm_size);
    if (res) {
        serial_printf("[WASM] Parse module error: %s\n", res);
        return;
    }

    res = m3_LoadModule(g_wasm_runtime, module);
    if (res) {
        serial_printf("[WASM] Load module error: %s\n", res);
        m3_FreeModule(module);
        return;
    }

    res = link_wasi(module);
    if (res) {
        serial_printf("[WASM] Link WASI error: %s\n", res);
        return;
    }

    IM3Function start_func = 0;
    res = m3_FindFunction(&start_func, g_wasm_runtime, "_start");
    if (res) {
        // Пробуем main если нет _start
        res = m3_FindFunction(&start_func, g_wasm_runtime, "main");
    }

    if (res) {
        serial_printf("[WASM] Entry point _start or main not found: %s\n", res);
        return;
    }

    serial_printf("[WASM] Running module entry function...\n");
    res = m3_CallV(start_func);
    if (res && res != m3Err_trapExit) {
        M3ErrorInfo info;
        m3_GetErrorInfo(g_wasm_runtime, &info);
        serial_printf("[WASM] Runtime error: %s (%s)\n", res, info.message);
    } else {
        serial_printf("[WASM] Module finished execution successfully.\n");
    }
}

void wasm_runtime_run_file(const char* filepath) {
    serial_printf("[WASM] Loading executable file %s...\n", filepath);
    int32_t fd = vfs_open(filepath, VFS_MODE_READ);
    if (fd < 0) {
        serial_printf("[WASM] Error: failed to open file %s (code %d)\n", filepath, fd);
        return;
    }

    uint32_t size = vfs_size(fd);
    if (size == 0 || size == 0xFFFFFFFFu) {
        serial_printf("[WASM] Error: file %s size is invalid (%u)\n", filepath, size);
        vfs_close(fd);
        return;
    }

    uint8_t* wasm_bytes = (uint8_t*)kheap_alloc(size);
    if (!wasm_bytes) {
        serial_printf("[WASM] Error: failed to allocate %u bytes for module buffer\n", size);
        vfs_close(fd);
        return;
    }

    uint32_t read_bytes = vfs_read(fd, wasm_bytes, size);
    if (read_bytes != size) {
        serial_printf("[WASM] Error: read only %u of %u bytes from file\n", read_bytes, size);
        kheap_free(wasm_bytes);
        vfs_close(fd);
        return;
    }
    vfs_close(fd);

    serial_printf("[WASM] File read successfully (%u bytes). Parsing & Running...\n", size);
    wasm_runtime_run(wasm_bytes, size);
    kheap_free(wasm_bytes);
}
