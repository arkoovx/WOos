#ifndef _NETSURF_STDDEF_H_
#define _NETSURF_STDDEF_H_

typedef unsigned long size_t;
typedef signed long   ptrdiff_t;

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

#endif
