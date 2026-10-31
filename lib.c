#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

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
unsigned long __isoc23_strtoul(const char* nptr, char** endptr, int base) __attribute__((weak, alias("strtoul")));

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
unsigned long long __isoc23_strtoull(const char* nptr, char** endptr, int base) __attribute__((weak, alias("strtoull")));

int __popcountdi2(uint64_t val) {
    int count = 0;
    while (val) {
        count += (val & 1);
        val >>= 1;
    }
    return count;
}

char* strchr(const char* s, int c) {
    while (*s != '\0') {
        if (*s == (char)c) {
            return (char*)s;
        }
        s++;
    }
    if (c == '\0') {
        return (char*)s;
    }
    return NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s != '\0') {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    if (c == '\0') {
        return (char*)s;
    }
    return (char*)last;
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - ('a' - 'A');
    }
    return c;
}

double fabs(double x) {
    union { double d; uint64_t u; } val;
    val.d = x;
    val.u &= ~0x8000000000000000ULL;
    return val.d;
}

float fabsf(float x) {
    union { float f; uint32_t u; } val;
    val.f = x;
    val.u &= ~0x80000000U;
    return val.f;
}

double copysign(double x, double y) {
    union { double d; uint64_t u; } ux, uy;
    ux.d = x;
    uy.d = y;
    ux.u = (ux.u & ~0x8000000000000000ULL) | (uy.u & 0x8000000000000000ULL);
    return ux.d;
}

float copysignf(float x, float y) {
    union { float f; uint32_t u; } ux, uy;
    ux.f = x;
    uy.f = y;
    ux.u = (ux.u & ~0x80000000U) | (uy.u & 0x80000000U);
    return ux.f;
}

double sqrt(double x) {
    double res;
    __asm__ ("sqrtsd %1, %0" : "=x"(res) : "x"(x));
    return res;
}

float sqrtf(float x) {
    float res;
    __asm__ ("sqrtss %1, %0" : "=x"(res) : "x"(x));
    return res;
}

double trunc(double x) {
    union { double d; uint64_t u; } val;
    val.d = x;
    int exp = (int)((val.u >> 52) & 0x7FF) - 1023;
    if (exp < 0) {
        val.u &= 0x8000000000000000ULL;
        return val.d;
    }
    if (exp >= 52) {
        return x;
    }
    uint64_t mask = 0x000FFFFFFFFFFFFFULL >> exp;
    val.u &= ~mask;
    return val.d;
}

float truncf(float x) {
    union { float f; uint32_t u; } val;
    val.f = x;
    int exp = (int)((val.u >> 23) & 0xFF) - 127;
    if (exp < 0) {
        val.u &= 0x80000000U;
        return val.f;
    }
    if (exp >= 23) {
        return x;
    }
    uint32_t mask = 0x007FFFFFU >> exp;
    val.u &= ~mask;
    return val.f;
}

double floor(double x) {
    double t = trunc(x);
    if (x < 0.0 && x != t) {
        return t - 1.0;
    }
    return t;
}

float floorf(float x) {
    float t = truncf(x);
    if (x < 0.0f && x != t) {
        return t - 1.0f;
    }
    return t;
}

double ceil(double x) {
    double t = trunc(x);
    if (x > 0.0 && x != t) {
        return t + 1.0;
    }
    return t;
}

float ceilf(float x) {
    float t = truncf(x);
    if (x > 0.0f && x != t) {
        return t + 1.0f;
    }
    return t;
}

double rint(double x) {
    double t = trunc(x);
    double diff = x - t;
    if (diff < 0.0) diff = -diff;
    if (diff > 0.5) {
        return (x > 0.0) ? (t + 1.0) : (t - 1.0);
    }
    if (diff < 0.5) {
        return t;
    }
    union { double d; uint64_t u; } val;
    val.d = t;
    int exp = (int)((val.u >> 52) & 0x7FF) - 1023;
    if (exp <= 0) {
        return (x > 0.0) ? 0.0 : -0.0;
    }
    int shift = 52 - exp;
    uint64_t bit = (val.u >> shift) & 1ULL;
    if (bit) {
        return (x > 0.0) ? (t + 1.0) : (t - 1.0);
    }
    return t;
}

float rintf(float x) {
    float t = truncf(x);
    float diff = x - t;
    if (diff < 0.0f) diff = -diff;
    if (diff > 0.5f) {
        return (x > 0.0f) ? (t + 1.0f) : (t - 1.0f);
    }
    if (diff < 0.5f) {
        return t;
    }
    union { float f; uint32_t u; } val;
    val.f = t;
    int exp = (int)((val.u >> 23) & 0xFF) - 127;
    if (exp <= 0) {
        return (x > 0.0f) ? 0.0f : -0.0f;
    }
    int shift = 23 - exp;
    uint32_t bit = (val.u >> shift) & 1U;
    if (bit) {
        return (x > 0.0f) ? (t + 1.0f) : (t - 1.0f);
    }
    return t;
}

