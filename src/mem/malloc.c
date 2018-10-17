/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

#ifdef ABT_CONFIG_USE_MEM_POOL

#include <sys/types.h>
#include <sys/mman.h>

#define PROTS           (PROT_READ | PROT_WRITE)

#if defined(HAVE_MAP_ANONYMOUS)
#define FLAGS_RP        (MAP_PRIVATE | MAP_ANONYMOUS)
#elif defined(HAVE_MAP_ANON)
#define FLAGS_RP        (MAP_PRIVATE | MAP_ANON)
#else
/* In this case, we don't allow using mmap. We always use malloc. */
#define FLAGS_RP        (MAP_PRIVATE)
#endif

#if defined(HAVE_MAP_HUGETLB)
#define FLAGS_HP        (FLAGS_RP | MAP_HUGETLB)
#define FD_HP           0
#define MMAP_DBG_MSG    "mmap a hugepage"
#else
/* NOTE: On Mac OS, we tried VM_FLAGS_SUPERPAGE_SIZE_ANY that is defined in
 * <mach/vm_statistics.h>, but mmap() failed with it and its execution was too
 * slow.  By that reason, we do not support it for now. */
#define FLAGS_HP        FLAGS_RP
#define FD_HP           0
#define MMAP_DBG_MSG    "mmap regular pages"
#endif

static void ABTI_mem_local_pool_create(ABTI_local *p_local, int entry_index);
static void ABTI_mem_local_pool_free(ABTI_local *p_local, int entry_index);
static void ABTI_mem_global_pool_create(ABTI_global *p_global,
                                        size_t element_size, int entry_index);
static void ABTI_mem_global_pool_free(ABTI_global *p_global, int entry_index);

#if ABT_MEM_USE_PAGE
static inline
void ABTI_mem_atomic_insert_bulk(ABTI_mem_global_pool *p_global_pool,
                                 ABTI_mem_bulk *p_bulk)
{
    while (1) {
        ABTI_mem_bulk *p_cur_bulk = p_global_pool->p_bulk;
        p_bulk->p_next = p_cur_bulk;
        if (ABTD_atomic_bool_cas_weak_ptr((void **)&p_global_pool->p_bulk,
                                          (void *)p_cur_bulk, (void *)p_bulk))
            return;
    }
}
#endif

void ABTI_mem_init(ABTI_global *p_global)
{
    int entry_index;
    for (entry_index = 0; entry_index < ABT_MEM_NUM_ENTRIES; entry_index++) {
        size_t element_size = ABTI_mem_get_element_size(entry_index);
        ABTI_mem_global_pool_create(gp_ABTI_global, element_size, entry_index);
    }
}

void ABTI_mem_init_local(ABTI_local *p_local)
{
    int entry_index;
    for (entry_index = 0; entry_index < ABT_MEM_NUM_ENTRIES; entry_index++)
        ABTI_mem_local_pool_create(p_local, entry_index);
}

void ABTI_mem_finalize(ABTI_global *p_global)
{
    int entry_index;
    for (entry_index = 0; entry_index < ABT_MEM_NUM_ENTRIES; entry_index++)
        ABTI_mem_global_pool_free(gp_ABTI_global, entry_index);
}

void ABTI_mem_finalize_local(ABTI_local *p_local)
{
    int entry_index;
    for (entry_index = 0; entry_index < ABT_MEM_NUM_ENTRIES; entry_index++)
        ABTI_mem_local_pool_free(p_local, entry_index);
}

