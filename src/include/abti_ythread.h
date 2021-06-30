/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_YTHREAD_H_INCLUDED
#define ABTI_YTHREAD_H_INCLUDED

/* Inlined functions for yieldable threads */

static inline ABTI_ythread *ABTI_ythread_get_ptr(ABT_thread thread)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_ythread *p_ythread;
    if (thread == ABT_THREAD_NULL) {
        p_ythread = NULL;
    } else {
        p_ythread = (ABTI_ythread *)thread;
    }
    return p_ythread;
#else
    return (ABTI_ythread *)thread;
#endif
}

static inline ABT_thread ABTI_ythread_get_handle(ABTI_ythread *p_ythread)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_thread h_thread;
    if (p_ythread == NULL) {
        h_thread = ABT_THREAD_NULL;
    } else {
        h_thread = (ABT_thread)p_ythread;
    }
    return h_thread;
#else
    return (ABT_thread)p_ythread;
#endif
}

static inline ABTI_ythread *
ABTI_ythread_context_get_ythread(ABTD_ythread_context *p_ctx)
{
    return (ABTI_ythread *)(((char *)p_ctx) - offsetof(ABTI_ythread, ctx));
}

ABTU_noreturn static inline void ABTI_ythread_context_jump(ABTI_ythread *p_old,
                                                           ABTI_ythread *p_new)
{
    ABTD_ythread_context_jump(&p_old->ctx, &p_new->ctx);
    ABTU_unreachable();
}

static inline void ABTI_ythread_context_switch(ABTI_ythread *p_old,
                                                        ABTI_ythread *p_new)
{
    ABTD_ythread_context_switch(&p_old->ctx, &p_new->ctx);
    /* Return the previous thread. */
}

ABTU_noreturn static inline void
ABTI_ythread_context_jump_with_call(ABTI_ythread *p_old, ABTI_ythread *p_new,
                                    void (*f_cb)(void *), void *cb_arg)
{
    ABTD_ythread_context_jump_with_call(&p_old->ctx, &p_new->ctx, f_cb, cb_arg);
    ABTU_unreachable();
}

static inline void
ABTI_ythread_context_switch_with_call(ABTI_ythread *p_old, ABTI_ythread *p_new,
                                      void (*f_cb)(void *), void *cb_arg)
{
    ABTD_ythread_context_switch_with_call(&p_old->ctx, &p_new->ctx, f_cb,
                                              cb_arg);
    /* Return the previous thread. */
}

ABTU_noreturn static inline void
ABTI_ythread_jump_to_primary(ABTI_xstream *p_local_xstream, ABTI_ythread *p_old,
                             ABTI_ythread *p_new)
{
    p_local_xstream->p_thread = &p_new->thread;
    p_new->thread.p_last_xstream = p_local_xstream;
    ABTI_ythread_context_jump(p_old, p_new);
    ABTU_unreachable();
}

static inline void ABTI_ythread_switch_to_sibling_nocb_internal(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, ABTI_ythread *p_new,
    ABT_bool is_finish, ABT_sync_event_type sync_event_type, void *p_sync)
{
    p_new->thread.p_parent = p_old->thread.p_parent;
    if (is_finish) {
        ABTI_xstream *p_local_xstream = *pp_local_xstream;
        ABTI_event_thread_finish(p_local_xstream, &p_old->thread,
                                 p_old->thread.p_parent);
        ABTI_event_thread_run(p_local_xstream, &p_new->thread, &p_old->thread,
                              p_new->thread.p_parent);
        p_local_xstream->p_thread = &p_new->thread;
        p_new->thread.p_last_xstream = p_local_xstream;
        ABTI_ythread_context_jump(p_old, p_new);
        ABTU_unreachable();
    } else {
        ABTI_xstream *p_local_xstream = *pp_local_xstream;
        ABTI_event_thread_run(p_local_xstream, &p_new->thread, &p_old->thread,
                              p_new->thread.p_parent);
        p_local_xstream->p_thread = &p_new->thread;
        p_new->thread.p_last_xstream = p_local_xstream;
        /* Context switch starts. */
        ABTI_ythread_context_switch(p_old, p_new);
        /* Context switch finishes. */
        *pp_local_xstream = p_old->thread.p_last_xstream;
    }
}

