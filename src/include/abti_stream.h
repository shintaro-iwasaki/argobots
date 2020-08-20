/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_XSTREAM_H_INCLUDED
#define ABTI_XSTREAM_H_INCLUDED

/* Inlined functions for Execution Stream (ES) */

static inline ABTI_xstream *ABTI_xstream_get_ptr(ABT_xstream xstream)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_xstream *p_xstream;
    if (xstream == ABT_XSTREAM_NULL) {
        p_xstream = NULL;
    } else {
        p_xstream = (ABTI_xstream *)xstream;
    }
    return p_xstream;
#else
    return (ABTI_xstream *)xstream;
#endif
}

static inline ABT_xstream ABTI_xstream_get_handle(ABTI_xstream *p_xstream)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_xstream h_xstream;
    if (p_xstream == NULL) {
        h_xstream = ABT_XSTREAM_NULL;
    } else {
        h_xstream = (ABT_xstream)p_xstream;
    }
    return h_xstream;
#else
    return (ABT_xstream)p_xstream;
#endif
}

static inline void ABTI_xstream_set_request(ABTI_xstream *p_xstream,
                                            uint32_t req)
{
    ABTD_atomic_fetch_or_uint32(&p_xstream->request, req);
}

static inline void ABTI_xstream_unset_request(ABTI_xstream *p_xstream,
                                              uint32_t req)
{
    ABTD_atomic_fetch_and_uint32(&p_xstream->request, ~req);
}

/* Get the first pool of the main scheduler. */
static inline ABTI_pool *ABTI_xstream_get_main_pool(ABTI_xstream *p_xstream)
{
    ABT_pool pool = p_xstream->p_main_sched->pools[0];
    return ABTI_pool_get_ptr(pool);
}

static inline void ABTI_xstream_terminate_ythread(ABTI_xstream *p_local_xstream,
                                                  ABTI_ythread *p_ythread)
{
    LOG_DEBUG("[U%" PRIu64 ":E%d] terminated\n",
              ABTI_thread_get_id(&p_ythread->thread),
              p_ythread->thread.p_last_xstream->rank);
    if (!(p_ythread->thread.type & ABTI_THREAD_TYPE_NAMED)) {
        ABTD_atomic_release_store_int(&p_ythread->thread.state,
                                      ABTI_THREAD_STATE_TERMINATED);
        ABTI_thread_free(p_local_xstream, &p_ythread->thread);
    } else {
        /* NOTE: We set the ULT's state as TERMINATED after checking refcount
         * because the ULT can be freed on a different ES.  In other words, we
         * must not access any field of p_thead after changing the state to
         * TERMINATED. */
        ABTD_atomic_release_store_int(&p_ythread->thread.state,
                                      ABTI_THREAD_STATE_TERMINATED);
    }
}

static inline void ABTI_xstream_terminate_task(ABTI_xstream *p_local_xstream,
                                               ABTI_thread *p_task)
{
    LOG_DEBUG("[T%" PRIu64 ":E%d] terminated\n", ABTI_thread_get_id(p_task),
              p_task->p_last_xstream->rank);
    if (!(p_task->type & ABTI_THREAD_TYPE_NAMED)) {
        ABTD_atomic_release_store_int(&p_task->state,
                                      ABTI_THREAD_STATE_TERMINATED);
        ABTI_task_free(p_local_xstream, p_task);
    } else {
        /* NOTE: We set the task's state as TERMINATED after checking refcount
         * because the task can be freed on a different ES.  In other words, we
         * must not access any field of p_task after changing the state to
         * TERMINATED. */
        ABTD_atomic_release_store_int(&p_task->state,
                                      ABTI_THREAD_STATE_TERMINATED);
    }
}

/* Get the native thread id associated with the target xstream. */
static inline ABTI_native_thread_id
ABTI_xstream_get_native_thread_id(ABTI_xstream *p_xstream)
{
    return (ABTI_native_thread_id)p_xstream;
}

#endif /* ABTI_XSTREAM_H_INCLUDED */
