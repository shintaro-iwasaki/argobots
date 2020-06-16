/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#ifndef ABTI_TOOL_H_INCLUDED
#define ABTI_TOOL_H_INCLUDED

static inline ABT_thread ABTI_thread_get_handle(ABTI_thread *p_thread);
static inline ABT_task ABTI_task_get_handle(ABTI_task *p_task);

#ifndef ABT_CONFIG_DISABLE_TOOL_INTERFACE
static inline ABTI_tool_context *
ABTI_tool_context_get_ptr(ABT_tool_context tctx)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABTI_tool_context *p_tctx;
    if (tctx == ABT_TOOL_CONTEXT_NULL) {
        p_tctx = NULL;
    } else {
        p_tctx = (ABTI_tool_context *)tctx;
    }
    return p_tctx;
#else
    return (ABTI_tool_context *)tctx;
#endif
}

static inline ABT_tool_context
ABTI_tool_context_get_handle(ABTI_tool_context *p_tctx)
{
#ifndef ABT_CONFIG_DISABLE_ERROR_CHECK
    ABT_tool_context h_tctx;
    if (p_tctx == NULL) {
        h_tctx = ABT_TOOL_CONTEXT_NULL;
    } else {
        h_tctx = (ABT_tool_context)p_tctx;
    }
    return h_tctx;
#else
    return (ABT_tool_context)p_tctx;
#endif
}
#endif /* !ABT_CONFIG_DISABLE_TOOL_INTERFACE */

static inline void ABTI_tool_event_thread_impl(
    uint64_t event_code, ABTI_thread *p_thread, ABTI_xstream *p_xstream,
    ABTI_pool *p_pool, ABT_sync_event_type sync_event_type, void *p_sync_object)
{
#ifdef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    return;
#else
    if (gp_ABTI_global->tool_event_mask & event_code) {
        ABTI_tool_context tctx;
        tctx.p_pool = p_pool;
        if (p_xstream && p_xstream->p_unit) {
            ABTI_unit *p_caller = p_xstream->p_unit;
            if (ABTI_unit_type_is_thread(p_caller->type)) {
                tctx.caller_type = ABT_EXEC_ENTITY_TYPE_THREAD;
                tctx.p_caller = p_caller;
            } else if (p_caller->type == ABTI_UNIT_TYPE_TASK) {
                tctx.caller_type = ABT_EXEC_ENTITY_TYPE_TASK;
                tctx.p_caller = p_caller;
            } else {
                tctx.caller_type = ABT_EXEC_ENTITY_TYPE_EXT;
            }
        } else {
            tctx.caller_type = ABT_EXEC_ENTITY_TYPE_EXT;
        }
        tctx.sync_event_type = sync_event_type;
        tctx.p_sync_object = p_sync_object;
        ABT_thread h_thread = ABTI_thread_get_handle(p_thread);
        ABT_xstream h_xstream = ABTI_xstream_get_handle(p_xstream);
        ABT_tool_context h_tctx = ABTI_tool_context_get_handle(&tctx);
        gp_ABTI_global->tool_thread_cb_f(h_thread, h_xstream, event_code,
                                         h_tctx);
    }
#endif
}

static inline void ABTI_tool_event_task_impl(
    uint64_t event_code, ABTI_task *p_task, ABTI_xstream *p_xstream,
    ABTI_pool *p_pool, ABT_sync_event_type sync_event_type, void *p_sync_object)
{
#ifdef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    return;
#else
    if (gp_ABTI_global->tool_event_mask & event_code) {
        ABTI_tool_context tctx;
        tctx.p_pool = p_pool;
        if (p_xstream && p_xstream->p_unit) {
            ABTI_unit *p_caller = p_xstream->p_unit;
            if (ABTI_unit_type_is_thread(p_caller->type)) {
                tctx.caller_type = ABT_EXEC_ENTITY_TYPE_THREAD;
                tctx.p_caller = p_caller;
            } else if (p_caller->type == ABTI_UNIT_TYPE_TASK) {
                tctx.caller_type = ABT_EXEC_ENTITY_TYPE_TASK;
                tctx.p_caller = p_caller;
            } else {
                tctx.caller_type = ABT_EXEC_ENTITY_TYPE_EXT;
            }
        } else {
            tctx.caller_type = ABT_EXEC_ENTITY_TYPE_EXT;
        }
        tctx.sync_event_type = sync_event_type;
        tctx.p_sync_object = p_sync_object;
        ABT_task h_task = ABTI_task_get_handle(p_task);
        ABT_xstream h_xstream = ABTI_xstream_get_handle(p_xstream);
        ABT_tool_context h_tctx = ABTI_tool_context_get_handle(&tctx);
        gp_ABTI_global->tool_task_cb_f(h_task, h_xstream, event_code, h_tctx);
    }
#endif
}

