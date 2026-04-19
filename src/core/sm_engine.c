/**
 * @file sm_engine.c
 * @brief Core state machine engine implementation (v3.0 Phase 2)
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Handle-based, multi-instance state machine engine with:
 *   - QP/C frontEvt optimization (D6)
 *   - Duplicate Inverse Storage on current_state and initialized (D7)
 *   - Numeric assertion IDs (D8) via SM_DEFINE_MODULE / SM_REQUIRE
 *   - Time events -- intrusive linked-list, arm/disarm, one-shot/periodic (D9)
 *   - Deferred events -- defer/recall with LIFO recall to front (D10)
 *   - Guard conditions with multi-guard fallthrough (D6 extension)
 *   - State timeout (fire-once internal event)
 *   - Min dwell time enforcement
 *   - HSM parent-state fallback (optional, SM_FEATURE_HSM)
 *   - Hard-bounded loops with SM_INVARIANT
 *
 * No extern globals. No heap. No application-specific state definitions.
 *
 * Assertion ID ranges:
 *   100-199  SM_Init
 *   200-299  SM_Process
 *   300-399  Time events
 *   400-499  Deferred events
 *   500-599  Event posting / queue ops
 *   600-699  Reset / misc lifecycle
 */

#include "sm_framework/sm_framework.h"
#include <string.h>

SM_DEFINE_MODULE("sm_engine");

/* =============================================================================
 * INTERNAL CONSTANTS
 * ===========================================================================*/

/**
 * @brief Internal timeout event
 *
 * Posted by SM_Process when a state's timeout_ms expires.  Defined as
 * (SM_EVENT_COUNT) so it sits one above the user's event range.
 * SM_PostEvent rejects events >= SM_EVENT_COUNT via its bounds check, so
 * this internal event uses a dedicated bypass path (sm_post_internal).
 */
#define SM_INTERNAL_TIMEOUT  ((uint16_t)(SM_EVENT_COUNT))

/* =============================================================================
 * INTERNAL HELPERS
 * ===========================================================================*/

/**
 * @brief Update the queue-free watermark (nMin)
 *
 * Called after every successful enqueue.  Tracks the minimum number of free
 * slots that has ever been observed.
 */
static void sm_queue_update_watermark(SM_EventQueue_t *q)
{
    uint8_t used = q->count;
    if (q->front_valid) {
        used++;
    }
    uint8_t free_slots = (uint8_t)(SM_EVENT_QUEUE_SIZE - (used < SM_EVENT_QUEUE_SIZE ? used : SM_EVENT_QUEUE_SIZE));
    if (free_slots < q->nMin) {
        q->nMin = free_slots;
    }
}

/**
 * @brief Internal event post that bypasses the event-range check
 *
 * Used for SM_INTERNAL_TIMEOUT and deferred-event recall.
 * NOT ISR-safe by itself -- caller must already be in a safe context
 * (SM_Process runs from task context, not ISR).
 */
static bool sm_post_internal(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    SM_EventQueue_t *q = &sm->event_queue;

    SM_Platform_EnterCritical();
    {
        if (!q->front_valid) {
            /* Fast path: place directly into front slot */
            q->front.event = event;
            q->front._reserved = 0U;
            q->front.data = data;
            q->front_valid = true;
            sm_queue_update_watermark(q);
            SM_Platform_ExitCritical();
            return true;
        }

        if (q->count >= SM_EVENT_QUEUE_SIZE) {
            SM_Platform_ExitCritical();
#if SM_FEATURE_STATISTICS
            sm->stats.total_events_dropped++;
#endif
            return false;
        }

        q->items[q->head].event = event;
        q->items[q->head]._reserved = 0U;
        q->items[q->head].data = data;
        q->head = (uint8_t)((q->head + 1U) % SM_EVENT_QUEUE_SIZE);
        q->count++;
        sm_queue_update_watermark(q);
    }
    SM_Platform_ExitCritical();
    return true;
}

/**
 * @brief Dequeue one event -- checks front slot first, then ring buffer
 *
 * Called only from SM_Process (single-thread context).
 */
static bool sm_event_dequeue(SM_Handle_t sm, SM_EventItem_t *item)
{
    SM_EventQueue_t *q = &sm->event_queue;

    /* Check front slot first (QP/C frontEvt optimization) */
    if (q->front_valid) {
        *item = q->front;
        q->front_valid = false;
        return true;
    }

    /* Fall through to ring buffer */
    if (q->count == 0U) {
        return false;
    }

    *item = q->items[q->tail];
    q->tail = (uint8_t)((q->tail + 1U) % SM_EVENT_QUEUE_SIZE);
    q->count--;

    return true;
}

