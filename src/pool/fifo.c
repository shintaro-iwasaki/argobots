/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"
#include <time.h>

static inline
double get_cur_time(void)
{
#if defined(HAVE_CLOCK_GETTIME)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((double)ts.tv_sec); + 1.0e-9 * ((double)ts.tv_nsec);
#elif defined(HAVE_GETTIMEOFDAY)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((double)tv.tv_sec) + 1.0e-6 * ((double)tv.tv_usec);
#else
#error "No timer function available"
    return 0.0;
#endif
}

/* FIFO pool implementation */

static int      pool_init(ABT_pool pool, ABT_pool_config config);
static int      pool_free(ABT_pool pool);
static size_t   pool_get_size(ABT_pool pool);
static void     pool_push_shared(ABT_pool pool, ABT_unit unit);
static void     pool_push_private(ABT_pool pool, ABT_unit unit);
static ABT_unit pool_pop_shared(ABT_pool pool);
static ABT_unit pool_pop_private(ABT_pool pool);
static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs);
static int      pool_remove_shared(ABT_pool pool, ABT_unit unit);
static int      pool_remove_private(ABT_pool pool, ABT_unit unit);

typedef ABTI_unit unit_t;
static ABT_unit_type unit_get_type(ABT_unit unit);
static ABT_thread unit_get_thread(ABT_unit unit);
static ABT_task unit_get_task(ABT_unit unit);
static ABT_bool unit_is_in_pool(ABT_unit unit);
static ABT_unit unit_create_from_thread(ABT_thread thread);
static ABT_unit unit_create_from_task(ABT_task task);
static void unit_free(ABT_unit *unit);

#define ABT_CONFIG_USE_CCSYNCH

#ifdef ABT_CONFIG_USE_CCSYNCH

struct ccsynch_node_t;
typedef struct ccsynch_node_t ccsynch_node;
struct ccsynch_node_t;

typedef struct ccsynch_node_t {
    ccsynch_node *p_next;
    char _padding[ABT_CONFIG_STATIC_CACHELINE_SIZE - sizeof(ccsynch_node *)];
    void (*apply)(ABT_pool, void *);
    void *p_arg;
    uint32_t status;
} ccsynch_node;

typedef struct ccsynch_handle_t {
    ccsynch_node *p_next;
} ccsynch_handle;

typedef struct ccsynch_t {
    ccsynch_node *p_tail;
} ccsynch;

#define CCSYNCH_WAIT  ((uint32_t)0x0)
#define CCSYNCH_READY ((uint32_t)0x1)
#define CCSYNCH_DONE  ((uint32_t)0x3)

static inline
int ccsynch_apply(ccsynch *p_synch, ccsynch_handle *p_handle,
                  void (*apply)(ABT_pool, void *), ABT_pool pool,
                  void *p_arg, int is_try)
{
    if (is_try && p_synch->p_tail->status != CCSYNCH_READY) {
        // pool is locked.
        return ABT_ERR_OTHER;
    }
    ccsynch_node *p_next = p_handle->p_next;
    if (!p_next) {
        p_next = (ccsynch_node *)ABTU_memalign(ABT_CONFIG_STATIC_CACHELINE_SIZE,
                                               sizeof(ccsynch_node));
    }
    p_next->p_next = NULL;
    p_next->status = CCSYNCH_WAIT;

    ccsynch_node *p_curr = (ccsynch_node *)
        ABTD_atomic_exchange_ptr((void **)&p_synch->p_tail, (void *)p_next);
    p_handle->p_next = p_curr;

    uint32_t status = ABTD_atomic_load_uint32(&p_curr->status);

    if (status == CCSYNCH_WAIT) {
        p_curr->p_arg = p_arg;
        p_curr->apply = apply;
        ABTD_atomic_store_ptr((void **)&p_curr->p_next, p_next);

        do {
            ABTD_atomic_pause();
            status = ABTD_atomic_load_uint32(&p_curr->status);
        } while (status == CCSYNCH_WAIT);
    }

    if (status != CCSYNCH_DONE) {
        apply(pool, p_arg);

        p_curr = p_next;
        p_next = (ccsynch_node *)
                 ABTD_atomic_load_ptr((void **)&p_curr->p_next);

        int count = 0;
        const int CCSYNCH_HELP_BOUND = 256;

        while (p_next && count++ < CCSYNCH_HELP_BOUND) {
            p_curr->apply(pool, p_curr->p_arg);
            ABTD_atomic_store_uint32(&p_curr->status, CCSYNCH_DONE);

            p_curr = p_next;
            p_next = (ccsynch_node *)
                     ABTD_atomic_load_ptr((void **)&p_curr->p_next);
        }

        ABTD_atomic_store_uint32(&p_curr->status, CCSYNCH_READY);
    }
    return ABT_SUCCESS;
}

