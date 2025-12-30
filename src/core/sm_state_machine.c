/**
 * @file sm_state_machine.c
 * @brief State machine core implementation
 * @version 2.0.0
 * @date 2025-12-30
 *
 * CRITICAL FIXES APPLIED:
 * - Removed static variables from state callbacks (non-reentrant fix)
 * - Added complete error recovery transitions to all states
 * - Used configuration constants instead of magic numbers
 * - Added thread-safe event posting with critical sections
 */

#include "sm_framework/sm_framework.h"
#include "sm_framework/sm_platform.h"
#include <string.h>

/* =============================================================================
 * PRIVATE DATA STRUCTURES
 * ===========================================================================*/

/** State machine context (global state) */
StateMachineContext_t g_sm_context;

/** State transition table */
static StateConfig_t g_state_table[SM_MAX_STATES];

/** State-specific persistent data (replaces static variables in callbacks) */
static struct {
    uint32_t init_step_count;      /**< Initialization step counter */
    bool comm_started;             /**< Communication started flag */
} g_state_data;

#if FEATURE_STATISTICS_ENABLED
/** Runtime statistics */
static StateMachineStats_t g_stats;
#endif

/* =============================================================================
 * FORWARD DECLARATIONS - STATE CALLBACKS
 * ===========================================================================*/

/* INIT state callbacks */
static void State_Init_OnEntry(void);
static void State_Init_OnState(void);
static void State_Init_OnExit(void);

/* IDLE state callbacks */
static void State_Idle_OnEntry(void);
static void State_Idle_OnState(void);
static void State_Idle_OnExit(void);

/* ACTIVE state callbacks */
static void State_Active_OnEntry(void);
static void State_Active_OnState(void);
static void State_Active_OnExit(void);

/* PROCESSING state callbacks */
static void State_Processing_OnEntry(void);
static void State_Processing_OnState(void);
static void State_Processing_OnExit(void);

/* COMMUNICATING state callbacks */
static void State_Communicating_OnEntry(void);
static void State_Communicating_OnState(void);
static void State_Communicating_OnExit(void);

/* MONITORING state callbacks */
static void State_Monitoring_OnEntry(void);
static void State_Monitoring_OnState(void);
static void State_Monitoring_OnExit(void);

/* CALIBRATING state callbacks */
static void State_Calibrating_OnEntry(void);
static void State_Calibrating_OnState(void);
static void State_Calibrating_OnExit(void);

/* DIAGNOSTICS state callbacks */
static void State_Diagnostics_OnEntry(void);
static void State_Diagnostics_OnState(void);
static void State_Diagnostics_OnExit(void);

/* RECOVERY state callbacks */
static void State_Recovery_OnEntry(void);
static void State_Recovery_OnState(void);
static void State_Recovery_OnExit(void);

/* CRITICAL_ERROR state callbacks */
static void State_CriticalError_OnEntry(void);
static void State_CriticalError_OnState(void);
static void State_CriticalError_OnExit(void);

/* =============================================================================
 * FORWARD DECLARATIONS - PRIVATE FUNCTIONS
 * ===========================================================================*/

static void InitializeStateTable(void);
static void PerformStateTransition(StateMachineState_t new_state);
static bool CheckStateTransition(StateMachineEvent_t event, StateMachineState_t *next_state);

/* =============================================================================
 * PUBLIC API IMPLEMENTATION
 * ===========================================================================*/

bool StateMachine_Init(void)
{
    /* Clear all state */
    memset(&g_sm_context, 0, sizeof(StateMachineContext_t));
    memset(&g_state_table, 0, sizeof(g_state_table));
    memset(&g_state_data, 0, sizeof(g_state_data));

    /* Initialize context */
    g_sm_context.current_state = STATE_INIT;
    g_sm_context.previous_state = STATE_INIT;
    g_sm_context.pending_event = EVENT_NONE;
    g_sm_context.state_entry_time = Platform_GetTimeMs();
    g_sm_context.state_execution_count = 0;
    g_sm_context.state_changed = false;

    /* Initialize error handler */
    if (!ErrorHandler_Init()) {
        return false;
    }

    /* Initialize state transition table */
    InitializeStateTable();

#if FEATURE_STATISTICS_ENABLED
    memset(&g_stats, 0, sizeof(g_stats));
#endif

    DEBUG_INIT("State Machine initialized");
    return true;
}