static inline void ABTI_ythread_switch_to_parent_nocb_internal(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, ABT_bool is_finish,
    ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_ythread *p_new = ABTI_thread_get_ythread(p_old->thread.p_parent);
    if (is_finish) {
        ABTI_xstream *p_local_xstream = *pp_local_xstream;
        ABTI_event_thread_finish(p_local_xstream, &p_old->thread,
                                 p_old->thread.p_parent);
        p_local_xstream->p_thread = &p_new->thread;
        ABTI_ASSERT(p_new->thread.p_last_xstream == p_local_xstream);
        ABTI_ythread_context_jump(p_old, p_new);
        ABTU_unreachable();
    } else {
        ABTI_xstream *p_local_xstream = *pp_local_xstream;
        ABTI_event_ythread_yield(p_local_xstream, p_old, p_old->thread.p_parent,
                                 sync_event_type, p_sync);
        p_local_xstream->p_thread = &p_new->thread;
        ABTI_ASSERT(p_new->thread.p_last_xstream == p_local_xstream);
        /* Context switch starts. */
        ABTI_ythread_context_switch(p_old, p_new);
        /* Context switch finishes. */
        *pp_local_xstream = p_old->thread.p_last_xstream;
    }
}

static inline void ABTI_ythread_switch_to_child_nocb_internal(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, ABTI_ythread *p_new)
{
    p_new->thread.p_parent = &p_old->thread;
    ABTI_xstream *p_local_xstream = *pp_local_xstream;
    ABTI_event_thread_run(p_local_xstream, &p_new->thread, &p_old->thread,
                          p_new->thread.p_parent);
    p_local_xstream->p_thread = &p_new->thread;
    p_new->thread.p_last_xstream = p_local_xstream;
    /* Context switch starts. */
    ABTI_ythread_context_switch(p_old, p_new);
    /* Context switch finishes. */
    *pp_local_xstream = p_old->thread.p_last_xstream;
}

static inline void ABTI_ythread_switch_to_sibling_cb_internal(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, ABTI_ythread *p_new,
    ABT_bool is_finish, void (*f_cb)(void *), void *cb_arg,
    ABT_sync_event_type sync_event_type, void *p_sync)
{
    p_new->thread.p_parent = p_old->thread.p_parent;
    if (is_finish) {
        ABTI_xstream *p_local_xstream = *pp_local_xstream;
        ABTI_event_thread_finish(p_local_xstream, &p_old->thread,
                                 p_old->thread.p_parent);
        ABTI_event_thread_run(p_local_xstream, &p_new->thread, &p_old->thread,
                              p_new->thread.p_parent);
        p_local_xstream->p_thread = &p_new->thread;
        p_new->thread.p_last_xstream = p_local_xstream;
        ABTI_ythread_context_jump_with_call(p_old, p_new, f_cb, cb_arg);
        ABTU_unreachable();
    } else {
        ABTI_xstream *p_local_xstream = *pp_local_xstream;
        ABTI_event_thread_run(p_local_xstream, &p_new->thread, &p_old->thread,
                              p_new->thread.p_parent);
        p_local_xstream->p_thread = &p_new->thread;
        p_new->thread.p_last_xstream = p_local_xstream;
        /* Context switch starts. */
        ABTI_ythread_context_switch_with_call(p_old, p_new, f_cb, cb_arg);
        /* Context switch finishes. */
        *pp_local_xstream = p_old->thread.p_last_xstream;
    }
}

