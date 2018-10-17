/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_MEM_POOL_DEF_H_INCLUDED
#define ABTI_MEM_POOL_DEF_H_INCLUDED

/*
 * Configurable parameters.
 */

/* Block size. */
#ifndef ABT_MEM_BLOCKSIZE_LOG
#define ABT_MEM_BLOCKSIZE_LOG 10
#endif

#undef ABT_MEM_BLOCKSIZE
#define ABT_MEM_BLOCKSIZE (1 << ABT_MEM_BLOCKSIZE_LOG)

/* Local pool capacity. */
#ifndef ABT_MEM_LOCALPOOL_NUM_BLOCKS_DEFAULT
#define ABT_MEM_LOCALPOOL_NUM_BLOCKS_DEFAULT 4
#endif

/* Number of blocks that are taken from a global pool to a local pool. It must
 * be smaller than ABT_MEM_LOCALPOOL_NUM_BLOCKS */
#ifndef ABT_MEM_GLOBAL_TO_LOCAL_NUM_BLOCKS_DEFAULT
#define ABT_MEM_GLOBAL_TO_LOCAL_NUM_BLOCKS_DEFAULT 2
#endif

#if ABT_MEM_LOCALPOOL_NUM_BLOCKS_DEFAULT \
    <= ABT_MEM_GLOBAL_TO_LOCAL_NUM_BLOCKS_DEFAULT
#error "Illegal ABT_MEM_GLOBAL_TO_LOCAL_NUM_BLOCKS"
#endif

/* Number of blocks that are returned to a global pool. It must be smaller
 * than ABT_MEM_LOCALPOOL_NUM_BLOCKS */
#ifndef ABT_MEM_LOCAL_TO_GLOBAL_NUM_BLOCKS_DEFAULT
#define ABT_MEM_LOCAL_TO_GLOBAL_NUM_BLOCKS_DEFAULT 2
#endif

#if ABT_MEM_LOCALPOOL_NUM_BLOCKS_DEFAULT \
    <= ABT_MEM_LOCAL_TO_GLOBAL_NUM_BLOCKS_DEFAULT
#error "Illegal ABT_MEM_LOCAL_TO_GLOBAL_NUM_BLOCKS"
#endif

#ifndef ABT_MEM_MALLOC_ALIGNMENT
#define ABT_MEM_MALLOC_ALIGNMENT ABT_CONFIG_STATIC_CACHELINE_SIZE
#endif

/*
 * Define if array-based block management is used.
 * Array-based block management
 *   Pros: No additional data segment is needed.
 *   Cons: Block size is large, so the overhead of copying blocks is large.
 * Linked list-based block management
 *   Pros: Block size is small, so the overhead of copying blocks is small.
 *   Cons: Each element has an additional data segment for linked list.
 */
#ifndef ABT_MEM_USE_ARRAY_BASED_BLOCK
#define ABT_MEM_USE_ARRAY_BASED_BLOCK 1
#endif

/*
 * Define if page-level allocation is used.
 * Page-level allocation
 *   Pros: First-time allocation becomes fast.
 *   Cons: Memory is never freed until the memory pool itself is freed..
 * Object-level allocation
 *   Pros: Memory can be freed if the amount of cached memory goes beyond a
 *         certain threshold.
 *   Cons: First-time allocation is slow.
 */
#ifndef ABT_MEM_USE_PAGE
#define ABT_MEM_USE_PAGE 1
#endif

#if !ABT_MEM_USE_PAGE

/* The capacity of the global pool. It should be larger than
 * ABT_MEM_GLOBAL_TO_LOCAL_NUM_BLOCKS and ABT_MEM_LOCAL_TO_GLOBAL_NUM_BLOCKS */
#ifndef ABT_MEM_GLOBALPOOL_NUM_BLOCKS
#define ABT_MEM_GLOBALPOOL_NUM_BLOCKS 8
#endif

#if ABT_MEM_GLOBALPOOL_NUM_BLOCKS <= ABT_MEM_GLOBAL_TO_LOCAL_NUM_BLOCKS_DEFAULT
#error "Illegal ABT_MEM_GLOBALPOOL_NUM_BLOCKS"
#endif
#if ABT_MEM_GLOBALPOOL_NUM_BLOCKS <= ABT_MEM_LOCAL_TO_GLOBAL_NUM_BLOCKS_DEFAULT
#error "Illegal ABT_MEM_GLOBALPOOL_NUM_BLOCKS"
#endif

#endif /* ABT_MEM_USE_PAGE */

struct ABTI_mem_element_t {
#if !ABT_MEM_USE_ARRAY_BASED_BLOCK
    struct ABTI_mem_element_t *p_next;
    /* memory_element is the first sizeof(memory_element) bytes of a page. */
#endif
};

typedef struct ABTI_mem_element_t ABTI_mem_element;

#if ABT_MEM_USE_PAGE
struct ABTI_mem_bulk_t {
    struct ABTI_mem_bulk_t *p_next;
    /* memory_bulk is allocated at the first sizeof(memory_bulk) bytes of
     * a page. */
} __attribute__ ((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)));

typedef struct ABTI_mem_bulk_t ABTI_mem_bulk;
#endif

struct ABTI_mem_block_t {
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
    ABTI_mem_element *p_elements[ABT_MEM_BLOCKSIZE];
#else
    ABTI_mem_element *p_head;
    ABTI_mem_element *p_tail;
#endif
};

typedef struct ABTI_mem_block_t ABTI_mem_block;

struct ABTI_mem_local_pool_t {
    int num_elements;
#if ABT_MEM_USE_PAGE
    size_t extra_mem_size;
    void *p_extra_mem_ptr;
#endif
    ABTI_mem_block *blocks;
} __attribute__ ((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)));

typedef struct ABTI_mem_local_pool_t ABTI_mem_local_pool;

struct ABTI_mem_global_pool_t {
    /* Constant after initialization. */
    int entry_index;
    size_t element_size;
    __attribute__ ((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    ABTI_spinlock lock;
    __attribute__ ((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    /* Protected by lock. */
    int num_elements;
    int len_blocks;
    ABTI_mem_block *blocks;
#if ABT_MEM_USE_PAGE
    /* This is not protected by lock; accessed by atomic swap. */
    __attribute__ ((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    ABTI_mem_bulk *p_bulk;
#endif
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    __attribute__ ((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    ABTI_spinlock external_pool_lock;
    /* Protected by external_pool_lock. */
    ABTI_mem_local_pool *p_external_pool;
#endif
} __attribute__ ((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)));

typedef struct ABTI_mem_global_pool_t ABTI_mem_global_pool;

#endif /* ABTI_MEM_POOL_DEF_H_INCLUDED */
