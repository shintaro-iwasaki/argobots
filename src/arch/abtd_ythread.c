/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

static inline void ythread_terminate(ABTI_xstream *p_local_xstream,
                                     ABTI_ythread *p_ythread);

void ABTD_ythread_func_wrapper(ABTD_ythread_context *p_arg)
{
    ABTD_ythread_context *p_ctx = p_arg;
    ABTI_ythread *p_ythread = ABTI_ythread_context_get_ythread(p_ctx);
    p_ythread->thread.f_thread(p_ythread->thread.p_arg);

    ABTI_xstream *p_local_xstream = p_ythread->thread.p_last_xstream;
    ythread_terminate(p_local_xstream, p_ythread);
}

void ABTD_ythread_exit(ABTI_xstream *p_local_xstream, ABTI_ythread *p_ythread)
{
    ythread_terminate(p_local_xstream, p_ythread);
}

static inline void ythread_terminate(ABTI_xstream *p_local_xstream,
                                     ABTI_ythread *p_ythread)
{
    ABTD_ythread_context *p_ctx = &p_ythread->ctx;
    ABTD_ythread_context *p_link =
        ABTD_atomic_acquire_load_ythread_context_ptr(&p_ctx->p_link);
    if (!p_link) {
        uint32_t req = ABTD_atomic_fetch_or_uint32(&p_ythread->thread.request,
                                                   ABTI_THREAD_REQ_JOIN);
        if (!(req & ABTI_THREAD_REQ_JOIN)) {
            /* This case means there is no join request.  Let's go back to the
             * parent ULT */
            ABTI_ythread_jump_to_parent_cb(
                p_local_xstream, p_ythread,
                ABTI_context_switch_callback_terminate, (void *)p_ythread);
            ABTU_unreachable();
        } else {
            /* This case means there has been a join request and the joiner has
             * blocked.  We have to wake up the joiner ULT. */
            do {
                p_link = ABTD_atomic_acquire_load_ythread_context_ptr(
                    &p_ctx->p_link);
            } while (!p_link);
        }
    }
    /* Now p_link != NULL. */
    ABTI_ythread *p_joiner = ABTI_ythread_context_get_ythread(p_link);
#ifndef ABT_CONFIG_ACTIVE_WAIT_POLICY
    if (p_joiner->thread.type == ABTI_THREAD_TYPE_EXT) {
        /* p_joiner is a non-yieldable thread (i.e., external thread). Wake up
         * the waiter via the futex.  Note that p_arg is used to store futex
         * (see thread_join_futexwait()). */
        ABTD_futex_single *p_futex =
            (ABTD_futex_single *)p_joiner->thread.p_arg;
        ABTD_futex_resume(p_futex);
    } else
#endif
        if (p_ythread->thread.p_last_xstream ==
                p_joiner->thread.p_last_xstream &&
            !(p_ythread->thread.type & ABTI_THREAD_TYPE_MAIN_SCHED)) {
        /* Only when the current ULT is on the same ES as p_joiner's, we can
         * jump to the joiner ULT.  Note that a parent ULT cannot be a joiner.
         */
        ABTI_event_ythread_resume(ABTI_xstream_get_local(p_local_xstream),
                                  p_joiner, &p_ythread->thread);
        ABTI_ythread_jump_to_sibling_cb(p_local_xstream, p_ythread, p_joiner,
                                        ABTI_context_switch_callback_terminate,
                                        (void *)p_ythread);
        ABTU_unreachable();
    } else {
        /* If the current ULT's associated ES is different from p_joiner's, we
         * can't directly jump to p_joiner.  Instead, we wake up p_joiner here
         * so that p_joiner's scheduler can resume it.  Note that the main
         * scheduler needs to jump back to the root scheduler, so the main
         * scheduler needs to take this path. */
        ABTI_ythread_set_ready(ABTI_xstream_get_local(p_local_xstream),
                               p_joiner);
    }
    /* The waiter has been resumed.  Let's switch to the parent. */
    ABTI_ythread_jump_to_parent_cb(p_local_xstream, p_ythread,
                                   ABTI_context_switch_callback_terminate,
                                   (void *)p_ythread);
    ABTU_unreachable();
}

void ABTD_ythread_cancel(ABTI_xstream *p_local_xstream, ABTI_ythread *p_ythread)
{
    /* When we cancel a ULT, if other ULT is blocked to join the canceled ULT,
     * we have to wake up the joiner ULT.  However, unlike the case when the
     * ULT has finished its execution and calls ythread_terminate/exit,
     * this function is called by the scheduler.  Therefore, we should not
     * context switch to the joiner ULT and need to always wake it up. */
    ABTD_ythread_context *p_ctx = &p_ythread->ctx;

    if (ABTD_atomic_acquire_load_ythread_context_ptr(&p_ctx->p_link)) {
        /* If p_link is set, it means that other ULT has called the join. */
        ABTI_ythread *p_joiner = ABTI_ythread_context_get_ythread(
            ABTD_atomic_relaxed_load_ythread_context_ptr(&p_ctx->p_link));
        ABTI_ythread_set_ready(ABTI_xstream_get_local(p_local_xstream),
                               p_joiner);
    } else {
        uint32_t req = ABTD_atomic_fetch_or_uint32(&p_ythread->thread.request,
                                                   ABTI_THREAD_REQ_JOIN);
        if (req & ABTI_THREAD_REQ_JOIN) {
            /* This case means there has been a join request and the joiner has
             * blocked.  We have to wake up the joiner ULT. */
            while (ABTD_atomic_acquire_load_ythread_context_ptr(
                       &p_ctx->p_link) == NULL)
                ;
            ABTI_ythread *p_joiner = ABTI_ythread_context_get_ythread(
                ABTD_atomic_relaxed_load_ythread_context_ptr(&p_ctx->p_link));
            ABTI_ythread_set_ready(ABTI_xstream_get_local(p_local_xstream),
                                   p_joiner);
        }
    }
    ABTI_event_thread_cancel(p_local_xstream, &p_ythread->thread);
}

void ABTD_ythread_print_context(ABTI_ythread *p_ythread, FILE *p_os, int indent)
{
    ABTD_ythread_context *p_ctx = &p_ythread->ctx;
    fprintf(p_os, "%*sp_ctx     : %p\n", indent, "", (void *)p_ctx);
    fprintf(p_os, "%*sp_link    : %p\n", indent, "",
            (void *)ABTD_atomic_acquire_load_ythread_context_ptr(
                &p_ctx->p_link));
    fflush(p_os);
}
