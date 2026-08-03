/**
 * @file sm_engine.h
 * @brief Core state machine engine API for v3.0
 * @version 3.0.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Handle-based, multi-instance state machine engine.
 * All functions take SM_Handle_t as the first parameter.
 * The framework is state-agnostic -- it does not define any application
 * states or events.
 */

#ifndef SM_ENGINE_H
#define SM_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sm_types.h"

/* =============================================================================
 * LIFECYCLE
 * ===========================================================================*/

/**
 * @brief Initialize a state machine instance
 *
 * Stores config pointer, initializes event queue, error handler, and sets
 * the initial state. The SM_Context_t must be statically allocated by the user.
 *
 * @param sm     Pointer to user-allocated SM_Context_t
 * @param config Pointer to configuration (states, transitions, initial state)
 * @return true if initialization succeeded, false on invalid parameters
 *
 * @note config->states array must have exactly SM_STATE_COUNT entries.
 * @note config pointer is stored (not copied) -- must remain valid.
 */
bool SM_Init(SM_Handle_t sm, const SM_Config_t *config);

/**
 * @brief Process one iteration of the state machine
 *
 * Per call (v4.0 order):
 *   1. Deferred on_entry for the initial state (after SM_Init / SM_Reset only)
 *   2. on_execute for the current state (exactly once per call)
 *   3. State timeout check -- posts SM_EVT_TIMEOUT once per state entry
 *      (retried next call if the queue was full)
 *   4. Time event tick (deadline-based, ms) -- fires post into the queue
 *   5. Event drain: up to SM_MAX_EVENTS_PER_PROCESS events, each with full
 *      run-to-completion semantics. A transition executes
 *      exit -> action -> state update -> entry ATOMICALLY within this call;
 *      subsequent drained events are evaluated against the new state, and
 *      the new state's min_dwell_ms gates further processing.
 *
 * Intended to run from the main/task context on a periodic schedule
 * (e.g. SM_TASK_PERIOD_MS).
 *
 * @param sm Handle to the state machine instance
 *
 * @warning Do not call from ISR context.
 * @warning Do not call recursively from inside state callbacks, guards,
 *          transition actions, or hooks invoked by this function — re-entering
 *          SM_Process on the same SM_Handle_t corrupts runtime state.
 *
 * @note If a matching transition has \a to_state >= SM_STATE_COUNT, the event
 *       is consumed and the machine stays in the current state (runtime warn
 *       when debug is enabled).
 * @note An event posted from a callback during the drain may be processed
 *       later in the same call if the drain budget allows -- this is
 *       intentional RTC chaining, bounded by SM_MAX_EVENTS_PER_PROCESS.
 */
void SM_Process(SM_Handle_t sm);

/**
 * @brief Reset the state machine to its initial state
 *
 * Flushes the event queue, clears errors (except critical lock),
 * and transitions to config->initial_state.
 *
 * @param sm Handle to the state machine instance
 *
 * @note Will NOT reset if critical error lock is active.
 */
void SM_Reset(SM_Handle_t sm);

/* =============================================================================
 * EVENT POSTING (ISR-SAFE)
 * ===========================================================================*/

/**
 * @brief Post an event to the state machine's event queue
 *
 * ISR-SAFE: Uses critical sections. Accepts user-defined events with IDs
 * `< SM_EVENT_COUNT`; SM_EVT_TIMEOUT is rejected (engine-only signal).
 *
 * Delivery is strict FIFO in post order for ALL sources -- user, ISR,
 * internal timeout, and time events share one ordering rule (v4.0; v3.0
 * let internal posts jump queued backlog via the front slot). The only
 * deliberate exception is SM_RecallEvent, which inserts at the true front.
 *
 * @param sm    Handle to the state machine instance
 * @param event User-defined event ID
 * @param data  Event payload (uint32_t)
 * @return true if event enqueued, false if queue full or invalid params.
 *         Prefer checking this return rather than SM_EventQueueIsFull() before
 *         posting (atomic enqueue decision).
 */
bool SM_PostEvent(SM_Handle_t sm, uint16_t event, uint32_t data);

/* =============================================================================
 * EVENT QUEUE QUERIES
 * ===========================================================================*/

/**
 * @brief Check if the event queue is full
 *
 * Mirrors SM_PostEvent's accept logic exactly: full iff the ring is full
 * (the front slot only counts as capacity when the queue is completely
 * empty, so effective capacity is SM_EVENT_QUEUE_SIZE, +1 transiently via
 * the empty-queue fast path). v4.0 fix: v3.0 could report not-full while a
 * post would in fact be dropped.
 *
 * @param sm Handle to the state machine instance
 * @return true if queue is full (SM_PostEvent would fail)
 *
 * @note Diagnostic only: not atomic with SM_PostEvent from an ISR — an ISR
 *       may post between this check and a subsequent action (TOCTOU).
 */
