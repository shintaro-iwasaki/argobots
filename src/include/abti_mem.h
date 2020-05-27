/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_MEM_H_INCLUDED
#define ABTI_MEM_H_INCLUDED

/* Memory allocation */

/* Header size should be a multiple of cache line size. It is constant. */
#define ABTI_MEM_SH_SIZE                                                       \
    (((sizeof(ABTI_thread) + sizeof(ABTI_stack_header) +                       \
       ABT_CONFIG_STATIC_CACHELINE_SIZE - 1) /                                 \
      ABT_CONFIG_STATIC_CACHELINE_SIZE) *                                      \
     ABT_CONFIG_STATIC_CACHELINE_SIZE)

#ifdef ABT_CONFIG_USE_MEM_POOL
typedef struct ABTI_blk_header ABTI_blk_header;

enum {
    ABTI_MEM_LP_MALLOC = 0,
    ABTI_MEM_LP_MMAP_RP,
    ABTI_MEM_LP_MMAP_HP_RP,
    ABTI_MEM_LP_MMAP_HP_THP,
    ABTI_MEM_LP_THP
};

struct ABTI_sp_header {
    uint32_t num_total_stacks;           /* Number of total stacks */
    ABTD_atomic_uint32 num_empty_stacks; /* Number of empty stacks */
    size_t stacksize;                    /* Stack size */
    uint64_t id;                         /* ID */
    ABTU_MEM_LARGEPAGE_TYPE lp_type;     /* Large page type */
    void *p_sp;             /* Pointer to the allocated stack page */
    ABTI_sp_header *p_next; /* Next stack page header */
};

struct ABTI_stack_header {
    ABTI_stack_header *p_next;
    ABTI_sp_header *p_sph;
    void *p_stack;
};

struct ABTI_page_header {
    uint32_t blk_size;                  /* Block size in bytes */
    uint32_t num_total_blks;            /* Number of total blocks */
    uint32_t num_empty_blks;            /* Number of empty blocks */
    ABTD_atomic_uint32 num_remote_free; /* Number of remote free blocks */
    ABTI_blk_header *p_head;            /* First empty block */
    ABTI_blk_header *p_free;            /* For remote free */
    ABTI_native_thread_id owner_id;     /* Owner's ID */
    ABTI_page_header *p_prev;           /* Prev page header */
    ABTI_page_header *p_next;           /* Next page header */
    ABTU_MEM_LARGEPAGE_TYPE lp_type;    /* Large page type */
};

struct ABTI_blk_header {
    ABTI_page_header *p_ph;  /* Page header */
    ABTI_blk_header *p_next; /* Next block header */
};

void ABTI_mem_init(ABTI_global *p_global);
void ABTI_mem_init_local(ABTI_xstream *p_local_xstream);
void ABTI_mem_finalize(ABTI_global *p_global);
void ABTI_mem_finalize_local(ABTI_xstream *p_local_xstream);
int ABTI_mem_check_lp_alloc(int lp_alloc);

char *ABTI_mem_take_global_stack(ABTI_xstream *p_local_xstream);
void ABTI_mem_add_stack_to_global(ABTI_stack_header *p_sh);
ABTI_page_header *ABTI_mem_alloc_page(ABTI_xstream *p_local_xstream,
                                      size_t blk_size);
void ABTI_mem_free_page(ABTI_xstream *p_local_xstream, ABTI_page_header *p_ph);
void ABTI_mem_take_free(ABTI_page_header *p_ph);
void ABTI_mem_free_remote(ABTI_page_header *p_ph, ABTI_blk_header *p_bh);
ABTI_page_header *ABTI_mem_take_global_page(ABTI_xstream *p_local_xstream);

char *ABTI_mem_alloc_sp(ABTI_xstream *p_local_xstream, size_t stacksize);

/******************************************************************************
 * Unless the stack is given by the user, we allocate a stack first and then
 * use the beginning of the allocated stack for allocating ABTI_thread and
 * ABTI_stack_header.  This way we need only one memory allocation call (e.g.,
 * ABTU_malloc).  The memory layout of the allocated stack will look like
 *  |-------------------|
 *  | ABTI_thread       |
 *  |-------------------|
 *  | ABTI_stack_header |
 *  |-------------------|
 *  | actual stack area |
 *  |-------------------|
 * Thus, the actual size of stack becomes
 * (requested stack size) - sizeof(ABTI_thread) - sizeof(ABTI_stack_header)
 * and it is set in the attribute field of ABTI_thread.
 *****************************************************************************/

