/**
 * @file sm_types.h
 * @brief All type definitions for State Machine Framework v4.1
 * @version 4.1.0
 * @date 2026-04-18
 *
 * @copyright Copyright (c) 2025-2026
 *
 * Contains all structs, enums, typedefs, and callback signatures used by the
 * framework. The framework is state-agnostic -- application states and events
 * are user-defined enums, not defined here.
 *
 * The SM_Context struct is defined here (visible for static allocation) but
 * users must access it ONLY through the SM_* API (handle-based convention).
 */

#ifndef SM_TYPES_H
#define SM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Include configuration (must come first -- types depend on config values) */
#include "sm_config.h"

/* =============================================================================
 * COMPILE-TIME VALIDATION
 *
 * _Static_assert is C11. For C99 compatibility, fall back to a negative-size
 * array trick that produces a compile error on failure.
 * ===========================================================================*/

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    /* C11 or later -- use _Static_assert directly */
    #define SM_STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)
#else
    /* C99 fallback: negative-size array trick */
    #define SM_STATIC_ASSERT_JOIN2(a, b) a ## b
    #define SM_STATIC_ASSERT_JOIN(a, b) SM_STATIC_ASSERT_JOIN2(a, b)
    #define SM_STATIC_ASSERT(expr, msg) \
        typedef char SM_STATIC_ASSERT_JOIN(sm_static_assert_, __LINE__) [(expr) ? 1 : -1]
#endif

SM_STATIC_ASSERT(SM_EVENT_QUEUE_SIZE > 0 && SM_EVENT_QUEUE_SIZE <= 64,
    "SM_EVENT_QUEUE_SIZE must be between 1 and 64");

SM_STATIC_ASSERT(SM_STATE_COUNT > 0 && SM_STATE_COUNT <= 255,
    "SM_STATE_COUNT must be between 1 and 255");

SM_STATIC_ASSERT(SM_EVENT_COUNT > 0 && SM_EVENT_COUNT <= 65535,
    "SM_EVENT_COUNT must be between 1 and 65535 (0xFFFF is reserved for SM_EVT_TIMEOUT)");

SM_STATIC_ASSERT(SM_MAX_EVENTS_PER_PROCESS > 0,
    "SM_MAX_EVENTS_PER_PROCESS must be >= 1");

SM_STATIC_ASSERT(SM_DEBUG_BUFFER_SIZE >= SM_DEBUG_MSG_MAX_LEN,
    "SM_DEBUG_BUFFER_SIZE must be >= SM_DEBUG_MSG_MAX_LEN");

SM_STATIC_ASSERT(SM_ERROR_HISTORY_SIZE > 0 && SM_ERROR_HISTORY_SIZE <= 255,
    "SM_ERROR_HISTORY_SIZE must be between 1 and 255");

SM_STATIC_ASSERT(SM_MAX_TRANSITIONS > 0,
    "SM_MAX_TRANSITIONS must be > 0");

SM_STATIC_ASSERT(SM_STATE_HISTORY_DEPTH > 0 && SM_STATE_HISTORY_DEPTH <= 255,
    "SM_STATE_HISTORY_DEPTH must be 1..255 (engine uses uint8_t history indices)");

#if SM_FEATURE_DEFER
SM_STATIC_ASSERT(SM_DEFER_QUEUE_SIZE > 0 && SM_DEFER_QUEUE_SIZE <= 32,
    "SM_DEFER_QUEUE_SIZE must be between 1 and 32");
#endif

#if SM_FEATURE_TIME_EVENTS
SM_STATIC_ASSERT(SM_FEATURE_MAX_TIME_EVENTS > 0 && SM_FEATURE_MAX_TIME_EVENTS <= 64,
    "SM_FEATURE_MAX_TIME_EVENTS must be between 1 and 64");
#endif

/* =============================================================================
 * FORWARD DECLARATION / OPAQUE HANDLE
 * ===========================================================================*/