int ABTI_mem_check_lp_alloc(int lp_alloc)
{
    size_t sp_size = gp_ABTI_global->mem_sp_size;
    size_t pg_size = gp_ABTI_global->mem_page_size;
    size_t alignment;
    void *p_page = NULL;

    switch (lp_alloc) {
        case ABTI_MEM_LP_MMAP_RP:
            p_page = mmap(NULL, pg_size, PROTS, FLAGS_RP, 0, 0);
            if (p_page != MAP_FAILED) {
                munmap(p_page, pg_size);
            } else {
                lp_alloc = ABTI_MEM_LP_MALLOC;
            }
            break;

        case ABTI_MEM_LP_MMAP_HP_RP:
            p_page = mmap(NULL, sp_size, PROTS, FLAGS_HP, 0, 0);
            if (p_page != MAP_FAILED) {
                munmap(p_page, sp_size);
            } else {
                p_page = mmap(NULL, pg_size, PROTS, FLAGS_RP, 0, 0);
                if (p_page != MAP_FAILED) {
                    munmap(p_page, pg_size);
                    lp_alloc = ABTI_MEM_LP_MMAP_RP;
                } else {
                    lp_alloc = ABTI_MEM_LP_MALLOC;
                }
            }
            break;

        case ABTI_MEM_LP_MMAP_HP_THP:
            p_page = mmap(NULL, sp_size, PROTS, FLAGS_HP, 0, 0);
            if (p_page != MAP_FAILED) {
                munmap(p_page, sp_size);
            } else {
                alignment = gp_ABTI_global->huge_page_size;
                p_page = ABTU_memalign(alignment, pg_size);
                if (p_page) {
                    ABTU_free(p_page);
                    lp_alloc = ABTI_MEM_LP_THP;
                } else {
                    lp_alloc = ABTI_MEM_LP_MALLOC;
                }
            }
            break;

        case ABTI_MEM_LP_THP:
            alignment = gp_ABTI_global->huge_page_size;
            p_page = ABTU_memalign(alignment, pg_size);
            if (p_page) {
                ABTU_free(p_page);
                lp_alloc = ABTI_MEM_LP_THP;
            } else {
                lp_alloc = ABTI_MEM_LP_MALLOC;
            }
            break;

        default:
            break;
    }

    return lp_alloc;
}

void *ABTI_mem_alloc_page(size_t *p_size)
{
    char *p_page = NULL;
    size_t pgsize = gp_ABTI_global->mem_page_size;
    /* The first 64 bytes are used for mmapped. */
    ABTI_ASSERT(pgsize >= 128);

    ABT_bool mmapped = ABT_FALSE;
    switch (gp_ABTI_global->mem_lp_alloc) {
        case ABTI_MEM_LP_MALLOC:
            mmapped = ABT_FALSE;
            p_page = (char *)ABTU_malloc(pgsize);
            LOG_DEBUG("malloc a regular page (%d): %p\n", pgsize, p_page);
            break;

        case ABTI_MEM_LP_MMAP_RP:
            p_page = (char *)mmap(NULL, pgsize, PROTS, FLAGS_RP, 0, 0);
            if ((void *)p_page != MAP_FAILED) {
                mmapped = ABT_TRUE;
                LOG_DEBUG("mmap a regular page (%d): %p\n", pgsize, p_page);
            } else {
                /* mmap failed and thus we fall back to malloc. */
                p_page = (char *)ABTU_malloc(pgsize);
                mmapped = ABT_FALSE;
                LOG_DEBUG("fall back to malloc a regular page (%d): %p\n",
                          pgsize, p_page);
            }
            break;

        case ABTI_MEM_LP_MMAP_HP_RP:
            /* We first try to mmap a huge page, and then if it fails, we mmap
             * a regular page. */
            p_page = (char *)mmap(NULL, pgsize, PROTS, FLAGS_HP, 0, 0);
            if ((void *)p_page != MAP_FAILED) {
                mmapped = ABT_TRUE;
                LOG_DEBUG(MMAP_DBG_MSG" (%d): %p\n", pgsize, p_page);
            } else {
                /* Huge pages are run out of. Use a normal mmap. */
                p_page = (char *)mmap(NULL, pgsize, PROTS, FLAGS_RP, 0, 0);
                if ((void *)p_page != MAP_FAILED) {
                    mmapped = ABT_TRUE;
                    LOG_DEBUG("fall back to mmap regular pages (%d): %p\n",
                              pgsize, p_page);
                } else {
                    /* mmap failed and thus we fall back to malloc. */
                    p_page = (char *)ABTU_malloc(pgsize);
                    mmapped = ABT_FALSE;
                    LOG_DEBUG("fall back to malloc a regular page (%d): %p\n",
                              pgsize, p_page);
                }
            }
            break;

        case ABTI_MEM_LP_MMAP_HP_THP:
            /* We first try to mmap a huge page, and then if it fails, try to
             * use a THP. */
            p_page = (char *)mmap(NULL, pgsize, PROTS, FLAGS_HP, 0, 0);
            if ((void *)p_page != MAP_FAILED) {
                mmapped = ABT_TRUE;
                LOG_DEBUG(MMAP_DBG_MSG" (%d): %p\n", pgsize, p_page);
            } else {
                mmapped = ABT_FALSE;
                size_t alignment = gp_ABTI_global->huge_page_size;
                p_page = (char *)ABTU_memalign(alignment, pgsize);
                LOG_DEBUG("memalign a THP (%d): %p\n", pgsize, p_page);
            }
            break;

        case ABTI_MEM_LP_THP:
            mmapped = ABT_FALSE;
            size_t alignment = gp_ABTI_global->huge_page_size;
            p_page = (char *)ABTU_memalign(alignment, pgsize);
            LOG_DEBUG("memalign a THP (%d): %p\n", pgsize, p_page);
            break;

        default:
            ABTI_ASSERT(0);
            break;
    }
    if (!p_page) {
        *p_size = 0;
        return NULL;
    }
    p_page[0] = mmapped ? 1 : 0;
    *p_size = pgsize - 64;
    return (void *)(p_page + 64);
}

