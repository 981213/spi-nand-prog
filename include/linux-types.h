#pragma once
#include <linux/types.h>
typedef __s8  s8;
typedef __u8  u8;
typedef __s16 s16;
typedef __u16 u16;
typedef __s32 s32;
typedef __u32 u32;
typedef __s64 s64;
typedef __u64 u64;

#define container_of(ptr, type, member) ({				\
	void *__mptr = (void *)(ptr);					\
	((type *)(__mptr - offsetof(type, member))); })

#define BIT(_B) (1 << (_B))
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define BITS_PER_LONG (sizeof(long) * 8)
#define GENMASK(h, l) \
	(((~0LU) - (1LU << (l)) + 1) & \
	 (~0LU >> (BITS_PER_LONG - 1 - (h))))
