/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include "abti.h"

/** @defgroup SELF Self
 * This group is for the self wok unit.
 */

/**
 * @ingroup SELF
 * @brief   Return the type of calling work unit.
 *
 * \c ABT_self_get_type() returns the type of calling work unit, e.g.,
 * \c ABT_UNIT_TYPE_THREAD for ULT and \c ABT_UNIT_TYPE_TASK for tasklet,
 * through \c type.
 * If this routine is called when Argobots has not been initialized,
 * \c ABT_ERR_UNINITIALIZED will be returned.
 * If this routine is called by an external thread, e.g., pthread,
 * \c ABT_ERR_INV_XSTREAM will be returned.
 *
 * Now \c type will be set to \c ABT_UNIT_TYPE_EXT when it returns an
 * error, but this behavior is deprecated; the user should check the error code
 * to check if this routine is called by a thread not managed by Argobots.
 *
 * @param[out] type  work unit type.
 * @return Error code
 * @retval ABT_SUCCESS           on success
 * @retval ABT_ERR_UNINITIALIZED Argobots has not been initialized
 * @retval ABT_ERR_INV_XSTREAM   called by an external thread
 */
int ABT_self_get_type(ABT_unit_type *type)
{
    int abt_errno = ABT_SUCCESS;
    ABT_unit_type ret = ABT_UNIT_TYPE_EXT;

    /* If Argobots has not been initialized, set type to ABT_UNIT_TYPE_EXT. */
    ABTI_SETUP_WITH_INIT_CHECK();

    ABTI_local *p_local = ABTI_local_get_local();
    ABTI_thread_type raw_type = ABTI_self_get_type(p_local);
    ret = ABTI_thread_type_get_type(raw_type);
    /* This is when an external thread called this routine. */
    if (ABTI_IS_EXT_THREAD_ENABLED && ret == ABT_UNIT_TYPE_EXT) {
        abt_errno = ABT_ERR_INV_XSTREAM;
        goto fn_exit;
    }

fn_exit:
    *type = ret;
    return abt_errno;

fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

/**
 * @ingroup SELF
 * @brief   Check if the caller is the primary ULT.
 *
 * \c ABT_self_is_primary() confirms whether the caller is the primary ULT and
 * returns the result through \c flag.
 * If the caller is the primary ULT, \c flag is set to \c ABT_TRUE.
 * Otherwise, \c flag is set to \c ABT_FALSE.
 *
 * @param[out] flag    result (<tt>ABT_TRUE</tt>: primary ULT,
 *                     <tt>ABT_FALSE</tt>: not)
 * @return Error code
 * @retval ABT_SUCCESS           on success
 * @retval ABT_ERR_UNINITIALIZED Argobots has not been initialized
 * @retval ABT_ERR_INV_XSTREAM   called by an external thread
 */
int ABT_self_is_primary(ABT_bool *flag)
{
    int abt_errno = ABT_SUCCESS;
    ABT_bool ret = ABT_FALSE;

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM_WITH_INIT_CHECK(&p_local_xstream);

    ABTI_thread *p_thread = p_local_xstream->p_thread;
    if (p_thread->type & ABTI_THREAD_TYPE_MAIN)
        ret = ABT_TRUE;

fn_exit:
    *flag = ret;
    return abt_errno;

fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

/**
 * @ingroup SELF
 * @brief   Check if the caller's ES is the primary ES.
 *
 * \c ABT_self_on_primary_xstream() checks whether the caller work unit is
 * associated with the primary ES. If the caller is running on the primary ES,
 * \c flag is set to \c ABT_TRUE. Otherwise, \c flag is set to \c ABT_FALSE.
 *
 * @param[out] flag    result (<tt>ABT_TRUE</tt>: primary ES,
 *                     <tt>ABT_FALSE</tt>: not)
 * @return Error code
 * @retval ABT_SUCCESS           on success
 * @retval ABT_ERR_UNINITIALIZED Argobots has not been initialized
 * @retval ABT_ERR_INV_XSTREAM   called by an external thread
 */
int ABT_self_on_primary_xstream(ABT_bool *flag)
{
    int abt_errno = ABT_SUCCESS;
    ABT_bool ret = ABT_FALSE;

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM_WITH_INIT_CHECK(&p_local_xstream);

    /* Return value */
    ret = (p_local_xstream->type == ABTI_XSTREAM_TYPE_PRIMARY) ? ABT_TRUE
                                                               : ABT_FALSE;
fn_exit:
    *flag = ret;
    return abt_errno;

fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

/**
 * @ingroup SELF
 * @brief   Get the last pool's ID of calling work unit.
 *
 * \c ABT_self_get_last_pool_id() returns the last pool's ID of caller work
 * unit.  If the work unit is not running, this routine returns the ID of the
 * pool where it is residing.  Otherwise, it returns the ID of the last pool
 * where the work unit was (i.e., the pool from which the work unit was
 * popped).
 * NOTE: If this routine is not called by Argobots work unit (ULT or tasklet),
 * \c pool_id will be set to \c -1.
 *
 * @param[out] pool_id  pool id
 * @return Error code
 * @retval ABT_SUCCESS           on success
 * @retval ABT_ERR_UNINITIALIZED Argobots has not been initialized
 * @retval ABT_ERR_INV_XSTREAM   called by an external thread
 */
int ABT_self_get_last_pool_id(int *pool_id)
{
    int abt_errno = ABT_SUCCESS;
    int ret = -1;

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM_WITH_INIT_CHECK(&p_local_xstream);

    ABTI_thread *p_self = p_local_xstream->p_thread;
    ABTI_ASSERT(p_self->p_pool);
    ret = p_self->p_pool->id;

fn_exit:
    *pool_id = ret;
    return abt_errno;

fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

/**
 * @ingroup SELF
 * @brief   Suspend the current ULT.
 *
 * \c ABT_self_suspend() suspends the execution of current ULT and switches
 * to the scheduler.  The caller ULT is not pushed to its associated pool and
 * its state becomes BLOCKED.  It can be awakened and be pushed back to the
 * pool when \c ABT_thread_resume() is called.
 *
 * This routine must be called by a ULT.  Otherwise, it returns
 * \c ABT_ERR_INV_THREAD without suspending the caller.
 *
 * @return Error code
 * @retval ABT_SUCCESS           on success
 * @retval ABT_ERR_UNINITIALIZED Argobots has not been initialized
 * @retval ABT_ERR_INV_XSTREAM   called by an external thread
 * @retval ABT_ERR_INV_THREAD    called by a non-yieldable thread (tasklet)
 */
int ABT_self_suspend(void)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_xstream *p_local_xstream;
    ABTI_ythread *p_self;
    ABTI_SETUP_LOCAL_YTHREAD_WITH_INIT_CHECK(&p_local_xstream, &p_self);

    abt_errno = ABTI_ythread_set_blocked(p_self);
    ABTI_CHECK_ERROR(abt_errno);

    ABTI_ythread_suspend(&p_local_xstream, p_self, ABT_SYNC_EVENT_TYPE_USER,
                         NULL);

fn_exit:
    return abt_errno;

fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

/**
 * @ingroup SELF
 * @brief   Set the argument for the work unit function
 *
 * \c ABT_self_set_arg() sets the argument for the caller's work unit
 * function.
 *
 * @param[in] arg  argument for the work unit function
 * @return Error code
 * @retval ABT_SUCCESS           on success
 * @retval ABT_ERR_UNINITIALIZED Argobots has not been initialized
 * @retval ABT_ERR_INV_XSTREAM   called by an external thread
 */
int ABT_self_set_arg(void *arg)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM_WITH_INIT_CHECK(&p_local_xstream);

    p_local_xstream->p_thread->p_arg = arg;

fn_exit:
    return abt_errno;

fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

/**
 * @ingroup SELF
 * @brief   Retrieve the argument for the work unit function
 *
 * \c ABT_self_get_arg() returns the argument for the caller's work unit
 * function.  If the caller is a ULT, this routine returns the function argument
 * passed to \c ABT_thread_create() when the caller was created or set by \c
 * ABT_thread_set_arg().  On the other hand, if the caller is a tasklet, this
 * routine returns the function argument passed to \c ABT_task_create().
 *
 * @param[out] arg  argument for the work unit function
 * @return Error code
 * @retval ABT_SUCCESS on success
 * @retval ABT_ERR_UNINITIALIZED Argobots has not been initialized
 * @retval ABT_ERR_INV_XSTREAM   called by an external thread
 */
int ABT_self_get_arg(void **arg)
{
    int abt_errno = ABT_SUCCESS;
    void *ret = NULL;

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM_WITH_INIT_CHECK(&p_local_xstream);

    ret = p_local_xstream->p_thread->p_arg;

fn_exit:
    *arg = ret;
    return abt_errno;

fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}

/**
 * @ingroup SELF
 * @brief   Check if the running work unit is unnamed
 *
 * \c ABT_self_is_unnamed() returns whether the current work units is unnamed or
 * not.  If the caller is an external thread, it sets ABT_FALSE and returns
 * ABT_ERR_INV_XSTREAM.
 *
 * @param[out] flag  result (<tt>ABT_TRUE</tt> if unnamed)
 *
 * @return Error code
 * @retval ABT_SUCCESS           on success
 * @retval ABT_ERR_UNINITIALIZED Argobots has not been initialized
 * @retval ABT_ERR_INV_XSTREAM   called by an external thread
 */
int ABT_self_is_unnamed(ABT_bool *flag)
{
    int abt_errno = ABT_SUCCESS;

    ABTI_xstream *p_local_xstream;
    ABTI_SETUP_LOCAL_XSTREAM_WITH_INIT_CHECK(&p_local_xstream);

    *flag = (p_local_xstream->p_thread->type & ABTI_THREAD_TYPE_NAMED)
                ? ABT_FALSE
                : ABT_TRUE;

fn_exit:
    return abt_errno;

fn_fail:
    HANDLE_ERROR_FUNC_WITH_CODE(abt_errno);
    goto fn_exit;
}