void ABTI_mem_free_page(void *ptr)
{
    char *p_page = ((char *)ptr) - 64;
    if (p_page[0] == 1) {
        // mmapped
        size_t pgsize = gp_ABTI_global->mem_page_size;
        munmap(p_page, pgsize);
    } else {
        ABTU_free(p_page);
    }
}

static inline
void ABTI_mem_local_pool_create_impl(ABTI_mem_local_pool *p_local_pool,
                                     int entry_index)
{
    int local_pool_max_blocks = ABTI_mem_get_local_pool_max_blocks(entry_index);
    memset(p_local_pool, 0, sizeof(ABTI_mem_local_pool));
    p_local_pool->blocks = (ABTI_mem_block *)
        ABTU_calloc(1, sizeof(ABTI_mem_block) * local_pool_max_blocks);
}

static inline
void ABTI_mem_local_pool_free_impl(ABTI_mem_local_pool *p_local_pool)
{
#if !ABT_MEM_USE_PAGE
    /* Free all objects. */
    const size_t blocki_from = 0;
    const size_t blocki_to = (p_local_pool->num_elements + ABT_MEM_BLOCKSIZE
                              - 1) >> ABT_MEM_BLOCKSIZE_LOG;
    int block_i;
    for (block_i = blocki_from; block_i < blocki_to; block_i++) {
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
        int num_elements_in_block = ABT_MEM_BLOCKSIZE;
        if (block_i == blocki_to - 1)
            num_elements_in_block = p_local_pool->num_elements
                                    - (block_i << ABT_MEM_BLOCKSIZE_LOG);
        ABTI_mem_block *p_block = &p_local_pool->blocks[block_i];
        int i;
        for (i = 0; i < num_elements_in_block; i++)
            ABTU_free(p_block->p_elements[i]);
#else /* ABT_MEM_USE_ARRAY_BASED_BLOCK */
        ABTI_mem_element *p_element = p_local_pool->blocks[block_i].p_head;
        while (p_element) {
            ABTI_mem_element *p_next = p_element->p_next;
            ABTU_free(p_element);
            p_element = p_next;
        }
#endif /* !ABT_MEM_USE_ARRAY_BASED_BLOCK */
    }
#endif /* !ABT_MEM_USE_PAGE */
    /* Free blocks. */
    ABTU_free(p_local_pool->blocks);
}

