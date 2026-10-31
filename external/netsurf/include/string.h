#ifndef _NETSURF_STRING_H_
#define _NETSURF_STRING_H_

#include "stddef.h"
#include "stdlib.h"
#include "apps/woos.h"

static inline char* strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char* copy = (char*)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

#endif