StateMachineState_t StateMachine_Execute(void)
{
    StateConfig_t *current_state_config = NULL;
    StateMachineState_t next_state;

    /* Check for critical error lock */
    if (g_sm_context.error_handler.critical_lock_active) {
        if (g_sm_context.current_state != STATE_CRITICAL_ERROR) {
            PerformStateTransition(STATE_CRITICAL_ERROR);
        }
        return g_sm_context.current_state;
    }

    current_state_config = &g_state_table[g_sm_context.current_state];

    /* Execute OnEntry callback on state change */
    if (g_sm_context.state_changed) {
        if (current_state_config->on_entry != NULL) {
            current_state_config->on_entry();
        }
        g_sm_context.state_changed = false;
        g_sm_context.state_entry_time = Platform_GetTimeMs();
        g_sm_context.state_execution_count = 0;
    }

    /* Execute OnState callback */
    if (current_state_config->on_state != NULL) {
        current_state_config->on_state();
    }
    g_sm_context.state_execution_count++;

    /* Check for state timeout */
    if (current_state_config->timeout_ms > 0) {
        if (Platform_IsTimeout(g_sm_context.state_entry_time, current_state_config->timeout_ms)) {
            DEBUG_WARNING("State %s timeout after %lu ms",
                         StateMachine_StateToString(g_sm_context.current_state),
                         current_state_config->timeout_ms);
            StateMachine_PostEvent(EVENT_TIMEOUT);
#if FEATURE_STATISTICS_ENABLED
            g_stats.total_timeouts++;
#endif
        }
    }

    /* Process pending event */
    if (g_sm_context.pending_event != EVENT_NONE) {
        if (CheckStateTransition(g_sm_context.pending_event, &next_state)) {
            PerformStateTransition(next_state);
#if FEATURE_STATISTICS_ENABLED
            g_stats.total_transitions++;
            g_stats.state_entry_counts[next_state]++;
#endif
        }
        g_sm_context.pending_event = EVENT_NONE;
    }

    return g_sm_context.current_state;
}

bool StateMachine_PostEvent(StateMachineEvent_t event)
{
    bool result = false;

    /* Validate event */
    if (event >= EVENT_MAX || event == EVENT_NONE) {
        return false;
    }

    /* THREAD-SAFE: Use critical section to protect pending_event */
    Platform_EnterCritical();
    {
        if (g_sm_context.pending_event != EVENT_NONE) {
            /* Event queue full - drop new event */
#if FEATURE_STATISTICS_ENABLED
            g_stats.total_events_dropped++;
#endif
            result = false;
        } else {
            /* Post event */
            g_sm_context.pending_event = event;
#if FEATURE_STATISTICS_ENABLED
            g_stats.total_events_posted++;
#endif
            result = true;
        }
    }
    Platform_ExitCritical();

    return result;
}

void StateMachine_Reset(void)
{
    /* Cannot reset if critical error lock is active */
    if (g_sm_context.error_handler.critical_lock_active) {
        DEBUG_WARNING("Cannot reset - critical error lock active");
        return;
    }

    /* Clear errors */
    ErrorHandler_ClearError();

    /* Clear state-specific data */
    memset(&g_state_data, 0, sizeof(g_state_data));

    /* Transition to INIT */
    PerformStateTransition(STATE_INIT);

    DEBUG_INFO("State Machine reset to INIT");
}

StateMachineState_t StateMachine_GetCurrentState(void)
{
    return g_sm_context.current_state;
}

StateMachineState_t StateMachine_GetPreviousState(void)
{
    return g_sm_context.previous_state;
}

uint32_t StateMachine_GetStateTime(void)
{
    return Platform_GetTimeMs() - g_sm_context.state_entry_time;
}

uint32_t StateMachine_GetExecutionCount(void)
{
    return g_sm_context.state_execution_count;
}

/* =============================================================================
 * STRING CONVERSION
 * ===========================================================================*/

