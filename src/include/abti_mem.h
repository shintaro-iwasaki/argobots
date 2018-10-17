/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_MEM_H_INCLUDED
#define ABTI_MEM_H_INCLUDED

/* Memory allocation */

#if defined(ABT_CONFIG_USE_ALIGNED_ALLOC)
#define ABTU_CA_MALLOC(s)   ABTU_memalign(ABT_CONFIG_STATIC_CACHELINE_SIZE, s)
#else
#define ABTU_CA_MALLOC(s)   ABTU_malloc(s)
#endif /* defined(ABT_CONFIG_USE_ALIGNED_ALLOC) */

#ifdef ABT_CONFIG_USE_MEM_POOL

enum {
    ABTI_MEM_LP_MALLOC = 0,
    ABTI_MEM_LP_MMAP_RP,
    ABTI_MEM_LP_MMAP_HP_RP,
    ABTI_MEM_LP_MMAP_HP_THP,
    ABTI_MEM_LP_THP
};

static inline
ABTI_thread *ABTI_mem_alloc_thread_with_stacksize(size_t *p_stacksize,
                                                  ABTI_thread_attr *p_attr)
{
    size_t stacksize, actual_stacksize;
    char *p_blk;
    ABTI_thread *p_thread;
    void *p_stack;

    /* Get the stack size */
    stacksize = *p_stacksize;
    actual_stacksize = stacksize - sizeof(ABTI_thread);

    /* Allocate a stack */
    p_blk = (char *)ABTU_CA_MALLOC(stacksize);

    /* Allocate ABTI_thread, ABTI_stack_header, and the actual stack area in
     * the allocated stack memory */
    p_thread = (ABTI_thread *)p_blk;
    p_stack = (void *)(p_blk + sizeof(ABTI_thread));

    /* Set attributes */
    if (p_attr) {
        /* Copy p_attr. */
        ABTI_thread_attr_copy(&p_thread->attr, p_attr);
        p_thread->attr.stacksize = actual_stacksize;
        p_thread->attr.p_stack = p_stack;
        p_thread->attr.stacktype = ABTI_STACK_TYPE_MALLOC;
    } else {
        /* Initialize p_attr. */
        ABTI_thread_attr_init(&p_thread->attr, p_stack, actual_stacksize,
                              ABTI_STACK_TYPE_MALLOC, ABT_TRUE);
    }

    *p_stacksize = actual_stacksize;
    ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, *p_stacksize);
    return p_thread;
}

static inline
ABTI_thread *ABTI_mem_alloc_thread(ABT_thread_attr attr, size_t *p_stacksize)
{
    size_t stacksize, def_stacksize, actual_stacksize;
    char *p_blk;
    ABTI_thread *p_thread;
    void *p_stack;

    /* Get the stack size */
    def_stacksize = ABTI_global_get_thread_stacksize();
    if (attr == ABT_THREAD_ATTR_NULL) {
        stacksize = def_stacksize;
    } else {
        ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);

        if (p_attr->p_stack != NULL) {
            ABTI_ASSERT(p_attr->stacktype == ABTI_STACK_TYPE_USER);
            /* Since the stack is given by the user, we create ABTI_thread and
             * ABTI_stack_header explicitly with a single ABTU_malloc call. */
            p_thread = (ABTI_thread *)ABTU_CA_MALLOC(sizeof(ABTI_thread));
            ABTI_thread_attr_copy(&p_thread->attr, p_attr);

            *p_stacksize = p_attr->stacksize;
            ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, *p_stacksize);
            return p_thread;
        }
        stacksize = p_attr->stacksize;
        if (stacksize != def_stacksize) {
            /* Since the stack size requested is not the same as default one,
             * we use ABTU_malloc. */
            *p_stacksize = stacksize;
            return ABTI_mem_alloc_thread_with_stacksize(p_stacksize, p_attr);
        }
    }

    /* Use the stack pool */
    p_blk =
        (char *)ABTI_mem_pool_alloc(ABT_MEM_ENTRY_INDEX_THREAD_STACK_AND_DESC);
    p_thread = (ABTI_thread *)p_blk;
    p_stack  = (void *)(p_blk + sizeof(ABTI_thread));

    /* Actual stack size */
    actual_stacksize = stacksize - sizeof(ABTI_thread);

    /* Set attributes */
    if (attr == ABT_THREAD_ATTR_NULL) {
        ABTI_thread_attr *p_myattr = &p_thread->attr;
        ABTI_thread_attr_init(p_myattr, p_stack, actual_stacksize,
                              ABTI_STACK_TYPE_MEMPOOL, ABT_TRUE);
    } else {
        ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);
        ABTI_thread_attr_copy(&p_thread->attr, p_attr);
        p_thread->attr.stacksize = actual_stacksize;
        p_thread->attr.p_stack = p_stack;
    }

    *p_stacksize = actual_stacksize;
    ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, *p_stacksize);
    return p_thread;
}