static inline void ccsynch_init(ccsynch *p_synch)
{
    ccsynch_node *p_node = (ccsynch_node *)
        ABTU_memalign(ABT_CONFIG_STATIC_CACHELINE_SIZE, sizeof(ccsynch_node));
    p_node->p_next = NULL;
    p_node->status = CCSYNCH_READY;

    p_synch->p_tail = p_node;
}

static inline void ccsynch_handle_init(ccsynch_handle *p_handle)
{
    p_handle->p_next = NULL;
}

static inline void ccsynch_free(ccsynch *p_synch)
{
    ABTU_free(p_synch->p_tail);
}

static inline void ccsynch_handle_free(ccsynch_handle *p_handle)
{
    if (p_handle->p_next)
        ABTU_free(p_handle->p_next);
}

#endif /* ABT_CONFIG_USE_CCSYNCH */

#ifndef ABT_CONFIG_USE_CCSYNCH

struct data {
    ABTI_spinlock mutex;
    size_t num_units;
    unit_t *p_head;
    unit_t *p_tail;
};
typedef struct data data_t;

#else

struct data {
    /* Constant after initializing data */
    ccsynch *p_synch;
    int num_handles;
    ccsynch_handle **p_handles;
    /* Only called by external threads. */
    ccsynch_handle *p_ext_handle;
    union {
        ABTI_spinlock mutex; /* dummy */
        ABTI_spinlock ext_handle_lock;
    };
    __attribute__ ((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    size_t num_units;
    unit_t *p_head;
    unit_t *p_tail;
    __attribute__ ((aligned(ABT_CONFIG_STATIC_CACHELINE_SIZE)))
    int is_empty; // whether this pool is empty or not.
};
typedef struct data data_t;

#endif

static inline data_t *pool_get_data_ptr(void *p_data)
{
    return (data_t *)p_data;
}


/* Obtain the FIFO pool definition according to the access type */
int ABTI_pool_get_fifo_def(ABT_pool_access access, ABT_pool_def *p_def)
{
    int abt_errno = ABT_SUCCESS;

    /* Definitions according to the access type */
    /* FIXME: need better implementation, e.g., lock-free one */
    switch (access) {
        case ABT_POOL_ACCESS_PRIV:
            p_def->p_push   = pool_push_private;
            p_def->p_pop    = pool_pop_private;
            p_def->p_remove = pool_remove_private;
            break;

        case ABT_POOL_ACCESS_SPSC:
        case ABT_POOL_ACCESS_MPSC:
        case ABT_POOL_ACCESS_SPMC:
        case ABT_POOL_ACCESS_MPMC:
            p_def->p_push   = pool_push_shared;
            p_def->p_pop    = pool_pop_shared;
            p_def->p_remove = pool_remove_shared;
            break;

        default:
            ABTI_CHECK_TRUE(0, ABT_ERR_INV_POOL_ACCESS);
    }

    /* Common definitions regardless of the access type */
    p_def->access               = access;
    p_def->p_init               = pool_init;
    p_def->p_free               = pool_free;
    p_def->p_get_size           = pool_get_size;
    p_def->p_pop_timedwait      = pool_pop_timedwait;
    p_def->u_get_type           = unit_get_type;
    p_def->u_get_thread         = unit_get_thread;
    p_def->u_get_task           = unit_get_task;
    p_def->u_is_in_pool         = unit_is_in_pool;
    p_def->u_create_from_thread = unit_create_from_thread;
    p_def->u_create_from_task   = unit_create_from_task;
    p_def->u_free               = unit_free;

  fn_exit:
    return abt_errno;

  fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}


/* Pool functions */

#ifndef ABT_CONFIG_USE_CCSYNCH

int pool_init(ABT_pool pool, ABT_pool_config config)
{
    ABTI_UNUSED(config);
    int abt_errno = ABT_SUCCESS;
    ABT_pool_access access;

    data_t *p_data = (data_t *)ABTU_malloc(sizeof(data_t));

    ABT_pool_get_access(pool, &access);

    if (access != ABT_POOL_ACCESS_PRIV) {
        /* Initialize the mutex */
        ABTI_spinlock_create(&p_data->mutex);
    }

    p_data->num_units = 0;
    p_data->p_head = NULL;
    p_data->p_tail = NULL;

    ABT_pool_set_data(pool, p_data);

    return abt_errno;
}

#else

static inline size_t get_aligned_size(size_t size) {
    return ((size + ABT_CONFIG_STATIC_CACHELINE_SIZE - 1)
            / ABT_CONFIG_STATIC_CACHELINE_SIZE)
           * ABT_CONFIG_STATIC_CACHELINE_SIZE;
}

int pool_init(ABT_pool pool, ABT_pool_config config)
{
    ABTI_UNUSED(config);
    int abt_errno = ABT_SUCCESS;
    ABT_pool_access access;
    int num_handles = gp_ABTI_global->max_xstreams;
    size_t data_size = get_aligned_size(sizeof(data_t));
    size_t synch_size = get_aligned_size(sizeof(ccsynch));
    size_t p_handles_size = get_aligned_size(sizeof(ccsynch_handle *)
                                             * num_handles);
    size_t handle_size = get_aligned_size(sizeof(ccsynch_handle));
    size_t handles_size = handle_size * num_handles;
    size_t buffer_size;

    ABT_pool_get_access(pool, &access);

    if (access == ABT_POOL_ACCESS_PRIV) {
        buffer_size = data_size;
    } else {
        buffer_size = data_size + synch_size + p_handles_size +
                      handles_size * 2;
    }

    char *p_buffer = (char *)ABTU_memalign(ABT_CONFIG_STATIC_CACHELINE_SIZE,
                                           buffer_size);
    data_t *p_data = (data_t *)p_buffer;
    p_buffer += data_size;

    if (access != ABT_POOL_ACCESS_PRIV) {
        int i;
        /* Initialize synch */
        p_data->p_synch = (ccsynch *)p_buffer;
        p_buffer += synch_size;
        ccsynch_init(p_data->p_synch);
        p_data->num_handles = num_handles;
        p_data->p_handles = (ccsynch_handle **)p_buffer;
        p_buffer += p_handles_size;
        for (i = 0; i < num_handles; i++) {
            ccsynch_handle *p_handle = (ccsynch_handle *)p_buffer;
            p_buffer += handle_size;
            ccsynch_handle_init(p_handle);
            p_data->p_handles[i] = p_handle;
        }

        ABTI_spinlock_create(&p_data->ext_handle_lock);

        p_data->p_ext_handle = (ccsynch_handle *)p_buffer;
        p_buffer += handles_size;
        ccsynch_handle_init(p_data->p_ext_handle);
    }

    p_data->num_units = 0;
    p_data->is_empty = 1;
    p_data->p_head = NULL;
    p_data->p_tail = NULL;

    ABT_pool_set_data(pool, p_data);

    return abt_errno;
}

#endif /* ABT_CONFIG_USE_CCSYNCH */

#ifndef ABT_CONFIG_USE_CCSYNCH

static int pool_free(ABT_pool pool)
{
    int abt_errno = ABT_SUCCESS;
    ABT_pool_access access;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);

    ABT_pool_get_access(pool, &access);
    if (access != ABT_POOL_ACCESS_PRIV) {
        ABTI_spinlock_free(&p_data->mutex);
    }

    ABTU_free(p_data);

    return abt_errno;
}

#else

static int pool_free(ABT_pool pool)
{
    int abt_errno = ABT_SUCCESS;
    ABT_pool_access access;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);

