#ifndef _STDLIB_H_
#define _STDLIB_H_
#include <stddef.h>
void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);
void* realloc(void* ptr, size_t size);
void abort(void);
int abs(int j);
long labs(long j);
int atoi(const char* nptr);
double strtod(const char* nptr, char** endptr);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
#endif