static inline void ABTI_ythread_switch_to_parent_cb_internal(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, ABT_bool is_finish,
    void (*f_cb)(void *), void *cb_arg, ABT_sync_event_type sync_event_type,
    void *p_sync)
{
    ABTI_ythread *p_new = ABTI_thread_get_ythread(p_old->thread.p_parent);
    if (is_finish) {
        ABTI_xstream *p_local_xstream = *pp_local_xstream;
        ABTI_event_thread_finish(p_local_xstream, &p_old->thread,
                                 p_old->thread.p_parent);
        p_local_xstream->p_thread = &p_new->thread;
        ABTI_ASSERT(p_new->thread.p_last_xstream == p_local_xstream);
        ABTI_ythread_context_jump_with_call(p_old, p_new, f_cb, cb_arg);
        ABTU_unreachable();
    } else {
        ABTI_xstream *p_local_xstream = *pp_local_xstream;
        p_local_xstream->p_thread = &p_new->thread;
        ABTI_ASSERT(p_new->thread.p_last_xstream == p_local_xstream);
        /* Context switch starts. */
        ABTI_ythread_context_switch_with_call(p_old, p_new, f_cb, cb_arg);
        /* Context switch finishes. */
        *pp_local_xstream = p_old->thread.p_last_xstream;
    }
}

static inline void ABTI_ythread_switch_to_child_cb_internal(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, ABTI_ythread *p_new,
    void (*f_cb)(void *), void *cb_arg)
{
    p_new->thread.p_parent = &p_old->thread;
    ABTI_xstream *p_local_xstream = *pp_local_xstream;
    ABTI_event_thread_run(p_local_xstream, &p_new->thread, &p_old->thread,
                          p_new->thread.p_parent);
    p_local_xstream->p_thread = &p_new->thread;
    p_new->thread.p_last_xstream = p_local_xstream;
    /* Context switch starts. */
    ABTI_ythread_context_switch_with_call(p_old, p_new, f_cb, cb_arg);
    /* Context switch finishes. */
    *pp_local_xstream = p_old->thread.p_last_xstream;
}

static inline ABT_bool ABTI_ythread_context_peek(ABTI_ythread *p_ythread,
                                                 void (*f_peek)(void *),
                                                 void *arg)
{
    return ABTD_ythread_context_peek(&p_ythread->ctx, f_peek, arg);
}

/* Return the previous thread. */
static inline void ABTI_ythread_switch_to_sibling_nocb(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, ABTI_ythread *p_new,
    ABT_sync_event_type sync_event_type, void *p_sync)
{
    return ABTI_ythread_switch_to_sibling_nocb_internal(pp_local_xstream, p_old,
                                                        p_new, ABT_FALSE,
                                                        sync_event_type,
                                                        p_sync);
}

static inline void ABTI_ythread_switch_to_parent_nocb(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old,
    ABT_sync_event_type sync_event_type, void *p_sync)
{
    return ABTI_ythread_switch_to_parent_nocb_internal(pp_local_xstream, p_old,
                                                       ABT_FALSE,
                                                       sync_event_type, p_sync);
}

static inline void ABTI_ythread_switch_to_child_nocb(ABTI_xstream **pp_local_xstream,
                                  ABTI_ythread *p_old, ABTI_ythread *p_new)
{
    return ABTI_ythread_switch_to_child_nocb_internal(pp_local_xstream, p_old,
                                                      p_new);
}

ABTU_noreturn static inline void
ABTI_ythread_jump_to_sibling_nocb(ABTI_xstream *p_local_xstream,
                                  ABTI_ythread *p_old, ABTI_ythread *p_new)
{
    ABTI_ythread_switch_to_sibling_nocb_internal(&p_local_xstream, p_old, p_new,
                                                 ABT_TRUE,
                                                 ABT_SYNC_EVENT_TYPE_UNKNOWN,
                                                 NULL);
    ABTU_unreachable();
}

ABTU_noreturn static inline void
ABTI_ythread_jump_to_parent_nocb(ABTI_xstream *p_local_xstream,
                                 ABTI_ythread *p_old)
{
    ABTI_ythread_switch_to_parent_nocb_internal(&p_local_xstream, p_old,
                                                ABT_TRUE,
                                                ABT_SYNC_EVENT_TYPE_UNKNOWN,
                                                NULL);
    ABTU_unreachable();
}