    ABT_pool_get_access(pool, &access);
    if (access != ABT_POOL_ACCESS_PRIV) {
        int i;
        ABTI_spinlock_free(&p_data->ext_handle_lock);
        for (i = 0; i < p_data->num_handles; i++)
            ccsynch_handle_free(p_data->p_handles[i]);
        ccsynch_free(p_data->p_synch);
    }

    ABTU_free(p_data);

    return abt_errno;
}

#endif /* ABT_CONFIG_USE_CCSYNCH */

static size_t pool_get_size(ABT_pool pool)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);
    return p_data->num_units;
}

#ifdef ABT_CONFIG_USE_CCSYNCH

static inline int pool_apply_shared(void (*apply)(ABT_pool, void *),
                                    ABT_pool pool, void *p_arg, int is_try)
{
    int rank;
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);
    ccsynch_handle *p_handle;
    ccsynch *p_synch = p_data->p_synch;

#ifndef ABT_CONFIG_DISABLE_EXT_THREAD
    if (!lp_ABTI_local) {
        rank = -1;
    } else {
        rank = ABTI_local_get_xstream()->rank;
    }
#else
    rank = ABTI_local_get_xstream()->rank;
#endif
    if (0 <= rank && rank < p_data->num_handles) {
        p_handle = p_data->p_handles[rank];
        return ccsynch_apply(p_synch, p_handle, apply, pool, p_arg, is_try);
    } else {
        /* Only one execution stream can use this handle. */
        p_handle = p_data->p_ext_handle;
        ABTI_spinlock_acquire(&p_data->ext_handle_lock);
        int ret = ccsynch_apply(p_synch, p_handle, apply, pool, p_arg, is_try);
        ABTI_spinlock_release(&p_data->ext_handle_lock);
        return ret;
    }
}

