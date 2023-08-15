#pragma once
#define __must_check __attribute__((__warn_unused_result__))
#define __force

#ifndef _WIN32
#include <stdint.h>
#endif

#define MAX_ERRNO	4095

#define IS_ERR_VALUE(x) ((uintptr_t)(void *)(x) >= (uintptr_t)-MAX_ERRNO)


static inline void * __must_check ERR_PTR(intptr_t error)
{
	return (void *) error;
}

static inline intptr_t __must_check PTR_ERR(__force const void *ptr)
{
	return (intptr_t) ptr;
}

static inline bool __must_check IS_ERR(__force const void *ptr)
{
	return IS_ERR_VALUE((uintptr_t)ptr);
}

static inline bool __must_check IS_ERR_OR_NULL(__force const void *ptr)
{
	return (!ptr) || IS_ERR_VALUE((uintptr_t)ptr);
}
