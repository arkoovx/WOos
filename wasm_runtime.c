#include "wasm_runtime.h"
#include "wasm3.h"
#include "wasi.h"
#include "serial.h"

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
