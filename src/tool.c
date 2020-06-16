/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup Tool interface
 * This group is for the tool interface.
 */

/**
 * @ingroup TOOL
 * @brief   Query information associated with the tool context.
 *
 * \c ABT_tool_query() returns information associated with the tool context
 * \c context through \c val.  Since \c context is valid only in the callback
 * handler, this function must be called in the callback handler.
 *
 * When \c query_kind is ABT_TOOL_QUERY_KIND_POOL, it sets \c *val to
 * \c ABT_pool of a pool to which a work unit is or will be pushed.  The query
 * is valid when \c event is THREAD_CREATE, THREAD_REVIVE, THREAD_YIELD,
 * THREAD_RESUME, TASK_CREATE, or TASK_REVIVE.  Otherwise, \c *val is set to
 * ABT_POOL_NULL.
 *
 * When \c query_kind is ABT_TOOL_QUERY_KIND_CALLER_TYPE, \c *val is set to
 * ABT_exec_entity_type of an entity which incurs this event.  The query is
 * valid for all events.
 *
 * When \c query_kind is ABT_TOOL_QUERY_KIND_CALLER_HANDLE, \c *val is set to a
 * handle of an entity which incurs this event.  Specifically, \c *val is set
 * to a ULT handle (ABT_thread) if the caller type is
 * ABT_EXEC_ENTITY_TYPE_THREAD.  \c *val is set to a tasklet handle (ABT_task)
 * if the caller type is ABT_EXEC_ENTITY_TYPE_TASK.  If the caller is an
 * external thread, \c *val is set to NULL.  The query is valid for all events.
 * Note that the caller is the previous work unit when \c event is THRAED_RUN
 * or TASK_RUN.
 *
 * When \c query_kind is ABT_TOOL_QUERY_KIND_SYNC_OBJECT_TYPE, \c *val is set to
 * ABT_sync_event_type of an synchronization object which incurs this event.
 * The synchronization object is returned when \c query_kind is
 * ABT_TOOL_QUERY_KIND_SYNC_OBJECT_HANDLE.  Synchronization events, and
 * ABT_sync_event_type, and synchronization objects are mapped as follows:
 *  - ABT_SYNC_EVENT_TYPE_USER:
 *      User's explicit call (e.g., ABT_thread_yield())
 *      The synchronization object is not set ((void *)NULL).
 *  - ABT_SYNC_EVENT_TYPE_XSTREAM_JOIN:
 *      Waiting for completion of execution streams (e.g., ABT_xstream_join())
 *      The synchronization object is an execution stream (ABT_xstream).
 *  - ABT_SYNC_EVENT_TYPE_THREAD_JOIN:
 *      Waiting for completion of ULTs (e.g., ABT_thread_join())
 *      The synchronization object is a ULT (ABT_thread).
 *  - ABT_SYNC_EVENT_TYPE_TASK_JOIN:
 *      Waiting for completion of tasklets (e.g., ABT_task_join())
 *      The synchronization object is a tasklet (ABT_task).
 *  - ABT_SYNC_EVENT_TYPE_MUTEX:
 *      Synchronization regarding a mutex (e.g., ABT_mutex_lock())
 *      The synchronization object is a mutex (ABT_mutex).
 *  - ABT_SYNC_EVENT_TYPE_COND:
 *      Synchronization regarding a condition variable(e.g., ABT_cond_wait())
 *      The synchronization object is a condition variable (ABT_cond).
 *  - ABT_SYNC_EVENT_TYPE_RWLOCK:
 *      Synchronization regarding a rwlock (e.g., ABT_rwlock_rdlock())
 *      The synchronization object is a rwlock (ABT_rwlock).
 *  - ABT_SYNC_EVENT_TYPE_EVENTUAL:
 *      Synchronization regarding an eventual (e.g., ABT_eventual_wait())
 *      The synchronization object is an eventual (ABT_eventual).
 *  - ABT_SYNC_EVENT_TYPE_FUTURE:
 *      Synchronization regarding a future (e.g., ABT_future_wait())
 *      The synchronization object is a future (ABT_future).
 *  - ABT_SYNC_EVENT_TYPE_BARRIER:
 *      Synchronization regarding a barrier (e.g., ABT_barrier_wait())
 *      The synchronization object is a barrier (ABT_barrier).
 *  - ABT_SYNC_EVENT_TYPE_OTHER:
 *      Unclassified synchronization (e.g., ABT_xstream_exit())
 *      The synchronization object is not set ((void *)NULL).
 *  - ABT_SYNC_EVENT_TYPE_UNKNOWN
 *      \c event is neither THREAD_YIELD nor THREAD_SUSPEND.
 *      The synchronization object is not set ((void *)NULL).
 * This query is valid for THREAD_YIELD and THREAD_SUSPEND.
 *
 * An object to which a returned handle points to may be in an intermediate
 * state, so users are discouraged not to read any internal state of such an
 * object (e.g., by ABT_thread_get_state() or ABT_pool_get_size()).
 *
 * @param[in]  context    handle to the tool context
 * @param[in]  event      event code passed to the callback function
 * @param[in]  query_kind query kind
 * @param[out] val        a pointer to storage where a returned value is saved
 * @return Error code
 * @retval ABT_SUCCESS        on success
 * @retval ABT_ERR_FEATURE_NA the tool feature is not supported
 */