bool SM_EventQueueIsFull(SM_Handle_t sm);

/**
 * @brief Check if the event queue is empty
 *
 * @param sm Handle to the state machine instance
 * @return true if no events pending
 *
 * @note Diagnostic only; same TOCTOU caveat as SM_EventQueueIsFull.
 */
bool SM_EventQueueIsEmpty(SM_Handle_t sm);

/**
 * @brief Get the number of events pending in the queue
 *
 * @param sm Handle to the state machine instance
 * @return Number of events currently queued
 *
 * @note Diagnostic only; same TOCTOU caveat as SM_EventQueueIsFull.
 */
uint8_t SM_EventQueueDepth(SM_Handle_t sm);

/**
 * @brief Flush (discard) all pending events
 *
 * @param sm Handle to the state machine instance
 */
void SM_EventQueueFlush(SM_Handle_t sm);

/**
 * @brief Get the minimum free queue slots ever recorded (watermark)
 *
 * Tracks the high-water mark of queue usage.  nMin starts at
 * SM_EVENT_QUEUE_SIZE and decreases toward 0 as the queue fills.
 * Useful for sizing the queue during development.
 *
 * @param sm Handle to the state machine instance
 * @return Minimum number of free slots that has ever been observed
 */
uint8_t SM_EventQueueGetMin(SM_Handle_t sm);

/* =============================================================================
 * STATE QUERIES
 * ===========================================================================*/

/**
 * @brief Get the current state index
 *
 * ISR-SAFE: current_state is volatile uint16_t.
 *
 * @param sm Handle to the state machine instance
 * @return Current state index
 */
uint16_t SM_GetState(SM_Handle_t sm);

/**
 * @brief Get the previous state index
 *
 * @param sm Handle to the state machine instance
 * @return Previous state index (before last transition)
 */
uint16_t SM_GetPreviousState(SM_Handle_t sm);

/**
 * @brief Get time spent in the current state (milliseconds)
 *
 * @param sm Handle to the state machine instance
 * @return Milliseconds since entering the current state
 */
uint32_t SM_GetStateTime(SM_Handle_t sm);

/**
 * @brief Get the number of SM_Process() executions in the current state
 *
 * @param sm Handle to the state machine instance
 * @return Execution count since entering the current state
 */
uint32_t SM_GetExecCount(SM_Handle_t sm);

/* =============================================================================
 * STATE HISTORY
 * ===========================================================================*/

/**
 * @brief Get recent state transition history
 *
 * Copies state indices from the history ring buffer into caller's buffer.
 * Most recent first.
 *
 * @param sm      Handle to the state machine instance
 * @param buf     Output buffer for state indices
 * @param buf_len Size of output buffer (max entries to write)
 * @param count   Output: actual number of entries written
 * @return true if successful, false if invalid parameters
 */
bool SM_GetStateHistory(SM_Handle_t sm, uint16_t *buf, uint8_t buf_len, uint8_t *count);

/* =============================================================================
 * RUNTIME TRANSITION MANAGEMENT (compile-time optional)
 * ===========================================================================*/

#if SM_FEATURE_RUNTIME_TRANSITIONS

/**
 * @brief Add a transition at runtime
 *
 * Appends a transition to the runtime transition table. These are checked
 * AFTER the const flash table during event processing.
 *
 * Valid events are the user range (`< SM_EVENT_COUNT`) plus SM_EVT_TIMEOUT,
 * so runtime transitions can handle state timeouts (v4.0; v3.0 rejected it).
 *
 * @param sm         Handle to the state machine instance
 * @param transition Pointer to transition definition to add
 * @return true if added, false if table full or invalid parameters
 *
 * @warning Not synchronized with ISR posting or concurrent SM_Process — call
 *          only from boot/initialization or the same non-ISR context as the
 *          state machine runtime (never from an interrupt).
 */
bool SM_AddTransition(SM_Handle_t sm, const SM_Transition_t *transition);

#endif /* SM_FEATURE_RUNTIME_TRANSITIONS */

/* =============================================================================
 * STATISTICS (compile-time optional)
 * ===========================================================================*/

#if SM_FEATURE_STATISTICS

/**
 * @brief Get a snapshot of runtime statistics
 *
 * @param sm    Handle to the state machine instance
 * @param stats Output: statistics snapshot
 * @return true if successful, false if invalid parameters
 */
bool SM_GetStats(SM_Handle_t sm, SM_Stats_t *stats);

/**
 * @brief Reset all statistics counters to zero
 *
 * @param sm Handle to the state machine instance
 */
void SM_ResetStats(SM_Handle_t sm);

#endif /* SM_FEATURE_STATISTICS */

/* =============================================================================
 * TIME EVENTS (compile-time optional, D9)
 * ===========================================================================*/

#if SM_FEATURE_TIME_EVENTS