/**
 * @brief Record a state in the history ring buffer
 */
static void sm_history_record(SM_Handle_t sm, uint16_t state)
{
    sm->state_history[sm->history_head] = state;
    sm->history_head = (uint8_t)((sm->history_head + 1U) % SM_STATE_HISTORY_DEPTH);
}

/**
 * @brief Get the state descriptor for a given state index
 *
 * @return Pointer to SM_StateDesc_t, or NULL if config/states is invalid
 */
static const SM_StateDesc_t *sm_get_state_desc(SM_Handle_t sm, uint16_t state_id)
{
    if (sm->config == NULL || sm->config->states == NULL ||
        state_id >= SM_STATE_COUNT) {
        return NULL;
    }
    return &sm->config->states[state_id];
}

/**
 * @brief Search a transition table for a matching (from_state, event) pair
 *
 * Evaluates guard conditions. If multiple transitions match the same
 * (from_state, event) with different guards, tries each in order until
 * one's guard returns true (or guard is NULL = always allow).
 *
 * @param table      Array of transitions
 * @param count      Number of entries in the table
 * @param sm         Handle (passed to guard functions)
 * @param from_state Current state index
 * @param event      Event ID
 * @param data       Event payload
 * @return Pointer to the matching transition, or NULL if none found
 */
static const SM_Transition_t *sm_find_transition(
    const SM_Transition_t *table, uint16_t count,
    SM_Handle_t sm, uint16_t from_state, uint16_t event, uint32_t data)
{
    if (table == NULL || count == 0U) {
        return NULL;
    }

    /*
     * Hard-bounded loop (D8). The bound is the table size itself, which
     * is finite and caller-provided.
     */
    for (uint16_t i = 0U; i < count; i++) {
        SM_INVARIANT(210, i < count);  /* guaranteed by loop condition */

        if (table[i].from_state == from_state && table[i].event == event) {
            /* Guard evaluation (D6 extension: multi-guard fallthrough) */
            if (table[i].guard == NULL || table[i].guard(sm, event, data)) {
                return &table[i];
            }
            /* Guard returned false -- keep searching for next matching entry */
        }
    }

    return NULL;
}

#if SM_FEATURE_HSM
/**
 * @brief Find transition with HSM parent-state fallback
 *
 * If no transition in the current state, walks up the parent chain
 * (bounded by SM_HSM_MAX_DEPTH) looking for a handler.
 */
static const SM_Transition_t *sm_find_transition_hsm(
    SM_Handle_t sm, uint16_t from_state, uint16_t event, uint32_t data)
{
    uint16_t state = from_state;

    for (uint8_t depth = 0U; depth < SM_HSM_MAX_DEPTH; depth++) {
        SM_INVARIANT(220, depth < SM_HSM_MAX_DEPTH);

        /* Search const flash table */
        const SM_Transition_t *t = sm_find_transition(
            sm->config->transitions, sm->config->transition_count,
            sm, state, event, data);
        if (t != NULL) {
            return t;
        }

#if SM_FEATURE_RUNTIME_TRANSITIONS
        /* Search runtime table */
        t = sm_find_transition(
            sm->rt_transitions, sm->rt_transition_count,
            sm, state, event, data);
        if (t != NULL) {
            return t;
        }
#endif

        /* Walk to parent */
        const SM_StateDesc_t *desc = sm_get_state_desc(sm, state);
        if (desc == NULL || desc->parent == UINT16_MAX) {
            break;  /* No parent -- stop */
        }
        state = desc->parent;
    }

    return NULL;
}
#endif /* SM_FEATURE_HSM */

/* =============================================================================
 * LIFECYCLE
 * ===========================================================================*/

