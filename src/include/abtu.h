/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTU_H_INCLUDED
#define ABTU_H_INCLUDED

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "abt_config.h"

/* Utility Functions */
static inline __attribute__((malloc))
void *ABTU_malloc(size_t size) {
    /* ABTU_malloc allocates a multiple of cache line size which is aligned
     * in order to avoid conflicts among objects. */
    /* Make size a multiple of cache line size. */

    /* alloc_size satisfies the following condition:
     * (alloc_size % ABT_CONFIG_STATIC_CACHELINE_SIZE) == 0
     * && size <= alloc_size
     * && alloc_size < (size + ABT_CONFIG_STATIC_CACHELINE_SIZE) */
#if !(ABT_CONFIG_STATIC_CACHELINE_SIZE & (ABT_CONFIG_STATIC_CACHELINE_SIZE - 1))
    /* ABT_CONFIG_STATIC_CACHELINE_SIZE is power of 2. */
    size_t alloc_size = (size + ABT_CONFIG_STATIC_CACHELINE_SIZE - 1)
                        & (~(ABT_CONFIG_STATIC_CACHELINE_SIZE - 1));
#else
    size_t alloc_size = ((size + ABT_CONFIG_STATIC_CACHELINE_SIZE - 1)
                         / ABT_CONFIG_STATIC_CACHELINE_SIZE)
                        * ABT_CONFIG_STATIC_CACHELINE_SIZE;
#endif
    void *p_ptr;
    int ret = posix_memalign(&p_ptr, ABT_CONFIG_STATIC_CACHELINE_SIZE,
                             alloc_size);
    assert(ret == 0);
    return p_ptr;
}

#define ABTU_free(a) free((void *)(a))

static inline __attribute__((malloc))
void *ABTU_calloc(size_t num, size_t size)
{
    size_t total_size = num * size;
    void *ptr = ABTU_malloc(total_size);
    __builtin_memset(ptr, 0, total_size);
    return ptr;
}

static inline __attribute__((malloc))
void *ABTU_realloc(void *p_mem, size_t orig_size, size_t new_size)
{
    void *p_new = ABTU_malloc(new_size);
    __builtin_memcpy(p_new, p_mem, orig_size);
    ABTU_free(p_mem);
    return p_new;
}

static inline __attribute__((malloc))
void *ABTU_memalign(size_t alignment, size_t size)
{
    void *p_ptr;
#if !(ABT_CONFIG_STATIC_CACHELINE_SIZE & (ABT_CONFIG_STATIC_CACHELINE_SIZE - 1))
    /* ABT_CONFIG_STATIC_CACHELINE_SIZE is power of 2. */
    size_t alloc_size = (size + ABT_CONFIG_STATIC_CACHELINE_SIZE - 1)
                        & (~(ABT_CONFIG_STATIC_CACHELINE_SIZE - 1));
#else
    size_t alloc_size = ((size + ABT_CONFIG_STATIC_CACHELINE_SIZE - 1)
                         / ABT_CONFIG_STATIC_CACHELINE_SIZE)
                        * ABT_CONFIG_STATIC_CACHELINE_SIZE;
#endif
    int ret = posix_memalign(&p_ptr, alignment, alloc_size);
    assert(ret == 0);
    return p_ptr;
}

#define ABTU_strcpy(d,s)        strcpy(d,s)
#define ABTU_strncpy(d,s,n)     strncpy(d,s,n)

/* The caller should free the memory returned. */
char *ABTU_get_indent_str(int indent);

int ABTU_get_int_len(size_t num);
char *ABTU_strtrim(char *str);

#endif /* ABTU_H_INCLUDED */
