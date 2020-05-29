/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_MEM_POOL_H_INCLUDED
#define ABTI_MEM_POOL_H_INCLUDED

#define ABT_MEM_POOL_MAX_LOCAL_BUCKETS 2
#define ABT_MEM_POOL_NUM_RETURN_BUCKETS 1
#define ABT_MEM_POOL_NUM_TAKE_BUCKETS 1

typedef struct ABTI_mem_pool_header {
    struct ABTI_mem_pool_header *p_next;
    union {
        /* This is used when it is in ABTI_mem_pool_global_pool */
        ABTI_sync_lifo_element lifo_elem;
        /* This is used when it is in ABTI_mem_pool_local_pool */
        int num_headers;
    } bucket_info;
} ABTI_mem_pool_header;

typedef struct ABTI_mem_pool_page {
    ABTI_sync_lifo_element lifo_elem;
    struct ABTI_mem_pool_page *p_next_empty_page;
    void *mem;
    size_t page_size;
    ABTU_MEM_LARGEPAGE_TYPE lp_type;
    void *p_mem_extra;
    size_t mem_extra_size;
} ABTI_mem_pool_page;

/*
 * To efficiently take/return multiple headers per bucket, headers are linked as
 * follows in the global pool (bucket_lifo).
 *
 * header (p_next)> header (p_next)> header ... (num_headers_per_bucket)
 *   | (connected via lifo_elem)
 *   V
 * header (p_next)> header (p_next)> header ... (num_headers_per_bucket)
 *   | (connected via lifo_elem)
 *   V
 * header (p_next)> header (p_next)> header ... (num_headers_per_bucket)
 *   .
 *   .
 */
