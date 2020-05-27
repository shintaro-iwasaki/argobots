/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_MEM_H_INCLUDED
#define ABTI_MEM_H_INCLUDED

/* Memory allocation */

#ifdef ABT_CONFIG_USE_MEM_POOL
enum {
    ABTI_MEM_LP_MALLOC = 0,
    ABTI_MEM_LP_MMAP_RP,
    ABTI_MEM_LP_MMAP_HP_RP,
    ABTI_MEM_LP_MMAP_HP_THP,
    ABTI_MEM_LP_THP
};

void ABTI_mem_init(ABTI_global *p_global);
void ABTI_mem_init_local(ABTI_xstream *p_local_xstream);
void ABTI_mem_finalize(ABTI_global *p_global);
void ABTI_mem_finalize_local(ABTI_xstream *p_local_xstream);
int ABTI_mem_check_lp_alloc(int lp_alloc);

/* Inline functions */
static inline ABTI_thread *
ABTI_mem_alloc_thread_desc(ABTI_xstream *p_local_xstream)
{
    ABTI_thread *p_thread;
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    /* If an external thread allocates a stack, we use ABTU_malloc. */
    if (p_local_xstream == NULL) {
        p_thread = (ABTI_thread *)ABTU_malloc(sizeof(ABTI_thread) + 4);
        /* This segment is allocated with malloc(). */
        *(uint32_t *)(((char *)p_thread) + sizeof(ABTI_thread)) = 1;
        return p_thread;
    }
#endif
    p_thread =
        (ABTI_thread *)ABTI_mem_pool_alloc(&p_local_xstream->mem_pool_thread_desc);
    /* This segment comes from a memory pool. */
    *(uint32_t *)(((char *)p_thread) + sizeof(ABTI_thread)) = 0;
    return p_thread;
}

static inline void *ABTI_mem_alloc_thread_stack_malloc(size_t stacksize)
{
    void *p_stack = ABTU_malloc(stacksize);
    ABTI_VALGRIND_REGISTER_STACK(p_stack, stacksize);
    return p_stack;
}

static inline void *
ABTI_mem_alloc_thread_stack_mempool(ABTI_xstream *p_local_xstream,
                                    size_t stacksize)
{
    void *p_stack = ABTI_mem_pool_alloc(&p_local_xstream->mem_pool_stack);
    ABTI_VALGRIND_REGISTER_STACK(p_stack, stacksize);
    return p_stack;
}

static inline void ABTI_mem_setup_thread_stack(ABTI_xstream *p_local_xstream,
                                               ABTI_thread *p_thread)
{
    ABTI_stack_type stacktype = p_thread->attr.stacktype;
    if (stacktype == ABTI_STACK_TYPE_MEMPOOL) {
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
        if (p_local_xstream == NULL) {
            p_thread->attr.p_stack =
                ABTI_mem_alloc_thread_stack_malloc(p_thread->attr.stacksize);
            /* stacktype is changed. */
            p_thread->attr.stacktype = ABTI_STACK_TYPE_MALLOC;
            return;
        }
#endif
        p_thread->attr.p_stack =
            ABTI_mem_alloc_thread_stack_mempool(p_local_xstream,
                                                p_thread->attr.stacksize);
    } else if (stacktype == ABTI_STACK_TYPE_MALLOC) {
        p_thread->attr.p_stack =
            ABTI_mem_alloc_thread_stack_malloc(p_thread->attr.stacksize);
    } else if (stacktype == ABTI_STACK_TYPE_USER) {
        /* Do not allocate stack, but Valgrind registration is preferred. */
        ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack,
                                     p_thread->attr.stacksize);
    } else {
        ABTI_ASSERT(stacktype == ABTI_STACK_TYPE_MAIN);
        /* Stack of the currently running Pthreads is used, so we do not need to
         * register it to Valgrind. */
    }
}

