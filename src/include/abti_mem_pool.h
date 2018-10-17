/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_MEM_POOL_H_INCLUDED
#define ABTI_MEM_POOL_H_INCLUDED

void ABTI_mem_init(ABTI_global *p_global);
void ABTI_mem_init_local(ABTI_local *p_local);
void ABTI_mem_finalize(ABTI_global *p_global);
void ABTI_mem_finalize_local(ABTI_local *p_local);
int ABTI_mem_check_lp_alloc(int lp_alloc);

void *ABTI_mem_alloc_page(size_t *p_size);
void ABTI_mem_free_page(void *ptr);

void ABTI_mem_pool_return_blocks(int entry_index,
                                 ABTI_mem_local_pool *p_local_pool);
void ABTI_mem_pool_take_blocks(int entry_index,
                               ABTI_mem_local_pool *p_local_pool);

static inline
size_t ABTI_mem_get_element_size(int entry_index) {
    switch (entry_index) {
        case ABT_MEM_ENTRY_INDEX_THREAD_STACK_AND_DESC:
            return gp_ABTI_global->thread_stacksize;
        case ABT_MEM_ENTRY_INDEX_TASK_DESC:
            return sizeof(ABTI_task);
        default:
            ABTI_ASSERT(0);
    }
    return 0;
}

static inline
int ABTI_mem_get_local_pool_max_blocks(int entry_index) {
    switch (entry_index) {
        case ABT_MEM_ENTRY_INDEX_THREAD_STACK_AND_DESC:
            return gp_ABTI_global->mem_max_stacks >> ABT_MEM_BLOCKSIZE_LOG;
        case ABT_MEM_ENTRY_INDEX_TASK_DESC:
            return ABT_MEM_LOCALPOOL_NUM_BLOCKS_DEFAULT;
        default:
            ABTI_ASSERT(0);
    }
    return 0;
}

static inline
int ABTI_mem_get_global_to_local_num_blocks(int entry_index) {
    switch (entry_index) {
        case ABT_MEM_ENTRY_INDEX_THREAD_STACK_AND_DESC:
            /* = ceil(mem_max_stacks / ABT_MEM_BLOCKSIZE / 2.0) */
            return ((gp_ABTI_global->mem_max_stacks >> ABT_MEM_BLOCKSIZE_LOG)
                    + 1) >> 1;
        case ABT_MEM_ENTRY_INDEX_TASK_DESC:
            return ABT_MEM_GLOBAL_TO_LOCAL_NUM_BLOCKS_DEFAULT;
        default:
            ABTI_ASSERT(0);
    }
    return 0;
}

static inline
int ABTI_mem_get_local_to_global_num_blocks(int entry_index) {
    switch (entry_index) {
        case ABT_MEM_ENTRY_INDEX_THREAD_STACK_AND_DESC:
            /* = ceil(mem_max_stacks / ABT_MEM_BLOCKSIZE / 2.0) */
            return ((gp_ABTI_global->mem_max_stacks >> ABT_MEM_BLOCKSIZE_LOG)
                    + 1) >> 1;
        case ABT_MEM_ENTRY_INDEX_TASK_DESC:
            return ABT_MEM_LOCAL_TO_GLOBAL_NUM_BLOCKS_DEFAULT;
        default:
            ABTI_ASSERT(0);
    }
    return 0;
}

static inline
void *ABTI_mem_element_to_ptr(ABTI_mem_element *p_element)
{
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
    return (void *)p_element;
#else
    return (void *)(((char *)p_element) + sizeof(ABTI_mem_element));
#endif
}

static inline
ABTI_mem_element *ABTI_mem_ptr_to_element(void *ptr)
{
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
    return (ABTI_mem_element *)ptr;
#else
    return (ABTI_mem_element *)(((char *)ptr) - sizeof(ABTI_mem_element));
#endif
}

