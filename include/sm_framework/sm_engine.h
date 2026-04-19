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
 * Dequeues one event, evaluates transitions, executes state callbacks.
 * Should be called periodically (every SM_TASK_PERIOD_MS).
 *
 * @param sm Handle to the state machine instance
 *
 * @warning Do not call from ISR context.
 * @note Phase 1 stub -- full implementation in Phase 2.
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
 * ISR-SAFE: Uses critical sections. Can be called from interrupts.
 * Events are processed in FIFO order by SM_Process().
 *
 * @param sm    Handle to the state machine instance
 * @param event User-defined event ID
 * @param data  Event payload (uint32_t)
 * @return true if event enqueued, false if queue full or invalid params
 */
bool SM_PostEvent(SM_Handle_t sm, uint16_t event, uint32_t data);

/* =============================================================================
 * EVENT QUEUE QUERIES
 * ===========================================================================*/

/**
 * @brief Check if the event queue is full
 *
 * @param sm Handle to the state machine instance
 * @return true if queue is full (SM_PostEvent would fail)
 */
bool SM_EventQueueIsFull(SM_Handle_t sm);

/**
 * @brief Check if the event queue is empty
 *
 * @param sm Handle to the state machine instance
 * @return true if no events pending
 */
bool SM_EventQueueIsEmpty(SM_Handle_t sm);

/**
 * @brief Get the number of events pending in the queue
 *
 * @param sm Handle to the state machine instance
 * @return Number of events currently queued
 */
uint8_t SM_EventQueueDepth(SM_Handle_t sm);

/**
 * @brief Flush (discard) all pending events
 *
 * @param sm Handle to the state machine instance
 */
void SM_EventQueueFlush(SM_Handle_t sm);

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
 * @param sm         Handle to the state machine instance
 * @param transition Pointer to transition definition to add
 * @return true if added, false if table full or invalid parameters
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

#ifdef __cplusplus
}
#endif

#endif /* SM_ENGINE_H */
