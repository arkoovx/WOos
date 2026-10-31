#ifndef _NETSURF_STDLIB_H_
#define _NETSURF_STDLIB_H_

#include "stddef.h"

void* woos_malloc(size_t size);
void  woos_free(void* ptr);
void* woos_realloc(void* ptr, size_t size);

static inline void* malloc(size_t size) {
    return woos_malloc(size);
}

static inline void free(void* ptr) {
    woos_free(ptr);
}

static inline void* realloc(void* ptr, size_t size) {
    return woos_realloc(ptr, size);
}

static inline void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* p = woos_malloc(total);
    if (p) {
        char* cp = (char*)p;
        for (size_t i = 0; i < total; i++) cp[i] = 0;
    }
    return p;
}

static inline int abs(int j) {
    return (j < 0) ? -j : j;
}

static inline int atoi(const char* s) {
    int res = 0;
    int sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res * sign;
}

#endif