static
void ABTI_mem_local_pool_create(ABTI_local *p_local, int entry_index)
{
    ABTI_mem_local_pool *p_local_pool = &p_local->mem_pools[entry_index];
    ABTI_mem_local_pool_create_impl(p_local_pool, entry_index);
}

static
void ABTI_mem_local_pool_free(ABTI_local *p_local, int entry_index)
{
    ABTI_mem_local_pool *p_local_pool = &p_local->mem_pools[entry_index];
    ABTI_mem_local_pool_free_impl(p_local_pool);
}

static
void ABTI_mem_global_pool_create(ABTI_global *p_global, size_t element_size,
                                 int entry_index)
{
    ABTI_mem_global_pool *p_global_pool =
        (ABTI_mem_global_pool *)ABTU_calloc(1, sizeof(ABTI_mem_global_pool));
    p_global_pool->entry_index = entry_index;
    p_global_pool->element_size = element_size;
    ABTI_spinlock_create(&p_global_pool->lock);
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    ABTI_spinlock_create(&p_global_pool->external_pool_lock);
    p_global_pool->p_external_pool =
        (ABTI_mem_local_pool *)ABTU_malloc(sizeof(ABTI_mem_local_pool));
    ABTI_mem_local_pool_create_impl(p_global_pool->p_external_pool,
                                    entry_index);
#endif
    p_global->p_mem_pools[entry_index] = p_global_pool;
}

static
void ABTI_mem_global_pool_free(ABTI_global *p_global, int entry_index)
{
    ABTI_mem_global_pool *p_global_pool = p_global->p_mem_pools[entry_index];
#if ABT_MEM_USE_PAGE
    /* Free bulks. */
    ABTI_mem_bulk *p_bulk = p_global_pool->p_bulk;
    while (p_bulk) {
        ABTI_mem_bulk *p_next = p_bulk->p_next;
        ABTI_mem_free_page(p_bulk);
        p_bulk = p_next;
    }
#else /* ABT_MEM_USE_PAGE */
    /* Free all objects. */
    const size_t blocki_from = 0;
    const size_t blocki_to = (p_global_pool->num_elements + ABT_MEM_BLOCKSIZE
                             - 1) >> ABT_MEM_BLOCKSIZE_LOG;
    int block_i;
    for (block_i = blocki_from; block_i < blocki_to; block_i++) {
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
        int num_elements_in_block = ABT_MEM_BLOCKSIZE;
        if (block_i == blocki_to - 1)
            num_elements_in_block = p_global_pool->num_elements
                                    - (block_i << ABT_MEM_BLOCKSIZE_LOG);
        ABTI_mem_block *p_block = &p_global_pool->blocks[block_i];
        int i;
        for (i = 0; i < num_elements_in_block; i++)
            ABTU_free(p_block->p_elements[i]);
#else /* ABT_MEM_USE_ARRAY_BASED_BLOCK */
        ABTI_mem_element *p_element = p_global_pool->blocks[block_i].p_head;
        while (p_element) {
            ABTI_mem_element *p_next = p_element->p_next;
            ABTU_free(p_element);
            p_element = p_next;
        }
#endif /* !ABT_MEM_USE_ARRAY_BASED_BLOCK */
    }
#endif /* !ABT_MEM_USE_PAGE */
    /* Free lock. */
    ABTI_spinlock_free(&p_global_pool->lock);
#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    ABTI_spinlock_free(&p_global_pool->external_pool_lock);
    ABTI_mem_local_pool_free_impl(p_global_pool->p_external_pool);
    ABTU_free(p_global_pool->p_external_pool);
#endif
    /* Free blocks. */
    ABTU_free(p_global_pool->blocks);

    ABTU_free(p_global_pool);
}