#endif /* ABT_CONFIG_USE_CCSYNCH */

static void pool_push_impl(ABT_pool pool, ABT_unit unit, int shared)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);
    unit_t *p_unit = (unit_t *)unit;

    if (shared) {
        ABTI_spinlock_acquire(&p_data->mutex);
    }
    if (p_data->num_units == 0) {
        p_data->p_head = p_unit;
        p_data->p_tail = p_unit;
        p_data->num_units++;
        ABTD_atomic_store_int32(&p_data->is_empty, 0);
        p_unit->p_prev = p_unit;
        p_unit->p_next = p_unit;
    } else {
        unit_t *p_head = p_data->p_head;
        unit_t *p_tail = p_data->p_tail;
        p_tail->p_next = p_unit;
        p_head->p_prev = p_unit;
        p_unit->p_prev = p_tail;
        p_unit->p_next = p_head;
        p_data->p_tail = p_unit;
        p_data->num_units++;
    }

    p_unit->pool = pool;
    if (shared) {
        ABTI_spinlock_release(&p_data->mutex);
    }
}

#ifndef ABT_CONFIG_USE_CCSYNCH

static void pool_push_shared(ABT_pool pool, ABT_unit unit)
{
    pool_push_impl(pool, unit, 1);
}

#else

static void pool_apply_push(ABT_pool pool, void *p_arg)
{
    ABT_unit *p_unit = (ABT_unit *) p_arg;
    pool_push_impl(pool, *p_unit, 0);
}

static void pool_push_shared(ABT_pool pool, ABT_unit unit)
{
    int is_try = 0;
    pool_apply_shared(pool_apply_push, pool, &unit, is_try);
}

#endif /* ABT_CONFIG_USE_CCSYNCH */

static void pool_push_private(ABT_pool pool, ABT_unit unit)
{
    pool_push_impl(pool, unit, 0);
}

static inline ABT_unit pool_pop_impl(ABT_pool pool, int shared)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);
    unit_t *p_unit = NULL;
    ABT_unit h_unit = ABT_UNIT_NULL;

    if (shared) {
        ABTI_spinlock_acquire(&p_data->mutex);
    }

    if (!p_data->is_empty) {
        p_unit = p_data->p_head;
        if (p_data->num_units == 1) {
            p_data->p_head = NULL;
            p_data->p_tail = NULL;
            p_data->num_units = 0;
            ABTD_atomic_store_int32(&p_data->is_empty, 1);
        } else {
            p_unit->p_prev->p_next = p_unit->p_next;
            p_unit->p_next->p_prev = p_unit->p_prev;
            p_data->p_head = p_unit->p_next;
            p_data->num_units--;
        }
        p_unit->p_prev = NULL;
        p_unit->p_next = NULL;
        p_unit->pool = ABT_POOL_NULL;

        h_unit = (ABT_unit)p_unit;
    }
    if (shared) {
        ABTI_spinlock_release(&p_data->mutex);
    }

    return h_unit;
}