static inline void ABTI_tool_event_thread_create(ABTI_thread *p_thread,
                                                 ABTI_xstream *p_xstream,
                                                 ABTI_pool *p_pool)
{
    ABTI_tool_event_thread_impl(ABT_TOOL_EVENT_THREAD_CREATE, p_thread,
                                p_xstream, p_pool, ABT_SYNC_EVENT_TYPE_UNKNOWN,
                                NULL);
}

static inline void ABTI_tool_event_thread_free(ABTI_thread *p_thread,
                                               ABTI_xstream *p_xstream)
{
    ABTI_tool_event_thread_impl(ABT_TOOL_EVENT_THREAD_FREE, p_thread, p_xstream,
                                NULL, ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
}

static inline void ABTI_tool_event_thread_revive(ABTI_thread *p_thread,
                                                 ABTI_xstream *p_xstream,
                                                 ABTI_pool *p_pool)
{
    ABTI_tool_event_thread_impl(ABT_TOOL_EVENT_THREAD_REVIVE, p_thread,
                                p_xstream, p_pool, ABT_SYNC_EVENT_TYPE_UNKNOWN,
                                NULL);
}

static inline void ABTI_tool_event_thread_run(ABTI_thread *p_thread,
                                              ABTI_xstream *p_xstream,
                                              ABTI_unit *p_prev)
{
    ABTI_tool_event_thread_impl(ABT_TOOL_EVENT_THREAD_RUN, p_thread, p_xstream,
                                NULL, ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
}

static inline void ABTI_tool_event_thread_finish(ABTI_thread *p_thread,
                                                 ABTI_xstream *p_xstream)
{
    ABTI_tool_event_thread_impl(ABT_TOOL_EVENT_THREAD_FINISH, p_thread,
                                p_xstream, NULL, ABT_SYNC_EVENT_TYPE_UNKNOWN,
                                NULL);
}

static inline void
ABTI_tool_event_thread_yield(ABTI_thread *p_thread, ABTI_xstream *p_xstream,
                             ABT_sync_event_type sync_event_type, void *p_sync)
{
#ifdef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    return;
#else
    if (ABTD_atomic_relaxed_load_uint32(&p_thread->unit_def.request) &
        ABTI_UNIT_REQ_BLOCK) {
        ABTI_tool_event_thread_impl(ABT_TOOL_EVENT_THREAD_SUSPEND, p_thread,
                                    p_xstream, p_thread->unit_def.p_pool,
                                    sync_event_type, p_sync);

    } else {
        ABTI_tool_event_thread_impl(ABT_TOOL_EVENT_THREAD_YIELD, p_thread,
                                    p_xstream, p_thread->unit_def.p_pool,
                                    sync_event_type, p_sync);
    }
#endif
}

static inline void
ABTI_tool_event_thread_suspend(ABTI_thread *p_thread, ABTI_xstream *p_xstream,
                               ABT_sync_event_type sync_event_type,
                               void *p_sync)
{
    ABTI_tool_event_thread_impl(ABT_TOOL_EVENT_THREAD_SUSPEND, p_thread,
                                p_xstream, NULL, sync_event_type, p_sync);
}

static inline void ABTI_tool_event_thread_resume(ABTI_thread *p_thread,
                                                 ABTI_xstream *p_xstream)
{
    ABTI_tool_event_thread_impl(ABT_TOOL_EVENT_THREAD_RESUME, p_thread,
                                p_xstream, p_thread->unit_def.p_pool,
                                ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
}

static inline void ABTI_tool_event_task_create(ABTI_task *p_task,
                                               ABTI_xstream *p_xstream,
                                               ABTI_pool *p_pool)
{
    ABTI_tool_event_task_impl(ABT_TOOL_EVENT_TASK_CREATE, p_task, p_xstream,
                              p_pool, ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
}

static inline void ABTI_tool_event_task_free(ABTI_task *p_task,
                                             ABTI_xstream *p_xstream)
{
    ABTI_tool_event_task_impl(ABT_TOOL_EVENT_TASK_FREE, p_task, p_xstream, NULL,
                              ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
}

static inline void ABTI_tool_event_task_revive(ABTI_task *p_task,
                                               ABTI_xstream *p_xstream,
                                               ABTI_pool *p_pool)
{
    ABTI_tool_event_task_impl(ABT_TOOL_EVENT_TASK_REVIVE, p_task, p_xstream,
                              p_pool, ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
}

static inline void ABTI_tool_event_task_run(ABTI_task *p_task,
                                            ABTI_xstream *p_xstream,
                                            ABTI_unit *p_prev)
{
    ABTI_tool_event_task_impl(ABT_TOOL_EVENT_TASK_RUN, p_task, p_xstream, NULL,
                              ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
}

static inline void ABTI_tool_event_task_finish(ABTI_task *p_task,
                                               ABTI_xstream *p_xstream)
{
    ABTI_tool_event_task_impl(ABT_TOOL_EVENT_TASK_FINISH, p_task, p_xstream,
                              NULL, ABT_SYNC_EVENT_TYPE_UNKNOWN, NULL);
}

#endif /* ABTI_TOOL_H_INCLUDED */
