#pragma once

#ifdef _WIN32
#include <stdint.h>
typedef int8_t  s8;
typedef uint8_t  u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;
typedef s64 loff_t;
#else
#include <linux/types.h>
typedef __s8  s8;
typedef __u8  u8;
typedef __s16 s16;
typedef __u16 u16;
typedef __s32 s32;
typedef __u32 u32;
typedef __s64 s64;
typedef __u64 u64;
#endif

#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	((type *)(__mptr - offsetof(type, member))); })

#define BIT(_B) (1 << (_B))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define BITS_PER_LONG (sizeof(intptr_t) * 8)
#define GENMASK(h, l) \
	((uintptr_t)(((~0ULL) - (1ULL << (l)) + 1) & \
	 (~0ULL >> (BITS_PER_LONG - 1 - (h)))))