static inline
ABTI_thread *ABTI_mem_alloc_main_thread(ABT_thread_attr attr)
{
    ABTI_thread *p_thread = (ABTI_thread *)ABTU_CA_MALLOC(sizeof(ABTI_thread));

    /* Set attributes */
    /* TODO: Need to set the actual stack address and size for the main ULT */
    ABTI_thread_attr *p_attr = &p_thread->attr;
    ABTI_thread_attr_init(p_attr, NULL, 0, ABTI_STACK_TYPE_MAIN, ABT_FALSE);

    return p_thread;
}

static inline
void ABTI_mem_free_thread(ABTI_thread *p_thread)
{
    ABTI_VALGRIND_UNREGISTER_STACK(p_thread->attr.p_stack);

    if (p_thread->attr.stacktype != ABTI_STACK_TYPE_MEMPOOL) {
        ABTU_free((void *)p_thread);
        return;
    }
    ABTI_mem_pool_free(ABT_MEM_ENTRY_INDEX_THREAD_STACK_AND_DESC, p_thread);
}

static inline
ABTI_task *ABTI_mem_alloc_task(void)
{
    return (ABTI_task *)ABTI_mem_pool_alloc(ABT_MEM_ENTRY_INDEX_TASK_DESC);
}

static inline
void ABTI_mem_free_task(ABTI_task *p_task)
{
    ABTI_mem_pool_free(ABT_MEM_ENTRY_INDEX_TASK_DESC, p_task);
}

#else /* ABT_CONFIG_USE_MEM_POOL */

#define ABTI_mem_init(p)
#define ABTI_mem_init_local(p)
#define ABTI_mem_finalize(p)
#define ABTI_mem_finalize_local(p)

static inline
ABTI_thread *ABTI_mem_alloc_thread_with_stacksize(size_t *p_stacksize)
{
    size_t stacksize, actual_stacksize;
    char *p_blk;
    void *p_stack;
    ABTI_thread *p_thread;

    /* Get the stack size */
    stacksize = *p_stacksize;
    actual_stacksize = stacksize - sizeof(ABTI_thread);

    /* Allocate ABTI_thread and a stack */
    p_blk = (char *)ABTU_CA_MALLOC(stacksize);
    p_thread = (ABTI_thread *)p_blk;
    p_stack = (void *)(p_blk + sizeof(ABTI_thread));

    /* Set attributes */
    ABTI_thread_attr *p_myattr = &p_thread->attr;
    ABTI_thread_attr_init(p_myattr, p_stack, actual_stacksize,
                          ABTI_STACK_TYPE_MALLOC, ABT_TRUE);

    *p_stacksize = actual_stacksize;
    ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, *p_stacksize);
    return p_thread;
}

static inline
ABTI_thread *ABTI_mem_alloc_thread(ABT_thread_attr attr, size_t *p_stacksize)
{
    ABTI_thread *p_thread;

    if (attr == ABT_THREAD_ATTR_NULL) {
        *p_stacksize = ABTI_global_get_thread_stacksize();
        return ABTI_mem_alloc_thread_with_stacksize(p_stacksize);
    }

    /* Allocate a stack and set attributes */
    ABTI_thread_attr *p_attr = ABTI_thread_attr_get_ptr(attr);
    if (p_attr->p_stack == NULL) {
        ABTI_ASSERT(p_attr->userstack == ABT_FALSE);

        char *p_blk = (char *)ABTU_CA_MALLOC(p_attr->stacksize);
        p_thread = (ABTI_thread *)p_blk;

        ABTI_thread_attr_copy(&p_thread->attr, p_attr);
        p_thread->attr.stacksize -= sizeof(ABTI_thread);
        p_thread->attr.p_stack = (void *)(p_blk + sizeof(ABTI_thread));

    } else {
        /* Since the stack is given by the user, we create ABTI_thread
         * explicitly instead of using a part of stack because the stack
         * will be freed by the user. */
        p_thread = (ABTI_thread *)ABTU_CA_MALLOC(sizeof(ABTI_thread));
        ABTI_thread_attr_copy(&p_thread->attr, p_attr);
    }

    *p_stacksize = p_thread->attr.stacksize;
    return p_thread;
}

static inline
ABTI_thread *ABTI_mem_alloc_main_thread(ABT_thread_attr attr)
{
    ABTI_thread *p_thread;

    p_thread = (ABTI_thread *)ABTU_CA_MALLOC(sizeof(ABTI_thread));

    /* Set attributes */
    /* TODO: Need to set the actual stack address and size for the main ULT */
    ABTI_thread_attr *p_attr = &p_thread->attr;
    ABTI_thread_attr_init(p_attr, NULL, 0, ABTI_STACK_TYPE_MAIN, ABT_FALSE);

    return p_thread;
}

static inline
void ABTI_mem_free_thread(ABTI_thread *p_thread)
{
    ABTI_VALGRIND_UNREGISTER_STACK(p_thread->attr.p_stack);
    ABTU_free(p_thread);
}

static inline
ABTI_task *ABTI_mem_alloc_task(void)
{
    return (ABTI_task *)ABTU_CA_MALLOC(sizeof(ABTI_task));
}

static inline
void ABTI_mem_free_task(ABTI_task *p_task)
{
    ABTU_free(p_task);
}

#endif /* ABT_CONFIG_USE_MEM_POOL */

#endif /* ABTI_MEM_H_INCLUDED */

