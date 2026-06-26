#ifndef WOOS_LWIP_ARCH_CC_H
#define WOOS_LWIP_ARCH_CC_H

#include <stdint.h>

typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uint64_t  u64_t;
typedef int64_t   s64_t;
typedef uintptr_t mem_ptr_t;

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#define LWIP_PLATFORM_DIAG(x) do { } while(0)
#define LWIP_PLATFORM_ASSERT(x) do { } while(0)

#define LWIP_RAND() 0x12345678u

#define LWIP_PLATFORM_HTONS(x) __builtin_bswap16(x)
#define LWIP_PLATFORM_HTONL(x) __builtin_bswap32(x)

#endif