bool SM_Init(SM_Handle_t sm, const SM_Config_t *config)
{
    SM_REQUIRE(100, sm != NULL);
    SM_REQUIRE(101, config != NULL);

    if (sm == NULL || config == NULL) {
        return false;
    }

    SM_REQUIRE(102, config->states != NULL);
    SM_REQUIRE(103, config->transitions != NULL);

    if (config->states == NULL || config->transitions == NULL) {
        return false;
    }

    SM_REQUIRE(104, config->initial_state < SM_STATE_COUNT);

    if (config->initial_state >= SM_STATE_COUNT) {
        return false;
    }

    /* Zero the entire context */
    memset(sm, 0, sizeof(SM_Context_t));

    /* Store config pointer (not copied -- must remain valid) */
    sm->config = config;

    /* Set initial state + DIS (D7) */
    sm->current_state = config->initial_state;
    sm->previous_state = config->initial_state;
    SM_DIS_UPDATE(sm->current_state, sm->state_dis, uint16_t);

    /* Initialize timing */
    sm->state_entry_time = SM_Platform_GetTimeMs();
    sm->state_exec_count = 0U;
    sm->state_entered = true;   /* First cycle should run on_entry */
    sm->timeout_fired = false;

    /* Event queue: head/tail/count zeroed by memset, init front + watermark */
    sm->event_queue.front_valid = false;
    sm->event_queue.nMin = SM_EVENT_QUEUE_SIZE;

    /* Error handler is zeroed by memset */
    sm->error.critical_lock = false;

    /* Callbacks start as NULL (zeroed) */

    /* Runtime transitions zeroed */
#if SM_FEATURE_RUNTIME_TRANSITIONS
    sm->rt_transition_count = 0U;
#endif

    /* Time events -- empty list */
#if SM_FEATURE_TIME_EVENTS
    sm->time_evt_head = NULL;
#endif

    /* Deferred events -- init queue */
#if SM_FEATURE_DEFER
    sm->defer_queue.head = 0U;
    sm->defer_queue.tail = 0U;
    sm->defer_queue.count = 0U;
    sm->defer_queue.front_valid = false;
    sm->defer_queue.nMin = SM_DEFER_QUEUE_SIZE;
#endif

    /* Statistics zeroed */
#if SM_FEATURE_STATISTICS
    memset(&sm->stats, 0, sizeof(SM_Stats_t));
#endif

    /* Initialized flag + DIS */
    sm->initialized = true;
    SM_DIS_UPDATE(sm->initialized ? 1U : 0U, sm->init_dis, uint8_t);

    /* Record initial state in history */
    sm_history_record(sm, config->initial_state);

#if SM_FEATURE_STATISTICS
    sm->stats.state_entry_counts[config->initial_state]++;
#endif

    SM_LOG_INFO("SM_Init: initial_state=%u, transitions=%u",
                (unsigned)config->initial_state,
                (unsigned)config->transition_count);

    return true;
}

