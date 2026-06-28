#include <stddef.h>
#include <stdint.h>

void* memcpy(void* restrict dest, const void* restrict src, size_t n) {
    unsigned char* d = dest;
    const unsigned char* s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = s1;
    const unsigned char* p2 = s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }
    return len;
}

void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = dest;
    const unsigned char* s = src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dest;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == '\0') break;
    }
    return 0;
}

int atoi(const char* s) {
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}

// Заглушка для ctype (используется lwip ip4addr_aton)
static const unsigned short ctype_b_data[384] = { 0 };
const unsigned short** __ctype_b_loc(void) {
    static const unsigned short* ptr = &ctype_b_data[128];
    static const unsigned short** pptr = &ptr;
    return pptr;
}

extern void* kheap_alloc(size_t size);
extern void* kheap_realloc(void* ptr, size_t size);
extern void kheap_free(void* ptr);

void* malloc(size_t size) {
    return kheap_alloc(size);
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = kheap_alloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    return kheap_realloc(ptr, size);
}

void free(void* ptr) {
    kheap_free(ptr);
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

extern void serial_printf(const char* format, ...);

void abort(void) {
    serial_printf("abort() called! Halting CPU.\n");
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    (void)base;
    unsigned long res = 0;
    while (*nptr >= '0' && *nptr <= '9') {
        res = res * 10 + (*nptr - '0');
        nptr++;
    }
    if (endptr) *endptr = (char*)nptr;
    return res;
}

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    (void)base;
    unsigned long long res = 0;
    while (*nptr >= '0' && *nptr <= '9') {
        res = res * 10 + (*nptr - '0');
        nptr++;
    }
    if (endptr) *endptr = (char*)nptr;
    return res;
}

int __popcountdi2(uint64_t val) {
    int count = 0;
    while (val) {
        count += (val & 1);
        val >>= 1;
    }
    return count;
}