double strtod(const char* nptr, char** endptr) {
    const char* s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    double sign = 1.0;
    if (*s == '-') { sign = -1.0; s++; }
    else if (*s == '+') { s++; }

    double val = 0.0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10.0 + (double)(*s - '0');
        s++;
    }
    if (*s == '.') {
        s++;
        double factor = 0.1;
        while (*s >= '0' && *s <= '9') {
            val += (double)(*s - '0') * factor;
            factor *= 0.1;
            s++;
        }
    }
    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_sign = 1;
        if (*s == '-') { exp_sign = -1; s++; }
        else if (*s == '+') { s++; }
        int exp = 0;
        while (*s >= '0' && *s <= '9') {
            exp = exp * 10 + (*s - '0');
            s++;
        }
        double mult = 1.0;
        for (int i = 0; i < exp; i++) mult *= 10.0;
        if (exp_sign < 0) val /= mult;
        else val *= mult;
    }
    if (endptr) *endptr = (char*)s;
    return sign * val;
}

int vsnprintf(char* str, size_t size, const char* format, va_list ap) {
    size_t written = 0;
    #define PUT(c) do { \
        if (str && written + 1 < size) { \
            str[written] = (c); \
        } \
        written++; \
    } while (0)

    if (!format) {
        if (str && size > 0) str[0] = '\0';
        return 0;
    }

    while (*format) {
        if (*format != '%') {
            PUT(*format++);
            continue;
        }
        format++;
        if (*format == '\0') break;

        int width = 0;
        int zero_pad = 0;
        if (*format == '0') {
            zero_pad = 1;
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        if (*format == '.') {
            format++;
            while (*format >= '0' && *format <= '9') format++;
        }
        int is_long = 0;
        if (*format == 'l') {
            is_long = 1;
            format++;
            if (*format == 'l') {
                is_long = 2;
                format++;
            }
        } else if (*format == 'z') {
            is_long = 2;
            format++;
        }

        switch (*format) {
            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                while (*s) PUT(*s++);
                break;
            }
            case 'c': {
                char c = (char)va_arg(ap, int);
                PUT(c);
                break;
            }
            case 'd':
            case 'i': {
                int64_t val = (is_long >= 2) ? va_arg(ap, int64_t) :
                              (is_long == 1) ? (int64_t)va_arg(ap, long) :
                                              (int64_t)va_arg(ap, int);
                if (val < 0) {
                    PUT('-');
                    val = -val;
                }
                char buf[32];
                int i = 0;
                if (val == 0) buf[i++] = '0';
                else {
                    while (val > 0) {
                        buf[i++] = '0' + (val % 10);
                        val /= 10;
                    }
                }
                while (width > i) {
                    PUT(zero_pad ? '0' : ' ');
                    width--;
                }
                for (int j = i - 1; j >= 0; j--) PUT(buf[j]);
                break;
            }
            case 'u': {
                uint64_t val = (is_long >= 2) ? va_arg(ap, uint64_t) :
                               (is_long == 1) ? (uint64_t)va_arg(ap, unsigned long) :
                                               (uint64_t)va_arg(ap, unsigned int);
                char buf[32];
                int i = 0;
                if (val == 0) buf[i++] = '0';
                else {
                    while (val > 0) {
                        buf[i++] = '0' + (val % 10);
                        val /= 10;
                    }
                }
                while (width > i) {
                    PUT(zero_pad ? '0' : ' ');
                    width--;
                }
                for (int j = i - 1; j >= 0; j--) PUT(buf[j]);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t val = (is_long >= 2) ? va_arg(ap, uint64_t) :
                               (is_long == 1) ? (uint64_t)va_arg(ap, unsigned long) :
                                               (uint64_t)va_arg(ap, unsigned int);
                char buf[32];
                int i = 0;
                const char* hex_digits = (*format == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                if (val == 0) buf[i++] = '0';
                else {
                    while (val > 0) {
                        buf[i++] = hex_digits[val % 16];
                        val /= 16;
                    }
                }
                while (width > i) {
                    PUT(zero_pad ? '0' : ' ');
                    width--;
                }
                for (int j = i - 1; j >= 0; j--) PUT(buf[j]);
                break;
            }
            case 'p': {
                uint64_t val = (uint64_t)va_arg(ap, void*);
                PUT('0');
                PUT('x');
                char buf[32];
                int i = 0;
                if (val == 0) buf[i++] = '0';
                else {
                    while (val > 0) {
                        buf[i++] = "0123456789abcdef"[val % 16];
                        val /= 16;
                    }
                }
                for (int j = i - 1; j >= 0; j--) PUT(buf[j]);
                break;
            }
            case '%': {
                PUT('%');
                break;
            }
            default: {
                PUT('%');
                PUT(*format);
                break;
            }
        }
        format++;
    }

    if (str && size > 0) {
        if (written < size) {
            str[written] = '\0';
        } else {
            str[size - 1] = '\0';
        }
    }
    #undef PUT
    return (int)written;
}

int snprintf(char* str, size_t size, const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    int res = vsnprintf(str, size, format, ap);
    va_end(ap);
    return res;
}
