/**
 * @file sm_config.h
 * @brief Configuration defaults for State Machine Framework v4.1
 * @version 4.1.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Compile-time configuration with #ifndef override pattern.
 * Users define values in their app_config.h BEFORE including sm_framework.h.
 *
 * REQUIRED: Application MUST define SM_STATE_COUNT and SM_EVENT_COUNT.
 *
 * USAGE:
 *   1. Copy config/sm_config_template.h to your project as app_config.h
 *   2. Define SM_STATE_COUNT and SM_EVENT_COUNT (mandatory)
 *   3. Override any other defaults as needed
 *   4. #include "app_config.h" BEFORE #include "sm_framework/sm_framework.h"
 */

#ifndef SM_CONFIG_H
#define SM_CONFIG_H

/* =============================================================================
 * MANDATORY: APPLICATION MUST DEFINE THESE
 * ===========================================================================*/

/**
 * @brief Number of states in the application FSM
 *
 * User MUST define this in app_config.h. The framework does not define any
 * application states -- the user provides their own enum.
 */
#ifndef SM_STATE_COUNT
#error "SM_STATE_COUNT must be defined by the application in app_config.h"
#endif

/**
 * @brief Number of events in the application FSM
 *
 * User MUST define this in app_config.h. The framework does not define any
 * application events -- the user provides their own enum.
 */
#ifndef SM_EVENT_COUNT
#error "SM_EVENT_COUNT must be defined by the application in app_config.h"
#endif

/* =============================================================================
 * EVENT QUEUE CONFIGURATION
 * ===========================================================================*/

/**
 * @brief Size of the ISR-safe event ring buffer
 *
 * Must be > 0 and <= 64. Larger values use more RAM but reduce event drops.
 * Each slot is 8 bytes (SM_EventItem_t).
 */
#ifndef SM_EVENT_QUEUE_SIZE
#define SM_EVENT_QUEUE_SIZE          (8U)
#endif

/**
 * @brief Maximum events processed per SM_Process() call (v4.0)
 *
 * SM_Process drains up to this many queued events per call, each with full
 * run-to-completion semantics (a transition mid-drain means later events are
 * evaluated against the new state). Bounds worst-case SM_Process execution
 * time: WCET ~= SM_MAX_EVENTS_PER_PROCESS * (slowest transition path).
 *
 * Default = SM_EVENT_QUEUE_SIZE so a full backlog normally clears in one
 * call. Set to 1 to restore the v3.0 one-event-per-call cadence.
 */
#ifndef SM_MAX_EVENTS_PER_PROCESS
#define SM_MAX_EVENTS_PER_PROCESS    (SM_EVENT_QUEUE_SIZE)
#endif

/* =============================================================================
 * TRANSITION TABLE CONFIGURATION
 * ===========================================================================*/

/**
 * @brief Maximum number of runtime transitions (when SM_FEATURE_RUNTIME_TRANSITIONS=1)
 *
 * Only used if SM_FEATURE_RUNTIME_TRANSITIONS is enabled.
 */
#ifndef SM_MAX_TRANSITIONS
#define SM_MAX_TRANSITIONS           (32U)
#endif

/* =============================================================================
 * ERROR HANDLING CONFIGURATION
 * ===========================================================================*/

/**
 * @brief Size of circular error history buffer
 *
 * Must be > 0 and <= 255. Each entry is ~16 bytes (SM_ErrorInfo_t).
 */
#ifndef SM_ERROR_HISTORY_SIZE
#define SM_ERROR_HISTORY_SIZE        (8U)
#endif

/**
 * @brief Maximum recovery attempts before escalation to critical
 */
#ifndef SM_ERROR_MAX_RECOVERY
#define SM_ERROR_MAX_RECOVERY        (3U)
#endif

/* =============================================================================
 * STATE HISTORY CONFIGURATION
 * ===========================================================================*/

/**
 * @brief Depth of state history ring buffer
 *
 * Stores the last N state transitions for debugging.
 */
#ifndef SM_STATE_HISTORY_DEPTH
#define SM_STATE_HISTORY_DEPTH       (4U)
#endif

/* =============================================================================
 * DEBUG CONFIGURATION
 * ===========================================================================*/

/**
 * @brief Debug verbosity level (compile-time)
 *
 * 0 = off (all debug compiled out)
 * 1 = error only
 * 2 = error + warn
 * 3 = error + warn + info
 * 4 = all (error + warn + info + verbose)
 */
#ifndef SM_DEBUG_LEVEL
#define SM_DEBUG_LEVEL               (4U)
#endif

/**
 * @brief Debug output buffer size (bytes)
 *
 * Must be >= SM_DEBUG_MSG_MAX_LEN.
 */
