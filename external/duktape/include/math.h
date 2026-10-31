#ifndef _MATH_H_
#define _MATH_H_

#define M_PI   3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_E    2.71828182845904523536
#define M_LN2  0.69314718055994530942
#define M_LN10 2.30258509299404568402

#define isnan(x) __builtin_isnan(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)

static inline double fabs(double x) { return __builtin_fabs(x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline double ceil(double x) { return __builtin_ceil(x); }
static inline float ceilf(float x) { return __builtin_ceilf(x); }
static inline double trunc(double x) { return __builtin_trunc(x); }
static inline float truncf(float x) { return __builtin_truncf(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline float sqrtf(float x) { return __builtin_sqrtf(x); }

double fmod(double x, double y);
double pow(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double exp(double x);
double log(double x);
double log2(double x);
double log10(double x);
double cbrt(double x);

#endif