void SM_Process(SM_Handle_t sm)
{
    if (sm == NULL) {
        return;
    }

    /* Verify initialized DIS */
    SM_DIS_VERIFY(sm->initialized ? 1U : 0U, sm->init_dis, uint8_t, 200);
    if (!sm->initialized) {
        return;
    }

    /* Verify current_state DIS (D7) */
    SM_DIS_VERIFY(sm->current_state, sm->state_dis, uint16_t, 201);

    /* Critical lock -- skip all processing (system is locked) */
    if (sm->error.critical_lock) {
        return;
    }

    /* --- Get current state descriptor --- */
    const SM_StateDesc_t *state_desc = sm_get_state_desc(sm, sm->current_state);
    SM_REQUIRE(202, state_desc != NULL);

    /* --- on_entry on first cycle after transition --- */
    if (sm->state_entered) {
        if (state_desc != NULL && state_desc->on_entry != NULL) {
            state_desc->on_entry(sm);
        }
        sm->state_entered = false;
        sm->state_entry_time = SM_Platform_GetTimeMs();
        sm->state_exec_count = 0U;
        sm->timeout_fired = false;
    }

    /* --- on_execute every cycle --- */
    if (state_desc != NULL && state_desc->on_execute != NULL) {
        state_desc->on_execute(sm);
    }
    sm->state_exec_count++;

    /* --- State timeout: fire-once internal event (D7 extension) --- */
    if (state_desc != NULL && state_desc->timeout_ms > 0U && !sm->timeout_fired) {
        uint32_t elapsed = SM_Platform_GetTimeMs() - sm->state_entry_time;
        if (elapsed >= state_desc->timeout_ms) {
            sm->timeout_fired = true;
            (void)sm_post_internal(sm, SM_INTERNAL_TIMEOUT, 0U);
            SM_LOG_VERBOSE("SM_Process: state %u timeout after %lu ms",
                           (unsigned)sm->current_state, (unsigned long)elapsed);
#if SM_FEATURE_STATISTICS
            sm->stats.total_timeouts++;
#endif
        }
    }

    /* --- Min dwell time: suppress event processing if too early --- */
    bool dwell_ok = true;
    if (state_desc != NULL && state_desc->min_dwell_ms > 0U) {
        uint32_t elapsed = SM_Platform_GetTimeMs() - sm->state_entry_time;
        if (elapsed < state_desc->min_dwell_ms) {
            dwell_ok = false;
        }
    }

    /* --- Dequeue and process ONE event (RTC: run-to-completion) --- */
    if (dwell_ok) {
        SM_EventItem_t evt;
        if (sm_event_dequeue(sm, &evt)) {
            const SM_Transition_t *trans = NULL;

#if SM_FEATURE_HSM
            trans = sm_find_transition_hsm(sm, sm->current_state,
                                           evt.event, evt.data);
#else
            /* Search const flash table first */
            trans = sm_find_transition(
                sm->config->transitions, sm->config->transition_count,
                sm, sm->current_state, evt.event, evt.data);

#if SM_FEATURE_RUNTIME_TRANSITIONS
            /* Search runtime table if no match in const table */
            if (trans == NULL) {
                trans = sm_find_transition(
                    sm->rt_transitions, sm->rt_transition_count,
                    sm, sm->current_state, evt.event, evt.data);
            }
#endif
#endif /* SM_FEATURE_HSM */

            if (trans != NULL) {
                uint16_t old_state = sm->current_state;
                uint16_t new_state = trans->to_state;

                SM_REQUIRE(203, new_state < SM_STATE_COUNT);

                /* --- Exit old state --- */
                if (state_desc != NULL && state_desc->on_exit != NULL) {
                    state_desc->on_exit(sm);
                }

                /* --- Execute transition action --- */
                if (trans->action != NULL) {
                    trans->action(sm, evt.event, evt.data);
                }

                /* --- Update state + DIS (D7) --- */
                sm->previous_state = old_state;
                sm->current_state = new_state;
                SM_DIS_UPDATE(sm->current_state, sm->state_dis, uint16_t);

                /* --- Record in history --- */
                sm_history_record(sm, new_state);

                /* --- Prepare for new state entry on next cycle --- */
                sm->state_entered = true;
                sm->timeout_fired = false;

                SM_LOG_INFO("SM_Process: %u -> %u (event=%u)",
                            (unsigned)old_state, (unsigned)new_state,
                            (unsigned)evt.event);

#if SM_FEATURE_STATISTICS
                sm->stats.total_transitions++;
                if (new_state < SM_STATE_COUNT) {
                    sm->stats.state_entry_counts[new_state]++;
                }
#endif
            } else {
                SM_LOG_VERBOSE("SM_Process: event=%u data=%lu -- no transition from state %u",
                               (unsigned)evt.event, (unsigned long)evt.data,
                               (unsigned)sm->current_state);
            }
        }
    }

    /* --- Tick time events (D9) --- */
#if SM_FEATURE_TIME_EVENTS
    SM_TimeEvt_Tick_(sm);
#endif
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

    /* Exit current state */
    const SM_StateDesc_t *desc = sm_get_state_desc(sm, sm->current_state);
    if (desc != NULL && desc->on_exit != NULL) {
        desc->on_exit(sm);
    }

    /* Flush event queue */
    SM_EventQueueFlush(sm);

    /* Flush deferred events */
#if SM_FEATURE_DEFER
    SM_FlushDeferred(sm);
#endif

    /* Clear errors */
    SM_Error_Clear(sm);

    /* Reset to initial state */
    sm->previous_state = sm->current_state;
    sm->current_state = sm->config->initial_state;
    SM_DIS_UPDATE(sm->current_state, sm->state_dis, uint16_t);
    sm->state_entry_time = SM_Platform_GetTimeMs();
    sm->state_exec_count = 0U;
    sm->state_entered = true;
    sm->timeout_fired = false;

    sm_history_record(sm, sm->config->initial_state);

    SM_LOG_INFO("SM_Reset: returned to state %u", (unsigned)sm->current_state);
}

/* =============================================================================
 * EVENT POSTING (ISR-SAFE) -- with frontEvt optimization (D6)
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

        /* frontEvt optimization (D6): if queue empty + front empty, use front */
        if (!q->front_valid && q->count == 0U) {
            q->front.event = event;
            q->front._reserved = 0U;
            q->front.data = data;
            q->front_valid = true;
            sm_queue_update_watermark(q);
#if SM_FEATURE_STATISTICS
            sm->stats.total_events_posted++;
#endif
            result = true;
        } else if (q->count >= SM_EVENT_QUEUE_SIZE) {
            /* Ring buffer full -- drop event */
#if SM_FEATURE_STATISTICS
            sm->stats.total_events_dropped++;
#endif
            result = false;
        } else {
            /* Enqueue into ring buffer */
            q->items[q->head].event = event;
            q->items[q->head]._reserved = 0U;
            q->items[q->head].data = data;
            q->head = (uint8_t)((q->head + 1U) % SM_EVENT_QUEUE_SIZE);
            q->count++;
            sm_queue_update_watermark(q);
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
    /* Full when front is occupied AND ring is full */
    return sm->event_queue.front_valid && (sm->event_queue.count >= SM_EVENT_QUEUE_SIZE);
}