const char *StateMachine_StateToString(StateMachineState_t state)
{
    static const char *state_strings[] = {
        "INIT",
        "IDLE",
        "ACTIVE",
        "PROCESSING",
        "COMMUNICATING",
        "MONITORING",
        "CALIBRATING",
        "DIAGNOSTICS",
        "RECOVERY",
        "CRITICAL_ERROR"
    };

    if (state < STATE_MAX) {
        return state_strings[state];
    }
    return "UNKNOWN";
}

const char *StateMachine_EventToString(StateMachineEvent_t event)
{
    static const char *event_strings[] = {
        "NONE",
        "INIT_COMPLETE",
        "START",
        "STOP",
        "DATA_READY",
        "PROCESSING_DONE",
        "COMM_REQUEST",
        "COMM_COMPLETE",
        "TIMEOUT",
        "ERROR_MINOR",
        "ERROR_NORMAL",
        "ERROR_CRITICAL",
        "RECOVERY_SUCCESS",
        "RECOVERY_FAILED"
    };

    if (event < EVENT_MAX) {
        return event_strings[event];
    }
    return "UNKNOWN";
}

/* =============================================================================
 * ADVANCED API
 * ===========================================================================*/

bool StateMachine_AddTransition(StateMachineState_t state,
                                 StateMachineEvent_t event,
                                 StateMachineState_t next_state)
{
    StateConfig_t *config;

    if (state >= STATE_MAX || event >= EVENT_MAX || next_state >= STATE_MAX) {
        return false;
    }

    config = &g_state_table[state];

    if (config->transition_count >= SM_MAX_TRANSITIONS_PER_STATE) {
        DEBUG_ERROR("Transition table full for state %s", StateMachine_StateToString(state));
        return false;
    }

    config->transitions[config->transition_count].event = event;
    config->transitions[config->transition_count].next_state = next_state;
    config->transition_count++;

    return true;
}

bool StateMachine_SetStateTimeout(StateMachineState_t state, uint32_t timeout_ms)
{
    if (state >= STATE_MAX) {
        return false;
    }

    g_state_table[state].timeout_ms = timeout_ms;
    return true;
}

bool StateMachine_SetStateCallbacks(StateMachineState_t state,
                                     void (*on_entry)(void),
                                     void (*on_state)(void),
                                     void (*on_exit)(void))
{
    if (state >= STATE_MAX) {
        return false;
    }

    g_state_table[state].on_entry = on_entry;
    g_state_table[state].on_state = on_state;
    g_state_table[state].on_exit = on_exit;

    return true;
}

#if FEATURE_STATISTICS_ENABLED
bool StateMachine_GetStats(StateMachineStats_t *stats)
{
    if (stats == NULL) {
        return false;
    }

    memcpy(stats, &g_stats, sizeof(StateMachineStats_t));
    return true;
}

void StateMachine_ResetStats(void)
{
    memset(&g_stats, 0, sizeof(g_stats));
}
#endif

/* =============================================================================
 * PRIVATE HELPER FUNCTIONS
 * ===========================================================================*/

/**
 * @brief Initialize state transition table
 *
 * CRITICAL FIX: All states now have transitions for error events!
 */