#ifndef SM_DEBUG_BUFFER_SIZE
#define SM_DEBUG_BUFFER_SIZE         (256U)
#endif

/**
 * @brief Maximum single debug message length (bytes)
 */
#ifndef SM_DEBUG_MSG_MAX_LEN
#define SM_DEBUG_MSG_MAX_LEN         (128U)
#endif

/* =============================================================================
 * FEATURE FLAGS
 * ===========================================================================*/

/**
 * @brief Enable hierarchical state machine (HSM) support
 *
 * When 0 (default): flat FSM, no parent field in state descriptors.
 * When 1: each SM_StateDesc_t has a parent field for nested states.
 */
#ifndef SM_FEATURE_HSM
#define SM_FEATURE_HSM               (0U)
#endif

/**
 * @brief Enable runtime transition table modification
 *
 * When 0 (default): only const flash transition table is used.
 * When 1: SM_AddTransition() API is available for runtime additions.
 */
#ifndef SM_FEATURE_RUNTIME_TRANSITIONS
#define SM_FEATURE_RUNTIME_TRANSITIONS (0U)
#endif

/**
 * @brief Enable statistics collection
 *
 * When 0 (default): no statistics overhead.
 * When 1: transition counts, event counts, timeout counts tracked.
 */
#ifndef SM_FEATURE_STATISTICS
#define SM_FEATURE_STATISTICS        (0U)
#endif

/**
 * @brief Enable debug output subsystem
 *
 * When 0: all SM_Debug_* functions become no-ops, zero code/RAM overhead.
 * When 1 (default): debug output enabled at level set by SM_DEBUG_LEVEL.
 */
#ifndef SM_FEATURE_DEBUG
#define SM_FEATURE_DEBUG             (1U)
#endif

/**
 * @brief Enable runtime assertions
 *
 * When 0: SM_ASSERT() compiles to ((void)0).
 * When 1 (default): SM_ASSERT() calls SM_Platform_Assert() on failure.
 */
#ifndef SM_FEATURE_ASSERT
#define SM_FEATURE_ASSERT            (1U)
#endif

/**
 * @brief Enable time event subsystem
 *
 * When 0: time event code compiles out, no RAM overhead.
 * When 1 (default): SM_TimeEvt_* APIs available, linked-list per instance.
 */
#ifndef SM_FEATURE_TIME_EVENTS
#define SM_FEATURE_TIME_EVENTS       (1U)
#endif

/**
 * @brief Maximum number of concurrently scheduled time events per instance
 *
 * v4.0: enforced at SM_TimeEvt_Arm time -- arming beyond this returns false
 * (v3.0 silently accepted timers that then never fired). Also the hard bound
 * when ticking the list in SM_Process(), guarding a corrupted list.
 *
 * Stack note: SM_TimeEvt_Tick_ collects fires into a stack array of
 * 8 * SM_FEATURE_MAX_TIME_EVENTS bytes (128 B at the default 16) so events
 * are posted OUTSIDE the list-walk critical section.
 */
#ifndef SM_FEATURE_MAX_TIME_EVENTS
#define SM_FEATURE_MAX_TIME_EVENTS   (16U)
#endif

/**
 * @brief Enable deferred event subsystem
 *
 * When 0 (default): deferred event queue not allocated, APIs compile out.
 * When 1: SM_DeferEvent / SM_RecallEvent / SM_FlushDeferred available.
 */
#ifndef SM_FEATURE_DEFER
#define SM_FEATURE_DEFER             (0U)
#endif

/**
 * @brief Size of the deferred event ring buffer
 *
 * Only used when SM_FEATURE_DEFER == 1. Each slot is 8 bytes (SM_EventItem_t).
 */
#ifndef SM_DEFER_QUEUE_SIZE
#define SM_DEFER_QUEUE_SIZE          (4U)
#endif

/**
 * @brief Maximum HSM nesting depth
 *
 * Only used when SM_FEATURE_HSM == 1. Bounds parent-chain traversal when
 * resolving transitions (parent fallback); full LCA entry/exit chains are not
 * implemented — see `sm_find_transition_hsm` in sm_engine.c.
 */
#ifndef SM_HSM_MAX_DEPTH
#define SM_HSM_MAX_DEPTH             (6U)
#endif

/* =============================================================================
 * TASK PERIOD
 * ===========================================================================*/

/**
 * @brief State machine task execution period in milliseconds
 *
 * How often SM_Process() should be called.
 * - Fast systems: 1-10ms
 * - Normal systems: 10-50ms
 * - Low power systems: 100-1000ms
 */
#ifndef SM_TASK_PERIOD_MS
#define SM_TASK_PERIOD_MS            (10U)
#endif

#endif /* SM_CONFIG_H */