/**
 * @brief State machine context (forward declaration)
 *
 * Defined fully at the bottom of this file. Users statically allocate this
 * struct but must only access it via SM_* API functions.
 */
typedef struct SM_Context SM_Context_t;

/**
 * @brief Opaque handle to a state machine instance
 *
 * Multiple instances supported -- user allocates SM_Context_t statically
 * and passes &ctx as the handle.
 */
typedef SM_Context_t *SM_Handle_t;

/* =============================================================================
 * EVENT TYPES
 * ===========================================================================*/

/**
 * @brief State-timeout event (framework-defined, public since v4.0)
 *
 * Posted by the engine when a state's timeout_ms elapses. Use it directly in
 * transition tables:
 *
 *   { STATE_WAITING, SM_EVT_TIMEOUT, STATE_FAULT, 0, NULL, NULL }
 *
 * SM_PostEvent rejects it (engine-only signal); SM_AddTransition accepts it.
 *
 * v4.1: this is a FIXED reserved identifier (0xFFFF), not a function of
 * SM_EVENT_COUNT, so it has the same value in every translation unit.
 * v4.0 defined it as SM_EVENT_COUNT, which silently disagreed whenever the
 * framework was compiled with different counts than the application (the
 * usual case with a pre-compiled library): the engine posted ITS value while
 * the application's table matched on ITS OWN, so every SM_EVT_TIMEOUT route
 * was dead code. User events therefore occupy 0..SM_EVENT_COUNT-1 and can
 * never collide with the reserved id.
 */
#define SM_EVT_TIMEOUT  ((uint16_t)0xFFFFU)

/**
 * @brief Event item stored in the ring buffer
 *
 * 8 bytes on 32-bit ARM (with explicit padding).
 * event is uint16_t to support up to 65535 user-defined events (0xFFFF reserved).
 * data is a uint32_t payload carried with every event.
 */
typedef struct {
    uint16_t event;       /**< User-defined event ID */
    uint16_t _reserved;   /**< Alignment padding (explicit) */
    uint32_t data;        /**< Event payload */
} SM_EventItem_t;

/**
 * @brief ISR-safe event ring buffer with QP/C frontEvt fast path
 *
 * head = next write position, tail = next read position.
 * count avoids head==tail ambiguity between empty and full.
 *
 * frontEvt fast path (D6, revised v4.0): when the queue is COMPLETELY empty,
 * a post places the event directly into the front slot instead of the ring
 * buffer, avoiding a ring round-trip for the common single-event case.
 * SM_Process dequeues the front slot first, then the ring FIFO. Because the
 * front slot is only ever claimed when nothing is pending, it always holds
 * the oldest event and delivery is strict FIFO in post order for ALL
 * sources (user, ISR, timeout, time events). The only deliberate exception
 * is SM_RecallEvent, which inserts at the true front by design.
 */
typedef struct {
    SM_EventItem_t items[SM_EVENT_QUEUE_SIZE]; /**< Ring buffer storage */
    SM_EventItem_t front;    /**< Front-event slot (bypass ring when empty) */
    volatile uint8_t head;   /**< Next write index */
    volatile uint8_t tail;   /**< Next read index */
    volatile uint8_t count;  /**< Number of items in queue (ring only) */
    volatile bool front_valid; /**< True if front slot holds a pending event */
    uint8_t nMin;            /**< Watermark: minimum free slots ever seen */
} SM_EventQueue_t;

/* =============================================================================
 * CALLBACK SIGNATURES
 * ===========================================================================*/

/**
 * @brief State callback (entry, execute, exit)
 *
 * Receives handle so callbacks can query state, post events, etc.
 *
 * @param sm Handle to the state machine instance
 */
typedef void (*SM_StateCallback_t)(SM_Handle_t sm);

/**
 * @brief Guard condition for a transition
 *
 * Returns true to allow the transition, false to block it.
 *
 * @param sm   Handle to the state machine instance
 * @param event  Event ID that triggered the transition check
 * @param data   Event payload
 * @return true if transition should proceed, false to block
 */
