/**
 * @file sm_engine.c
 * @brief Core state machine engine implementation (v4.0)
 * @version 4.1.0
 * @date 2026-08-02
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Handle-based, multi-instance state machine engine with:
 *   - Strict-FIFO event queue with frontEvt fast path (D6, revised v4.0)
 *   - Duplicate Inverse Storage on current_state and initialized (D7)
 *   - Numeric assertion IDs (D8) via SM_DEFINE_MODULE / SM_REQUIRE
 *   - Time events -- deadline-based (ms), one-shot/periodic, drift-free (D9, v4.0)
 *   - Deferred events -- FIFO recall to true queue front (D10, revised v4.0)
 *   - Guard conditions with multi-guard fallthrough (D6 extension)
 *   - State timeout via public SM_EVT_TIMEOUT (retries post until queued)
 *   - Min dwell time enforcement (re-checked per drained event)
 *   - Bounded event drain: up to SM_MAX_EVENTS_PER_PROCESS per SM_Process
 *   - Atomic transitions: exit -> action -> state update -> entry in one call
 *   - HSM parent-state fallback (optional, SM_FEATURE_HSM)
 *   - Hard-bounded loops with SM_INVARIANT
 *
 * No extern globals. No heap. No application-specific state definitions.
 *
 * v4.0 semantic changes from v3.0 (see MIGRATION.md):
 *   - Event delivery is strict FIFO in post order for ALL sources (user,
 *     ISR, internal timeout, time events). Internal events no longer jump
 *     the queue via the front slot.
 *   - SM_Process drains up to SM_MAX_EVENTS_PER_PROCESS events per call
 *     (was exactly one).
 *   - A transition runs exit -> action -> entry atomically within the same
 *     SM_Process call (entry no longer waits for the next call). The
 *     deferred-entry path (state_entered flag) remains only for the initial
 *     state after SM_Init / SM_Reset.
 *   - Time events count milliseconds against SM_Platform_GetTimeMs()
 *     deadlines (wrap-safe), not SM_Process invocations. Periodic timers
 *     advance deadline by whole intervals (drift-free, missed periods
 *     coalesce into a single event).
 *   - SM_TimeEvt_Tick_ runs BEFORE the event drain, so a timer firing in
 *     this cycle is normally delivered in this same cycle.
 *
 * Assertion ID ranges (sm_engine):
 *   100-199  SM_Init (105/106: application vs library build dimensions,
 *            107: full ABI fingerprint -- layout + semantics macros)
 *   200-299  SM_Process
 *   300-399  Time events (302 re-init of an armed timer, 340 disarm-all)
 *   400-499  Deferred events
 *   500-599  Event posting / queue ops
 *   600-699  Reset / misc lifecycle
 *
 * See sm_error.c for 700-799 (error handler).
 */

#include "sm_framework/sm_framework.h"
#include <string.h>

SM_DEFINE_MODULE("sm_engine");

#if SM_FEATURE_TIME_EVENTS
/* Defined with the time-event implementation further down; SM_Reset (above
 * it in this file) needs it to clear the schedule. */
static void sm_timeevt_disarm_all(SM_Handle_t sm);
#endif

/* =============================================================================
 * INTERNAL HELPERS
 * ===========================================================================*/

/**
 * @brief Update the queue-free watermark (nMin)
 *
 * Called after every successful enqueue (inside the critical section).
 * Tracks the minimum number of free slots that has ever been observed.
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
 * @brief Strict-FIFO event post shared by SM_PostEvent and internal posts
 *
 * ISR-safe (critical section). The front slot is used only as a fast path
 * when the queue is completely empty; otherwise events append to the ring.
 * Because the front slot is always the oldest pending event, delivery order
 * equals post order for every source -- user, ISR, timeout, and time events
 * alike. (v3.0 allowed internal posts to claim the front slot ahead of ring
 * backlog; that priority inversion is removed in v4.0.)
 *
 * @return true if enqueued, false if the ring is full (event dropped;
 *         counted in stats.total_events_dropped when statistics enabled)
 */