static inline ABTI_thread *ABTI_mem_alloc_thread(ABTI_xstream *p_local_xstream,
                                                 ABTI_thread_attr *p_attr)
{
    ABTI_thread *p_thread = ABTI_mem_alloc_thread_desc(p_local_xstream);

    if (!p_attr) {
        /* Initialize thread_attr to the default one. */
        ABTI_thread_attr_init(&p_thread->attr, NULL,
                              ABTI_global_get_thread_stacksize(),
                              ABTI_STACK_TYPE_MEMPOOL, ABT_TRUE);
    } else {
        /* Copy a user-given attribute. */
        ABTI_thread_attr_copy(&p_thread->attr, p_attr);
    }

    ABTI_mem_setup_thread_stack(p_local_xstream, p_thread);

    return p_thread;
}

static inline void ABTI_mem_free_thread_desc(ABTI_xstream *p_local_xstream,
                                             ABTI_thread *p_thread)
{
    /* Release a thread descriptor. */
    if (*(uint32_t *)(((char *)p_thread) + sizeof(ABTI_thread))) {
        ABTU_free(p_thread);
    } else {
        /* Came from a memory pool. */
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
        if (p_local_xstream == NULL) {
            /* Return a stack to the global pool. */
            ABTI_spinlock_acquire(&gp_ABTI_global->mem_pool_desc_lock);
            ABTI_mem_pool_free(&gp_ABTI_global->mem_pool_desc_ext, p_thread);
            ABTI_spinlock_release(&gp_ABTI_global->mem_pool_desc_lock);
            return;
        }
#endif
        ABTI_mem_pool_free(&p_local_xstream->mem_pool_desc, p_thread);
    }
}

static inline void ABTI_mem_release_thread_stack(ABTI_xstream *p_local_xstream,
                                                 ABTI_thread *p_thread)
{
    /* Release a stack. */
    if (p_thread->attr.stacktype == ABTI_STACK_TYPE_MALLOC) {
        ABTI_VALGRIND_UNREGISTER_STACK(p_thread->attr.p_stack);
        ABTU_free(p_thread->attr.p_stack);
    } else if (p_thread->attr.stacktype == ABTI_STACK_TYPE_MEMPOOL) {
        ABTI_VALGRIND_UNREGISTER_STACK(p_thread->attr.p_stack);
        /* Came from a memory pool. */
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
        if (p_local_xstream == NULL) {
            /* Return a stack to the global pool. */
            ABTI_spinlock_acquire(&gp_ABTI_global->mem_pool_stack_lock);
            ABTI_mem_pool_free(&gp_ABTI_global->mem_pool_stack_ext,
                               p_thread->attr.p_stack);
            ABTI_spinlock_release(&gp_ABTI_global->mem_pool_stack_lock);
            return;
        }
#endif
        ABTI_mem_pool_free(&p_local_xstream->mem_pool_stack,
                           p_thread->attr.p_stack);
    } else if (p_thread->attr.stacktype == ABTI_STACK_TYPE_USER) {
        /* The stack is allocated by a user, so no need to release a stack. */
        ABTI_VALGRIND_UNREGISTER_STACK(p_thread->attr.p_stack);
    }
}

static inline void ABTI_mem_free_thread(ABTI_xstream *p_local_xstream,
                                        ABTI_thread *p_thread)
{
    ABTI_mem_release_thread_stack(p_local_xstream, p_thread);
    ABTI_mem_free_thread_desc(p_local_xstream, p_thread);
}

static inline ABTI_task *ABTI_mem_alloc_task(ABTI_xstream *p_local_xstream)
{
    ABTI_task *p_task;
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    if (p_local_xstream == NULL) {
        /* For external threads */
        p_task = (ABTI_task *)ABTU_malloc(sizeof(ABTI_task) + 4);
        *(uint32_t *)(((char *)p_task) + sizeof(ABTI_task)) = 1;
        return p_task;
    }
#endif

    /* Find the page that has an empty block */
    p_task =
        (ABTI_task *)ABTI_mem_pool_alloc(&p_local_xstream->mem_pool_task_desc);
    /* To distinguish it from a malloc'ed case, assign non-NULL value. */
    *(uint32_t *)(((char *)p_task) + sizeof(ABTI_task)) = 0;
    return p_task;
}

