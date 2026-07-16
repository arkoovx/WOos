#include "wasm_runtime.h"
#include "wasm3.h"
#include "wasi.h"
#include "serial.h"
#include "vfs.h"
#include "kheap.h"

void wasm_runtime_init(void) {
    serial_printf("[WASM] WebAssembly runtime system initialized.\n");
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
        serial_printf("[WASM] Error: read only %u of %u bytes\n", read_bytes, size);
        kheap_free(wasm_bytes);
        vfs_close(fd);
        return;
    }
    vfs_close(fd);

    IM3Environment env = m3_NewEnvironment();
    if (!env) {
        serial_printf("[WASM] Failed to create environment.\n");
        kheap_free(wasm_bytes);
        return;
    }

    IM3Runtime runtime = m3_NewRuntime(env, 64 * 1024, 0);
    if (!runtime) {
        serial_printf("[WASM] Failed to create runtime.\n");
        m3_FreeEnvironment(env);
        kheap_free(wasm_bytes);
        return;
    }

    IM3Module module = 0;
    M3Result res = m3_ParseModule(env, &module, wasm_bytes, size);
    if (res) {
        serial_printf("[WASM] Parse module error: %s\n", res);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        kheap_free(wasm_bytes);
        return;
    }

    res = m3_LoadModule(runtime, module);
    if (res) {
        serial_printf("[WASM] Load module error: %s\n", res);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        kheap_free(wasm_bytes);
        return;
    }

    res = link_wasi(module);
    if (res) {
        serial_printf("[WASM] Link WASI error: %s\n", res);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        kheap_free(wasm_bytes);
        return;
    }

    IM3Function start_func = 0;
    res = m3_FindFunction(&start_func, runtime, "_start");
    if (res) {
        res = m3_FindFunction(&start_func, runtime, "main");
    }

    if (res) {
        serial_printf("[WASM] Entry point not found: %s\n", res);
        m3_FreeRuntime(runtime);
        m3_FreeEnvironment(env);
        kheap_free(wasm_bytes);
        return;
    }

    serial_printf("[WASM] Running module entry function...\n");
    res = m3_CallV(start_func);
    if (res && res != m3Err_trapExit) {
        M3ErrorInfo error_info;
        m3_GetErrorInfo(runtime, &error_info);
        serial_printf("[WASM] Runtime error: %s (%s)\n", res, error_info.message);
    } else {
        serial_printf("[WASM] Module finished execution successfully.\n");
    }

    m3_FreeRuntime(runtime);
    m3_FreeEnvironment(env);
    kheap_free(wasm_bytes);
}
