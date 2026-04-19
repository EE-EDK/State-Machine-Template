/**
 * @file sm_engine.c
 * @brief Core state machine engine implementation (v3.0)
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Handle-based, multi-instance state machine engine.
 * Event queue ring buffer is fully implemented (foundation for everything).
 * SM_Process() is a Phase 1 stub -- full transition/callback logic in Phase 2.
 *
 * No extern globals. No application-specific state callbacks.
 * The framework is state-agnostic.
 */

#include "sm_framework/sm_framework.h"
#include <string.h>

/* =============================================================================
 * INTERNAL HELPERS (sm_ prefix, static)
 * ===========================================================================*/

/**
 * @brief Dequeue one event from the ring buffer
 *
 * Called only from SM_Process() context (single-thread), so no critical
 * section needed -- SM_Process is documented as non-ISR-safe.
 *
 * @param sm    Handle to the state machine instance
 * @param item  Output: dequeued event
 * @return true if an event was dequeued, false if queue empty
 */
static bool sm_event_dequeue(SM_Handle_t sm, SM_EventItem_t *item)
{
    SM_EventQueue_t *q = &sm->event_queue;

    if (q->count == 0U) {
        return false;
    }

    *item = q->items[q->tail];
    q->tail = (uint8_t)((q->tail + 1U) % SM_EVENT_QUEUE_SIZE);
    q->count--;

    return true;
}

/* =============================================================================
 * LIFECYCLE
 * ===========================================================================*/

bool SM_Init(SM_Handle_t sm, const SM_Config_t *config)
{
    if (sm == NULL || config == NULL) {
        return false;
    }

    if (config->states == NULL || config->transitions == NULL) {
        return false;
    }

    if (config->initial_state >= SM_STATE_COUNT) {
        return false;
    }

    /* Zero the entire context */
    memset(sm, 0, sizeof(SM_Context_t));

    /* Store config pointer (not copied -- must remain valid) */
    sm->config = config;

    /* Set initial state */
    sm->current_state = config->initial_state;
    sm->previous_state = config->initial_state;

    /* Initialize timing */
    sm->state_entry_time = SM_Platform_GetTimeMs();
    sm->state_exec_count = 0U;
    sm->state_entered = true;   /* First cycle should run on_entry */
    sm->timeout_fired = false;

    /* Event queue is zeroed by memset (head=0, tail=0, count=0) */

    /* Error handler is zeroed by memset */
    sm->error.critical_lock = false;

    /* Callbacks start as NULL (zeroed) */

    /* Runtime transitions zeroed */
#if SM_FEATURE_RUNTIME_TRANSITIONS
    sm->rt_transition_count = 0U;
#endif

    /* Statistics zeroed */
#if SM_FEATURE_STATISTICS
    memset(&sm->stats, 0, sizeof(SM_Stats_t));
#endif

    sm->initialized = true;

    SM_LOG_INFO("SM_Init: initial_state=%u, transitions=%u",
                (unsigned)config->initial_state,
                (unsigned)config->transition_count);

    return true;
}

void SM_Process(SM_Handle_t sm)
{
    if (sm == NULL || !sm->initialized) {
        return;
    }

    /* Phase 1 stub: dequeue events but don't process transitions.
     * Full implementation comes in Phase 2.
     * We do run on_entry on the first call and on_execute every call. */

    const SM_StateDesc_t *state_desc = NULL;

    if (sm->config != NULL && sm->config->states != NULL &&
        sm->current_state < SM_STATE_COUNT) {
        state_desc = &sm->config->states[sm->current_state];
    }

    /* Run on_entry on first cycle after transition */
    if (sm->state_entered && state_desc != NULL) {
        if (state_desc->on_entry != NULL) {
            state_desc->on_entry(sm);
        }
        sm->state_entered = false;
        sm->state_entry_time = SM_Platform_GetTimeMs();
        sm->state_exec_count = 0U;
    }

    /* Run on_execute every cycle */
    if (state_desc != NULL && state_desc->on_execute != NULL) {
        state_desc->on_execute(sm);
    }
    sm->state_exec_count++;

    /* Dequeue and discard events (Phase 1 -- no transition logic yet) */
    SM_EventItem_t evt;
    while (sm_event_dequeue(sm, &evt)) {
        SM_LOG_VERBOSE("SM_Process: event=%u data=%lu dequeued (stub -- not processed)",
                       (unsigned)evt.event, (unsigned long)evt.data);
        (void)evt; /* suppress unused in release builds */
    }
}

void SM_Reset(SM_Handle_t sm)
{
    if (sm == NULL || !sm->initialized) {
        return;
    }

    /* Cannot reset if critical lock is active */
    if (sm->error.critical_lock) {
        SM_LOG_WARN("SM_Reset: blocked by critical error lock");
        return;
    }

    /* Flush event queue */
    SM_EventQueueFlush(sm);

    /* Clear errors */
    SM_Error_Clear(sm);

    /* Reset to initial state */
    sm->previous_state = sm->current_state;
    sm->current_state = sm->config->initial_state;
    sm->state_entry_time = SM_Platform_GetTimeMs();
    sm->state_exec_count = 0U;
    sm->state_entered = true;
    sm->timeout_fired = false;

    SM_LOG_INFO("SM_Reset: returned to state %u", (unsigned)sm->current_state);
}