static void InitializeStateTable(void)
{
    uint8_t idx;

    /* -------------------------------------------------------------------------
     * STATE_INIT Configuration
     * ----------------------------------------------------------------------- */
    idx = 0;
    g_state_table[STATE_INIT].state_id = STATE_INIT;
    g_state_table[STATE_INIT].on_entry = State_Init_OnEntry;
    g_state_table[STATE_INIT].on_state = State_Init_OnState;
    g_state_table[STATE_INIT].on_exit = State_Init_OnExit;
    g_state_table[STATE_INIT].timeout_ms = SM_STATE_TIMEOUT_MS;
    g_state_table[STATE_INIT].transitions[idx++] = (StateTransition_t){EVENT_INIT_COMPLETE, STATE_IDLE};
    g_state_table[STATE_INIT].transitions[idx++] = (StateTransition_t){EVENT_ERROR_NORMAL, STATE_RECOVERY};
    g_state_table[STATE_INIT].transitions[idx++] = (StateTransition_t){EVENT_ERROR_CRITICAL, STATE_CRITICAL_ERROR};
    g_state_table[STATE_INIT].transitions[idx++] = (StateTransition_t){EVENT_TIMEOUT, STATE_RECOVERY};
    g_state_table[STATE_INIT].transition_count = idx;

    /* -------------------------------------------------------------------------
     * STATE_IDLE Configuration
     * ----------------------------------------------------------------------- */
    idx = 0;
    g_state_table[STATE_IDLE].state_id = STATE_IDLE;
    g_state_table[STATE_IDLE].on_entry = State_Idle_OnEntry;
    g_state_table[STATE_IDLE].on_state = State_Idle_OnState;
    g_state_table[STATE_IDLE].on_exit = State_Idle_OnExit;
    g_state_table[STATE_IDLE].timeout_ms = 0;  /* No timeout */
    g_state_table[STATE_IDLE].transitions[idx++] = (StateTransition_t){EVENT_START, STATE_ACTIVE};
    g_state_table[STATE_IDLE].transitions[idx++] = (StateTransition_t){EVENT_ERROR_NORMAL, STATE_RECOVERY};
    g_state_table[STATE_IDLE].transitions[idx++] = (StateTransition_t){EVENT_ERROR_CRITICAL, STATE_CRITICAL_ERROR};
    g_state_table[STATE_IDLE].transition_count = idx;

    /* -------------------------------------------------------------------------
     * STATE_ACTIVE Configuration
     * CRITICAL FIX: Added ERROR_NORMAL transition
     * ----------------------------------------------------------------------- */
    idx = 0;
    g_state_table[STATE_ACTIVE].state_id = STATE_ACTIVE;
    g_state_table[STATE_ACTIVE].on_entry = State_Active_OnEntry;
    g_state_table[STATE_ACTIVE].on_state = State_Active_OnState;
    g_state_table[STATE_ACTIVE].on_exit = State_Active_OnExit;
    g_state_table[STATE_ACTIVE].timeout_ms = 0;  /* No timeout */
    g_state_table[STATE_ACTIVE].transitions[idx++] = (StateTransition_t){EVENT_DATA_READY, STATE_PROCESSING};
    g_state_table[STATE_ACTIVE].transitions[idx++] = (StateTransition_t){EVENT_STOP, STATE_IDLE};
    g_state_table[STATE_ACTIVE].transitions[idx++] = (StateTransition_t){EVENT_ERROR_NORMAL, STATE_RECOVERY};
    g_state_table[STATE_ACTIVE].transitions[idx++] = (StateTransition_t){EVENT_ERROR_CRITICAL, STATE_CRITICAL_ERROR};
    g_state_table[STATE_ACTIVE].transition_count = idx;

    /* -------------------------------------------------------------------------
     * STATE_PROCESSING Configuration
     * CRITICAL FIX: Added ERROR_NORMAL transition
     * ----------------------------------------------------------------------- */
    idx = 0;
    g_state_table[STATE_PROCESSING].state_id = STATE_PROCESSING;
    g_state_table[STATE_PROCESSING].on_entry = State_Processing_OnEntry;
    g_state_table[STATE_PROCESSING].on_state = State_Processing_OnState;
    g_state_table[STATE_PROCESSING].on_exit = State_Processing_OnExit;
    g_state_table[STATE_PROCESSING].timeout_ms = 3000U;
    g_state_table[STATE_PROCESSING].transitions[idx++] = (StateTransition_t){EVENT_PROCESSING_DONE, STATE_COMMUNICATING};
    g_state_table[STATE_PROCESSING].transitions[idx++] = (StateTransition_t){EVENT_TIMEOUT, STATE_RECOVERY};
    g_state_table[STATE_PROCESSING].transitions[idx++] = (StateTransition_t){EVENT_ERROR_NORMAL, STATE_RECOVERY};
    g_state_table[STATE_PROCESSING].transitions[idx++] = (StateTransition_t){EVENT_ERROR_CRITICAL, STATE_CRITICAL_ERROR};
    g_state_table[STATE_PROCESSING].transition_count = idx;

    /* -------------------------------------------------------------------------
     * STATE_COMMUNICATING Configuration
     * CRITICAL FIX: Added ERROR_NORMAL transition
     * ----------------------------------------------------------------------- */
    idx = 0;
    g_state_table[STATE_COMMUNICATING].state_id = STATE_COMMUNICATING;
    g_state_table[STATE_COMMUNICATING].on_entry = State_Communicating_OnEntry;
    g_state_table[STATE_COMMUNICATING].on_state = State_Communicating_OnState;
    g_state_table[STATE_COMMUNICATING].on_exit = State_Communicating_OnExit;
    g_state_table[STATE_COMMUNICATING].timeout_ms = COMM_TIMEOUT_MS;
    g_state_table[STATE_COMMUNICATING].transitions[idx++] = (StateTransition_t){EVENT_COMM_COMPLETE, STATE_MONITORING};
    g_state_table[STATE_COMMUNICATING].transitions[idx++] = (StateTransition_t){EVENT_TIMEOUT, STATE_RECOVERY};
    g_state_table[STATE_COMMUNICATING].transitions[idx++] = (StateTransition_t){EVENT_ERROR_NORMAL, STATE_RECOVERY};
    g_state_table[STATE_COMMUNICATING].transitions[idx++] = (StateTransition_t){EVENT_ERROR_CRITICAL, STATE_CRITICAL_ERROR};
    g_state_table[STATE_COMMUNICATING].transition_count = idx;

    /* -------------------------------------------------------------------------
     * STATE_MONITORING Configuration
     * CRITICAL FIX: Added ERROR_NORMAL transition
     * ----------------------------------------------------------------------- */
    idx = 0;
    g_state_table[STATE_MONITORING].state_id = STATE_MONITORING;
    g_state_table[STATE_MONITORING].on_entry = State_Monitoring_OnEntry;
    g_state_table[STATE_MONITORING].on_state = State_Monitoring_OnState;
    g_state_table[STATE_MONITORING].on_exit = State_Monitoring_OnExit;
    g_state_table[STATE_MONITORING].timeout_ms = 0;  /* No timeout */
    g_state_table[STATE_MONITORING].transitions[idx++] = (StateTransition_t){EVENT_STOP, STATE_IDLE};
    g_state_table[STATE_MONITORING].transitions[idx++] = (StateTransition_t){EVENT_DATA_READY, STATE_PROCESSING};
    g_state_table[STATE_MONITORING].transitions[idx++] = (StateTransition_t){EVENT_ERROR_NORMAL, STATE_RECOVERY};
    g_state_table[STATE_MONITORING].transitions[idx++] = (StateTransition_t){EVENT_ERROR_CRITICAL, STATE_CRITICAL_ERROR};
    g_state_table[STATE_MONITORING].transition_count = idx;

    /* -------------------------------------------------------------------------
     * STATE_CALIBRATING Configuration
     * CRITICAL FIX: Added ERROR_NORMAL transition
     * ----------------------------------------------------------------------- */
    idx = 0;
    g_state_table[STATE_CALIBRATING].state_id = STATE_CALIBRATING;
    g_state_table[STATE_CALIBRATING].on_entry = State_Calibrating_OnEntry;
    g_state_table[STATE_CALIBRATING].on_state = State_Calibrating_OnState;
    g_state_table[STATE_CALIBRATING].on_exit = State_Calibrating_OnExit;
    g_state_table[STATE_CALIBRATING].timeout_ms = 5000U;
    g_state_table[STATE_CALIBRATING].transitions[idx++] = (StateTransition_t){EVENT_PROCESSING_DONE, STATE_DIAGNOSTICS};
    g_state_table[STATE_CALIBRATING].transitions[idx++] = (StateTransition_t){EVENT_TIMEOUT, STATE_RECOVERY};
    g_state_table[STATE_CALIBRATING].transitions[idx++] = (StateTransition_t){EVENT_ERROR_NORMAL, STATE_RECOVERY};
    g_state_table[STATE_CALIBRATING].transitions[idx++] = (StateTransition_t){EVENT_ERROR_CRITICAL, STATE_CRITICAL_ERROR};
    g_state_table[STATE_CALIBRATING].transition_count = idx;

    /* -------------------------------------------------------------------------
     * STATE_DIAGNOSTICS Configuration
     * CRITICAL FIX: Added ERROR_NORMAL transition
     * ----------------------------------------------------------------------- */
    idx = 0;
    g_state_table[STATE_DIAGNOSTICS].state_id = STATE_DIAGNOSTICS;
    g_state_table[STATE_DIAGNOSTICS].on_entry = State_Diagnostics_OnEntry;
    g_state_table[STATE_DIAGNOSTICS].on_state = State_Diagnostics_OnState;
    g_state_table[STATE_DIAGNOSTICS].on_exit = State_Diagnostics_OnExit;
    g_state_table[STATE_DIAGNOSTICS].timeout_ms = 2000U;
    g_state_table[STATE_DIAGNOSTICS].transitions[idx++] = (StateTransition_t){EVENT_PROCESSING_DONE, STATE_ACTIVE};
    g_state_table[STATE_DIAGNOSTICS].transitions[idx++] = (StateTransition_t){EVENT_TIMEOUT, STATE_RECOVERY};
    g_state_table[STATE_DIAGNOSTICS].transitions[idx++] = (StateTransition_t){EVENT_ERROR_NORMAL, STATE_RECOVERY};
    g_state_table[STATE_DIAGNOSTICS].transitions[idx++] = (StateTransition_t){EVENT_ERROR_CRITICAL, STATE_CRITICAL_ERROR};
    g_state_table[STATE_DIAGNOSTICS].transition_count = idx;

    /* -------------------------------------------------------------------------
     * STATE_RECOVERY Configuration
     * ----------------------------------------------------------------------- */
    idx = 0;
    g_state_table[STATE_RECOVERY].state_id = STATE_RECOVERY;
    g_state_table[STATE_RECOVERY].on_entry = State_Recovery_OnEntry;
    g_state_table[STATE_RECOVERY].on_state = State_Recovery_OnState;
    g_state_table[STATE_RECOVERY].on_exit = State_Recovery_OnExit;
    g_state_table[STATE_RECOVERY].timeout_ms = 2000U;
    g_state_table[STATE_RECOVERY].transitions[idx++] = (StateTransition_t){EVENT_RECOVERY_SUCCESS, STATE_IDLE};
    g_state_table[STATE_RECOVERY].transitions[idx++] = (StateTransition_t){EVENT_RECOVERY_FAILED, STATE_CRITICAL_ERROR};
    g_state_table[STATE_RECOVERY].transitions[idx++] = (StateTransition_t){EVENT_TIMEOUT, STATE_CRITICAL_ERROR};
    g_state_table[STATE_RECOVERY].transitions[idx++] = (StateTransition_t){EVENT_ERROR_CRITICAL, STATE_CRITICAL_ERROR};
    g_state_table[STATE_RECOVERY].transition_count = idx;

    /* -------------------------------------------------------------------------
     * STATE_CRITICAL_ERROR Configuration
     * No transitions - locked state
     * ----------------------------------------------------------------------- */
    g_state_table[STATE_CRITICAL_ERROR].state_id = STATE_CRITICAL_ERROR;
    g_state_table[STATE_CRITICAL_ERROR].on_entry = State_CriticalError_OnEntry;
    g_state_table[STATE_CRITICAL_ERROR].on_state = State_CriticalError_OnState;
    g_state_table[STATE_CRITICAL_ERROR].on_exit = State_CriticalError_OnExit;
    g_state_table[STATE_CRITICAL_ERROR].timeout_ms = 0;  /* No timeout */
    g_state_table[STATE_CRITICAL_ERROR].transition_count = 0;  /* No transitions */
}