typedef struct ABTI_mem_pool_global_pool {
    size_t header_size;
    size_t page_size;
    int num_headers_per_bucket;
    int num_lp_type_requests;
    size_t alignment_hint;
    ABTU_MEM_LARGEPAGE_TYPE lp_type_requests[4];
    __attribute__((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    ABTI_sync_lifo bucket_lifo; /* LIFO of available buckets. */
    __attribute__((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    ABTI_sync_lifo mem_page_lifo; /* LIFO of non-empty pages. */
    __attribute__((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    ABTD_atomic_ptr p_mem_page_empty; /* List of empty pages. */
    __attribute__((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    /* List of the remaining headers that are not enough to create one
     * complete bucket. This is protected by a spinlock. The number of headers
     * is stored in partial_bucket.bucket_info.num_headers. */
    ABTI_spinlock partial_bucket_lock;
    ABTI_mem_pool_header *partial_bucket;
} ABTI_mem_pool_global_pool;

/*
 * To efficiently take/return multiple headers per bucket, headers are stored as
 * follows in the local pool.
 *
 * buckets[0]:
 *  = header (p_next)> header (p_next)> header ...  (num_headers_per_bucket)
 * buckets[1]:
 *  = header (p_next)> header (p_next)> header ...  (num_headers_per_bucket)
 *  .
 *  .
 * buckets[bucket_index]:
 *  = header (p_next)> header (p_next)> header ...
 *                              (buckets[bucket_index]->bucket_info.num_headers)
 */
typedef struct ABTI_mem_pool_local_pool {
    ABTI_mem_pool_global_pool *p_global_pool;
    size_t num_headers_per_bucket; /* Cached value to reduce dereference. It
                                      must be equal to
                                      p_global_pool->num_headers_per_bucket. */
    size_t bucket_index;
    ABTI_mem_pool_header *buckets[ABT_MEM_POOL_MAX_LOCAL_BUCKETS];
} ABTI_mem_pool_local_pool;

void ABTI_mem_pool_init_global_pool(
    ABTI_mem_pool_global_pool *p_global_pool, int num_headers_per_bucket,
    size_t header_size, size_t page_size,
    const ABTU_MEM_LARGEPAGE_TYPE *lp_type_requests, int num_lp_type_requests,
    size_t alignment_hint);
void ABTI_mem_pool_destroy_global_pool(
    ABTI_mem_pool_global_pool *p_global_pool);
void ABTI_mem_pool_init_local_pool(ABTI_mem_pool_local_pool *p_local_pool,
                                   ABTI_mem_pool_global_pool *p_global_pool);
void ABTI_mem_pool_destroy_local_pool(ABTI_mem_pool_local_pool *p_local_pool);
ABTI_mem_pool_header *
ABTI_mem_pool_take_bucket(ABTI_mem_pool_global_pool *p_global_pool);
void ABTI_mem_pool_return_bucket(ABTI_mem_pool_global_pool *p_global_pool,
                                 ABTI_mem_pool_header *bucket);

static inline void *ABTI_mem_pool_alloc(ABTI_mem_pool_local_pool *p_local_pool)
{
    size_t bucket_index = p_local_pool->bucket_index;
    ABTI_mem_pool_header *cur_bucket = p_local_pool->buckets[bucket_index];
    int num_headers_in_cur_bucket = cur_bucket->bucket_info.num_headers;
    /* At least one header is available in the current bucket, so it must be
     * larger than 0. */
    ABTI_ASSERT(num_headers_in_cur_bucket >= 1);
    if (num_headers_in_cur_bucket == 1) {
        /*cur_bucket will be empty after allocation. */
        if (bucket_index == 0) {
            /* cur_bucket is the last header in this pool.
             * Let's get some buckets from the global pool. */
            int i;
            for (i = 0; i < ABT_MEM_POOL_NUM_TAKE_BUCKETS; i++) {
                p_local_pool->buckets[i] =
                    ABTI_mem_pool_take_bucket(p_local_pool->p_global_pool);
            }
            p_local_pool->bucket_index = ABT_MEM_POOL_NUM_TAKE_BUCKETS - 1;
        } else {
            p_local_pool->bucket_index = bucket_index - 1;
        }
        /* Now buckets[bucket_index] is full of headers. */
    } else {
        /* Let's return the header in the bucket. */
        ABTI_mem_pool_header *p_next = cur_bucket->p_next;
        p_next->bucket_info.num_headers = num_headers_in_cur_bucket - 1;
        p_local_pool->buckets[bucket_index] = p_next;
    }
    /* At least one header is available in the current bucket. */
    return (void *)cur_bucket;
}

static inline void ABTI_mem_pool_free(ABTI_mem_pool_local_pool *p_local_pool,
                                      void *mem)
{
    /* At least one header is available in the current bucket. */
    size_t bucket_index = p_local_pool->bucket_index;
    ABTI_mem_pool_header *p_freed_header = (ABTI_mem_pool_header *)mem;
    ABTI_mem_pool_header *cur_bucket = p_local_pool->buckets[bucket_index];
    if (cur_bucket->bucket_info.num_headers ==
        p_local_pool->num_headers_per_bucket) {
        /* cur_bucket is full. */
        if (++bucket_index == ABT_MEM_POOL_MAX_LOCAL_BUCKETS) {
            int i;
            /* All buckets are full, so let's return some old buckets. */
            for (i = 0; i < ABT_MEM_POOL_NUM_RETURN_BUCKETS; i++) {
                ABTI_mem_pool_return_bucket(p_local_pool->p_global_pool,
                                            p_local_pool->buckets[i]);
            }
            for (i = ABT_MEM_POOL_NUM_RETURN_BUCKETS; i < ABT_MEM_POOL_MAX_LOCAL_BUCKETS; i++) {
                p_local_pool->buckets[i - ABT_MEM_POOL_NUM_RETURN_BUCKETS] =
                p_local_pool->buckets[i];
            }
            bucket_index = ABT_MEM_POOL_MAX_LOCAL_BUCKETS -
                           ABT_MEM_POOL_NUM_RETURN_BUCKETS;
        }
        p_local_pool->bucket_index = bucket_index;
        p_freed_header->p_next = NULL;
        p_freed_header->bucket_info.num_headers = 1;
    } else {
        p_freed_header->p_next = cur_bucket;
        p_freed_header->bucket_info.num_headers =
            cur_bucket->bucket_info.num_headers + 1;
    }
    p_local_pool->buckets[bucket_index] = p_freed_header;
    /* At least one header is available in the current bucket. */
}

#endif /* ABTI_MEM_POOL_H_INCLUDED */