void ABTI_mem_pool_take_blocks(int entry_index,
                               ABTI_mem_local_pool *p_local_pool)
{
    size_t global_to_local_num_blocks
        = ABTI_mem_get_global_to_local_num_blocks(entry_index);

    ABTI_ASSERT(p_local_pool->num_elements == 0);
    int num_taken_blocks = 0;
    size_t element_size = 0;
    ABTI_mem_global_pool *p_global_pool
        = gp_ABTI_global->p_mem_pools[entry_index];
    /* Critical section. Take blocks from a global pool. */
    ABTI_spinlock_acquire(&p_global_pool->lock);
    {
        int num_global_pool_full_blocks = p_global_pool->num_elements
                                          >> ABT_MEM_BLOCKSIZE_LOG;
        num_taken_blocks = (num_global_pool_full_blocks
                            < global_to_local_num_blocks)
                           ? num_global_pool_full_blocks
                           : global_to_local_num_blocks;
        if (num_taken_blocks != 0) {
            int global_block_i = num_global_pool_full_blocks;
            memcpy(p_local_pool->blocks,
                   &p_global_pool->blocks[global_block_i - num_taken_blocks],
                   sizeof(ABTI_mem_block) * num_taken_blocks);
            p_global_pool->num_elements -= num_taken_blocks
                                           << ABT_MEM_BLOCKSIZE_LOG;
            if ((p_global_pool->num_elements & (ABT_MEM_BLOCKSIZE - 1)) != 0)
                /* Copy the last block and clear the remaining. */
                memcpy(&p_global_pool->blocks[global_block_i
                                              - num_taken_blocks],
                       &p_global_pool->blocks[global_block_i],
                       sizeof(ABTI_mem_block));
        }
        if (global_to_local_num_blocks != num_taken_blocks) {
#if !ABT_MEM_USE_ARRAY_BASED_BLOCK
            size_t base_element_size = p_global_pool->element_size
                                       + sizeof(ABTI_mem_element);
#else
            size_t base_element_size = p_global_pool->element_size;
#endif
            element_size = (base_element_size + ABT_MEM_MALLOC_ALIGNMENT - 1)
                           & (~(ABT_MEM_MALLOC_ALIGNMENT - 1));
        }
    }
    ABTI_spinlock_release(&p_global_pool->lock);
    int num_remaining_blocks = global_to_local_num_blocks - num_taken_blocks;
    if (num_remaining_blocks) {
        int num_remaining_elements = num_remaining_blocks
                                     << ABT_MEM_BLOCKSIZE_LOG;
        ABTI_ASSERT(element_size);
        /* Allocate remaining blocks. */
#if ABT_MEM_USE_PAGE
        /* Use extra_mem_ptr. */
        int block_i = num_taken_blocks;
        ABTI_mem_block *blocks = p_local_pool->blocks;
        size_t extra_mem_size = p_local_pool->extra_mem_size;
        char *p_extra_mem_ptr = (char *)p_local_pool->p_extra_mem_ptr;

        ABTI_mem_block *p_block = &blocks[block_i];
        int block_num_elements = 0;
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
        ABTI_mem_element **p_block_elements = p_block->p_elements;
#else
        ABTI_mem_element *p_block_head = NULL;
        ABTI_mem_element *p_block_tail = NULL;
#endif
        while (num_remaining_elements) {
            if (!p_extra_mem_ptr) {
                /* Allocate new page and extract elements from it. */
                p_extra_mem_ptr = (char *)ABTI_mem_alloc_page(&extra_mem_size);
                ABTI_mem_bulk *p_bulk = (ABTI_mem_bulk *)p_extra_mem_ptr;
                p_extra_mem_ptr += sizeof(ABTI_mem_bulk);
                extra_mem_size -= sizeof(ABTI_mem_bulk);
                /* Add a new bulk. */
                ABTI_mem_atomic_insert_bulk(p_global_pool, p_bulk);
            }
            while (num_remaining_elements && extra_mem_size >= element_size) {
                ABTI_mem_element *p_new_element
                    = (ABTI_mem_element *)p_extra_mem_ptr;
                p_extra_mem_ptr += element_size;
                extra_mem_size -= element_size;
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
                p_block_elements[block_num_elements] = p_new_element;
#else
                p_new_element->p_next = NULL;
                if (block_num_elements == 0) {
                    p_block_head = p_new_element;
                    p_block_tail = p_new_element;
                } else {
                    p_block_tail->p_next = p_new_element;
                    p_block_tail = p_new_element;
                }
#endif
                block_num_elements++;
                if (block_num_elements == ABT_MEM_BLOCKSIZE) {
#if !ABT_MEM_USE_ARRAY_BASED_BLOCK
                    p_block->p_head = p_block_head;
                    p_block->p_tail = p_block_tail;
#endif
                    block_i++;
                    p_block = &blocks[block_i];
                    block_num_elements = 0;
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
                    p_block_elements = p_block->p_elements;
#endif
                }
                num_remaining_elements--;
            }
            if (extra_mem_size < element_size) {
                extra_mem_size = 0;
                p_extra_mem_ptr = NULL;
            }
        }
        p_local_pool->extra_mem_size = extra_mem_size;
        p_local_pool->p_extra_mem_ptr = p_extra_mem_ptr;
#else /* ABT_MEM_USE_PAGE */
        int block_i = num_taken_blocks;
        ABTI_mem_block *blocks = p_local_pool->blocks;
        ABTI_mem_block *p_block = &blocks[block_i];
        int block_num_elements = 0;
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
        ABTI_mem_element **p_block_elements = p_block->p_elements;
#else
        ABTI_mem_element *p_block_head = NULL;
        ABTI_mem_element *p_block_tail = NULL;
#endif
        int i;
        for (i = 0; i < num_remaining_elements; i++) {
            /* Allocate all single elements. */
            ABTI_mem_element *p_new_element
                = (ABTI_mem_element *)ABTU_malloc(element_size);
            /* Put it to a tail block. */
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
            p_block_elements[block_num_elements] = p_new_element;
#else
            p_new_element->p_next = NULL;
            if (block_num_elements == 0) {
                p_block_head = p_new_element;
                p_block_tail = p_new_element;
            } else {
                p_block_tail->p_next = p_new_element;
                p_block_tail = p_new_element;
            }
#endif
            block_num_elements++;
            if (block_num_elements == ABT_MEM_BLOCKSIZE) {
#if !ABT_MEM_USE_ARRAY_BASED_BLOCK
                p_block->p_head = p_block_head;
                p_block->p_tail = p_block_tail;
#endif
                block_i++;
                p_block = &blocks[block_i];
                block_num_elements = 0;
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
                p_block_elements = p_block->p_elements;
#endif
            }
        }
#endif /* !ABT_MEM_USE_PAGE */
        ABTI_ASSERT(block_num_elements == 0);
    }
    /* Update local_pool variable here. */
    p_local_pool->num_elements = global_to_local_num_blocks
                                 << ABT_MEM_BLOCKSIZE_LOG;
}