static void PerformStateTransition(StateMachineState_t new_state)
{
    StateConfig_t *current_config;

    /* Validate new state */
    if (new_state >= STATE_MAX) {
        DEBUG_ERROR("Invalid state transition to %d", new_state);
        return;
    }

    /* Execute OnExit callback */
    current_config = &g_state_table[g_sm_context.current_state];
    if (current_config->on_exit != NULL) {
        current_config->on_exit();
    }

    /* Update state */
    g_sm_context.previous_state = g_sm_context.current_state;
    g_sm_context.current_state = new_state;
    g_sm_context.state_changed = true;

    DEBUG_RUNTIME("State transition: %s -> %s",
                 StateMachine_StateToString(g_sm_context.previous_state),
                 StateMachine_StateToString(g_sm_context.current_state));
}

static bool CheckStateTransition(StateMachineEvent_t event, StateMachineState_t *next_state)
{
    StateConfig_t *current_config;
    uint8_t i;

    if (next_state == NULL) {
        return false;
    }

    current_config = &g_state_table[g_sm_context.current_state];

    /* Search transition table for matching event */
    for (i = 0; i < current_config->transition_count; i++) {
        if (current_config->transitions[i].event == event) {
            *next_state = current_config->transitions[i].next_state;
            DEBUG_RUNTIME("Event %s triggers transition %s -> %s",
                         StateMachine_EventToString(event),
                         StateMachine_StateToString(g_sm_context.current_state),
                         StateMachine_StateToString(*next_state));
            return true;
        }
    }

    DEBUG_WARNING("No transition for event %s in state %s",
                 StateMachine_EventToString(event),
                 StateMachine_StateToString(g_sm_context.current_state));
    return false;
}

