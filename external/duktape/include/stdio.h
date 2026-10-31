#ifndef _STDIO_H_
#define _STDIO_H_
#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)
typedef struct FILE FILE;
#define stdin  ((FILE*)0)
#define stdout ((FILE*)1)
#define stderr ((FILE*)2)

int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int sscanf(const char *str, const char *format, ...);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
#endif