#ifndef ABT_CONFIG_USE_CCSYNCH

static ABT_unit pool_pop_shared(ABT_pool pool)
{
    return pool_pop_impl(pool, 1);
}

#else

static void pool_apply_pop(ABT_pool pool, void *p_arg)
{
    ABT_unit *p_unit = (ABT_unit *) p_arg;
    *p_unit = pool_pop_impl(pool, 0);
}

static ABT_unit pool_pop_shared(ABT_pool pool)
{
    /* Quick check. */
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);
    if (ABTD_atomic_load_int32(&p_data->is_empty))
        return ABT_UNIT_NULL;

    ABT_unit unit = (void*)0;
    const int is_try = 0;
    if (pool_apply_shared(pool_apply_pop, pool, &unit, is_try) != ABT_SUCCESS) {
        return ABT_UNIT_NULL;
    }
    return unit;
}

#endif /* ABT_CONFIG_USE_CCSYNCH */

static ABT_unit pool_pop_private(ABT_pool pool)
{
    return pool_pop_impl(pool, 0);
}

#ifndef ABT_CONFIG_USE_CCSYNCH

static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);
    unit_t *p_unit = NULL;
    ABT_unit h_unit = ABT_UNIT_NULL;

    double time_start = get_cur_time();

    do {
        ABTI_spinlock_acquire(&p_data->mutex);
        if (p_data->num_units > 0) {
            p_unit = p_data->p_head;
            if (p_data->num_units == 1) {
                p_data->p_head = NULL;
                p_data->p_tail = NULL;
            } else {
                p_unit->p_prev->p_next = p_unit->p_next;
                p_unit->p_next->p_prev = p_unit->p_prev;
                p_data->p_head = p_unit->p_next;
            }
            p_data->num_units--;

            p_unit->p_prev = NULL;
            p_unit->p_next = NULL;
            p_unit->pool = ABT_POOL_NULL;

            h_unit = (ABT_unit)p_unit;
            ABTI_spinlock_release(&p_data->mutex);
        } else {
            ABTI_spinlock_release(&p_data->mutex);
            /* Sleep. */
            const int sleep_nsecs = 100;
            struct timespec ts = {0, sleep_nsecs};
            nanosleep(&ts, NULL);

            struct timespec ts_end;
            double elapsed = get_cur_time() - time_start;
            if (elapsed > abstime_secs)
                break;
        }
    } while(h_unit == ABT_UNIT_NULL);

    return h_unit;
}

#else

static ABT_unit pool_pop_timedwait(ABT_pool pool, double abstime_secs)
{
    ABT_unit h_unit = ABT_UNIT_NULL;

    double time_start = get_cur_time();

    do {
        ABT_unit h_unit = pool_pop_shared(pool);
        if (h_unit != ABT_UNIT_NULL)
            break;
        /* Sleep. */
        const int sleep_nsecs = 100;
        struct timespec ts = {0, sleep_nsecs};
        nanosleep(&ts, NULL);

        struct timespec ts_end;
        double elapsed = get_cur_time() - time_start;
        if (elapsed > abstime_secs)
            break;
    } while(1);

    return h_unit;
}

#endif /* ABT_CONFIG_USE_CCSYNCH */


static inline int pool_remove_impl(ABT_pool pool, ABT_unit unit, int shared)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);
    unit_t *p_unit = (unit_t *)unit;

    ABTI_CHECK_TRUE_RET(p_data->num_units != 0, ABT_ERR_POOL);
    ABTI_CHECK_TRUE_RET(p_unit->pool != ABT_POOL_NULL, ABT_ERR_POOL);
    ABTI_CHECK_TRUE_MSG_RET(p_unit->pool == pool, ABT_ERR_POOL, "Not my pool");

    if (shared) {
        ABTI_spinlock_acquire(&p_data->mutex);
    }
    if (p_data->num_units == 1) {
        p_data->p_head = NULL;
        p_data->p_tail = NULL;
        p_data->num_units = 0;
        ABTD_atomic_store_int32(&p_data->is_empty, 1);
    } else {
        p_unit->p_prev->p_next = p_unit->p_next;
        p_unit->p_next->p_prev = p_unit->p_prev;
        if (p_unit == p_data->p_head) {
            p_data->p_head = p_unit->p_next;
        } else if (p_unit == p_data->p_tail) {
            p_data->p_tail = p_unit->p_prev;
        }
        p_data->num_units--;
    }

    p_unit->pool = ABT_POOL_NULL;
    if (shared) {
        ABTI_spinlock_release(&p_data->mutex);
    }

    p_unit->p_prev = NULL;
    p_unit->p_next = NULL;

    return ABT_SUCCESS;
}