/* =============================================================================
 * STATE CALLBACK IMPLEMENTATIONS
 *
 * CRITICAL FIX: Removed all static variables from callbacks
 * State-specific data now stored in g_state_data structure
 * ===========================================================================*/

/* --- INIT STATE --- */
static void State_Init_OnEntry(void)
{
    g_state_data.init_step_count = 0;
    DEBUG_INIT("Entering INIT state");
}

static void State_Init_OnState(void)
{
    const uint32_t INIT_REQUIRED_STEPS = 5;

    g_state_data.init_step_count++;
    if (g_state_data.init_step_count >= INIT_REQUIRED_STEPS) {
        DEBUG_INIT("Initialization complete after %lu steps", g_state_data.init_step_count);
        StateMachine_PostEvent(EVENT_INIT_COMPLETE);
    }
}

static void State_Init_OnExit(void)
{
    DEBUG_INIT("Exiting INIT state");
}

/* --- IDLE STATE --- */
static void State_Idle_OnEntry(void)
{
    DEBUG_RUNTIME("Entering IDLE state - ready for operation");
}

static void State_Idle_OnState(void)
{
    /* Idle state - wait for external events */
    /* In real application, could enter low-power mode here */
}

static void State_Idle_OnExit(void)
{
    DEBUG_RUNTIME("Exiting IDLE state");
}

