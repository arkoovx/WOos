#ifndef _NETSURF_MATH_H_
#define _NETSURF_MATH_H_

#define M_PI   3.14159265358979323846
#define M_PI_2 1.57079632679489661923

#define isnan(x) __builtin_isnan(x)

static inline float ceilf(float x) {
    int i = (int)x;
    if (x > (float)i) return (float)(i + 1);
    return (float)i;
}

static inline double ceil(double x) {
    long long i = (long long)x;
    if (x > (double)i) return (double)(i + 1);
    return (double)i;
}

static inline double fabs(double x) {
    return (x < 0.0) ? -x : x;
}

static inline float fabsf(float x) {
    return (x < 0.0f) ? -x : x;
}

static inline double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double guess = x >= 1.0 ? x / 2.0 : 1.0;
    for (int i = 0; i < 16; i++) {
        guess = 0.5 * (guess + x / guess);
    }
    return guess;
}

static inline double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    long long q = (long long)(x / y);
    return x - (double)q * y;
}

// Taylor series approximations for sin and cos
static inline double sin(double x) {
    x = fmod(x, 2.0 * M_PI);
    if (x > M_PI) x -= 2.0 * M_PI;
    if (x < -M_PI) x += 2.0 * M_PI;
    double x2 = x * x;
    return x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0)));
}

static inline double cos(double x) {
    return sin(x + M_PI_2);
}

static inline double acos(double x) {
    if (x < -1.0) x = -1.0;
    if (x > 1.0) x = 1.0;
    // Approximating acos(x) = pi/2 - asin(x)
    double asin_approx = x + (x * x * x) / 6.0;
    return M_PI_2 - asin_approx;
}

#endif