bool SM_EventQueueIsEmpty(SM_Handle_t sm)
{
    if (sm == NULL) {
        return true;
    }
    return !sm->event_queue.front_valid && (sm->event_queue.count == 0U);
}

uint8_t SM_EventQueueDepth(SM_Handle_t sm)
{
    if (sm == NULL) {
        return 0U;
    }
    uint8_t depth = sm->event_queue.count;
    if (sm->event_queue.front_valid) {
        depth++;
    }
    return depth;
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
        sm->event_queue.front_valid = false;
    }
    SM_Platform_ExitCritical();
}

uint8_t SM_EventQueueGetMin(SM_Handle_t sm)
{
    if (sm == NULL) {
        return 0U;
    }
    return sm->event_queue.nMin;
}

/* =============================================================================
 * STATE QUERIES
 * ===========================================================================*/

uint16_t SM_GetState(SM_Handle_t sm)
{
    if (sm == NULL) {
        return 0U;
    }
    /* Verify DIS before returning (D7) */
    SM_DIS_VERIFY(sm->current_state, sm->state_dis, uint16_t, 250);
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
        SM_INVARIANT(260, i < available);
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

/* =============================================================================
 * TIME EVENTS (compile-time optional, D9)
 * ===========================================================================*/

#if SM_FEATURE_TIME_EVENTS

void SM_TimeEvt_Init(SM_TimeEvt_t *te, SM_Handle_t sm, uint16_t sig, uint32_t data)
{
    SM_REQUIRE(300, te != NULL);
    SM_REQUIRE(301, sm != NULL);

    if (te == NULL || sm == NULL) {
        return;
    }

    te->next = NULL;
    te->sm = sm;
    te->sig = sig;
    te->_pad = 0U;
    te->data = data;
    te->ctr = 0U;        /* disarmed */
    te->interval = 0U;
}

void SM_TimeEvt_Arm(SM_TimeEvt_t *te, uint32_t ticks, uint32_t interval)
{
    SM_REQUIRE(310, te != NULL);
    SM_REQUIRE(311, te->sm != NULL);
    SM_REQUIRE(312, ticks > 0U);

    if (te == NULL || te->sm == NULL || ticks == 0U) {
        return;
    }

    SM_Handle_t sm = te->sm;

    SM_Platform_EnterCritical();
    {
        te->ctr = ticks;
        te->interval = interval;

        /* Insert at head of the linked list if not already in list */
        /* Check if already present to avoid re-insertion */
        bool found = false;
        SM_TimeEvt_t *cur = sm->time_evt_head;
        uint16_t guard = 0U;
        while (cur != NULL && guard < SM_FEATURE_MAX_TIME_EVENTS) {
            if (cur == te) {
                found = true;
                break;
            }
            cur = cur->next;
            guard++;
        }
        SM_INVARIANT(313, guard <= SM_FEATURE_MAX_TIME_EVENTS);

        if (!found) {
            te->next = sm->time_evt_head;
            sm->time_evt_head = te;
        }
    }
    SM_Platform_ExitCritical();
}

bool SM_TimeEvt_Disarm(SM_TimeEvt_t *te)
{
    SM_REQUIRE(320, te != NULL);

    if (te == NULL || te->sm == NULL) {
        return false;
    }

    SM_Handle_t sm = te->sm;
    bool was_armed = false;

    SM_Platform_EnterCritical();
    {
        was_armed = (te->ctr > 0U);
        te->ctr = 0U;
        te->interval = 0U;

        /* Remove from linked list */
        if (sm->time_evt_head == te) {
            sm->time_evt_head = te->next;
            te->next = NULL;
        } else {
            SM_TimeEvt_t *prev = sm->time_evt_head;
            uint16_t guard = 0U;
            while (prev != NULL && prev->next != te &&
                   guard < SM_FEATURE_MAX_TIME_EVENTS) {
                prev = prev->next;
                guard++;
            }
            SM_INVARIANT(321, guard <= SM_FEATURE_MAX_TIME_EVENTS);

            if (prev != NULL && prev->next == te) {
                prev->next = te->next;
                te->next = NULL;
            }
        }
    }
    SM_Platform_ExitCritical();

    return was_armed;
}

void SM_TimeEvt_Tick_(SM_Handle_t sm)
{
    if (sm == NULL || sm->time_evt_head == NULL) {
        return;
    }

    SM_TimeEvt_t *te = sm->time_evt_head;
    uint16_t tick_count = 0U;

    while (te != NULL && tick_count < SM_FEATURE_MAX_TIME_EVENTS) {
        SM_INVARIANT(330, tick_count < SM_FEATURE_MAX_TIME_EVENTS);

        SM_TimeEvt_t *next = te->next;  /* cache next before potential removal */

        if (te->ctr > 0U) {
            te->ctr--;

            /* QP/C convention: fire when ctr reaches 0 after decrement */
            if (te->ctr == 0U) {
                /* Post the event */
                (void)sm_post_internal(sm, te->sig, te->data);

                SM_LOG_VERBOSE("TimeEvt: sig=%u fired", (unsigned)te->sig);

                if (te->interval > 0U) {
                    /* Periodic: reload */
                    te->ctr = te->interval;
                } else {
                    /* One-shot: leave disarmed (ctr == 0) but keep in list
                     * so user can re-arm without re-inserting. */
                }
            }
        }

        te = next;
        tick_count++;
    }
}

#endif /* SM_FEATURE_TIME_EVENTS */

/* =============================================================================
 * DEFERRED EVENTS (compile-time optional, D10)
 * ===========================================================================*/

#if SM_FEATURE_DEFER

bool SM_DeferEvent(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    SM_REQUIRE(400, sm != NULL);

    if (sm == NULL || !sm->initialized) {
        return false;
    }

    SM_EventQueue_t *dq = &sm->defer_queue;

    if (dq->count >= SM_DEFER_QUEUE_SIZE) {
        SM_LOG_WARN("SM_DeferEvent: defer queue full");
        return false;
    }

    dq->items[dq->head].event = event;
    dq->items[dq->head]._reserved = 0U;
    dq->items[dq->head].data = data;
    dq->head = (uint8_t)((dq->head + 1U) % SM_DEFER_QUEUE_SIZE);
    dq->count++;

    SM_LOG_VERBOSE("SM_DeferEvent: event=%u deferred (count=%u)",
                   (unsigned)event, (unsigned)dq->count);

    return true;
}

bool SM_RecallEvent(SM_Handle_t sm)
{
    SM_REQUIRE(410, sm != NULL);

    if (sm == NULL || !sm->initialized) {
        return false;
    }

    SM_EventQueue_t *dq = &sm->defer_queue;

    if (dq->count == 0U) {
        return false;
    }

    /* Pop from defer queue (FIFO dequeue from tail) */
    SM_EventItem_t item = dq->items[dq->tail];
    dq->tail = (uint8_t)((dq->tail + 1U) % SM_DEFER_QUEUE_SIZE);
    dq->count--;

    /* Post to front of main queue (LIFO recall -- place at front) */
    SM_EventQueue_t *mq = &sm->event_queue;

    SM_Platform_EnterCritical();
    {
        if (!mq->front_valid) {
            /* Front slot is free -- use it */
            mq->front = item;
            mq->front_valid = true;
        } else {
            /* Front is occupied -- push into ring buffer if room */
            if (mq->count < SM_EVENT_QUEUE_SIZE) {
                mq->items[mq->head] = item;
                mq->head = (uint8_t)((mq->head + 1U) % SM_EVENT_QUEUE_SIZE);
                mq->count++;
            } else {
                SM_Platform_ExitCritical();
                SM_LOG_WARN("SM_RecallEvent: main queue full, event lost");
#if SM_FEATURE_STATISTICS
                sm->stats.total_events_dropped++;
#endif
                return false;
            }
        }
    }
    SM_Platform_ExitCritical();

    SM_LOG_VERBOSE("SM_RecallEvent: event=%u recalled to front",
                   (unsigned)item.event);

    return true;
}

void SM_FlushDeferred(SM_Handle_t sm)
{
    if (sm == NULL) {
        return;
    }

    sm->defer_queue.head = 0U;
    sm->defer_queue.tail = 0U;
    sm->defer_queue.count = 0U;
    sm->defer_queue.front_valid = false;
}

#endif /* SM_FEATURE_DEFER */
