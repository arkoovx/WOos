#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <setjmp.h>
#include <time.h>
#include <math.h>

/* --- Memory Allocator (similar to apps/woos_mem.h) --- */
#define HEAP_SIZE (16 * 1024 * 1024)

typedef struct block_header {
    uint32_t size;
    struct block_header* next;
    uint8_t used;
    uint8_t _pad[7];
} block_header_t;

static uint8_t s_heap[HEAP_SIZE];
static block_header_t* s_heap_head = NULL;

static void heap_init(void) {
    if (!s_heap_head) {
        s_heap_head = (block_header_t*)s_heap;
        s_heap_head->size = HEAP_SIZE;
        s_heap_head->next = NULL;
        s_heap_head->used = 0;
    }
}

void* malloc(size_t size) {
    if (size == 0) return NULL;
    heap_init();

    uint32_t need = (uint32_t)((size + 7) & ~7);
    uint32_t min_split = (uint32_t)sizeof(block_header_t) + 8;

    for (block_header_t* cur = s_heap_head; cur != NULL; cur = cur->next) {
        if (cur->used) continue;
        if (cur->size <= sizeof(block_header_t)) continue;

        uint32_t payload = cur->size - (uint32_t)sizeof(block_header_t);
        if (payload < need) continue;

        if (payload >= need + min_split) {
            block_header_t* next_block = (block_header_t*)((uint8_t*)cur + sizeof(block_header_t) + need);
            next_block->size = cur->size - sizeof(block_header_t) - need;
            next_block->used = 0;
            next_block->next = cur->next;

            cur->size = sizeof(block_header_t) + need;
            cur->next = next_block;
        }

        cur->used = 1;
        return (void*)((uint8_t*)cur + sizeof(block_header_t));
    }
    return NULL;
}

void free(void* ptr) {
    if (!ptr) return;
    block_header_t* cur = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    cur->used = 0;

    for (block_header_t* b = s_heap_head; b != NULL; b = b->next) {
        while (b->next && !b->used && !b->next->used &&
               (uint8_t*)b + b->size == (uint8_t*)b->next) {
            b->size += b->next->size;
            b->next = b->next->next;
        }
    }
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) {
        free(ptr);
        return NULL;
    }
    block_header_t* cur = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    uint32_t old_payload = cur->size - (uint32_t)sizeof(block_header_t);
    if (old_payload >= new_size) return ptr;

    void* new_ptr = malloc(new_size);
    if (new_ptr) {
        size_t copy_sz = old_payload < new_size ? old_payload : new_size;
        uint8_t* dst = (uint8_t*)new_ptr;
        const uint8_t* src = (const uint8_t*)ptr;
        for (size_t i = 0; i < copy_sz; i++) dst[i] = src[i];
        free(ptr);
    }
    return new_ptr;
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* p = malloc(total);
    if (p) {
        uint8_t* bp = (uint8_t*)p;
        for (size_t i = 0; i < total; i++) bp[i] = 0;
    }
    return p;
}

/* --- Standard Memory & String Functions --- */
void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* memset(void* dst, int val, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)val;
    return dst;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return (int)p1[i] - (int)p2[i];
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (int)(unsigned char)s1[i] - (int)(unsigned char)s2[i];
        if (s1[i] == '\0') break;
    }
    return 0;
}

/* --- Control Flow and Error Handling --- */
int setjmp(jmp_buf env) {
    (void)env;
    return 0;
}

void longjmp(jmp_buf env, int val) {
    (void)env;
    (void)val;
    __builtin_trap();
}

void abort(void) {
    __builtin_trap();
}

int abs(int j) {
    return j < 0 ? -j : j;
}

long labs(long j) {
    return j < 0 ? -j : j;
}

/* --- Date / Time Stubs --- */
time_t time(time_t* tloc) {
    if (tloc) *tloc = 0;
    return 0;
}

static struct tm s_tm_inst = {0};

struct tm* gmtime(const time_t* timep) {
    (void)timep;
    return &s_tm_inst;
}

