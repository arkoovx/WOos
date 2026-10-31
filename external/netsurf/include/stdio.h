#ifndef _NETSURF_STDIO_H_
#define _NETSURF_STDIO_H_

#include "stddef.h"
#include "apps/woos.h"

#define EOF (-1)

typedef struct FILE FILE;

#define stderr ((FILE*)2)
#define stdout ((FILE*)1)
#define stdin  ((FILE*)0)

static inline int snprintf(char* str, size_t size, const char* format, ...) {
    // Basic formatting fallback using woos_draw_text or int_to_str
    if (size == 0) return 0;
    str[0] = '\0';
    return 0;
}

static inline int sprintf(char* str, const char* format, ...) {
    if (str) str[0] = '\0';
    return 0;
}

static inline int printf(const char* format, ...) {
    return 0;
}

static inline int fprintf(FILE* stream, const char* format, ...) {
    return 0;
}

#endif