static bool sm_post_internal(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    SM_EventQueue_t *q = &sm->event_queue;
    bool result;

    SM_Platform_EnterCritical();
    {
        if (!q->front_valid && q->count == 0U) {
            /* Fast path: queue completely empty -- use the front slot */
            q->front.event = event;
            q->front._reserved = 0U;
            q->front.data = data;
            q->front_valid = true;
            sm_queue_update_watermark(q);
            result = true;
        } else if (q->count >= SM_EVENT_QUEUE_SIZE) {
            /* Ring full -- drop */
            result = false;
        } else {
            q->items[q->head].event = event;
            q->items[q->head]._reserved = 0U;
            q->items[q->head].data = data;
            q->head = (uint8_t)((q->head + 1U) % SM_EVENT_QUEUE_SIZE);
            q->count++;
            sm_queue_update_watermark(q);
            result = true;
        }

#if SM_FEATURE_STATISTICS
        if (result) {
            sm->stats.total_events_posted++;
        } else {
            sm->stats.total_events_dropped++;
        }
#endif
    }
    SM_Platform_ExitCritical();

    return result;
}

/**
 * @brief Dequeue one event -- checks front slot first, then ring buffer
 *
 * Called only from SM_Process (single consumer). Uses a critical section so
 * the front slot / ring bookkeeping cannot interleave with an ISR post.
 */