static inline void ABTI_ythread_yield_nocb(ABTI_xstream **pp_local_xstream,
                                           ABTI_ythread *p_ythread,
                                           ABT_sync_event_type sync_event_type,
                                           void *p_sync)
{
    /* Change the state of current running thread */
    ABTD_atomic_release_store_int(&p_ythread->thread.state,
                                  ABT_THREAD_STATE_READY);

    /* Switch to the top scheduler */
    ABTI_ythread_switch_to_parent_nocb(pp_local_xstream, p_ythread,
                                       sync_event_type, p_sync);
    /* Back to the original thread */
}

/* Return the previous thread. */
static inline void ABTI_ythread_switch_to_sibling_cb(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, ABTI_ythread *p_new,
    void (*f_cb)(void *), void *cb_arg, ABT_sync_event_type sync_event_type,
    void *p_sync)
{
    ABTI_ythread_switch_to_sibling_cb_internal(pp_local_xstream, p_old,
                                                      p_new, ABT_FALSE, f_cb,
                                                      cb_arg, sync_event_type,
                                                      p_sync);
}

static inline void  ABTI_ythread_switch_to_parent_cb(
    ABTI_xstream **pp_local_xstream, ABTI_ythread *p_old, void (*f_cb)(void *),
    void *cb_arg, ABT_sync_event_type sync_event_type, void *p_sync)
{
    ABTI_ythread_switch_to_parent_cb_internal(pp_local_xstream, p_old,
                                                     ABT_FALSE, f_cb, cb_arg,
                                                     sync_event_type, p_sync);
}

static inline void ABTI_ythread_switch_to_child_cb(ABTI_xstream **pp_local_xstream,
                                ABTI_ythread *p_old, ABTI_ythread *p_new,
                                void (*f_cb)(void *), void *cb_arg)
{
    ABTI_ythread_switch_to_child_cb_internal(pp_local_xstream, p_old,
                                                    p_new, f_cb, cb_arg);
}

ABTU_noreturn static inline void
ABTI_ythread_jump_to_sibling_cb(ABTI_xstream *p_local_xstream,
                                ABTI_ythread *p_old, ABTI_ythread *p_new,
                                void (*f_cb)(void *), void *cb_arg)
{
    ABTI_ythread_switch_to_sibling_cb_internal(&p_local_xstream, p_old, p_new,
                                               ABT_TRUE, f_cb, cb_arg,
                                               ABT_SYNC_EVENT_TYPE_UNKNOWN,
                                               NULL);
    ABTU_unreachable();
}

ABTU_noreturn static inline void
ABTI_ythread_jump_to_parent_cb(ABTI_xstream *p_local_xstream,
                               ABTI_ythread *p_old, void (*f_cb)(void *),
                               void *cb_arg)
{
    ABTI_ythread_switch_to_parent_cb_internal(&p_local_xstream, p_old, ABT_TRUE,
                                              f_cb, cb_arg,
                                              ABT_SYNC_EVENT_TYPE_UNKNOWN,
                                              NULL);
    ABTU_unreachable();
}

static inline void ABTI_ythread_yield_cb(ABTI_xstream **pp_local_xstream,
                                         ABTI_ythread *p_ythread,
                                         void (*f_cb)(void *), void *cb_arg,
                                         ABT_sync_event_type sync_event_type,
                                         void *p_sync)
{
    /* Change the state of current running thread */
    ABTD_atomic_release_store_int(&p_ythread->thread.state,
                                  ABT_THREAD_STATE_READY);
    /* Switch to the top scheduler */
    ABTI_ythread_switch_to_parent_cb(pp_local_xstream, p_ythread, f_cb, cb_arg,
                                     sync_event_type, p_sync);
    /* Back to the original thread */
}