/* Inline functions */
static inline void
ABTI_mem_alloc_thread_mempool_impl(ABTI_xstream *p_local_xstream,
                                   ABTI_thread **pp_thread, void **pp_stack,
                                   size_t *p_actual_stacksize)
{
    char *p_blk = NULL;
    ABTI_stack_header *p_sh;
    size_t stacksize = ABTI_global_get_thread_stacksize();
    /* Use the stack pool */
    if (p_local_xstream->p_mem_stack) {
        /* ES's stack pool has an available stack */
        p_sh = p_local_xstream->p_mem_stack;
        p_local_xstream->p_mem_stack = p_sh->p_next;
        p_local_xstream->num_stacks--;

        p_sh->p_next = NULL;
        p_blk = (char *)p_sh - sizeof(ABTI_thread);

    } else {
        /* Check stacks in the global data */
        if (gp_ABTI_global->p_mem_stack) {
            p_blk = ABTI_mem_take_global_stack(p_local_xstream);
            if (p_blk == NULL) {
                p_blk = ABTI_mem_alloc_sp(p_local_xstream, stacksize);
            }
        } else {
            /* Allocate a new stack if we don't have any empty stack */
            p_blk = ABTI_mem_alloc_sp(p_local_xstream, stacksize);
        }

        p_sh = (ABTI_stack_header *)(p_blk + sizeof(ABTI_thread));
        p_sh->p_next = NULL;
    }

    /* Actual stack size */
    *p_actual_stacksize = stacksize - ABTI_MEM_SH_SIZE;

    /* Get the ABTI_thread pointer and stack pointer */
    *pp_thread = (ABTI_thread *)p_blk;
    *pp_stack = p_sh->p_stack;
}

static inline void ABTI_mem_alloc_thread_malloc_impl(size_t stacksize,
                                                     ABTI_thread **pp_thread,
                                                     void **pp_stack,
                                                     size_t *p_actual_stacksize)
{
    /* Get the stack size */
    *p_actual_stacksize = stacksize;
    /* Allocate a stack */
    ABTI_thread *p_thread = (ABTI_thread *)ABTU_malloc(sizeof(ABTI_thread) + 4);
    /* This segment is allocated by malloc(). */
    *(uint32_t *)(((char *)p_thread) + sizeof(ABTI_thread)) = 1;
    *pp_thread = p_thread;
    /* Set p_thread and p_stack */
    *pp_stack = (void *)ABTU_malloc(stacksize);
}

static inline ABTI_thread *
ABTI_mem_alloc_thread_default(ABTI_xstream *p_local_xstream)
{
    size_t actual_stacksize;
    ABTI_thread *p_thread;
    void *p_stack;
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    /* If an external thread allocates a stack, we use ABTU_malloc. */
    if (p_local_xstream == NULL) {
        ABTI_mem_alloc_thread_malloc_impl(ABTI_global_get_thread_stacksize(),
                                          &p_thread, &p_stack,
                                          &actual_stacksize);
        /* Initialize p_attr. */
        ABTI_thread_attr_init(&p_thread->attr, p_stack, actual_stacksize,
                              ABTI_STACK_TYPE_MALLOC, ABT_TRUE);
        ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, actual_stacksize);
        return p_thread;
    }
#endif
    p_thread = (ABTI_thread *)ABTI_mem_pool_alloc(
        &p_local_xstream->mem_pool_thread_desc);
    /* This segment comes from a memory pool. */
    *(uint32_t *)(((char *)p_thread) + sizeof(ABTI_thread)) = 0;
    p_stack = ABTI_mem_pool_alloc(&p_local_xstream->mem_pool_stack);
    actual_stacksize = ABTI_global_get_thread_stacksize();
    /* Initialize p_attr. */
    ABTI_thread_attr_init(&p_thread->attr, p_stack, actual_stacksize,
                          ABTI_STACK_TYPE_MEMPOOL, ABT_TRUE);
    ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, actual_stacksize);
    return p_thread;
}

static inline ABTI_thread *
ABTI_mem_alloc_thread_mempool(ABTI_xstream *p_local_xstream,
                              ABTI_thread_attr *p_attr)
{
    size_t actual_stacksize;
    ABTI_thread *p_thread;
    void *p_stack;
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    /* If an external thread allocates a stack, we use ABTU_malloc. */
    if (p_local_xstream == NULL) {
        ABTI_mem_alloc_thread_malloc_impl(ABTI_global_get_thread_stacksize(),
                                          &p_thread, &p_stack,
                                          &actual_stacksize);
        /* Copy p_attr. */
        ABTI_thread_attr_copy(&p_thread->attr, p_attr);
        p_thread->attr.stacktype = ABTI_STACK_TYPE_MALLOC;
        p_thread->attr.stacksize = actual_stacksize;
        p_thread->attr.p_stack = p_stack;
        ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, actual_stacksize);
        return p_thread;
    }
#endif
    p_thread = (ABTI_thread *)ABTI_mem_pool_alloc(
        &p_local_xstream->mem_pool_thread_desc);
    /* This segment comes from a memory pool. */
    *(uint32_t *)(((char *)p_thread) + sizeof(ABTI_thread)) = 0;
    p_stack = ABTI_mem_pool_alloc(&p_local_xstream->mem_pool_stack);
    actual_stacksize = ABTI_global_get_thread_stacksize();
    /* Copy p_attr. */
    ABTI_thread_attr_copy(&p_thread->attr, p_attr);
    p_thread->attr.stacksize = actual_stacksize;
    p_thread->attr.p_stack = p_stack;
    ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, actual_stacksize);
    return p_thread;
}