static inline void ABTI_mem_free_task(ABTI_xstream *p_local_xstream,
                                      ABTI_task *p_task)
{
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    if (*(uint32_t *)(((char *)p_task) + sizeof(ABTI_task))) {
        /* This was allocated by an external thread. */
        ABTU_free(p_task);
        return;
    } else if (!p_local_xstream) {
        /* Return a stack and a descriptor to their global pools. */
        ABTI_spinlock_acquire(&gp_ABTI_global->mem_pool_task_desc_lock);
        ABTI_mem_pool_free(&gp_ABTI_global->mem_pool_task_desc_ext, p_task);
        ABTI_spinlock_release(&gp_ABTI_global->mem_pool_task_desc_lock);
        return;
    }
#endif
    ABTI_mem_pool_free(&p_local_xstream->mem_pool_task_desc, p_task);
}

#else /* ABT_CONFIG_USE_MEM_POOL */

#define ABTI_mem_init(p)
#define ABTI_mem_init_local(p)
#define ABTI_mem_finalize(p)
#define ABTI_mem_finalize_local(p)

static inline ABTI_thread *
ABTI_mem_alloc_thread_with_stacksize(size_t *p_stacksize)
{
    size_t stacksize, actual_stacksize;
    char *p_blk;
    void *p_stack;
    ABTI_thread *p_thread;

    /* Get the stack size */
    stacksize = *p_stacksize;
    actual_stacksize = stacksize - sizeof(ABTI_thread);

    /* Allocate ABTI_thread and a stack */
    p_blk = (char *)ABTU_malloc(stacksize);
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

static inline ABTI_thread *ABTI_mem_alloc_thread(ABT_thread_attr attr,
                                                 size_t *p_stacksize)
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

        char *p_blk = (char *)ABTU_malloc(p_attr->stacksize);
        p_thread = (ABTI_thread *)p_blk;

        ABTI_thread_attr_copy(&p_thread->attr, p_attr);
        p_thread->attr.stacksize -= sizeof(ABTI_thread);
        p_thread->attr.p_stack = (void *)(p_blk + sizeof(ABTI_thread));

    } else {
        /* Since the stack is given by the user, we create ABTI_thread
         * explicitly instead of using a part of stack because the stack
         * will be freed by the user. */
        p_thread = (ABTI_thread *)ABTU_malloc(sizeof(ABTI_thread));
        ABTI_thread_attr_copy(&p_thread->attr, p_attr);
    }

    *p_stacksize = p_thread->attr.stacksize;
    return p_thread;
}

static inline ABTI_thread *ABTI_mem_alloc_main_thread(ABT_thread_attr attr)
{
    ABTI_thread *p_thread;

    p_thread = (ABTI_thread *)ABTU_malloc(sizeof(ABTI_thread));

    /* Set attributes */
    /* TODO: Need to set the actual stack address and size for the main ULT */
    ABTI_thread_attr *p_attr = &p_thread->attr;
    ABTI_thread_attr_init(p_attr, NULL, 0, ABTI_STACK_TYPE_MAIN, ABT_FALSE);

    return p_thread;
}

static inline void ABTI_mem_free_thread(ABTI_thread *p_thread)
{
    ABTI_VALGRIND_UNREGISTER_STACK(p_thread->attr.p_stack);
    ABTU_free(p_thread);
}

static inline ABTI_task *ABTI_mem_alloc_task(void)
{
    return (ABTI_task *)ABTU_malloc(sizeof(ABTI_task));
}

static inline void ABTI_mem_free_task(ABTI_task *p_task)
{
    ABTU_free(p_task);
}

#endif /* ABT_CONFIG_USE_MEM_POOL */

#endif /* ABTI_MEM_H_INCLUDED */