static bool sm_event_dequeue(SM_Handle_t sm, SM_EventItem_t *item)
{
    SM_EventQueue_t *q = &sm->event_queue;
    bool result = false;

    SM_Platform_EnterCritical();
    {
        if (q->front_valid) {
            *item = q->front;
            q->front_valid = false;
            result = true;
        } else if (q->count > 0U) {
            *item = q->items[q->tail];
            q->tail = (uint8_t)((q->tail + 1U) % SM_EVENT_QUEUE_SIZE);
            q->count--;
            result = true;
        }
    }
    SM_Platform_ExitCritical();

    return result;
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

/**
 * @brief Find a transition for (current state, event) across all tables
 *
 * Searches the const flash table, then the runtime table (if enabled).
 * With SM_FEATURE_HSM, walks up the parent chain (bounded by
 * SM_HSM_MAX_DEPTH) when the current state has no handler.
 */
static const SM_Transition_t *sm_resolve_transition(
    SM_Handle_t sm, uint16_t from_state, uint16_t event, uint32_t data)
{
#if SM_FEATURE_HSM
    uint16_t state = from_state;

    for (uint8_t depth = 0U; depth < SM_HSM_MAX_DEPTH; depth++) {
        SM_INVARIANT(220, depth < SM_HSM_MAX_DEPTH);

        const SM_Transition_t *t = sm_find_transition(
            sm->config->transitions, sm->config->transition_count,
            sm, state, event, data);
        if (t != NULL) {
            return t;
        }

#if SM_FEATURE_RUNTIME_TRANSITIONS
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
#else
    const SM_Transition_t *t = sm_find_transition(
        sm->config->transitions, sm->config->transition_count,
        sm, from_state, event, data);

#if SM_FEATURE_RUNTIME_TRANSITIONS
    if (t == NULL) {
        t = sm_find_transition(
            sm->rt_transitions, sm->rt_transition_count,
            sm, from_state, event, data);
    }
#endif

    return t;
#endif /* SM_FEATURE_HSM */
}

/**
 * @brief Execute one transition atomically: exit -> action -> update -> entry
 *
 * The new state's on_entry runs within the same SM_Process call, so an
 * observer polling SM_GetState never sees the new state before its entry
 * effects have been applied (beyond the microseconds between the state
 * write and entry-callback return).
 *
 * @param sm    Handle
 * @param trans Transition to execute (to_state already range-checked)
 * @param evt   Event that triggered the transition
 */
static void sm_execute_transition(SM_Handle_t sm, const SM_Transition_t *trans,
                                  const SM_EventItem_t *evt)
{
    uint16_t old_state = sm->current_state;
    uint16_t new_state = trans->to_state;

    /* --- Exit old state --- */
    const SM_StateDesc_t *old_desc = sm_get_state_desc(sm, old_state);
    if (old_desc != NULL && old_desc->on_exit != NULL) {
        old_desc->on_exit(sm);
    }

    /* --- Transition action (state still reads as old_state here) --- */
    if (trans->action != NULL) {
        trans->action(sm, evt->event, evt->data);
    }

    /* --- Update state + DIS (D7), indivisibly (v4.1).
     *     SM_GetState is documented ISR-safe and verifies the pair, so an
     *     ISR must never observe the field updated and the shadow stale. --- */
    sm->previous_state = old_state;
    SM_DIS_ASSIGN(sm->current_state, sm->state_dis, uint16_t, new_state);

    /* --- Record in history --- */
    sm_history_record(sm, new_state);

    /* --- Reset per-state timing, then enter new state in this same call --- */
    sm->state_entry_time = SM_Platform_GetTimeMs();
    sm->state_exec_count = 0U;
    sm->state_entered = false;
    sm->timeout_fired = false;

#if SM_FEATURE_STATISTICS
    sm->stats.total_transitions++;
    sm->stats.state_entry_counts[new_state]++;
#endif

    const SM_StateDesc_t *new_desc = sm_get_state_desc(sm, new_state);
    if (new_desc != NULL && new_desc->on_entry != NULL) {
        new_desc->on_entry(sm);
    }

    SM_LOG_INFO("SM_Process: %u -> %u (event=%u)",
                (unsigned)old_state, (unsigned)new_state,
                (unsigned)evt->event);
}

/* =============================================================================
 * LIFECYCLE
 * ===========================================================================*/

bool SM_Init_(SM_Handle_t sm, const SM_Config_t *config,
              uint16_t app_state_count, uint16_t app_event_count,
              uint32_t app_abi)
{
    SM_REQUIRE(100, sm != NULL);
    SM_REQUIRE(101, config != NULL);

    /* Build consistency (v4.1): the caller's compile-time dimensions must
     * equal the ones this library was compiled with. They are not merely a
     * convention -- the checks below, SM_PostEvent's accept range and the
     * statistics array all use the LIBRARY's copies, so a mismatched
     * application would be range-checked against numbers it never saw. */
    SM_REQUIRE(105, app_state_count == (uint16_t)SM_STATE_COUNT);
    SM_REQUIRE(106, app_event_count == (uint16_t)SM_EVENT_COUNT);

    if (app_state_count != (uint16_t)SM_STATE_COUNT ||
        app_event_count != (uint16_t)SM_EVENT_COUNT) {
        SM_LOG_ERROR("SM_Init: build mismatch -- app has %u states/%u events, "
                     "library was built with %u/%u",
                     (unsigned)app_state_count, (unsigned)app_event_count,
                     (unsigned)SM_STATE_COUNT, (unsigned)SM_EVENT_COUNT);
        return false;
    }

    /* Full ABI check (v4.2, D23). 105/106 above cover two macros; the context
     * the application allocated depends on eight, and the engine's semantics on
     * several more. This compares all of them at once -- including
     * sizeof(SM_Context_t), which is the number that decides whether the writes
     * below stay inside the caller's object.
     *
     * Deliberately placed before the memset: everything up to here compares
     * values passed by register. The first dereference of `sm` must not happen
     * until the layout is known to agree. */
    SM_REQUIRE(107, app_abi == (uint32_t)SM_ABI_FINGERPRINT);

    if (app_abi != (uint32_t)SM_ABI_FINGERPRINT) {
        SM_LOG_ERROR("SM_Init: ABI mismatch -- app fingerprint 0x%08lX, "
                     "library 0x%08lX. The two disagree on a macro that "
                     "changes SM_Context_t's layout or the engine's "
                     "semantics; set them once for the whole build.",
                     (unsigned long)app_abi,
                     (unsigned long)SM_ABI_FINGERPRINT);
        return false;
    }

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

    /* Set initial state + DIS (D7).
     *
     * Plain stores, not SM_DIS_ASSIGN: SM_Init constructs the instance, and
     * an instance under construction is not yet observable by contract --
     * the memset above already precludes concurrent access. Every DIS update
     * on a LIVE machine (sm_execute_transition, SM_Reset, SM_Error_Report)
     * is indivisible instead.
     *
     * DIS-ATOMIC-EXEMPT: instance under construction, not yet observable; the
     * memset above already precludes concurrent access. Applies to all three
     * pairs written below (state, critical_lock, initialized). */
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

    /* Error handler is zeroed by memset -- set DIS for critical_lock */
    sm->error.critical_lock = false;
    SM_DIS_UPDATE(sm->error.critical_lock ? 1U : 0U,
                  sm->error.critical_lock_dis, uint8_t);

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

    /* Critical lock -- verify DIS, then skip all processing if locked */
    SM_DIS_VERIFY(sm->error.critical_lock ? 1U : 0U,
                  sm->error.critical_lock_dis, uint8_t, 205);
    if (sm->error.critical_lock) {
        return;
    }

    /* --- Get current state descriptor --- */
    const SM_StateDesc_t *state_desc = sm_get_state_desc(sm, sm->current_state);
    SM_REQUIRE(202, state_desc != NULL);
    if (state_desc == NULL) {
        return;
    }

    /* --- Deferred entry: only for the initial state after SM_Init/SM_Reset.
     *     Event-driven transitions run their entry inline (v4.0). --- */
    if (sm->state_entered) {
        sm->state_entered = false;
        sm->state_entry_time = SM_Platform_GetTimeMs();
        sm->state_exec_count = 0U;
        sm->timeout_fired = false;
        if (state_desc->on_entry != NULL) {
            state_desc->on_entry(sm);
        }
    }

    /* --- on_execute once per SM_Process, for the state current at entry
     *     to this call (transitions later in this call do NOT re-run it) --- */
    if (state_desc->on_execute != NULL) {
        state_desc->on_execute(sm);
    }
    sm->state_exec_count++;

    /* --- State timeout: post public SM_EVT_TIMEOUT once per state entry.
     *     The fired latch is only set when the post actually succeeds, so a
     *     full queue cannot permanently swallow the timeout (v4.0). --- */
    if (state_desc->timeout_ms > 0U && !sm->timeout_fired) {
        uint32_t elapsed = SM_Platform_GetTimeMs() - sm->state_entry_time;
        if (elapsed >= state_desc->timeout_ms) {
            if (sm_post_internal(sm, SM_EVT_TIMEOUT, 0U)) {
                sm->timeout_fired = true;
#if SM_FEATURE_STATISTICS
                sm->stats.total_timeouts++;
#endif
                SM_LOG_VERBOSE("SM_Process: state %u timeout after %lu ms",
                               (unsigned)sm->current_state, (unsigned long)elapsed);
            } else {
                SM_LOG_WARN("SM_Process: timeout post failed (queue full), will retry");
            }
        }
    }

    /* --- Tick time events BEFORE the drain so a timer firing this cycle is
     *     normally delivered this cycle (v4.0; was after, costing a cycle) --- */
#if SM_FEATURE_TIME_EVENTS
    SM_TimeEvt_Tick_(sm);
#endif

    /* --- Drain up to SM_MAX_EVENTS_PER_PROCESS events (RTC per event).
     *     Each iteration re-reads the current state: a transition mid-drain
     *     means subsequent events are evaluated against the NEW state, and
     *     the new state's min_dwell_ms gates further processing. --- */
    for (uint16_t drained = 0U; drained < SM_MAX_EVENTS_PER_PROCESS; drained++) {
        SM_INVARIANT(230, drained < SM_MAX_EVENTS_PER_PROCESS);

        state_desc = sm_get_state_desc(sm, sm->current_state);
        if (state_desc == NULL) {
            break;
        }

        /* Min dwell: leave events queued until the state has aged enough */
        if (state_desc->min_dwell_ms > 0U) {
            uint32_t elapsed = SM_Platform_GetTimeMs() - sm->state_entry_time;
            if (elapsed < state_desc->min_dwell_ms) {
                break;
            }
        }

        SM_EventItem_t evt;
        if (!sm_event_dequeue(sm, &evt)) {
            break;
        }

        const SM_Transition_t *trans = sm_resolve_transition(
            sm, sm->current_state, evt.event, evt.data);

        if (trans == NULL) {
            SM_LOG_VERBOSE("SM_Process: event=%u data=%lu -- no transition from state %u",
                           (unsigned)evt.event, (unsigned long)evt.data,
                           (unsigned)sm->current_state);
            continue;  /* Event consumed; keep draining */
        }

        if (trans->to_state >= SM_STATE_COUNT) {
            SM_LOG_WARN("SM_Process: drop transition (invalid to_state=%u)",
                        (unsigned)trans->to_state);
            continue;
        }

        sm_execute_transition(sm, trans, &evt);
    }
}

void SM_Reset(SM_Handle_t sm)
{
    if (sm == NULL || !sm->initialized) {
        return;
    }

    if (sm->config == NULL) {
        return;
    }

    /* Cannot reset if critical lock is active -- verify DIS */
    SM_DIS_VERIFY(sm->error.critical_lock ? 1U : 0U,
                  sm->error.critical_lock_dis, uint8_t, 600);
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

    /* Disarm every scheduled timer (v4.1). Without this, a timer armed by
     * the state we just left keeps firing into the reset machine -- the old
     * state's on_exit only disarms the timers it knows about. */
#if SM_FEATURE_TIME_EVENTS
    sm_timeevt_disarm_all(sm);
#endif

    /* Flush deferred events */
#if SM_FEATURE_DEFER
    SM_FlushDeferred(sm);
#endif

    /* Clear errors */
    SM_Error_Clear(sm);

    /* Reset to initial state. Entry is deferred to the next SM_Process
     * (same as after SM_Init) so SM_Reset stays safe to call from outside
     * the processing context. */
    sm->previous_state = sm->current_state;
    SM_DIS_ASSIGN(sm->current_state, sm->state_dis, uint16_t,
                  sm->config->initial_state);
    sm->state_entry_time = SM_Platform_GetTimeMs();
    sm->state_exec_count = 0U;
    sm->state_entered = true;
    sm->timeout_fired = false;

    sm_history_record(sm, sm->config->initial_state);

    SM_LOG_INFO("SM_Reset: returned to state %u", (unsigned)sm->current_state);
}

/* =============================================================================
 * EVENT POSTING (ISR-SAFE) -- strict FIFO with frontEvt fast path (D6, v4.0)
 * ===========================================================================*/

bool SM_PostEvent(SM_Handle_t sm, uint16_t event, uint32_t data)
{
    if (sm == NULL || !sm->initialized) {
        return false;
    }

    /* User events must be < SM_EVENT_COUNT. SM_EVT_TIMEOUT is engine-only:
     * it is posted by the timeout mechanism, never by application code. */
    if (event >= SM_EVENT_COUNT) {
        return false;
    }

    return sm_post_internal(sm, event, data);
}

/* =============================================================================
 * EVENT QUEUE QUERIES
 * ===========================================================================*/

bool SM_EventQueueIsFull(SM_Handle_t sm)
{
    if (sm == NULL) {
        return true;
    }
    /* Mirrors SM_PostEvent's accept logic exactly: a post fails iff the ring
     * is full, regardless of the front slot (the front slot is only usable
     * when the queue is completely empty). */
    return sm->event_queue.count >= SM_EVENT_QUEUE_SIZE;
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

    /* Valid events: user range plus the public timeout event, so runtime
     * transitions can handle state timeouts too (v4.0; v3.0 rejected it). */
    bool event_ok = (transition->event < SM_EVENT_COUNT) ||
                    (transition->event == SM_EVT_TIMEOUT);

    if (transition->from_state >= SM_STATE_COUNT ||
        transition->to_state >= SM_STATE_COUNT ||
        !event_ok) {
        SM_LOG_WARN("SM_AddTransition: invalid from/to/event");
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
 *
 * v4.0: deadline-based against SM_Platform_GetTimeMs().
 *   - "deadline passed" test uses modular arithmetic: (now - deadline) with
 *     uint32_t wrap is < 0x80000000 iff deadline <= now (within ~24.8 days),
 *     so 32-bit tick wrap at 49.7 days is handled.
 *   - Periodic reload advances the deadline by whole multiples of interval
 *     (phase-preserving, drift-free). If SM_Process stalls across several
 *     periods, the missed fires COALESCE into one event and the next
 *     deadline lands on the original phase grid.
 *   - One-shot timers unlink from the list on expiry, so the list holds
 *     exactly the scheduled timers and the SM_FEATURE_MAX_TIME_EVENTS bound
 *     in SM_TimeEvt_Arm is meaningful.
 * ===========================================================================*/

#if SM_FEATURE_TIME_EVENTS

/** Deadline reached iff (now - deadline) mod 2^32 is in [0, 2^31). */
#define SM_TIMEEVT_DUE(now_, deadline_) \
    ((uint32_t)((now_) - (deadline_)) < 0x80000000UL)

/**
 * @brief Disarm and unlink every time event scheduled on this instance
 *
 * Used by SM_Reset (v4.1): timers armed by the state being left used to keep
 * firing into the freshly reset machine, because Reset flushed both queues
 * but never touched the timer list.
 */
static void sm_timeevt_disarm_all(SM_Handle_t sm)
{
    SM_Platform_EnterCritical();
    {
        SM_TimeEvt_t *te = sm->time_evt_head;
        uint16_t walk = 0U;

        while (te != NULL && walk < SM_FEATURE_MAX_TIME_EVENTS) {
            SM_TimeEvt_t *next = te->next;
            te->armed = false;
            te->deadline = 0U;
            te->interval = 0U;
            te->next = NULL;
            te = next;
            walk++;
        }
        SM_INVARIANT(340, walk <= SM_FEATURE_MAX_TIME_EVENTS);

        sm->time_evt_head = NULL;
    }
    SM_Platform_ExitCritical();
}

void SM_TimeEvt_Init(SM_TimeEvt_t *te, SM_Handle_t sm, uint16_t sig, uint32_t data)
{
    SM_REQUIRE(300, te != NULL);
    SM_REQUIRE(301, sm != NULL);

    if (te == NULL || sm == NULL) {
        return;
    }

    /* Re-initializing a still-scheduled timer used to clear te->next without
     * unlinking it, silently orphaning every timer behind it in the owner's
     * list (v4.1 fix).
     *
     * The check searches the OWNER's list for this node rather than testing
     * te->armed: a freshly declared timer -- including one on the stack, as
     * the test suite and many applications use -- holds indeterminate values,
     * so reading its fields before initialization would be the bug it is
     * meant to prevent. Walking the list touches only memory the instance
     * already owns, and te->next is read only after te is found in it. */
    SM_Platform_EnterCritical();
    {
        SM_TimeEvt_t *prev = NULL;
        SM_TimeEvt_t *cur = sm->time_evt_head;
        uint16_t walk = 0U;

        while (cur != NULL && walk < SM_FEATURE_MAX_TIME_EVENTS) {
            if (cur == te) {
                if (prev != NULL) {
                    prev->next = te->next;
                } else {
                    sm->time_evt_head = te->next;
                }
                break;
            }
            prev = cur;
            cur = cur->next;
            walk++;
        }
        SM_INVARIANT(302, walk <= SM_FEATURE_MAX_TIME_EVENTS);
    }
    SM_Platform_ExitCritical();

    te->next = NULL;
    te->sm = sm;
    te->sig = sig;
    te->_pad = 0U;
    te->data = data;
    te->deadline = 0U;
    te->interval = 0U;
    te->armed = false;
}

bool SM_TimeEvt_Arm(SM_TimeEvt_t *te, uint32_t delay_ms, uint32_t interval_ms)
{
    SM_REQUIRE(310, te != NULL);
    SM_REQUIRE(311, (te == NULL) || (te->sm != NULL));
    SM_REQUIRE(312, delay_ms > 0U);
    /* Modular deadline comparison limits spans to < 2^31 ms (~24.8 days) */
    SM_REQUIRE(314, delay_ms < 0x80000000UL && interval_ms < 0x80000000UL);

    if (te == NULL || te->sm == NULL || delay_ms == 0U ||
        delay_ms >= 0x80000000UL || interval_ms >= 0x80000000UL) {
        return false;
    }

    SM_Handle_t sm = te->sm;
    uint32_t now = SM_Platform_GetTimeMs();
    bool result = true;

    SM_Platform_EnterCritical();
    {
        /* Scan for the node while counting list length. The list can never
         * exceed SM_FEATURE_MAX_TIME_EVENTS (enforced here), so the scan
         * bound doubles as a corruption guard. */
        bool found = false;
        uint16_t len = 0U;
        SM_TimeEvt_t *cur = sm->time_evt_head;
        while (cur != NULL && len < SM_FEATURE_MAX_TIME_EVENTS) {
            if (cur == te) {
                found = true;
            }
            cur = cur->next;
            len++;
        }
        SM_INVARIANT(313, len <= SM_FEATURE_MAX_TIME_EVENTS);

        if (!found && len >= SM_FEATURE_MAX_TIME_EVENTS) {
            /* Capacity exhausted -- reject rather than silently never firing
             * (v3.0 allowed over-insertion; timers past the bound never
             * ticked and re-arming them could corrupt the list). */
            result = false;
        } else {
            te->deadline = now + delay_ms;
            te->interval = interval_ms;
            te->armed = true;

            if (!found) {
                te->next = sm->time_evt_head;
                sm->time_evt_head = te;
            }
        }
    }
    SM_Platform_ExitCritical();

    if (!result) {
        SM_LOG_WARN("SM_TimeEvt_Arm: capacity reached (%u timers)",
                    (unsigned)SM_FEATURE_MAX_TIME_EVENTS);
    }

    return result;
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
        was_armed = te->armed;
        te->armed = false;
        te->deadline = 0U;
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

    uint32_t now = SM_Platform_GetTimeMs();

    /* Phase 1 (short critical section): walk the list, decide which timers
     * fire, update deadlines, unlink expired one-shots. Collected fires are
     * posted in phase 2 OUTSIDE the critical section, so interrupts are
     * only masked for the list walk, not for queue insertions + logging
     * (v3.0 held one critical section across all of it).
     *
     * Race note: a Disarm racing between phase 1 and phase 2 cannot recall
     * an already-collected fire -- the event may still be delivered once.
     * This matches the documented QP/C time-event contract. */
    struct {
        uint16_t sig;
        uint32_t data;
    } fired[SM_FEATURE_MAX_TIME_EVENTS];
    uint8_t n_fired = 0U;

    SM_Platform_EnterCritical();
    {
        SM_TimeEvt_t *prev = NULL;
        SM_TimeEvt_t *te = sm->time_evt_head;
        uint16_t walk = 0U;

        while (te != NULL && walk < SM_FEATURE_MAX_TIME_EVENTS) {
            SM_INVARIANT(330, walk < SM_FEATURE_MAX_TIME_EVENTS);

            SM_TimeEvt_t *next = te->next;

            if (te->armed && SM_TIMEEVT_DUE(now, te->deadline)) {
                fired[n_fired].sig = te->sig;
                fired[n_fired].data = te->data;
                n_fired++;

                if (te->interval > 0U) {
                    /* Periodic: advance by whole intervals past now
                     * (drift-free, phase-preserving; missed periods
                     * coalesce into this single fire). */
                    uint32_t late = now - te->deadline;
                    uint32_t periods = (late / te->interval) + 1U;
                    te->deadline += periods * te->interval;
                    prev = te;
                } else {
                    /* One-shot: disarm and unlink so the list length
                     * reflects scheduled timers only. */
                    te->armed = false;
                    if (prev != NULL) {
                        prev->next = next;
                    } else {
                        sm->time_evt_head = next;
                    }
                    te->next = NULL;
                }
            } else {
                prev = te;
            }

            te = next;
            walk++;
        }
    }
    SM_Platform_ExitCritical();

    /* Phase 2: post collected fires in list order (each post takes its own
     * short critical section). Delivery order among same-tick fires follows
     * list order, as in v3.0. */
    for (uint8_t i = 0U; i < n_fired; i++) {
        SM_INVARIANT(331, i < SM_FEATURE_MAX_TIME_EVENTS);
        if (!sm_post_internal(sm, fired[i].sig, fired[i].data)) {
            SM_LOG_WARN("TimeEvt: sig=%u dropped (queue full)",
                        (unsigned)fired[i].sig);
        } else {
            SM_LOG_VERBOSE("TimeEvt: sig=%u fired", (unsigned)fired[i].sig);
        }
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

    /* Same accept range as SM_PostEvent (v4.1): a deferred event is recalled
     * straight into the main queue, so accepting an out-of-range id here
     * would smuggle past the posting guard. */
    if (event >= SM_EVENT_COUNT) {
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

    /* Peek the oldest deferred event (FIFO among deferred events). The
     * defer-queue slot is only released after the insert succeeds, so a
     * full main queue leaves the event safely deferred (v4.0; v3.0 lost
     * the event on this path). */
    SM_EventItem_t item = dq->items[dq->tail];

    SM_EventQueue_t *mq = &sm->event_queue;
    bool inserted = false;

    SM_Platform_EnterCritical();
    {
        if (!mq->front_valid) {
            /* Front slot free -- recalled event becomes the next event */
            mq->front = item;
            mq->front_valid = true;
            inserted = true;
        } else if (mq->count < SM_EVENT_QUEUE_SIZE) {
            /* True front insertion (QP/C postLIFO semantics): displace the
             * current front into the ring's tail-1 slot so it is dequeued
             * immediately after the recalled event, ahead of the backlog.
             * (v3.0 appended to the BACK of the ring here, contradicting
             * the documented recall-to-front contract.) */
            mq->tail = (uint8_t)((mq->tail + SM_EVENT_QUEUE_SIZE - 1U) % SM_EVENT_QUEUE_SIZE);
            mq->items[mq->tail] = mq->front;
            mq->count++;
            mq->front = item;
            inserted = true;
        }

        if (inserted) {
            sm_queue_update_watermark(mq);
        }
    }
    SM_Platform_ExitCritical();

    if (!inserted) {
        SM_LOG_WARN("SM_RecallEvent: main queue full, event stays deferred");
        return false;
    }

    /* Commit: release the defer-queue slot */
    dq->tail = (uint8_t)((dq->tail + 1U) % SM_DEFER_QUEUE_SIZE);
    dq->count--;

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