static inline ABTI_thread *
ABTI_mem_alloc_thread_malloc(ABTI_thread_attr *p_attr)
{
    size_t actual_stacksize;
    ABTI_thread *p_thread;
    void *p_stack;
    ABTI_mem_alloc_thread_malloc_impl(p_attr->stacksize, &p_thread, &p_stack,
                                      &actual_stacksize);
    /* Copy p_attr. */
    ABTI_thread_attr_copy(&p_thread->attr, p_attr);
    p_thread->attr.stacksize = actual_stacksize;
    p_thread->attr.p_stack = p_stack;

    ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, actual_stacksize);
    return p_thread;
}

static inline ABTI_thread *ABTI_mem_alloc_thread_user(ABTI_thread_attr *p_attr)
{
    /* Do not allocate stack, but Valgrind registration is preferred. */
    ABTI_thread *p_thread = (ABTI_thread *)ABTU_malloc(sizeof(ABTI_thread) + 4);
    *(uint32_t *)(((char *)p_thread) + sizeof(ABTI_thread)) = 1;
    ABTI_thread_attr_copy(&p_thread->attr, p_attr);

    ABTI_VALGRIND_REGISTER_STACK(p_thread->attr.p_stack, p_attr->stacksize);
    return p_thread;
}

static inline ABTI_thread *ABTI_mem_alloc_thread_main(ABTI_thread_attr *p_attr)
{
    /* Stack of the currently running Pthreads is used. */
    ABTI_thread *p_thread = (ABTI_thread *)ABTU_malloc(sizeof(ABTI_thread) + 4);
    *(uint32_t *)(((char *)p_thread) + sizeof(ABTI_thread)) = 1;
    ABTI_thread_attr_copy(&p_thread->attr, p_attr);
    return p_thread;
}

static inline ABTI_thread *ABTI_mem_alloc_thread(ABTI_xstream *p_local_xstream,
                                                 ABTI_thread_attr *p_attr)
{
    if (!p_attr) {
        return ABTI_mem_alloc_thread_default(p_local_xstream);
    }
    ABTI_stack_type stacktype = p_attr->stacktype;
    if (stacktype == ABTI_STACK_TYPE_MEMPOOL) {
        return ABTI_mem_alloc_thread_mempool(p_local_xstream, p_attr);
    } else if (stacktype == ABTI_STACK_TYPE_MALLOC) {
        return ABTI_mem_alloc_thread_malloc(p_attr);
    } else if (stacktype == ABTI_STACK_TYPE_USER) {
        return ABTI_mem_alloc_thread_user(p_attr);
    } else {
        ABTI_ASSERT(stacktype == ABTI_STACK_TYPE_MAIN);
        return ABTI_mem_alloc_thread_main(p_attr);
    }
}

static inline void ABTI_mem_free_thread(ABTI_xstream *p_local_xstream,
                                        ABTI_thread *p_thread)
{
    ABTI_VALGRIND_UNREGISTER_STACK(p_thread->attr.p_stack);

    /* Return stack. */
    if (p_thread->attr.stacktype == ABTI_STACK_TYPE_MALLOC) {
        ABTU_free(p_thread->attr.p_stack);
    } else if (p_thread->attr.stacktype == ABTI_STACK_TYPE_MEMPOOL) {
        /* Came from a memory pool. */
        do {
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
            if (p_local_xstream == NULL) {
                /* Return a stack to the global pool. */
                ABTI_spinlock_acquire(&gp_ABTI_global->mem_pool_stack_lock);
                ABTI_mem_pool_free(&gp_ABTI_global->mem_pool_stack_ext,
                                   p_thread->attr.p_stack);
                ABTI_spinlock_release(&gp_ABTI_global->mem_pool_stack_lock);
                break;
            }
#endif
            ABTI_mem_pool_free(&p_local_xstream->mem_pool_stack,
                               p_thread->attr.p_stack);
        } while (0);
    }

    /* Return a thread descriptor. */
    if (*(uint32_t *)(((char *)p_thread) + sizeof(ABTI_thread))) {
        ABTU_free(p_thread);
    } else {
        /* Came from a memory pool. */
        do {
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
            if (p_local_xstream == NULL) {
                /* Return a stack to the global pool. */
                ABTI_spinlock_acquire(
                    &gp_ABTI_global->mem_pool_thread_desc_lock);
                ABTI_mem_pool_free(&gp_ABTI_global->mem_pool_thread_desc_ext,
                                   p_thread);
                ABTI_spinlock_release(
                    &gp_ABTI_global->mem_pool_thread_desc_lock);
                break;
            }
#endif
            ABTI_mem_pool_free(&p_local_xstream->mem_pool_thread_desc,
                               p_thread);
        } while (0);
    }
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