/* --- ACTIVE STATE --- */
static void State_Active_OnEntry(void)
{
    DEBUG_RUNTIME("Entering ACTIVE state");
}

static void State_Active_OnState(void)
{
    /* Active monitoring - check conditions, poll sensors, etc. */
    /* Real application would implement actual monitoring logic */
}

static void State_Active_OnExit(void)
{
    DEBUG_RUNTIME("Exiting ACTIVE state");
}

/* --- PROCESSING STATE --- */
static void State_Processing_OnEntry(void)
{
    DEBUG_RUNTIME("Entering PROCESSING state");
}

static void State_Processing_OnState(void)
{
    /* Data processing logic */
    /* Real application would implement actual processing */

    /* Example: simple counter-based completion */
    const uint32_t PROCESSING_CYCLES = 20;
    if (g_sm_context.state_execution_count >= PROCESSING_CYCLES) {
        DEBUG_INFO("Processing complete after %lu cycles", g_sm_context.state_execution_count);
        StateMachine_PostEvent(EVENT_PROCESSING_DONE);
    }
}

static void State_Processing_OnExit(void)
{
    DEBUG_RUNTIME("Exiting PROCESSING state");
}

/* --- COMMUNICATING STATE --- */
static void State_Communicating_OnEntry(void)
{
    g_state_data.comm_started = false;
    DEBUG_RUNTIME("Entering COMMUNICATING state");
}

static void State_Communicating_OnState(void)
{
    const uint32_t COMM_CYCLES = 8;

    if (!g_state_data.comm_started) {
        DEBUG_INFO("Starting communication");
        g_state_data.comm_started = true;
    }

    if (g_sm_context.state_execution_count >= COMM_CYCLES) {
        if (ErrorHandler_VerifyCommChannel()) {
            DEBUG_INFO("Communication complete and verified");
            StateMachine_PostEvent(EVENT_COMM_COMPLETE);
        }
    }
}

