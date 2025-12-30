/**
 * @file sm_state_machine.h
 * @brief State machine core API
 * @version 2.0.0
 * @date 2025-12-30
 *
 * @copyright Copyright (c) 2025
 *
 * This file contains the public API for the state machine core functionality.
 */

#ifndef SM_STATE_MACHINE_H
#define SM_STATE_MACHINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sm_types.h"

/* =============================================================================
 * STATE MACHINE INITIALIZATION
 * ===========================================================================*/

/**
 * @brief Initialize the state machine
 *
 * Must be called once before using any other state machine functions.
 * Initializes all subsystems (state machine, error handler, debug system).
 *
 * @return true if initialization successful, false otherwise
 *
 * @note This function:
 *       - Resets all state machine variables
 *       - Initializes error handler
 *       - Initializes state table
 *       - Sets initial state to STATE_INIT
 */
bool StateMachine_Init(void);

/**
 * @brief Execute one iteration of the state machine
 *
 * This function should be called periodically (every SM_TASK_PERIOD_MS).
 * It executes the current state callbacks and processes pending events.
 *
 * @return Current state after execution
 *
 * @note This function is non-blocking and should complete quickly.
 *       Call from main loop or RTOS task.
 *
 * @warning Do not call from interrupt context
 */
StateMachineState_t StateMachine_Execute(void);

/**
 * @brief Reset the state machine to initial state
 *
 * Clears all errors (except critical lock) and returns to STATE_INIT.
 *
 * @note Will NOT reset if critical error lock is active
 */
void StateMachine_Reset(void);

/* =============================================================================
 * EVENT HANDLING
 * ===========================================================================*/

/**
 * @brief Post an event to trigger state transition
 *
 * This is the ONLY way to trigger state transitions. Events are processed
 * in the next call to StateMachine_Execute().
 *
 * @param event Event to post
 * @return true if event posted successfully, false if event queue full
 *
 * @note THREAD-SAFE: This function uses critical sections and can be called
 *       from interrupts or different tasks.
 *
 * @warning Only ONE event can be pending at a time. If an event is already
 *          pending, this function returns false and the new event is dropped.
 */
bool StateMachine_PostEvent(StateMachineEvent_t event);

/* =============================================================================
 * STATE QUERIES
 * ===========================================================================*/

/**
 * @brief Get current state
 *
 * @return Current active state
 */
StateMachineState_t StateMachine_GetCurrentState(void);

/**
 * @brief Get previous state
 *
 * @return State before the last transition
 *
 * @note Useful for transition logic and debugging
 */
StateMachineState_t StateMachine_GetPreviousState(void);

/**
 * @brief Get time spent in current state
 *
 * @return Milliseconds since entering current state
 */
uint32_t StateMachine_GetStateTime(void);

/**
 * @brief Get number of times current state has executed
 *
 * @return Execution count for current state
 *
 * @note Resets to 0 when entering a new state
 */
uint32_t StateMachine_GetExecutionCount(void);

/* =============================================================================
 * STRING CONVERSION UTILITIES
 * ===========================================================================*/

/**
 * @brief Convert state to string representation
 *
 * @param state State to convert
 * @return Pointer to constant string (e.g., "ACTIVE", "IDLE")
 *
 * @note Returns "UNKNOWN" for invalid states
 */
const char *StateMachine_StateToString(StateMachineState_t state);

/**
 * @brief Convert event to string representation
 *
 * @param event Event to convert
 * @return Pointer to constant string (e.g., "EVENT_START", "EVENT_TIMEOUT")
 *
 * @note Returns "UNKNOWN" for invalid events
 */
const char *StateMachine_EventToString(StateMachineEvent_t event);

/* =============================================================================
 * ADVANCED: STATE TABLE MANIPULATION
 * ===========================================================================*/

/**
 * @brief Add a transition to a state
 *
 * Allows runtime modification of state transition table.
 *
 * @param state State to add transition to
 * @param event Event that triggers transition
 * @param next_state Target state
 * @return true if transition added, false if table full or invalid parameters
 *
 * @warning Use with caution - modifying transition table at runtime can lead
 *          to unpredictable behavior. Prefer configuring at initialization.
 */
bool StateMachine_AddTransition(StateMachineState_t state,
                                 StateMachineEvent_t event,
                                 StateMachineState_t next_state);

/**
 * @brief Set state timeout
 *
 * Configure or change timeout for a specific state.
 *
 * @param state State to configure
 * @param timeout_ms Timeout in milliseconds (0 = no timeout)
 * @return true if successful, false if invalid state
 */
bool StateMachine_SetStateTimeout(StateMachineState_t state, uint32_t timeout_ms);

/**
 * @brief Set state callbacks
 *
 * Configure entry, state, and exit callbacks for a state.
 *
 * @param state State to configure
 * @param on_entry Entry callback (can be NULL)
 * @param on_state State callback (can be NULL)
 * @param on_exit Exit callback (can be NULL)
 * @return true if successful, false if invalid state
 */
bool StateMachine_SetStateCallbacks(StateMachineState_t state,
                                     void (*on_entry)(void),
                                     void (*on_state)(void),
                                     void (*on_exit)(void));

/* =============================================================================
 * STATISTICS (if FEATURE_STATISTICS_ENABLED)
 * ===========================================================================*/

#if FEATURE_STATISTICS_ENABLED

/**
 * @brief State machine statistics
 */
typedef struct {
    uint32_t total_transitions;        /**< Total number of state transitions */
    uint32_t total_events_posted;      /**< Total events posted */
    uint32_t total_events_dropped;     /**< Events dropped (queue full) */
    uint32_t total_timeouts;           /**< State timeouts occurred */
    uint32_t state_entry_counts[SM_MAX_STATES]; /**< Times each state entered */
    uint32_t max_execution_time_us;    /**< Maximum execution time */
    uint32_t avg_execution_time_us;    /**< Average execution time */
} StateMachineStats_t;

/**
 * @brief Get state machine statistics
 *
 * @param stats Pointer to structure to fill with statistics
 * @return true if successful, false if stats is NULL
 */
bool StateMachine_GetStats(StateMachineStats_t *stats);

/**
 * @brief Reset statistics counters
 */
void StateMachine_ResetStats(void);

#endif /* FEATURE_STATISTICS_ENABLED */

#ifdef __cplusplus
}
#endif

#endif /* SM_STATE_MACHINE_H */