#ifndef ABT_CONFIG_USE_CCSYNCH

static int pool_remove_shared(ABT_pool pool, ABT_unit unit)
{
    return pool_remove_impl(pool, unit, 1);
}

#else

static void pool_apply_remove(ABT_pool pool, void *p_arg)
{
    ABT_unit *p_unit = (ABT_unit *) p_arg;
    pool_remove_impl(pool, *p_unit, 0);
}

static int pool_remove_shared(ABT_pool pool, ABT_unit unit)
{
    const int is_try = 0;
    pool_apply_shared(pool_apply_remove, pool, &unit, is_try);
    return ABT_SUCCESS;
}

#endif /* ABT_CONFIG_USE_CCSYNCH */

static int pool_remove_private(ABT_pool pool, ABT_unit unit)
{
    return pool_remove_impl(pool, unit, 0);
}

#if 0
int pool_print(ABT_pool pool)
{
    ABTI_pool *p_pool = ABTI_pool_get_ptr(pool);
    void *data = ABTI_pool_get_data(p_pool);
    data_t *p_data = pool_get_data_ptr(data);
    printf("[");
    printf("num_units: %zu ", p_data->num_units);
    printf("head: %p ", p_data->p_head);
    printf("tail: %p", p_data->p_tail);
    printf("]");

    return ABT_SUCCESS;
}
#endif

/* Unit functions */

static ABT_unit_type unit_get_type(ABT_unit unit)
{
   unit_t *p_unit = (unit_t *)unit;
   return p_unit->type;
}

static ABT_thread unit_get_thread(ABT_unit unit)
{
    ABT_thread h_thread;
    unit_t *p_unit = (unit_t *)unit;
    if (p_unit->type == ABT_UNIT_TYPE_THREAD) {
        h_thread = p_unit->thread;
    } else {
        h_thread = ABT_THREAD_NULL;
    }
    return h_thread;
}

static ABT_task unit_get_task(ABT_unit unit)
{
    ABT_task h_task;
    unit_t *p_unit = (unit_t *)unit;
    if (p_unit->type == ABT_UNIT_TYPE_TASK) {
        h_task = p_unit->task;
    } else {
        h_task = ABT_TASK_NULL;
    }
    return h_task;
}

static ABT_bool unit_is_in_pool(ABT_unit unit)
{
    unit_t *p_unit = (unit_t *)unit;
    return (p_unit->pool != ABT_POOL_NULL) ? ABT_TRUE : ABT_FALSE;
}

static ABT_unit unit_create_from_thread(ABT_thread thread)
{
    ABTI_thread *p_thread = ABTI_thread_get_ptr(thread);
    unit_t *p_unit = &p_thread->unit_def;
    p_unit->p_prev = NULL;
    p_unit->p_next = NULL;
    p_unit->pool   = ABT_POOL_NULL;
    p_unit->thread = thread;
    p_unit->type   = ABT_UNIT_TYPE_THREAD;

    return (ABT_unit)p_unit;
}

static ABT_unit unit_create_from_task(ABT_task task)
{
    ABTI_task *p_task = ABTI_task_get_ptr(task);
    unit_t *p_unit = &p_task->unit_def;
    p_unit->p_prev = NULL;
    p_unit->p_next = NULL;
    p_unit->pool   = ABT_POOL_NULL;
    p_unit->task   = task;
    p_unit->type   = ABT_UNIT_TYPE_TASK;

    return (ABT_unit)p_unit;
}

static void unit_free(ABT_unit *unit)
{
    *unit = ABT_UNIT_NULL;
}

