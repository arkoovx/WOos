#pragma once
#include <stddef.h>

static inline int abs(int x) { return x < 0 ? -x : x; }

static inline void qsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *)) {
    char *b = (char *)base;
    for (size_t i = 1; i < nel; i++) {
        for (size_t j = i; j > 0; j--) {
            char *p1 = b + (j - 1) * width;
            char *p2 = b + j * width;
            if (compar(p1, p2) > 0) {
                for (size_t k = 0; k < width; k++) {
                    char tmp = p1[k];
                    p1[k] = p2[k];
                    p2[k] = tmp;
                }
            } else {
                break;
            }
        }
    }
}