struct tm* gmtime_r(const time_t* timep, struct tm* result) {
    (void)timep;
    if (result) {
        memset(result, 0, sizeof(struct tm));
        return result;
    }
    return &s_tm_inst;
}

struct tm* localtime(const time_t* timep) {
    (void)timep;
    return &s_tm_inst;
}

struct tm* localtime_r(const time_t* timep, struct tm* result) {
    (void)timep;
    if (result) {
        memset(result, 0, sizeof(struct tm));
        return result;
    }
    return &s_tm_inst;
}

time_t mktime(struct tm* tm) {
    (void)tm;
    return 0;
}

double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}

/* --- Formatted Output (vsnprintf, snprintf, sprintf, sscanf) --- */
static int write_str(char* buf, size_t sz, int* pos, const char* s) {
    int count = 0;
    while (*s) {
        if ((size_t)*pos + 1 < sz) {
            buf[*pos] = *s;
        }
        (*pos)++;
        s++;
        count++;
    }
    return count;
}

static int write_int64(char* buf, size_t sz, int* pos, int64_t val, int base, int is_unsigned) {
    char tmp[32];
    int idx = 0;
    uint64_t uval;
    if (!is_unsigned && val < 0) {
        if ((size_t)*pos + 1 < sz) buf[*pos] = '-';
        (*pos)++;
        uval = (uint64_t)(-val);
    } else {
        uval = (uint64_t)val;
    }

    if (uval == 0) {
        tmp[idx++] = '0';
    } else {
        while (uval > 0) {
            int rem = uval % base;
            tmp[idx++] = (rem < 10) ? ('0' + rem) : ('a' + rem - 10);
            uval /= base;
        }
    }

    int count = 0;
    for (int i = idx - 1; i >= 0; i--) {
        if ((size_t)*pos + 1 < sz) buf[*pos] = tmp[i];
        (*pos)++;
        count++;
    }
    return count;
}

int vsnprintf(char* buf, size_t sz, const char* fmt, va_list ap) {
    if (!fmt) return 0;
    int pos = 0;

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            int is_long = 0;
            while (*fmt == 'l') { is_long++; fmt++; }

            if (*fmt == 'd' || *fmt == 'i') {
                int64_t val = (is_long >= 2) ? va_arg(ap, long long) :
                              (is_long == 1) ? va_arg(ap, long) : va_arg(ap, int);
                write_int64(buf, sz, &pos, val, 10, 0);
            } else if (*fmt == 'u') {
                uint64_t val = (is_long >= 2) ? va_arg(ap, unsigned long long) :
                               (is_long == 1) ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                write_int64(buf, sz, &pos, (int64_t)val, 10, 1);
            } else if (*fmt == 'x' || *fmt == 'X') {
                uint64_t val = (is_long >= 2) ? va_arg(ap, unsigned long long) :
                               (is_long == 1) ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                write_int64(buf, sz, &pos, (int64_t)val, 16, 1);
            } else if (*fmt == 'p') {
                void* p = va_arg(ap, void*);
                write_str(buf, sz, &pos, "0x");
                write_int64(buf, sz, &pos, (uintptr_t)p, 16, 1);
            } else if (*fmt == 's') {
                const char* s = va_arg(ap, const char*);
                write_str(buf, sz, &pos, s ? s : "(null)");
            } else if (*fmt == 'c') {
                char c = (char)va_arg(ap, int);
                if ((size_t)pos + 1 < sz) buf[pos] = c;
                pos++;
            } else if (*fmt == '%') {
                if ((size_t)pos + 1 < sz) buf[pos] = '%';
                pos++;
            } else {
                if ((size_t)pos + 1 < sz) buf[pos] = *fmt;
                pos++;
            }
        } else {
            if ((size_t)pos + 1 < sz) buf[pos] = *fmt;
            pos++;
        }
        fmt++;
    }

    if (sz > 0) {
        if ((size_t)pos < sz) buf[pos] = '\0';
        else buf[sz - 1] = '\0';
    }
    return pos;
}