typedef bool (*SM_Guard_t)(SM_Handle_t sm, uint16_t event, uint32_t data);

/**
 * @brief Action executed during a transition (between exit and entry)
 *
 * @param sm   Handle to the state machine instance
 * @param event  Event ID that triggered the transition
 * @param data   Event payload
 */
typedef void (*SM_Action_t)(SM_Handle_t sm, uint16_t event, uint32_t data);

/* =============================================================================
 * TIME EVENTS (compile-time optional)
 * ===========================================================================*/

#if SM_FEATURE_TIME_EVENTS

/**
 * @brief Time event (intrusive linked-list node)
 *
 * One-shot or periodic timer that posts an event to the owning state machine
 * when its millisecond deadline passes. Managed via SM_TimeEvt_Arm /
 * SM_TimeEvt_Disarm. The linked list is walked by SM_TimeEvt_Tick_() inside
 * SM_Process().
 *
 * Users allocate SM_TimeEvt_t statically and pass them to the API.
 *
 * v4.0: deadlines are absolute SM_Platform_GetTimeMs() values (wrap-safe
 * modular comparison; delay/interval must be < 2^31 ms ~= 24.8 days).
 * Firing is checked once per SM_Process(), so the effective resolution is
 * the SM_Process call period -- but a late check fires immediately instead
 * of stretching with the call cadence (the v3.0 tick-counted behavior).
 * Periodic timers advance deadline by whole intervals: drift-free and
 * phase-preserving; periods missed during a stall coalesce into ONE event.
 */
typedef struct SM_TimeEvt {
    struct SM_TimeEvt *next;   /**< Next node in the per-instance linked list */
    SM_Handle_t sm;            /**< Owning state machine handle */
    uint16_t sig;              /**< Event ID to post on expiry */
    uint16_t _pad;             /**< Alignment padding */
    uint32_t data;             /**< Event payload to post */
    uint32_t deadline;         /**< Absolute fire time (SM_Platform_GetTimeMs) */
    uint32_t interval;         /**< Reload period in ms (0 = one-shot) */
    bool armed;                /**< True while scheduled to fire */
    uint8_t _pad2[3];          /**< Alignment padding (explicit) */
} SM_TimeEvt_t;

#endif /* SM_FEATURE_TIME_EVENTS */

/* =============================================================================
 * TRANSITION TABLE
 * ===========================================================================*/

/**
 * @brief Transition definition (const, lives in flash)
 *
 * Defines: when in from_state and event occurs, if guard allows,
 * execute action and move to to_state.
 */
typedef struct {
    uint16_t from_state;   /**< Source state index */
    uint16_t event;        /**< Event that triggers this transition */
    uint16_t to_state;     /**< Destination state index */
    uint16_t _reserved;    /**< Alignment padding */
    SM_Guard_t guard;      /**< Guard condition (NULL = always allow) */
    SM_Action_t action;    /**< Transition action (NULL = no action) */
} SM_Transition_t;

/* =============================================================================
 * STATE DESCRIPTOR
 * ===========================================================================*/

/**
 * @brief State descriptor (const, lives in flash)
 *
 * Defines callbacks and timing constraints for a single state.
 * Array of these is indexed by state ID.
 */
typedef struct {
    SM_StateCallback_t on_entry;   /**< Called once on state entry */
    SM_StateCallback_t on_execute; /**< Called every SM_Process() cycle while in state */
    SM_StateCallback_t on_exit;    /**< Called once on state exit */
    uint32_t timeout_ms;           /**< Auto-timeout: posts SM_EVT_TIMEOUT once per
                                        entry when elapsed >= timeout_ms (0 = none) */
    uint32_t min_dwell_ms;         /**< Minimum time before ANY queued event is
                                        processed in this state (0 = none).
                                        Interaction: if min_dwell_ms > timeout_ms the
                                        SM_EVT_TIMEOUT event still POSTS at
                                        timeout_ms but is only PROCESSED once the
                                        dwell has elapsed -- the effective timeout
                                        transition time is max(timeout_ms,
                                        min_dwell_ms). */
#if SM_FEATURE_HSM
    uint16_t parent;               /**< Parent state index (UINT16_MAX = no parent) */
#endif
} SM_StateDesc_t;