void ABTI_mem_pool_return_blocks(int entry_index,
                                 ABTI_mem_local_pool *p_local_pool)
{
    size_t local_pool_max_blocks
        = ABTI_mem_get_local_pool_max_blocks(entry_index);
    size_t local_to_global_num_blocks
        = ABTI_mem_get_local_to_global_num_blocks(entry_index);

    ABTI_ASSERT(p_local_pool->num_elements == (local_pool_max_blocks
                                               << ABT_MEM_BLOCKSIZE_LOG));
    ABTI_mem_global_pool *p_global_pool
        = gp_ABTI_global->p_mem_pools[entry_index];

    ABTI_spinlock_acquire(&p_global_pool->lock);
    int len_required_blocks = ((p_global_pool->num_elements + ABT_MEM_BLOCKSIZE
                               - 1) >> ABT_MEM_BLOCKSIZE_LOG)
                              + local_to_global_num_blocks;
    ABTI_mem_block *blocks = p_global_pool->blocks;
    if (p_global_pool->len_blocks < len_required_blocks) {
        /* Extend blocks. */
        const int len_blocks = p_global_pool->len_blocks;
        int new_len_blocks = (len_blocks * 2 < len_required_blocks)
                             ? len_required_blocks : (len_blocks * 2);
        ABTI_mem_block *new_blocks = (ABTI_mem_block *)
            ABTU_realloc(blocks, sizeof(ABTI_mem_block) * new_len_blocks);
        p_global_pool->len_blocks = new_len_blocks;
        blocks = new_blocks;
        p_global_pool->blocks = new_blocks;
    }
    /* Return blocks to a global pool. */
    {
        int block_i = p_global_pool->num_elements >> ABT_MEM_BLOCKSIZE_LOG;
        if ((p_global_pool->num_elements & (ABT_MEM_BLOCKSIZE - 1)) != 0) {
            /* Copy the last block of a global pool. */
            memcpy(&blocks[block_i + local_to_global_num_blocks],
                   &blocks[block_i], sizeof(ABTI_mem_block));
        }
        memcpy(&blocks[block_i],
               &p_local_pool->blocks[local_pool_max_blocks
                                     - local_to_global_num_blocks],
               sizeof(ABTI_mem_block) * local_to_global_num_blocks);
    }
    p_global_pool->num_elements += local_to_global_num_blocks
                                   << ABT_MEM_BLOCKSIZE_LOG;
#if !ABT_MEM_USE_PAGE
    /* Free objects if # of blocks is more than GLOBALPOOL_NUM_BLOCKS. */
    if (p_global_pool->num_elements > ABT_MEM_GLOBALPOOL_NUM_BLOCKS
                                      * ABT_MEM_BLOCKSIZE) {
        const size_t blocki_from = ABT_MEM_GLOBALPOOL_NUM_BLOCKS;
        const size_t blocki_to = (p_global_pool->num_elements
                                  + ABT_MEM_BLOCKSIZE - 1)
                                 >> ABT_MEM_BLOCKSIZE_LOG;
        int block_i;
        for (block_i = blocki_from; block_i < blocki_to; block_i++) {
#if ABT_MEM_USE_ARRAY_BASED_BLOCK
            int num_elements_in_block = ABT_MEM_BLOCKSIZE;
            if (block_i == blocki_to - 1)
                num_elements_in_block = p_global_pool->num_elements
                                        - (block_i << ABT_MEM_BLOCKSIZE_LOG);
            ABTI_mem_block *p_block = &p_global_pool->blocks[block_i];
            int i;
            for (i = 0; i < num_elements_in_block; i++)
                ABTU_free(p_block->p_elements[i]);
#else
            ABTI_mem_element *p_element = blocks[block_i].p_head;
            while (p_element) {
                ABTI_mem_element *p_next = p_element->p_next;
                ABTU_free(p_element);
                p_element = p_next;
            }
#endif
        }
        p_global_pool->num_elements = ABT_MEM_GLOBALPOOL_NUM_BLOCKS
                                      * ABT_MEM_BLOCKSIZE;
    }
#endif /* !ABT_MEM_USE_PAGE */
    ABTI_spinlock_release(&p_global_pool->lock);
    p_local_pool->num_elements -= local_to_global_num_blocks
                                  << ABT_MEM_BLOCKSIZE_LOG;
}

#endif /* ABT_CONFIG_USE_MEM_POOL */