/* =============================================================================
 * EVENT POSTING (ISR-SAFE)
 * ===========================================================================*/

bool SM_PostEvent(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    if (sm == NULL || !sm->initialized) {
        return false;
    }

    if (event >= SM_EVENT_COUNT) {
        return false;
    }

    bool result = false;

    SM_Platform_EnterCritical();
    {
        SM_EventQueue_t *q = &sm->event_queue;

        if (q->count >= SM_EVENT_QUEUE_SIZE) {
            /* Queue full -- drop event */
#if SM_FEATURE_STATISTICS
            sm->stats.total_events_dropped++;
#endif
            result = false;
        } else {
            /* Enqueue at head */
            q->items[q->head].event = event;
            q->items[q->head]._reserved = 0U;
            q->items[q->head].data = data;
            q->head = (uint8_t)((q->head + 1U) % SM_EVENT_QUEUE_SIZE);
            q->count++;
#if SM_FEATURE_STATISTICS
            sm->stats.total_events_posted++;
#endif
            result = true;
        }
    }
    SM_Platform_ExitCritical();

    return result;
}

/* =============================================================================
 * EVENT QUEUE QUERIES
 * ===========================================================================*/

bool SM_EventQueueIsFull(SM_Handle_t sm)
{
    if (sm == NULL) {
        return true;
    }
    return sm->event_queue.count >= SM_EVENT_QUEUE_SIZE;
}

bool SM_EventQueueIsEmpty(SM_Handle_t sm)
{
    if (sm == NULL) {
        return true;
    }
    return sm->event_queue.count == 0U;
}

uint8_t SM_EventQueueDepth(SM_Handle_t sm)
{
    if (sm == NULL) {
        return 0U;
    }
    return sm->event_queue.count;
}

void SM_EventQueueFlush(SM_Handle_t sm)
{
    if (sm == NULL) {
        return;
    }

    SM_Platform_EnterCritical();
    {
        sm->event_queue.head = 0U;
        sm->event_queue.tail = 0U;
        sm->event_queue.count = 0U;
    }
    SM_Platform_ExitCritical();
}

/* =============================================================================
 * STATE QUERIES
 * ===========================================================================*/

uint16_t SM_GetState(SM_Handle_t sm)
{
    if (sm == NULL) {
        return 0U;
    }
    return sm->current_state;  /* volatile read -- ISR-safe */
}

uint16_t SM_GetPreviousState(SM_Handle_t sm)
{
    if (sm == NULL) {
        return 0U;
    }
    return sm->previous_state;
}

uint32_t SM_GetStateTime(SM_Handle_t sm)
{
    if (sm == NULL) {
        return 0U;
    }
    return SM_Platform_GetTimeMs() - sm->state_entry_time;
}

uint32_t SM_GetExecCount(SM_Handle_t sm)
{
    if (sm == NULL) {
        return 0U;
    }
    return sm->state_exec_count;
}

/* =============================================================================
 * STATE HISTORY
 * ===========================================================================*/

bool SM_GetStateHistory(SM_Handle_t sm, uint16_t *buf, uint8_t buf_len, uint8_t *count)
{
    if (sm == NULL || buf == NULL || count == NULL || buf_len == 0U) {
        return false;
    }

    uint8_t available = SM_STATE_HISTORY_DEPTH;
    if (available > buf_len) {
        available = buf_len;
    }

    /* Read history ring: most recent first */
    for (uint8_t i = 0U; i < available; i++) {
        uint8_t idx = (uint8_t)((sm->history_head + SM_STATE_HISTORY_DEPTH - 1U - i)
                                % SM_STATE_HISTORY_DEPTH);
        buf[i] = sm->state_history[idx];
    }

    *count = available;
    return true;
}

/* =============================================================================
 * RUNTIME TRANSITIONS (compile-time optional)
 * ===========================================================================*/

#if SM_FEATURE_RUNTIME_TRANSITIONS

bool SM_AddTransition(SM_Handle_t sm, const SM_Transition_t *transition)
{
    if (sm == NULL || transition == NULL || !sm->initialized) {
        return false;
    }

    if (sm->rt_transition_count >= SM_MAX_TRANSITIONS) {
        SM_LOG_WARN("SM_AddTransition: runtime table full (%u/%u)",
                    (unsigned)sm->rt_transition_count,
                    (unsigned)SM_MAX_TRANSITIONS);
        return false;
    }

    sm->rt_transitions[sm->rt_transition_count] = *transition;
    sm->rt_transition_count++;

    return true;
}

#endif /* SM_FEATURE_RUNTIME_TRANSITIONS */

/* =============================================================================
 * STATISTICS (compile-time optional)
 * ===========================================================================*/

#if SM_FEATURE_STATISTICS

bool SM_GetStats(SM_Handle_t sm, SM_Stats_t *stats)
{
    if (sm == NULL || stats == NULL) {
        return false;
    }

    memcpy(stats, &sm->stats, sizeof(SM_Stats_t));
    return true;
}

void SM_ResetStats(SM_Handle_t sm)
{
    if (sm == NULL) {
        return;
    }

    memset(&sm->stats, 0, sizeof(SM_Stats_t));
}

#endif /* SM_FEATURE_STATISTICS */