/* =============================================================================
 * ERROR HANDLING TYPES
 * ===========================================================================*/

/**
 * @brief Error severity levels (framework-defined)
 *
 * MINOR:    Auto-recovery, no state change
 * NORMAL:   Managed recovery, application handles
 * CRITICAL: System lock, requires reset
 */
typedef enum {
    SM_ERROR_NONE = 0,        /**< No error */
    SM_ERROR_MINOR,           /**< Minor -- auto-recovery */
    SM_ERROR_NORMAL,          /**< Normal -- managed recovery */
    SM_ERROR_CRITICAL,        /**< Critical -- system lock */
    SM_ERROR_LEVEL_COUNT      /**< Number of error levels (sentinel) */
} SM_ErrorLevel_t;

/**
 * @brief Error information record
 *
 * Stored in history ring buffer and as current error.
 */
typedef struct {
    SM_ErrorLevel_t level;    /**< Error severity */
    uint16_t code;            /**< User-defined error code */
    uint16_t state;           /**< State index when error occurred */
    uint32_t timestamp;       /**< Time of error (ms) */
    uint8_t retry_count;      /**< Recovery attempts so far */
    bool recovered;           /**< True if recovery succeeded */
} SM_ErrorInfo_t;

/**
 * @brief Error statistics counters
 */
typedef struct {
    uint32_t errors_by_level[SM_ERROR_LEVEL_COUNT]; /**< Total errors per severity level */
    uint32_t recovery_success;                      /**< Successful recovery attempts */
    uint32_t recovery_fail;                         /**< Failed recovery attempts */
    uint32_t last_error_time;                       /**< Timestamp of most recent error (ms) */
} SM_ErrorStats_t;

/**
 * @brief Error handler context (embedded in SM_Context)
 */
typedef struct {
    SM_ErrorInfo_t current;                          /**< Current active error */
    SM_ErrorInfo_t history[SM_ERROR_HISTORY_SIZE];    /**< Error history ring */
    SM_ErrorStats_t stats;                           /**< Error statistics */
    uint8_t history_index;                           /**< Next write position in history */
    uint8_t history_count;                           /**< Actual entries in history (not always max) */
    bool minor_active;                               /**< Minor error in progress */
    uint32_t minor_timestamp;                        /**< When minor error started */
    volatile bool critical_lock;                     /**< Critical error lock (volatile for ISR access) */
    volatile uint8_t critical_lock_dis;              /**< DIS shadow of critical_lock (D7) */
} SM_ErrorHandler_t;

/**
 * @brief Error recovery callback
 *
 * User-provided function to attempt recovery from a specific error.
 *
 * @param sm         Handle to the state machine instance
 * @param error_code User-defined error code
 * @return true if recovery succeeded, false if failed
 */
typedef bool (*SM_RecoveryCallback_t)(SM_Handle_t sm, uint16_t error_code);

/**
 * @brief Error notification callback
 *
 * Called when any error is reported, regardless of level.
 *
 * @param sm    Handle to the state machine instance
 * @param level Error severity
 * @param code  User-defined error code
 */
typedef void (*SM_ErrorCallback_t)(SM_Handle_t sm, SM_ErrorLevel_t level, uint16_t code);

/* =============================================================================
 * STATISTICS (compile-time optional)
 * ===========================================================================*/

#if SM_FEATURE_STATISTICS

/**
 * @brief Runtime statistics counters
 */