static inline
void *ABTI_mem_pool_alloc(int entry_index)
{
    ABTI_local *p_local = lp_ABTI_local;
    ABTI_mem_local_pool *p_local_pool;

#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    ABT_bool use_external_pool = ABT_FALSE;
    if (ABTU_unlikely(!p_local)) {
        /* Use an external pool. */
        ABTI_mem_global_pool *p_global_pool
            = gp_ABTI_global->p_mem_pools[entry_index];
        ABTI_spinlock_acquire(&p_global_pool->external_pool_lock);
        p_local_pool = p_global_pool->p_external_pool;
        use_external_pool = ABT_TRUE;
    } else {
        p_local_pool = &p_local->mem_pools[entry_index];
    }
#else
    p_local_pool = &p_local->mem_pools[entry_index];
#endif

    int local_pool_num_elements = p_local_pool->num_elements;
    if (ABTU_unlikely(local_pool_num_elements == 0)) {
        ABTI_mem_pool_take_blocks(entry_index, p_local_pool);
        local_pool_num_elements = p_local_pool->num_elements;
    }

    printf("local_pool_num_elements = %d\n", local_pool_num_elements);
    /* Take an element from a local pool. */
    --local_pool_num_elements;
    p_local_pool->num_elements = local_pool_num_elements;
    int block_i = local_pool_num_elements >> ABT_MEM_BLOCKSIZE_LOG;
    ABTI_mem_block *p_block = &p_local_pool->blocks[block_i];
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
    int block_num_elements = local_pool_num_elements & (ABT_MEM_BLOCKSIZE - 1);
    ABTI_mem_element *p_element = p_block->p_elements[block_num_elements];
#else
    ABTI_mem_element *p_element = p_block->p_head;
    ABTI_mem_element *p_next = p_element->p_next;
    p_block->p_head = p_next;
#endif

#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    if (ABTU_unlikely(use_external_pool)) {
        /* Release a lock of an external pool. */
        ABTI_mem_global_pool *p_global_pool
            = gp_ABTI_global->p_mem_pools[entry_index];
        ABTI_spinlock_release(&p_global_pool->external_pool_lock);
    }
#endif
    return ABTI_mem_element_to_ptr(p_element);
}

static inline
void ABTI_mem_pool_free(int entry_index, void *ptr)
{
    printf("ptr = %p\n", ptr);
    ABTI_local *p_local = lp_ABTI_local;
    ABTI_mem_local_pool *p_local_pool;

#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    ABT_bool use_external_pool = ABT_FALSE;
    if (!p_local) {
        /* Use an external pool. */
        ABTI_mem_global_pool *p_global_pool
            = gp_ABTI_global->p_mem_pools[entry_index];
        ABTI_spinlock_acquire(&p_global_pool->external_pool_lock);
        p_local_pool = p_global_pool->p_external_pool;
        use_external_pool = ABT_TRUE;
    } else {
        p_local_pool = &p_local->mem_pools[entry_index];
    }
#else
    p_local_pool = &p_local->mem_pools[entry_index];
#endif

    int local_pool_num_elements = p_local_pool->num_elements;
    if (!(local_pool_num_elements & (ABT_MEM_BLOCKSIZE - 1))) {
        /* Quick check to avoid calling ABTI_mem_get_localpool_num_blocks(). */
        size_t local_pool_max_blocks
            = ABTI_mem_get_local_pool_max_blocks(entry_index);
        if (ABTU_unlikely(local_pool_num_elements ==
                          (local_pool_max_blocks << ABT_MEM_BLOCKSIZE_LOG))) {
            ABTI_mem_pool_return_blocks(entry_index, p_local_pool);
            local_pool_num_elements = p_local_pool->num_elements;
        }
    }

    /* Return an element to a local pool. */
    int block_i = local_pool_num_elements >> ABT_MEM_BLOCKSIZE_LOG;
    p_local_pool->num_elements = local_pool_num_elements + 1;
    int block_num_elements = local_pool_num_elements & (ABT_MEM_BLOCKSIZE - 1);
    ABTI_mem_block *p_block = &p_local_pool->blocks[block_i];
    ABTI_mem_element *p_element = ABTI_mem_ptr_to_element(ptr);
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
    p_block->p_elements[block_num_elements] = p_element;
#else
    if (ABTU_unlikely(block_num_elements == 0)) {
        p_block->p_head = p_element;
        p_block->p_tail = p_element;
        p_element->p_next = NULL;
    } else {
        p_element->p_next = p_block->p_head;
        p_block->p_head = p_element;
    }
#endif

#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    if (ABTU_unlikely(use_external_pool)) {
        /* Release a lock of am external pool. */
        ABTI_mem_global_pool *p_global_pool
            = gp_ABTI_global->p_mem_pools[entry_index];
        ABTI_spinlock_release(&p_global_pool->external_pool_lock);
    }
#endif
}

#endif /* ABTI_MEM_POOL_H_INCLUDED */