int ABT_tool_query(ABT_tool_context context, uint64_t event,
                   ABT_tool_query_kind query_kind, void *val)
{
#ifdef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    return ABT_ERR_FEATURE_NA;
#else
    int abt_errno = ABT_SUCCESS;

    ABTI_tool_context *p_tctx = ABTI_tool_context_get_ptr(context);
    ABTI_CHECK_NULL_TOOL_CONTEXT_PTR(p_tctx);

    switch (query_kind) {
        case ABT_TOOL_QUERY_KIND_POOL:
            *(ABT_pool *)val = ABTI_pool_get_handle(p_tctx->p_pool);
            break;
        case ABT_TOOL_QUERY_KIND_CALLER_TYPE:
            *(ABT_exec_entity_type *)val = p_tctx->caller_type;
            break;
        case ABT_TOOL_QUERY_KIND_CALLER_HANDLE:
            switch (p_tctx->caller_type) {
                case ABT_EXEC_ENTITY_TYPE_THREAD:
                    *(ABT_thread *)val = ABTI_thread_get_handle(
                        ABTI_unit_get_thread(p_tctx->p_caller));
                    break;
                case ABT_EXEC_ENTITY_TYPE_TASK:
                    *(ABT_task *)val = ABTI_task_get_handle(
                        ABTI_unit_get_task(p_tctx->p_caller));
                    break;
                default:
                    *(void **)val = NULL;
            }
            break;
        case ABT_TOOL_QUERY_KIND_SYNC_OBJECT_TYPE:
            *(ABT_sync_event_type *)val = p_tctx->sync_event_type;
            break;
        case ABT_TOOL_QUERY_KIND_SYNC_OBJECT_HANDLE:
            switch (p_tctx->sync_event_type) {
                case ABT_SYNC_EVENT_TYPE_XSTREAM_JOIN:
                    *(ABT_xstream *)val = ABTI_xstream_get_handle(
                        (ABTI_xstream *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_THREAD_JOIN:
                    *(ABT_thread *)val = ABTI_thread_get_handle(
                        (ABTI_thread *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_TASK_JOIN:
                    *(ABT_task *)val = ABTI_task_get_handle(
                        (ABTI_task *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_MUTEX:
                    *(ABT_mutex *)val = ABTI_mutex_get_handle(
                        (ABTI_mutex *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_COND:
                    *(ABT_cond *)val = ABTI_cond_get_handle(
                        (ABTI_cond *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_RWLOCK:
                    *(ABT_rwlock *)val = ABTI_rwlock_get_handle(
                        (ABTI_rwlock *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_EVENTUAL:
                    *(ABT_eventual *)val = ABTI_eventual_get_handle(
                        (ABTI_eventual *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_FUTURE:
                    *(ABT_future *)val = ABTI_future_get_handle(
                        (ABTI_future *)p_tctx->p_sync_object);
                    break;
                case ABT_SYNC_EVENT_TYPE_BARRIER:
                    *(ABT_barrier *)val = ABTI_barrier_get_handle(
                        (ABTI_barrier *)p_tctx->p_sync_object);
                    break;
                default:
                    *(void **)val = NULL;
            }
            break;
        default:
            abt_errno = ABT_ERR_OTHER;
            goto fn_fail;
    }

fn_exit:
    return abt_errno;

fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
#endif
}

/**
 * @ingroup TOOL
 * @brief   Register a callback function for ULT events
 *
 * \c ABT_tool_register_thread_callback() sets a callback function \c cb_func
 * for ULT events.  Events that are not in \c event_mask are excluded.  Users
 * can stop the event callback by setting \c cb_func to zero.
 *
 * \c cb_func is called with a target thread (the first argument), an underlying
 * execution stream (the second argument), an event code (the third argument,
 * see ABT_TOOL_EVENT_THREAD), and the tool context that can be used for
 * ABT_tool_query().  If the event occurs on an external thread,
 * ABT_XSTREAM_NULL is passed.  The returned tool context is only valid in the
 * callback function.
 *
 * An object to which a returned handle points to may be in an intermediate
 * state, so users are discouraged not to read any internal state of such an
 * object (e.g., by ABT_thread_get_state()).
 *
 * @param[in]  cb_func     a callback function pointer
 * @param[in]  event_mask  event code masks
 * @return Error code
 * @retval ABT_SUCCESS         on success
 * @retval ABT_ERR_FEATURE_NA  tool feature is not supported
 */
int ABT_tool_register_thread_callback(ABT_tool_thread_callback_fn cb_func,
                                      uint64_t event_mask)
{
#ifdef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    return ABT_ERR_FEATURE_NA;
#else
    uint64_t thread_event_mask;
    if (cb_func == NULL) {
        thread_event_mask = ABT_TOOL_EVENT_THREAD_NONE;
        gp_ABTI_global->tool_event_mask &= ~ABT_TOOL_EVENT_THREAD_ALL;
    } else {
        thread_event_mask = event_mask & ABT_TOOL_EVENT_THREAD_ALL;
    }
    gp_ABTI_global->tool_event_mask =
        (gp_ABTI_global->tool_event_mask & (~ABT_TOOL_EVENT_THREAD_ALL)) |
        thread_event_mask;
    gp_ABTI_global->tool_thread_cb_f = cb_func;
    return ABT_SUCCESS;
#endif
}

/**
 * @ingroup TOOL
 * @brief   Register a callback function for tasklet events
 *
 * \c ABT_tool_register_task_callback() sets a callback function \c cb_func for
 * tasklet events.  Events that are not in \c event_mask are excluded.  Users
 * can stop the event callback by setting \c cb_func to zero.
 *
 * \c cb_func is called with a target tasklet (the first argument), an
 * underlying execution stream (the second argument), an event code (the third
 * argument, see ABT_TOOL_EVENT_TASK), and the tool context that can be used
 * for ABT_tool_query().  If the event occurs on an external thread,
 * ABT_XSTREAM_NULL is passed.  The returned tool context is only valid in the
 * callback function.
 *
 * An object to which a returned handle points to may be in an intermediate
 * state, so users are discouraged not to read any internal state of such an
 * object (e.g., by ABT_thread_get_state()).
 *
 * @param[in]  cb_func     a callback function pointer
 * @param[in]  event_mask  event code masks
 * @return Error code
 * @retval ABT_SUCCESS         on success
 * @retval ABT_ERR_FEATURE_NA  tool feature is not supported
 */
int ABT_tool_register_task_callback(ABT_tool_task_callback_fn cb_func,
                                    uint64_t event_mask)
{
#ifdef ABT_CONFIG_DISABLE_TOOL_INTERFACE
    return ABT_ERR_FEATURE_NA;
#else
    uint64_t task_event_mask;
    if (cb_func == NULL) {
        task_event_mask = ABT_TOOL_EVENT_TASK_NONE;
        gp_ABTI_global->tool_event_mask &= ~ABT_TOOL_EVENT_TASK_ALL;
    } else {
        task_event_mask = event_mask & ABT_TOOL_EVENT_TASK_ALL;
    }
    gp_ABTI_global->tool_event_mask =
        (gp_ABTI_global->tool_event_mask & (~ABT_TOOL_EVENT_TASK_ALL)) |
        task_event_mask;
    gp_ABTI_global->tool_task_cb_f = cb_func;
    return ABT_SUCCESS;
#endif
}