int snprintf(char* buf, size_t sz, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int res = vsnprintf(buf, sz, fmt, ap);
    va_end(ap);
    return res;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int res = vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);
    return res;
}

int sscanf(const char* str, const char* fmt, ...) {
    // Duktape only calls sscanf for pointer parsing: sscanf(str, "%p", &ptr)
    if (!str || !fmt) return 0;
    va_list ap;
    va_start(ap, fmt);
    if (fmt[0] == '%' && fmt[1] == 'p') {
        void** out_ptr = va_arg(ap, void**);
        uintptr_t val = 0;
        const char* p = str;
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
        while (*p) {
            char c = *p;
            if (c >= '0' && c <= '9') val = (val << 4) | (c - '0');
            else if (c >= 'a' && c <= 'f') val = (val << 4) | (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') val = (val << 4) | (c - 'A' + 10);
            else break;
            p++;
        }
        if (out_ptr) *out_ptr = (void*)val;
        va_end(ap);
        return 1;
    }
    va_end(ap);
    return 0;
}

/* --- Math Functions --- */
double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    long long q = (long long)(x / y);
    return x - (double)q * y;
}

double sin(double x) {
    x = fmod(x, 2.0 * M_PI);
    if (x > M_PI) x -= 2.0 * M_PI;
    if (x < -M_PI) x += 2.0 * M_PI;
    double x2 = x * x;
    return x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0)));
}

double cos(double x) {
    return sin(x + M_PI_2);
}

double tan(double x) {
    double c = cos(x);
    if (c == 0.0) return 0.0;
    return sin(x) / c;
}

double atan(double x) {
    // Polynomial approximation
    double x2 = x * x;
    return x / (1.0 + 0.28 * x2);
}

double atan2(double y, double x) {
    if (x > 0.0) return atan(y / x);
    if (x < 0.0 && y >= 0.0) return atan(y / x) + M_PI;
    if (x < 0.0 && y < 0.0) return atan(y / x) - M_PI;
    if (x == 0.0 && y > 0.0) return M_PI_2;
    if (x == 0.0 && y < 0.0) return -M_PI_2;
    return 0.0;
}

double asin(double x) {
    if (x < -1.0) x = -1.0;
    if (x > 1.0) x = 1.0;
    return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x) {
    return M_PI_2 - asin(x);
}

double exp(double x) {
    if (x == 0.0) return 1.0;
    if (x < 0.0) return 1.0 / exp(-x);
    // Taylor series: 1 + x + x^2/2! + x^3/3! + ...
    double term = 1.0;
    double sum = 1.0;
    for (int i = 1; i < 25; i++) {
        term *= x / (double)i;
        sum += term;
        if (term < 1e-15) break;
    }
    return sum;
}

double log(double x) {
    if (x <= 0.0) return -1.0 / 0.0; // -inf
    // Bring into [0.5, 1.5] by factoring powers of 2
    int k = 0;
    while (x > 1.5) { x *= 0.5; k++; }
    while (x < 0.5) { x *= 2.0; k--; }
    double z = (x - 1.0) / (x + 1.0);
    double z2 = z * z;
    double term = z;
    double sum = 0.0;
    for (int i = 1; i < 20; i += 2) {
        sum += term / (double)i;
        term *= z2;
    }
    return 2.0 * sum + (double)k * M_LN2;
}

double log2(double x) {
    return log(x) / M_LN2;
}

double log10(double x) {
    return log(x) / M_LN10;
}

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (x == 0.0) return 0.0;
    if (x > 0.0) {
        return exp(y * log(x));
    }
    // Negative base
    long long iy = (long long)y;
    if ((double)iy == y) {
        double p = exp(y * log(-x));
        return (iy & 1) ? -p : p;
    }
    return 0.0 / 0.0; // NaN
}

double cbrt(double x) {
    if (x == 0.0) return 0.0;
    if (x > 0.0) return pow(x, 1.0 / 3.0);
    return -pow(-x, 1.0 / 3.0);
}