/* Return ABT_TRUE if p_prev should terminate. */
static inline ABT_bool
ABTI_context_switch_callback_handle_request(ABTI_ythread *p_prev)
{
#if defined(ABT_CONFIG_DISABLE_THREAD_CANCEL) &&                               \
    defined(ABT_CONFIG_DISABLE_MIGRATION)
    return ABT_FALSE;
#else
    /* At least either cancellation or migration is enabled. */
    const uint32_t request =
        ABTD_atomic_acquire_load_uint32(&p_prev->thread.request);

    /* Check cancellation request. */
#ifndef ABT_CONFIG_DISABLE_THREAD_CANCEL
    if (ABTU_unlikely(request & ABTI_THREAD_REQ_CANCEL)) {
        ABTD_ythread_cancel(p_prev->thread.p_last_xstream, p_prev);
        ABTI_xstream_terminate_thread(ABTI_global_get_global(),
                                      ABTI_xstream_get_local(
                                          p_prev->thread.p_last_xstream),
                                      &p_prev->thread);
        return ABT_TRUE;
    }
#endif /* !ABT_CONFIG_DISABLE_THREAD_CANCEL */

    /* Check migration request. */
#ifndef ABT_CONFIG_DISABLE_MIGRATION
    if (ABTU_unlikely(request & ABTI_THREAD_REQ_MIGRATE)) {
        /* This is the case when the ULT requests migration of itself. */
        ABTD_atomic_release_store_int(&p_prev->thread.state,
                                      ABT_THREAD_STATE_READY);
        int abt_errno =
            ABTI_xstream_migrate_thread(ABTI_global_get_global(),
                                        ABTI_xstream_get_local(
                                            p_prev->thread.p_last_xstream),
                                        &p_prev->thread);
        if (abt_errno != ABT_SUCCESS) {
            /* Migration failed.  Let's push it back to its associated pool. */
            ABTI_pool_add_thread(&p_prev->thread);
        }
        return ABT_FALSE;
    }
#endif /* !ABT_CONFIG_DISABLE_MIGRATION */
#endif
    /* This thread does not terminate. */
    return ABT_FALSE;
}

