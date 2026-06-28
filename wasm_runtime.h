#ifndef WOOS_WASM_RUNTIME_H
#define WOOS_WASM_RUNTIME_H

#include "kernel.h"

void wasm_runtime_init(void);
void wasm_runtime_run(const uint8_t* wasm_bytes, uint32_t wasm_size);

#endif