typedef struct {
    uint32_t total_transitions;                  /**< Total state transitions */
    uint32_t total_events_posted;                /**< Total events posted (user posts
                                                      plus engine-internal timeout and
                                                      time-event posts) */
    uint32_t total_events_dropped;               /**< Events dropped (queue full) */
    uint32_t total_timeouts;                     /**< State timeouts fired */
    uint32_t state_entry_counts[SM_STATE_COUNT]; /**< Per-state entry count */
} SM_Stats_t;

#endif /* SM_FEATURE_STATISTICS */

/* =============================================================================
 * CONFIGURATION STRUCT
 * ===========================================================================*/

/**
 * @brief Configuration passed to SM_Init()
 *
 * Points to user-provided const arrays of state descriptors and transitions.
 * These arrays live in flash (const).
 */
typedef struct {
    const SM_StateDesc_t *states;        /**< Array of state descriptors [SM_STATE_COUNT] */
    const SM_Transition_t *transitions;  /**< Array of transitions */
    uint16_t transition_count;           /**< Number of entries in transitions array */
    uint16_t initial_state;              /**< Starting state index (typically 0) */
} SM_Config_t;

/* =============================================================================
 * STATE MACHINE CONTEXT (full definition)
 *
 * Defined here for static allocation. Users MUST NOT access fields directly --
 * use the SM_* API exclusively.
 * ===========================================================================*/

struct SM_Context {
    /* --- Current state --- */
    volatile uint16_t current_state;   /**< Current state index (volatile for ISR-safe reads) */
    uint16_t previous_state;           /**< Previous state index */

    /* --- DIS (Duplicate Inverse Storage) for safety-critical fields --- */
    uint16_t state_dis;                /**< Bitwise inverse of current_state (D7) */
    uint8_t init_dis;                  /**< Bitwise inverse of initialized (D7) */

    /* --- State history ring --- */
    uint16_t state_history[SM_STATE_HISTORY_DEPTH]; /**< Recent state transitions */
    uint8_t history_head;              /**< Next write position in history ring */

    /* --- Timing --- */
    uint32_t state_entry_time;         /**< Timestamp of last state entry (ms) */
    uint32_t state_exec_count;         /**< Executions since entering current state */
    bool state_entered;                /**< True on first cycle after transition */
    bool timeout_fired;                /**< Prevents repeated timeout events per state entry */

    /* --- Event queue --- */
    SM_EventQueue_t event_queue;       /**< ISR-safe event ring buffer with frontEvt */

    /* --- Error handler --- */
    SM_ErrorHandler_t error;           /**< Error handling state */

    /* --- Configuration (const pointer to user's config) --- */
    const SM_Config_t *config;         /**< Pointer to user-provided config */

    /* --- Callbacks --- */
    SM_RecoveryCallback_t recovery_cb; /**< Optional recovery callback */
    SM_ErrorCallback_t error_cb;       /**< Optional error notification callback */

    /* --- Runtime transitions (optional) --- */
#if SM_FEATURE_RUNTIME_TRANSITIONS
    SM_Transition_t rt_transitions[SM_MAX_TRANSITIONS]; /**< Runtime transition table */
    uint16_t rt_transition_count;      /**< Number of runtime transitions */
#endif

    /* --- Time events (optional, D9) --- */
#if SM_FEATURE_TIME_EVENTS
    SM_TimeEvt_t *time_evt_head;       /**< Head of intrusive linked-list of time events */
#endif

    /* --- Deferred events (optional, D10) --- */
#if SM_FEATURE_DEFER
    SM_EventQueue_t defer_queue;       /**< Deferred event ring buffer (SM_DEFER_QUEUE_SIZE) */
#endif

    /* --- Statistics (optional) --- */
#if SM_FEATURE_STATISTICS
    SM_Stats_t stats;                  /**< Runtime statistics */
#endif

    /* --- Initialized flag --- */
    bool initialized;                  /**< True after successful SM_Init() */
};

#ifdef __cplusplus
}
#endif

#endif /* SM_TYPES_H */