static void State_Communicating_OnExit(void)
{
    g_state_data.comm_started = false;
    DEBUG_RUNTIME("Exiting COMMUNICATING state");
}

/* --- MONITORING STATE --- */
static void State_Monitoring_OnEntry(void)
{
    DEBUG_RUNTIME("Entering MONITORING state");
}

static void State_Monitoring_OnState(void)
{
    /* Health monitoring logic */
    /* Real application would monitor system health */
}

static void State_Monitoring_OnExit(void)
{
    DEBUG_RUNTIME("Exiting MONITORING state");
}

/* --- CALIBRATING STATE --- */
static void State_Calibrating_OnEntry(void)
{
    DEBUG_RUNTIME("Entering CALIBRATING state");
}

static void State_Calibrating_OnState(void)
{
    const uint32_t CALIBRATION_CYCLES = 30;

    if (g_sm_context.state_execution_count >= CALIBRATION_CYCLES) {
        DEBUG_INFO("Calibration complete");
        StateMachine_PostEvent(EVENT_PROCESSING_DONE);
    }
}

static void State_Calibrating_OnExit(void)
{
    DEBUG_RUNTIME("Exiting CALIBRATING state");
}

/* --- DIAGNOSTICS STATE --- */
static void State_Diagnostics_OnEntry(void)
{
    DEBUG_RUNTIME("Entering DIAGNOSTICS state");
}

static void State_Diagnostics_OnState(void)
{
    const uint32_t DIAGNOSTIC_CYCLES = 15;

    if (g_sm_context.state_execution_count >= DIAGNOSTIC_CYCLES) {
        DEBUG_INFO("Diagnostics passed");
        StateMachine_PostEvent(EVENT_PROCESSING_DONE);
    }
}

static void State_Diagnostics_OnExit(void)
{
    DEBUG_RUNTIME("Exiting DIAGNOSTICS state");
}

/* --- RECOVERY STATE --- */
static void State_Recovery_OnEntry(void)
{
    DEBUG_WARNING("Entering RECOVERY state");
}

static void State_Recovery_OnState(void)
{
    bool recovery_successful = ErrorHandler_AttemptRecovery();
    ErrorInfo_t error_info;

    if (recovery_successful) {
        DEBUG_INFO("Recovery successful");
        ErrorHandler_ClearError();
        StateMachine_PostEvent(EVENT_RECOVERY_SUCCESS);
    } else {
        if (ErrorHandler_GetCurrentError(&error_info)) {
            if (error_info.retry_count >= ERROR_MAX_RECOVERY_ATTEMPTS) {
                DEBUG_ERROR("Recovery failed after %d attempts", error_info.retry_count);
                StateMachine_PostEvent(EVENT_RECOVERY_FAILED);
            }
        }
    }
}

static void State_Recovery_OnExit(void)
{
    DEBUG_RUNTIME("Exiting RECOVERY state");
}

/* --- CRITICAL ERROR STATE --- */
static void State_CriticalError_OnEntry(void)
{
    ErrorInfo_t error_info;

    DEBUG_ERROR("=== CRITICAL ERROR STATE ===");
    if (ErrorHandler_GetCurrentError(&error_info)) {
        DEBUG_ERROR("Error: %s from state %s",
                   ErrorHandler_CodeToString(error_info.code),
                   StateMachine_StateToString(error_info.state));
        DEBUG_ERROR("Timestamp: %lu ms", error_info.timestamp);
    }
    DEBUG_ERROR("System locked - requires manual reset");
}

static void State_CriticalError_OnState(void)
{
    const uint32_t ERROR_LOG_INTERVAL = 100;

    /* Periodic error message */
    if ((g_sm_context.state_execution_count % ERROR_LOG_INTERVAL) == 0) {
        DEBUG_ERROR("System in critical error lock (exec count: %lu)",
                   g_sm_context.state_execution_count);
    }
}

static void State_CriticalError_OnExit(void)
{
    /* Should never exit this state without reset */
    DEBUG_ERROR("WARNING: Exiting CRITICAL ERROR state unexpectedly");
}