/**
 * @brief Initialize a time event (does NOT arm it)
 *
 * Must be called once before SM_TimeEvt_Arm.
 * Sets the owning state machine and event signal.
 *
 * @param te    Pointer to user-allocated SM_TimeEvt_t
 * @param sm    Handle to the owning state machine
 * @param sig   Event ID to post on expiry
 * @param data  Event payload to carry
 */
void SM_TimeEvt_Init(SM_TimeEvt_t *te, SM_Handle_t sm, uint16_t sig, uint32_t data);

/**
 * @brief Arm (start) a time event -- millisecond deadline (v4.0)
 *
 * ISR-SAFE: uses critical sections.
 * Schedules the timer to fire when SM_Platform_GetTimeMs() reaches
 * now + delay_ms, checked once per SM_Process() call (so the effective
 * resolution is the SM_Process period, but a late check fires immediately
 * rather than stretching with call cadence). Re-arming an already-scheduled
 * timer updates its deadline in place (no duplicate list entry).
 *
 * @param te          Pointer to an initialized SM_TimeEvt_t
 * @param delay_ms    Milliseconds until first fire. Must be > 0 and < 2^31.
 * @param interval_ms Reload period in ms for periodic firing (0 = one-shot).
 *                    Must be < 2^31. Periodic deadlines advance by whole
 *                    intervals: drift-free, and periods missed during a
 *                    stall coalesce into a single event.
 * @return true if armed; false on invalid parameters or when
 *         SM_FEATURE_MAX_TIME_EVENTS timers are already scheduled on this
 *         instance (v4.0: capacity is enforced here instead of timers
 *         silently never firing).
 */
bool SM_TimeEvt_Arm(SM_TimeEvt_t *te, uint32_t delay_ms, uint32_t interval_ms);

/**
 * @brief Disarm (stop) a time event
 *
 * ISR-SAFE: uses critical sections.
 * Removes the time event from the linked list if it was scheduled.
 *
 * @param te  Pointer to the time event to disarm
 * @return true if the event was armed and has been disarmed,
 *         false if it was already disarmed
 *
 * @note A disarm can race a fire already collected by SM_TimeEvt_Tick_ in
 *       the same cycle: the event may still be delivered once. If the state
 *       no longer handles it, it is discarded (standard time-event contract).
 */
bool SM_TimeEvt_Disarm(SM_TimeEvt_t *te);

/**
 * @brief Tick all time events for a state machine instance (internal)
 *
 * Called from SM_Process BEFORE the event drain, so a timer firing this
 * cycle is normally delivered this cycle. Two-phase: the list walk runs in
 * a short critical section; collected fires are posted outside it, keeping
 * interrupt-masked time small and bounded.
 *
 * @param sm Handle to the state machine instance
 *
 * @warning Internal API -- do not call directly from application code.
 */
void SM_TimeEvt_Tick_(SM_Handle_t sm);

#endif /* SM_FEATURE_TIME_EVENTS */

/* =============================================================================
 * DEFERRED EVENTS (compile-time optional, D10)
 * ===========================================================================*/

#if SM_FEATURE_DEFER

/**
 * @brief Defer an event for later processing
 *
 * Places the event into the deferred queue (FIFO). Deferred events are
 * recalled one at a time -- oldest first -- via SM_RecallEvent, typically
 * on state entry.
 *
 * NOT ISR-safe -- call only from state callbacks or SM_Process context.
 *
 * @param sm    Handle to the state machine instance
 * @param event Event ID to defer
 * @param data  Event payload
 * @return true if deferred, false if defer queue full or invalid params
 */
bool SM_DeferEvent(SM_Handle_t sm, uint16_t event, uint32_t data);

/**
 * @brief Recall one deferred event to the true front of the main queue
 *
 * Pops the OLDEST deferred event (FIFO among deferred events) and inserts
 * it at the front of the main queue so it is the next event processed,
 * ahead of any queued backlog. If the front slot is occupied, the current
 * front is displaced into the ring immediately behind the recalled event
 * (QP/C postLIFO semantics). v4.0 fixes two v3.0 defects here: the "front"
 * insert actually appended to the BACK when the front slot was occupied,
 * and a full main queue LOST the event -- it now stays safely deferred.
 *
 * NOT ISR-safe -- call only from state callbacks or SM_Process context.
 *
 * @param sm Handle to the state machine instance
 * @return true if an event was recalled, false if the defer queue is empty
 *         or the main queue is full (event remains deferred)
 */
bool SM_RecallEvent(SM_Handle_t sm);

/**
 * @brief Discard all deferred events
 *
 * @param sm Handle to the state machine instance
 */
void SM_FlushDeferred(SM_Handle_t sm);

#endif /* SM_FEATURE_DEFER */

#ifdef __cplusplus
}
#endif

#endif /* SM_ENGINE_H */