static inline void ABTI_context_switch_callback_yield(void *arg)
{
    // ABTI_event_ythread_yield(p_local_xstream, p_cur_ythread,
    //                      p_cur_ythread->thread.p_parent,
    //                      ABT_SYNC_EVENT_TYPE_USER, NULL);
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    if (ABTI_context_switch_callback_handle_request(p_prev))
        return;
    /* Push this thread back to the pool. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_READY);
    ABTI_pool_add_thread(&p_prev->thread);
}

/* Before yield_to, p_prev->thread.p_pool's num_blocked must be incremented to
 * avoid making a pool empty. */
static inline void ABTI_context_switch_callback_yield_to(void *arg)
{
    // ABTI_event_ythread_yield(p_local_xstream, p_cur_ythread,
    //                      p_cur_ythread->thread.p_parent,
    //                      ABT_SYNC_EVENT_TYPE_USER, NULL);
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    if (ABTI_context_switch_callback_handle_request(p_prev))
        return;
    /* p_prev->thread.p_pool is loaded before ABTI_pool_add_thread() to keep
     * num_blocked consistent. Otherwise, other threads might pop p_prev
     * that has been pushed by ABTI_pool_add_thread() and change
     * p_prev->thread.p_pool by ABT_unit_set_associated_pool(). */
    ABTI_pool *p_pool = p_prev->thread.p_pool;
    /* Push this thread back to the pool. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_READY);
    ABTI_pool_add_thread(&p_prev->thread);
    /* Decrease the number of blocked threads, which has been increased
     * by p_prev to avoid making a pool size 0. */
    ABTI_pool_dec_num_blocked(p_pool);
}

static inline void ABTI_context_switch_callback_suspend(void *arg)
{
    // ABTI_event_ythread_suspend(p_local_xstream, p_cur_ythread,
    //                            p_cur_ythread->thread.p_parent,
    //                            ABT_SYNC_EVENT_TYPE_USER, NULL);
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    if (ABTI_context_switch_callback_handle_request(p_prev))
        return;
    /* Increase the number of blocked threads */
    ABTI_pool_inc_num_blocked(p_prev->thread.p_pool);
    /* Set this thread's state to BLOCKED. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_BLOCKED);
}

static inline void ABTI_context_switch_callback_terminate(void *arg)
{
    /* Terminate this thread. */
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    ABTI_xstream_terminate_thread(ABTI_global_get_global(),
                                  ABTI_xstream_get_local(
                                      p_prev->thread.p_last_xstream),
                                  &p_prev->thread);
}

typedef struct {
    ABTI_ythread *p_prev;
    ABTD_spinlock *p_lock;
} ABTI_context_switch_callback_suspend_unlock_arg;

static inline void ABTI_context_switch_callback_suspend_unlock(void *arg)
{
    ABTI_context_switch_callback_suspend_unlock_arg *p_arg =
        (ABTI_context_switch_callback_suspend_unlock_arg *)arg;
    /* p_arg might point to the stack of the original ULT, so do not
     * access it after that ULT becomes resumable. */
    ABTI_ythread *p_prev = p_arg->p_prev;
    ABTD_spinlock *p_lock = p_arg->p_lock;
    if (ABTI_context_switch_callback_handle_request(p_prev))
        return;
    /* Increase the number of blocked threads */
    ABTI_pool_inc_num_blocked(p_prev->thread.p_pool);
    /* Set this thread's state to BLOCKED. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_BLOCKED);
    /* Release the lock. */
    ABTD_spinlock_release(p_lock);
}

typedef struct {
    ABTI_ythread *p_prev;
    ABTI_ythread *p_target;
} ABTI_context_switch_callback_suspend_join_arg;

static inline void ABTI_context_switch_callback_suspend_join(void *arg)
{
    ABTI_context_switch_callback_suspend_join_arg *p_arg =
        (ABTI_context_switch_callback_suspend_join_arg *)arg;
    /* p_arg might point to the stack of the original ULT, so do not
     * access it after that ULT becomes resumable. */
    ABTI_ythread *p_prev = p_arg->p_prev;
    ABTI_ythread *p_target = p_arg->p_target;
    if (ABTI_context_switch_callback_handle_request(p_prev))
        return;
    /* Increase the number of blocked threads */
    ABTI_pool_inc_num_blocked(p_prev->thread.p_pool);
    /* Set this thread's state to BLOCKED. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_BLOCKED);
    /* Set the link in the context of the target ULT. This p_link might be
     * read by p_target running on another ES in parallel, so release-store
     * is needed here. */
    ABTD_atomic_release_store_ythread_context_ptr(&p_target->ctx.p_link,
                                                  &p_prev->ctx);
}

typedef struct {
    ABTI_ythread *p_prev;
    ABTI_sched *p_main_sched;
} ABTI_context_switch_callback_suspend_replace_sched_arg;

static inline void ABTI_context_switch_callback_suspend_replace_sched(void *arg)
{
    ABTI_context_switch_callback_suspend_replace_sched_arg *p_arg =
        (ABTI_context_switch_callback_suspend_replace_sched_arg *)arg;
    /* p_arg might point to the stack of the original ULT, so do not
     * access it after that ULT becomes resumable. */
    ABTI_ythread *p_prev = p_arg->p_prev;
    ABTI_sched *p_main_sched = p_arg->p_main_sched;
    if (ABTI_context_switch_callback_handle_request(p_prev))
        return;
    /* Increase the number of blocked threads */
    ABTI_pool_inc_num_blocked(p_prev->thread.p_pool);
    /* Set this thread's state to BLOCKED. */
    ABTD_atomic_release_store_int(&p_prev->thread.state,
                                  ABT_THREAD_STATE_BLOCKED);
    /* Ask the current main scheduler to replace its scheduler */
    ABTI_sched_set_request(p_main_sched, ABTI_SCHED_REQ_REPLACE);
}

static inline void ABTI_context_switch_callback_orphan(void *arg)
{
    ABTI_ythread *p_prev = (ABTI_ythread *)arg;
    ABTI_thread_unset_associated_pool(ABTI_global_get_global(),
                                      &p_prev->thread);
}

#endif /* ABTI_YTHREAD_H_INCLUDED */
